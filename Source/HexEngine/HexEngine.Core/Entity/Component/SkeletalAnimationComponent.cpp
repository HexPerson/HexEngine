
#include "SkeletalAnimationComponent.hpp"
#include "StaticMeshComponent.hpp"
#include "../Entity.hpp"
#include "../../Environment/TimeManager.hpp"
#include "../../Math/FloatMath.hpp"
#include "../../Scene/AnimatedMesh.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/DropDown.hpp"
#include "../../GUI/Elements/Checkbox.hpp"
#include "../../GUI/Elements/DragInt.hpp"
#include "../../GUI/Elements/ContextMenu.hpp"

namespace HexEngine
{
	SkeletalAnimationComponent::SkeletalAnimationComponent(Entity* entity) :
		UpdateComponent(entity)
	{
	}

	SkeletalAnimationComponent::SkeletalAnimationComponent(Entity* entity, SkeletalAnimationComponent* copy) :
		UpdateComponent(entity, copy)
	{
	}

	void SkeletalAnimationComponent::SetAnimationData(std::shared_ptr<AnimatedMesh> mesh, std::shared_ptr<AnimationData> animData)
	{
		_mesh = mesh;
		_animData = animData;
		_boneInfo = mesh->GetAllBoneInfo();

		// offset the time slightly because animations starting at the same time will be perfectly in sync which looks weird
		_animationStartTime = g_pEnv->_timeManager->_currentTime + GetRandomFloat(0.5f, 1.0f);
	}

	bool SkeletalAnimationComponent::TryAutoBindFromEntityMesh()
	{
		if (_animData)
			return true; // already bound

		Entity* owner = GetEntity();
		if (owner == nullptr)
			return false;

		// StaticMeshComponent owns the loaded Mesh; if it's an AnimatedMesh
		// (skinned) it carries the AnimationData we need. Async mesh load
		// is possible - bail quietly when the component isn't ready yet,
		// the next caller (Update or CreateWidget on next open) retries.
		auto* smc = owner->GetComponent<StaticMeshComponent>();
		if (smc == nullptr)
			return false;

		auto mesh = smc->GetMesh();
		if (mesh == nullptr || !mesh->HasAnimations())
			return false;

		auto animatedMesh = std::dynamic_pointer_cast<AnimatedMesh>(mesh);
		if (animatedMesh == nullptr)
			return false;

		auto animData = animatedMesh->GetAnimationData();
		if (animData == nullptr)
			return false;

		SetAnimationData(animatedMesh, animData);
		return true;
	}

	const std::array<math::Matrix, MAX_BONES>& SkeletalAnimationComponent::GetBoneTransformArray() const
	{
		return _transforms;
	}

	void SkeletalAnimationComponent::OnMessage(Message* message, MessageListener* sender)
	{
		UpdateComponent::OnMessage(message, sender);

		if (message->_id == MessageId::PVSVisibilityChanged)
		{
			bool visible = message->CastAs<PVSVisibilityChangedMessage>()->visible;

			if (visible)
			{
				SetTickRate(_previousTickRate);
			}
			else
			{
				_previousTickRate = GetTickRate();
				SetTickRate(100); // almost completely disable animations
			}
		}
	}

	void SkeletalAnimationComponent::Update(float frameTime)
	{
		UpdateComponent::Update(frameTime);

		// Prefab deserialization doesn't wire up the StaticMesh -> Skeletal
		// link, so catch the bind here once the mesh has finished loading.
		// No-op when _animData is already populated.
		if (!_animData)
			TryAutoBindFromEntityMesh();

		if (_animData && _animData->_animations.size() > 0 && _animIndex != -1)
		{
			Animation& anim = _animData->_animations.at(std::min((uint32_t)_animData->_animations.size() - 1, _animIndex));

			UpdateBoneTransform(&anim, g_pEnv->_timeManager->_currentTime - _animationStartTime, _transforms);

			if (_nextAnimIndex != -1)
			{
				Animation& anim2 = _animData->_animations.at(std::min((uint32_t)_animData->_animations.size() - 1, _nextAnimIndex));

				std::array<math::Matrix, MAX_BONES> blendTransforms;
				UpdateBoneTransform(&anim2, g_pEnv->_timeManager->_currentTime - _animationStartTime, blendTransforms);

				for (uint32_t i = 0; i < blendTransforms.size(); ++i)
				{
					_transforms[i] = math::Matrix::Lerp(_transforms[i], blendTransforms[i], _blendFactor);
				}

				_blendFactor += g_pEnv->_timeManager->_frameTime * 12.3f;

				if (_blendFactor >= 1.0f)
				{
					_animIndex = _nextAnimIndex;
					_nextAnimIndex = -1;
					_blendFactor = 0.0f;
				}
			}

			//memcpy(_animationBuffer->_boneTransforms, (uint8_t*)_transforms.data(), _transforms.size() * sizeof(math::Matrix));
		}
	}

