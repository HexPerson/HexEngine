
#include "SkeletalAnimationComponent.hpp"
#include "../../Environment/TimeManager.hpp"
#include "../../Math/FloatMath.hpp"

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

		if (_animData && _animData->_animations.size() > 0 && _animIndex != -1)
		{
			Animation& anim = _animData->_animations.at(min(_animData->_animations.size() - 1, _animIndex));

			UpdateBoneTransform(&anim, g_pEnv->_timeManager->_currentTime - _animationStartTime, _transforms);

			if (_nextAnimIndex != -1)
			{
				Animation& anim2 = _animData->_animations.at(min(_animData->_animations.size() - 1, _nextAnimIndex));

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

		ReadNodeHierarchy(animation->_rootNode, AnimationTime, Identity);

		//Transforms.resize(_boneInfo.size());

		for (uint32_t i = 0; i < _boneInfo.size(); i++)
		{
			Transforms[i] = _boneInfo[i].FinalTransformation;// .Invert().Transpose();
			//Transforms[i] = Transforms[i].Invert().Transpose(); // this is to fix normals where animated meshes have been scaled non uniformly
		}
	}

	void SkeletalAnimationComponent::ReadNodeHierarchy(AnimChannel* animation, float AnimationTime, math::Matrix& ParentTransform)
	{
		auto transform = animation->nodeTransform;

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

			bi.FinalTransformation = _animData->_globalInverseTransform * GlobalTransformation * bi.BoneOffset;

			auto transposeGlobalTransform = GlobalTransformation.Transpose();

			bi.Position = transposeGlobalTransform.Translation();
			bi.Rotation = math::Quaternion::CreateFromRotationMatrix(transposeGlobalTransform);
		}

		for (uint32_t i = 0; i < animation->children.size(); i++)
		{
			ReadNodeHierarchy(animation->children.at(i), AnimationTime, GlobalTransformation);
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
}