	void SkeletalAnimationComponent::UpdateBoneTransform(Animation* animation, float TimeInSeconds, std::array<math::Matrix, MAX_BONES>& Transforms)
	{
		math::Matrix Identity;

		//TimeInSeconds -= animation->time;

		//TimeInSeconds *= animation->speed;

		float TicksPerSecond = animation->ticksPerSecond != 0 ? animation->ticksPerSecond : 25.0f;

		float TimeInTicks = TimeInSeconds * TicksPerSecond;
		float AnimationTime = fmod(TimeInTicks, animation->duration);

		//if (abs(AnimationTime - animation->duration) < 0.5f)
		//	animation->speed *= -1.0f;

		ReadNodeHierarchy(animation, animation->_rootNode, AnimationTime, Identity);

		//Transforms.resize(_boneInfo.size());

		for (uint32_t i = 0; i < _boneInfo.size(); i++)
		{
			Transforms[i] = _boneInfo[i].FinalTransformation;// .Invert().Transpose();
			//Transforms[i] = Transforms[i].Invert().Transpose(); // this is to fix normals where animated meshes have been scaled non uniformly
		}
	}

	void SkeletalAnimationComponent::ReadNodeHierarchy(const Animation* owningAnim, AnimChannel* animation, float AnimationTime, math::Matrix& ParentTransform)
	{
		auto transform = animation->nodeTransform;

		// Synthetic / static channels (added by the importer to cover scene
		// nodes that the source FBX didn't keyframe) carry no keys - the
		// runtime is meant to fall back to the bone's bind-pose transform
		// (nodeTransform) which the importer copied from the source scene
		// graph. Without this branch, FBX clips that only animate a subset
		// of joints (Mixamo idle = spine only) cause every un-keyframed
		// bone to read past the end of empty key vectors and either crash
		// or collapse to origin, which is what manifests as "the rig has
		// only spine bones left" after a sibling-merged import.
		const bool hasKeys = !animation->positionKeys.empty()
			|| !animation->rotationKeys.empty()
			|| !animation->scaleKeys.empty();

		if (!hasKeys)
		{
			// nodeTransform was loaded from Assimp without an explicit
			// transpose at the call site (see ProcessNode); the keyed-path
			// rebuild applies a transpose at the end of its TRS compose
			// step, so we leave nodeTransform as-is here to land both
			// paths in the same matrix convention.
			math::Matrix GlobalTransformation = ParentTransform * transform;

			const auto& boneMap = _mesh->GetBoneMap();
			if (auto it = boneMap.find(animation->nodeName); it != boneMap.end())
			{
				uint32_t BoneIndex = it->second;
				auto& bi = _boneInfo[BoneIndex];

				const math::Matrix& git =
					(owningAnim && owningAnim->_globalInverseTransform != math::Matrix::Identity)
						? owningAnim->_globalInverseTransform
						: _animData->_globalInverseTransform;

				bi.FinalTransformation = git * GlobalTransformation * bi.BoneOffset;

				auto transposeGlobalTransform = GlobalTransformation.Transpose();
				bi.Position = transposeGlobalTransform.Translation();
				bi.Rotation = math::Quaternion::CreateFromRotationMatrix(transposeGlobalTransform);
			}

			for (uint32_t i = 0; i < animation->children.size(); i++)
				ReadNodeHierarchy(owningAnim, animation->children.at(i), AnimationTime, GlobalTransformation);

			return;
		}

		// Interpolate scaling and generate scaling transformation matrix
		math::Vector3 Scaling;
		CalcInterpolatedScaling(Scaling, AnimationTime, animation);
		math::Matrix ScalingM = math::Matrix::CreateScale(Scaling.x, Scaling.y, Scaling.z);

		// Interpolate rotation and generate rotation transformation matrix
		math::Quaternion RotationQ;
		CalcInterpolatedRotation(RotationQ, AnimationTime, animation);
		math::Matrix RotationM = math::Matrix::CreateFromQuaternion(RotationQ);

		// Interpolate translation and generate translation transformation matrix
		math::Vector3 Translation;
		CalcInterpolatedPosition(Translation, AnimationTime, animation);

		// Root motion: when enabled, intercept the root bone's translation,
		// accumulate the per-frame delta in entity-local space, and zero the
		// translation that goes into the bone so the skeleton stays put
		// relative to the entity origin. Game/script code drains the delta
		// via ConsumeRootMotionDelta() and applies it to the entity transform.
		//
		// The "root bone" is whichever bone we resolve to a valid bone-map
		// entry FIRST while descending the channel tree. We cache its name so
		// blended animations and re-played animations consistently strip the
		// same node (animation files can share root bone names but vary in
		// scene-graph parents above it).
		bool isRootBone = false;
		if (_rootMotion && _mesh)
		{
			const auto& boneMap = _mesh->GetBoneMap();
			const bool nodeIsInBoneMap = (boneMap.find(animation->nodeName) != boneMap.end());
			if (nodeIsInBoneMap)
			{
				if (_rootBoneName.empty())
					_rootBoneName = animation->nodeName;
				if (animation->nodeName == _rootBoneName)
					isRootBone = true;
			}
		}

		if (isRootBone)
		{
			const math::Vector3 currentPos = Translation;
			if (_hasLastRootBonePosition)
			{
				// Planar root motion: accumulate horizontal (XZ) delta only.
				// Y delta stays in the bone so the animation's vertical
				// component (jumps, crouches, vertical-stance variation
				// between clips) still drives the rig - the entity isn't
				// asked to track those. The latched-first-sample anchor
				// approach (used in a previous revision) failed here
				// because _animationStartTime carries a random offset
				// (SetAnimationData seeds it to avoid lockstep on shared
				// clips), so the first ReadNodeHierarchy visit landed
				// partway through the loop and latched a mid-animation
				// pose as the "bind" position - the rig then rendered
				// permanently offset from the entity.
				_rootMotionDelta.x += currentPos.x - _lastRootBonePosition.x;
				_rootMotionDelta.z += currentPos.z - _lastRootBonePosition.z;
			}
			_lastRootBonePosition = currentPos;
			_hasLastRootBonePosition = true;

			// Strip horizontal translation from the bone; let Y pass through.
			// This keeps the rig's hip height driven by the animation (so
			// the feet stay on the floor at the bind-pose Y, and jumps
			// animate properly) while XZ motion is delegated to the
			// entity via ConsumeRootMotionDelta.
			Translation.x = 0.0f;
			Translation.z = 0.0f;
		}

		math::Matrix TranslationM = math::Matrix::CreateTranslation(Translation);

		// Combine the above transformations
		transform = ScalingM * RotationM * TranslationM;
		transform = transform.Transpose();

		math::Matrix GlobalTransformation = ParentTransform * transform;

		const auto& boneMap = _mesh->GetBoneMap();

		if (auto it = boneMap.find(animation->nodeName); it != boneMap.end())
		{
			uint32_t BoneIndex = it->second;

			auto& bi = _boneInfo[BoneIndex];

			// Prefer the owning animation's per-clip root transform - sibling-
			// merged FBX animations stamp their own here. Fall back to the
			// AnimationData's shared value for legacy / pre-merge .hmesh
			// files where Animation::_globalInverseTransform is the default
			// identity. Using Matrix::Identity directly as the sentinel is
			// safe: a true identity root transform has no effect either way.
			const math::Matrix& git =
				(owningAnim && owningAnim->_globalInverseTransform != math::Matrix::Identity)
					? owningAnim->_globalInverseTransform
					: _animData->_globalInverseTransform;

			bi.FinalTransformation = git * GlobalTransformation * bi.BoneOffset;

			auto transposeGlobalTransform = GlobalTransformation.Transpose();

			bi.Position = transposeGlobalTransform.Translation();
			bi.Rotation = math::Quaternion::CreateFromRotationMatrix(transposeGlobalTransform);
		}

		for (uint32_t i = 0; i < animation->children.size(); i++)
		{
			ReadNodeHierarchy(owningAnim, animation->children.at(i), AnimationTime, GlobalTransformation);
		}
	}

	const AnimChannel* SkeletalAnimationComponent::FindNodeAnim(const AnimChannel* pAnimation, const std::string& NodeName)
	{
		/*for (auto it = pAnimation->nodeToAnimMap.begin(); it != pAnimation->nodeToAnimMap.end(); it++)
		{
			if (it->first == NodeName)
				return it->second;
		}*/

		if (pAnimation->nodeName == NodeName)
			return pAnimation;

		for (auto& child : pAnimation->children)
		{
			auto res = FindNodeAnim(child, NodeName);

			if (res != nullptr)
				return res;
		}

		return nullptr;
	}

	void SkeletalAnimationComponent::CalcInterpolatedScaling(math::Vector3& Out, float AnimationTime, const AnimChannel* pNodeAnim)
	{
		// After the importer's CoalesceSyntheticPivotChannels pass, a channel
		// only carries the TRS slots whose synthetic-pivot source actually
		// existed in the FBX. Bones with only a "_$AssimpFbx$_Rotation"
		// synthetic end up here with empty scaleKeys; previously FindScaling
		// would index past the end and crash. Default to identity scale and
		// return early.
		if (pNodeAnim->scaleKeys.empty()) {
			Out = math::Vector3(1.0f, 1.0f, 1.0f);
			return;
		}
		if (pNodeAnim->scaleKeys.size() == 1) {
			Out = pNodeAnim->scaleKeys[0].second;
			return;
		}

		uint32_t ScalingIndex = FindScaling(AnimationTime, pNodeAnim);
		uint32_t NextScalingIndex = (ScalingIndex + 1);
		//assert(NextScalingIndex < pNodeAnim->scaleKeys.size());
		float t1 = (float)pNodeAnim->scaleKeys[ScalingIndex].first - (float)pNodeAnim->scaleKeys[0].first;
		float t2 = (float)pNodeAnim->scaleKeys[NextScalingIndex].first - (float)pNodeAnim->scaleKeys[0].first;
		float DeltaTime = t2 - t1;
		float Factor = std::clamp((AnimationTime - (float)t1) / DeltaTime, 0.0f, 1.0f);
		//assert(Factor >= 0.0f && Factor <= 1.0f);
		const math::Vector3& Start = pNodeAnim->scaleKeys[ScalingIndex].second;
		const math::Vector3& End = pNodeAnim->scaleKeys[NextScalingIndex].second;
		math::Vector3 Delta = End - Start;
		Out = Start + Factor * Delta;
	}

	void SkeletalAnimationComponent::CalcInterpolatedPosition(math::Vector3& Out, float AnimationTime, const AnimChannel* pNodeAnim)
	{
		// Same empty-slot handling as CalcInterpolatedScaling - bones that
		// only have rotation synthetics (typical for forearm twist joints
		// in Mixamo rigs) leave positionKeys empty.
		if (pNodeAnim->positionKeys.empty()) {
			Out = math::Vector3::Zero;
			return;
		}
		if (pNodeAnim->positionKeys.size() == 1) {
			Out = pNodeAnim->positionKeys[0].second;
			return;
		}

		uint32_t PositionIndex = FindPosition(AnimationTime, pNodeAnim);
		uint32_t NextPositionIndex = (PositionIndex + 1);
		//assert(NextPositionIndex < pNodeAnim->positionKeys.size());
		float t1 = (float)pNodeAnim->positionKeys[PositionIndex].first - (float)pNodeAnim->positionKeys[0].first;
		float t2 = (float)pNodeAnim->positionKeys[NextPositionIndex].first - (float)pNodeAnim->positionKeys[0].first;
		float DeltaTime = t2 - t1;
		float Factor = std::clamp((AnimationTime - (float)t1) / DeltaTime, 0.0f, 1.0f);
		//assert(Factor >= 0.0f && Factor <= 1.0f);
		const math::Vector3& Start = pNodeAnim->positionKeys[PositionIndex].second;
		const math::Vector3& End = pNodeAnim->positionKeys[NextPositionIndex].second;
		math::Vector3 Delta = End - Start;
		Out = Start + Factor * Delta;
	}


	void SkeletalAnimationComponent::CalcInterpolatedRotation(math::Quaternion& Out, float AnimationTime, const AnimChannel* pNodeAnim)
	{
		// Same empty-slot handling as the other two interpolators.
		if (pNodeAnim->rotationKeys.empty()) {
			Out = math::Quaternion::Identity;
			return;
		}
		// we need at least two values to interpolate...
		if (pNodeAnim->rotationKeys.size() == 1) {
			Out = pNodeAnim->rotationKeys[0].second;
			return;
		}

		uint32_t RotationIndex = FindRotation(AnimationTime, pNodeAnim);
		uint32_t NextRotationIndex = (RotationIndex + 1);
		//assert(NextRotationIndex < pNodeAnim->rotationKeys.size());
		float t1 = (float)pNodeAnim->rotationKeys[RotationIndex].first - (float)pNodeAnim->rotationKeys[0].first;
		float t2 = (float)pNodeAnim->rotationKeys[NextRotationIndex].first - (float)pNodeAnim->rotationKeys[0].first;
		float DeltaTime = t2 - t1;
		float Factor = std::clamp((AnimationTime - (float)t1) / DeltaTime, 0.0f, 1.0f);
		//assert(Factor >= 0.0f && Factor <= 1.0f);
		const math::Quaternion& StartRotationQ = pNodeAnim->rotationKeys[RotationIndex].second;
		const math::Quaternion& EndRotationQ = pNodeAnim->rotationKeys[NextRotationIndex].second;

		math::Quaternion::Lerp(StartRotationQ, EndRotationQ, Factor, Out);
		//Out.Normalize();
	}

	uint32_t SkeletalAnimationComponent::FindScaling(float AnimationTime, const AnimChannel* pNodeAnim)
	{
		//assert(pNodeAnim->scaleKeys.size() > 0);

		for (uint32_t i = 0; i < pNodeAnim->scaleKeys.size() - 1; i++) {
			float t = (float)pNodeAnim->scaleKeys[i + 1].first - (float)pNodeAnim->scaleKeys[0].first;
			if (AnimationTime <= t) {
				return i;
			}
		}

		return (uint32_t)pNodeAnim->scaleKeys.size() - 2;
	}

	uint32_t SkeletalAnimationComponent::FindPosition(float AnimationTime, const AnimChannel* pNodeAnim)
	{
		for (uint32_t i = 0; i < pNodeAnim->positionKeys.size() - 1; i++) {
			float t = (float)pNodeAnim->positionKeys[i + 1].first - (float)pNodeAnim->positionKeys[0].first;
			if (AnimationTime <= t) {
				return i;
			}
		}

		return (uint32_t)pNodeAnim->positionKeys.size() - 2;
	}

	uint32_t SkeletalAnimationComponent::FindRotation(float AnimationTime, const AnimChannel* pNodeAnim)
	{
		assert(pNodeAnim->rotationKeys.size() > 0);

		for (uint32_t i = 0; i < pNodeAnim->rotationKeys.size() - 1; i++) {
			float t = (float)pNodeAnim->rotationKeys[i + 1].first - (float)pNodeAnim->rotationKeys[0].first;
			if (AnimationTime <= t) {
				return i;
			}
		}

		return (uint32_t)pNodeAnim->rotationKeys.size() - 2;
	}

	void SkeletalAnimationComponent::StopAnimating()
	{
		//_animData->_animIndex = -1;
	}

	void SkeletalAnimationComponent::SetAnimationIndex(uint32_t idx)
	{
		if (idx < 0 || idx >= _animData->_animations.size())
			return;

		_animIndex = idx;

		// TODO move this to anim component
		//_animData->_animations.at(idx).time = g_pEnv->_timeManager->_currentTime;
	}

	void SkeletalAnimationComponent::BlendToAnimationIndex(uint32_t idx)
	{
		// no point blending into the animation we are already running
		//if (_nextAnimIndex == idx)
		//	return;

		_blendFactor = 0.0f;
		_nextAnimIndex = idx;

		// TODO move this to anim component
		//_animData->_animations.at(idx).time = g_pEnv->_timeManager->_currentTime;
	}

	BoneInfo* SkeletalAnimationComponent::GetBoneInfoByName(const std::string& name)
	{
		auto& boneMap = _mesh->GetBoneMap();

		auto it = boneMap.find(name);

		if (it == boneMap.end())
			return nullptr;

		return &_boneInfo[it->second];
	}

	math::Vector3 SkeletalAnimationComponent::ConsumeRootMotionDelta()
	{
		const math::Vector3 result = _rootMotionDelta;
		_rootMotionDelta = math::Vector3::Zero;
		return result;
	}

	void SkeletalAnimationComponent::Serialize(json& data, JsonFile* file)
	{
		SERIALIZE_VALUE(_animIndex);
		SERIALIZE_VALUE(_rootMotion);
	}

	void SkeletalAnimationComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		(void)mask;
		_serializationState = BaseComponent::SerializationState::Deserializing;

		DESERIALIZE_VALUE(_animIndex);
		DESERIALIZE_VALUE(_rootMotion);

		_serializationState = BaseComponent::SerializationState::Ready;
	}

	bool SkeletalAnimationComponent::CreateWidget(ComponentWidget* widget)
	{
		const int32_t fullWidth = widget->GetSize().x - 20;

		// Prefab-instantiated components arrive at the inspector with no
		// AnimationData wired up (Deserialize doesn't know about the
		// sibling StaticMeshComponent's mesh). Pull from the entity here
		// so the dropdown can populate on first open.
		TryAutoBindFromEntityMesh();

		// --- Animation dropdown -------------------------------------------------
		// Lists every animation in the bound AnimationData by name. Selecting
		// an entry calls BlendToAnimationIndex so the existing blend logic in
		// Update() smoothly crossfades to the new clip rather than snapping.
		// When _animData is missing (mesh not yet bound, or non-skeletal) we
		// still render the dropdown with a single "(no animations)" entry so
		// the editor doesn't appear broken.
		DropDown* animDropdown = new DropDown(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Animation");

		auto updateDropdownLabel = [this, animDropdown]()
		{
			if (_animData && _animIndex < _animData->_animations.size())
			{
				const auto& name = _animData->_animations[_animIndex].name;
				animDropdown->SetValue(std::wstring(name.begin(), name.end()));
			}
			else
			{
				animDropdown->SetValue(L"(none)");
			}
		};

		if (_animData && !_animData->_animations.empty())
		{
			for (uint32_t i = 0; i < _animData->_animations.size(); ++i)
			{
				const std::string& nm = _animData->_animations[i].name;
				const std::wstring wname = nm.empty()
					? (L"Animation " + std::to_wstring(i))
					: std::wstring(nm.begin(), nm.end());
				animDropdown->GetContextMenu()->AddItem(new ContextItem(
					wname,
					[this, i, updateDropdownLabel](const std::wstring&)
					{
						BlendToAnimationIndex(i);
						_animIndex = i;
						updateDropdownLabel();
					}));
			}
		}
		else
		{
			animDropdown->GetContextMenu()->AddItem(new ContextItem(
				L"(no animations)",
				[](const std::wstring&) {}));
		}
		updateDropdownLabel();

		// --- Root motion toggle -------------------------------------------------
		// When ticked, the root bone's per-frame translation is stripped from
		// the skeleton and accumulated in _rootMotionDelta. Gameplay code calls
		// ConsumeRootMotionDelta() to drive the entity's Transform. See
		// ReadNodeHierarchy() for the strip logic.
		new Checkbox(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Root Motion", &_rootMotion);

		// --- Tick rate ----------------------------------------------------------
		// UpdateComponent's tick rate. 1 = every frame. Higher values let the
		// component skip frames - useful for distant background characters
		// where animation precision is irrelevant. PVS visibility already
		// auto-bumps this to 100 when off-screen (see OnMessage), so this
		// slider is for the user's coarse default.
		//
		// DragInt writes via the int32_t* we hand it, so we need a stable
		// backing value that outlives this CreateWidget call. The widget
		// system retains the lambda alongside the element, so capturing the
		// buffer by value-into-a-shared_ptr keeps it alive for the lifetime
		// of the widget.
		auto tickRateBuf = std::make_shared<int32_t>(GetTickRate());
		DragInt* tickRate = new DragInt(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Tick Rate", tickRateBuf.get(), 1, 30, 1);
		tickRate->SetOnDrag([this, tickRateBuf](int32_t* /*v*/, int32_t /*oldVal*/, int32_t /*newVal*/)
		{
			SetTickRate(*tickRateBuf);
			_previousTickRate = *tickRateBuf;
		});

		return true;
	}
}