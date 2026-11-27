
#include "SkeletalAnimationComponent.hpp"
#include "StaticMeshComponent.hpp"
#include "../Entity.hpp"
#include "../Messaging/Message.hpp"
#include "../../Environment/TimeManager.hpp"
#include "../../Math/FloatMath.hpp"
#include "../../Scene/AnimatedMesh.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/DropDown.hpp"
#include "../../GUI/Elements/Checkbox.hpp"
#include "../../GUI/Elements/DragInt.hpp"
#include "../../GUI/Elements/ContextMenu.hpp"
#include "../../HexEngine.hpp"
#include <chrono>

namespace HexEngine
{
	HVar anim_stats("anim_stats", "Log skeletal animation CPU cost (pose evaluation + per-draw bone uploads)", false, false, true);
	HVar anim_statsFrames("anim_statsFrames", "How many frames anim_stats averages over between log lines", 120, 10, 2000);

	namespace
	{
		struct AnimTelemetry
		{
			int64_t frame = -1;
			uint32_t frames = 0;
			double poseMs = 0.0;
			uint32_t poses = 0;
			double uploadMs = 0.0;
			uint32_t uploads = 0;
			double skinMs = 0.0;
			uint32_t skins = 0;
		};

		AnimTelemetry g_animTelemetry;

		// Called before accumulating: on the first sample of a new frame, log and
		// reset once the averaging window is full.
		void AdvanceTelemetryFrame()
		{
			AnimTelemetry& t = g_animTelemetry;
			const int64_t frame = (g_pEnv && g_pEnv->_timeManager) ? g_pEnv->_timeManager->_frameCount : 0;
			if (frame == t.frame)
				return;

			t.frame = frame;
			if (t.frames >= (uint32_t)std::max(1, anim_statsFrames._val.i32))
			{
				const double n = (double)t.frames;
				LOG_INFO("anim_stats (%u frames): pose eval %.3f ms/frame (%.1f poses/frame, %.1f us each) | GPU skin dispatch %.3f ms/frame (%.1f/frame) | VS-skinning bone uploads %.3f ms/frame (%.1f/frame)",
					t.frames,
					t.poseMs / n, t.poses / n, t.poses ? (t.poseMs * 1000.0 / t.poses) : 0.0,
					t.skinMs / n, t.skins / n,
					t.uploadMs / n, t.uploads / n);
				t = AnimTelemetry{};
				t.frame = frame;
			}
			++t.frames;
		}

		void RecordGpuSkin(float milliseconds)
		{
			AdvanceTelemetryFrame();
			g_animTelemetry.skinMs += milliseconds;
			++g_animTelemetry.skins;
		}

		void RecordPose(float milliseconds)
		{
			AdvanceTelemetryFrame();
			g_animTelemetry.poseMs += milliseconds;
			++g_animTelemetry.poses;
		}
	}

	bool SkeletalAnimationComponent::IsTelemetryEnabled()
	{
		return anim_stats._val.b;
	}

	void SkeletalAnimationComponent::RecordBoneUpload(float milliseconds)
	{
		AdvanceTelemetryFrame();
		g_animTelemetry.uploadMs += milliseconds;
		++g_animTelemetry.uploads;
	}

	SkeletalAnimationComponent::SkeletalAnimationComponent(Entity* entity) :
		UpdateComponent(entity)
	{
	}

	SkeletalAnimationComponent::SkeletalAnimationComponent(Entity* entity, SkeletalAnimationComponent* copy) :
		UpdateComponent(entity, copy)
	{
		// Carry the authored (serialised) settings across a clone. Without this a
		// duplicated / prefab-spawned character played clip 0 (the bind-pose take)
		// instead of its configured idle. Runtime state (bound mesh, compiled
		// clips, pose) is rebuilt lazily by TryAutoBindFromEntityMesh.
		if (copy != nullptr)
		{
			_animIndex = copy->_animIndex;
			_rootMotion = copy->_rootMotion;
		}
	}

	SkeletalAnimationComponent::~SkeletalAnimationComponent() = default;

	void SkeletalAnimationComponent::SetAnimationData(std::shared_ptr<AnimatedMesh> mesh, std::shared_ptr<AnimationData> animData)
	{
		if (mesh != _mesh)
		{
			// A different body: the compute-skinned vertex buffers and last frame's
			// palette belong to the old mesh / skeleton.
			_gpuSkin.reset();
			_prevPoseValid = false;
		}
		_mesh = mesh;
		_animData = animData;
		_boneInfo = mesh->GetAllBoneInfo();
		_compiledClips.clear();	// bone indices are per mesh

		// offset the time slightly because animations starting at the same time will be perfectly in sync which looks weird
		_animationStartTime = g_pEnv->_timeManager->_currentTime + GetRandomFloat(0.5f, 1.0f);
	}

	bool SkeletalAnimationComponent::TryAutoBindFromEntityMesh()
	{
		Entity* owner = GetEntity();
		if (owner == nullptr)
			return _animData != nullptr;

		// StaticMeshComponent owns the loaded Mesh; if it's an AnimatedMesh
		// (skinned) it carries the AnimationData we need. Async mesh load
		// is possible - bail quietly when the component isn't ready yet,
		// the next caller (Update or CreateWidget on next open) retries.
		// Also rebinds when the entity's mesh was swapped for another skinned body
		// (set_component_field / inspector): the old skeleton's bone layout would
		// otherwise drive the new mesh's vertices.
		auto* smc = owner->GetComponent<StaticMeshComponent>();
		if (smc == nullptr)
			return _animData != nullptr;

		auto mesh = smc->GetMesh();
		if (_animData && mesh.get() == _mesh.get())
			return true; // already bound to this mesh

		if (mesh == nullptr || !mesh->HasAnimations())
			return _animData != nullptr;

		auto animatedMesh = std::dynamic_pointer_cast<AnimatedMesh>(mesh);
		if (animatedMesh == nullptr)
			return _animData != nullptr;

		auto animData = animatedMesh->GetAnimationData();
		if (animData == nullptr)
			return _animData != nullptr;

		SetAnimationData(animatedMesh, animData);
		return true;
	}

	const std::array<math::Matrix, MAX_BONES>& SkeletalAnimationComponent::GetBoneTransformArray() const
	{
		return _transforms;
	}

	const std::array<math::Matrix, MAX_BONES>& SkeletalAnimationComponent::GetBoneTransformArrayPrev() const
	{
		// Before the first snapshot the two are identical, so a mesh drawn on its very first
		// frame reports no deformation velocity rather than a garbage one.
		return _prevPoseValid ? _transformsPrev : _transforms;
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
		// link, so catch the bind here once the mesh has finished loading, and
		// rebind if the mesh was swapped. Cheap no-op when already bound.
		TryAutoBindFromEntityMesh();

		if (_mesh && _animData && _animData->_animations.size() > 0 && _animIndex != -1)
		{
			const bool telemetry = IsTelemetryEnabled();
			const auto poseStart = telemetry ? std::chrono::high_resolution_clock::now() : std::chrono::high_resolution_clock::time_point{};

			SnapshotPrevPose();

			if (_editorMode)
			{
				EvaluateNow();
				if (telemetry)
					RecordPose(std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - poseStart).count());
				return;
			}

			const uint32_t lastClip = (uint32_t)_animData->_animations.size() - 1;
			// A ragdoll holds the clip still (see SetPhysicsPose).
			const float clipSeconds = !_physicsPose.empty() ? _physicsFreezeSeconds
				: g_pEnv->_timeManager->_currentTime - _animationStartTime;
			const auto clipTicks = [&](uint32_t clip)
			{
				const Animation& anim = _animData->_animations[clip];
				if (clip == _oneShotClip && _physicsPose.empty())
				{
					// One-shot: its own clock from 0, held on the last frame.
					const float t = std::max(0.0f, g_pEnv->_timeManager->_currentTime - _oneShotStart) * AnimationUtils::GetTicksPerSecond(anim);
					return anim.duration > 0.0f ? std::min(t, anim.duration * 0.999f) : 0.0f;
				}
				return anim.duration > 0.0f ? fmod(clipSeconds * AnimationUtils::GetTicksPerSecond(anim), anim.duration) : 0.0f;
			};

			const uint32_t clip = std::min(lastClip, _animIndex);
			EvaluateClip(clip, clipTicks(clip), _transforms, false);

			// Clip events fire from the clip that dominates the pose (the incoming one
			// past the crossfade's midpoint), so a blend never doubles them up.
			{
				const uint32_t dominant = (_nextAnimIndex != -1 && _blendFactor >= 0.5f)
					? std::min(lastClip, _nextAnimIndex) : clip;
				FireClipEvents(dominant, clipTicks(dominant));
			}

			if (_nextAnimIndex != -1)
			{
				std::array<math::Matrix, MAX_BONES> blendTransforms;
				const uint32_t nextClip = std::min(lastClip, _nextAnimIndex);
				EvaluateClip(nextClip, clipTicks(nextClip), blendTransforms, false);

				// Final matrices are lerped (not per-bone TRS) because sibling-merged
				// clips can each carry a different root transform - see
				// Animation::_globalInverseTransform - so their local poses aren't
				// directly comparable. The crossfade is ~80 ms, short enough that
				// the slight shrink of a matrix lerp isn't visible.
				for (uint32_t i = 0; i < blendTransforms.size(); ++i)
				{
					_transforms[i] = math::Matrix::Lerp(_transforms[i], blendTransforms[i], _blendFactor);
				}

				_blendFactor += g_pEnv->_timeManager->GetGameFrameTime() * 12.3f;

				if (_blendFactor >= 1.0f)
				{
					_animIndex = _nextAnimIndex;
					_nextAnimIndex = -1;
					_blendFactor = 0.0f;
				}
			}

			++_poseVersion;

			if (telemetry)
				RecordPose(std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - poseStart).count());
		}
	}

	void SkeletalAnimationComponent::FireClipEvents(uint32_t clipIndex, float ticks)
	{
		const Animation& anim = _animData->_animations[clipIndex];

		// A different clip, or the same clip restarted: start tracking from here
		// without firing (the time jumped, nothing was "passed").
		if (clipIndex != _eventClip || _animationStartTime != _eventStartTime)
		{
			_eventClip = clipIndex;
			_eventStartTime = _animationStartTime;
			_eventPrevTicks = ticks;
			return;
		}

		const float prev = _eventPrevTicks;
		_eventPrevTicks = ticks;
		if (anim.events.empty() || ticks == prev || GetEntity() == nullptr)
			return;

		// (prev, ticks], or across the loop: (prev, duration] then [0, ticks].
		const bool wrapped = ticks < prev;
		for (const AnimationEvent& ev : anim.events)
		{
			const bool passed = wrapped
				? (ev.time > prev || ev.time <= ticks)
				: (ev.time > prev && ev.time <= ticks);
			if (!passed)
				continue;
			AnimationEventMessage msg(ev.name, clipIndex, ev.time);
			GetEntity()->OnMessage(&msg, this);
		}
	}

	GpuSkinInstance* SkeletalAnimationComponent::PrepareGpuSkin(AnimatedMesh* mesh)
	{
		// Only the mesh this component evaluates poses for, and only once a pose exists
		// (before that the vertex-shader path draws the bind pose, as it always has).
		if (mesh == nullptr || mesh != _mesh.get() || _poseVersion == 0 || !GpuSkinning::IsAvailable())
			return nullptr;

		if (_gpuSkin == nullptr)
			_gpuSkin = std::make_unique<GpuSkinInstance>();

		// Skin once per new pose: every later pass this frame (shadow cascades, main,
		// outline...) draws the same output.
		if (_gpuSkin->_skinnedPoseVersion != _poseVersion)
		{
			const bool telemetry = IsTelemetryEnabled();
			const auto skinStart = telemetry ? std::chrono::high_resolution_clock::now() : std::chrono::high_resolution_clock::time_point{};

			if (!_gpuSkin->Skin(*mesh, _transforms.data(), GetBoneTransformArrayPrev().data()))
				return nullptr;

			_gpuSkin->_skinnedPoseVersion = _poseVersion;

			if (telemetry)
				RecordGpuSkin(std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - skinStart).count());
		}

		return _gpuSkin.get();
	}

	SkeletalAnimationComponent::CompiledClip& SkeletalAnimationComponent::GetCompiledClip(uint32_t clipIndex)
	{
		// Edited data (animation editor) bumps the revision: channels or keys may have
		// moved, so every cached layout is stale.
		if (_compiledClips.size() != _animData->_animations.size() || _compiledRevision != _animData->_revision)
		{
			_compiledClips.clear();
			_compiledClips.resize(_animData->_animations.size());
			_compiledRevision = _animData->_revision;
		}

		CompiledClip& clip = _compiledClips[clipIndex];
		const Animation* anim = &_animData->_animations[clipIndex];

		// Also catches the animation vector having been reallocated or swapped.
		if (clip.anim != anim)
			CompileClip(clip, anim);

		return clip;
	}

	void SkeletalAnimationComponent::CompileClip(CompiledClip& clip, const Animation* anim)
	{
		clip.anim = anim;
		clip.nodes.clear();
		clip.cursors.clear();
		clip.rootMotionNode = -2;

		if (anim->_rootNode == nullptr)
			return;

		const auto& boneMap = _mesh->GetBoneMap();

		// Iterative pre-order walk in the same child order the old recursive
		// ReadNodeHierarchy used, so "first bone visited" (root motion) is unchanged.
		std::vector<std::pair<const AnimChannel*, int32_t>> stack;
		stack.emplace_back(anim->_rootNode, -1);

		while (!stack.empty())
		{
			const auto [channel, parent] = stack.back();
			stack.pop_back();

			const int32_t index = (int32_t)clip.nodes.size();
			PoseNode& node = clip.nodes.emplace_back();
			node.channel = channel;
			node.parent = parent;
			node.hasKeys = !channel->positionKeys.empty() || !channel->rotationKeys.empty() || !channel->scaleKeys.empty();

			if (auto it = boneMap.find(channel->nodeName); it != boneMap.end() && it->second < (uint32_t)MAX_BONES)
				node.bone = (int32_t)it->second;

			for (auto child = channel->children.rbegin(); child != channel->children.rend(); ++child)
				stack.emplace_back(*child, index);
		}

		clip.cursors.resize(clip.nodes.size());
	}

	int32_t SkeletalAnimationComponent::ResolveRootMotionNode(CompiledClip& clip)
	{
		if (clip.rootMotionNode != -2)
			return clip.rootMotionNode;

		// The root bone is the first keyed bone met while descending the channel
		// tree. Its name is cached on the component so every clip strips the same
		// joint even when their scene-graph parents above it differ.
		if (_rootBoneName.empty())
		{
			for (const PoseNode& node : clip.nodes)
			{
				if (node.hasKeys && node.bone >= 0)
				{
					_rootBoneName = node.channel->nodeName;
					break;
				}
			}
		}

		clip.rootMotionNode = -1;
		for (int32_t i = 0; i < (int32_t)clip.nodes.size(); ++i)
		{
			const PoseNode& node = clip.nodes[i];
			if (node.hasKeys && node.bone >= 0 && node.channel->nodeName == _rootBoneName)
			{
				clip.rootMotionNode = i;
				break;
			}
		}

		return clip.rootMotionNode;
	}

	namespace
	{
		// Index i of the key pair [i, i+1] to interpolate at `time`: the smallest i
		// with time <= key[i+1] (absolute clip time in ticks), or the last pair - the
		// same answer as AnimationUtils::FindKeyPair, but resuming from `cursor`, so
		// steady playback costs one or two comparisons. Requires keys.size() >= 2.
		template<typename T>
		uint32_t FindKeyIndex(const std::vector<std::pair<float, T>>& keys, float time, uint32_t& cursor)
		{
			const uint32_t lastPair = (uint32_t)keys.size() - 2;

			uint32_t i = std::min(cursor, lastPair);

			// Time went backwards (clip looped, or a clip switch): rescan from the start.
			if (i > 0 && time <= keys[i].first)
				i = 0;

			while (i < lastPair && time > keys[i + 1].first)
				++i;

			cursor = i;
			return i;
		}

		math::Vector3 SampleVector(const std::vector<std::pair<float, math::Vector3>>& keys, float time, uint32_t& cursor, const math::Vector3& fallback)
		{
			// Channels only carry the TRS slots their FBX source animated (bones
			// with only a rotation synthetic have no position/scale keys).
			if (keys.empty())
				return fallback;
			if (keys.size() == 1)
				return keys[0].second;

			const uint32_t i = FindKeyIndex(keys, time, cursor);
			const math::Vector3& start = keys[i].second;
			return start + AnimationUtils::KeyFactor(keys, i, time) * (keys[i + 1].second - start);
		}

		math::Quaternion SampleRotation(const std::vector<std::pair<float, math::Quaternion>>& keys, float time, uint32_t& cursor)
		{
			if (keys.empty())
				return math::Quaternion::Identity;
			if (keys.size() == 1)
				return keys[0].second;

			const uint32_t i = FindKeyIndex(keys, time, cursor);
			math::Quaternion out;
			math::Quaternion::Lerp(keys[i].second, keys[i + 1].second, AnimationUtils::KeyFactor(keys, i, time), out);
			return out;
		}
	}

	void SkeletalAnimationComponent::EvaluateClip(uint32_t clipIndex, float ticks, std::array<math::Matrix, MAX_BONES>& transforms, bool editorPose)
	{
		CompiledClip& clip = GetCompiledClip(clipIndex);
		const Animation& anim = *clip.anim;
		const float animTime = ticks;
		const bool overrides = editorPose && !_poseOverrides.empty();

		// Prefer the clip's own root transform - sibling-merged FBX animations stamp
		// their own here. Fall back to the AnimationData's shared value for legacy /
		// pre-merge .hmesh files where it is the default identity.
		const math::Matrix& git = anim._globalInverseTransform != math::Matrix::Identity
			? anim._globalInverseTransform
			: _animData->_globalInverseTransform;

		// The editor shows the clip's real data, so root motion is never stripped there.
		const int32_t rootMotionNode = (_rootMotion && !editorPose) ? ResolveRootMotionNode(clip) : -1;

		_nodeGlobals.resize(clip.nodes.size());
		_lastEvalClip = (int32_t)clipIndex;
		_lastEvalRoot = git;
		const bool physics = !_physicsPose.empty() && !editorPose;
		const math::Matrix gitInverse = physics ? git.Invert() : math::Matrix::Identity;

		for (int32_t i = 0; i < (int32_t)clip.nodes.size(); ++i)
		{
			const PoseNode& node = clip.nodes[i];
			const AnimChannel* channel = node.channel;

			math::Matrix local;
			const auto overrideIt = overrides ? _poseOverrides.find(channel->nodeName) : _poseOverrides.end();
			if (overrideIt != _poseOverrides.end())
			{
				local = AnimationUtils::ComposeLocal(overrideIt->second);
			}
			else if (!node.hasKeys)
			{
				// Synthetic / static channels (the importer adds them for scene nodes the
				// FBX didn't keyframe) fall back to the bind-pose nodeTransform. It is
				// already in the composed convention, so no transpose here.
				local = channel->nodeTransform;
			}
			else
			{
				KeyCursor& cursor = clip.cursors[i];
				const math::Vector3 scale = SampleVector(channel->scaleKeys, animTime, cursor.scl, math::Vector3::One);
				const math::Quaternion rotation = SampleRotation(channel->rotationKeys, animTime, cursor.rot);
				math::Vector3 translation = SampleVector(channel->positionKeys, animTime, cursor.pos, math::Vector3::Zero);

				if (i == rootMotionNode)
				{
					// Planar root motion: accumulate the horizontal (XZ) delta for the
					// entity (ConsumeRootMotionDelta) and strip it from the bone. Y stays
					// in the bone so hip height, crouches and jumps still animate.
					if (_hasLastRootBonePosition)
					{
						_rootMotionDelta.x += translation.x - _lastRootBonePosition.x;
						_rootMotionDelta.z += translation.z - _lastRootBonePosition.z;
					}
					_lastRootBonePosition = translation;
					_hasLastRootBonePosition = true;

					translation.x = 0.0f;
					translation.z = 0.0f;
				}

				local = AnimationUtils::ComposeLocal({ translation, rotation, scale });
			}

			math::Matrix& global = _nodeGlobals[i];
			global = node.parent >= 0 ? _nodeGlobals[node.parent] * local : local;

			if (physics)
			{
				// Entity-local row frame -> this loop's convention: git * global = frame^T.
				if (auto it = _physicsPose.find(channel->nodeName); it != _physicsPose.end())
				{
					const math::Matrix driven = gitInverse * it->second.Transpose();
					global = _physicsWeight >= 0.999f ? driven : math::Matrix::Lerp(global, driven, _physicsWeight);
				}
			}

			if (node.bone >= 0)
			{
				BoneInfo& bi = _boneInfo[node.bone];
				bi.FinalTransformation = git * global * bi.BoneOffset;

				if (_attachmentBones.test(node.bone))
				{
					const math::Matrix transposed = global.Transpose();
					bi.Position = transposed.Translation();
					bi.Rotation = math::Quaternion::CreateFromRotationMatrix(transposed);
				}
			}
		}

		for (uint32_t i = 0; i < _boneInfo.size(); i++)
		{
			transforms[i] = _boneInfo[i].FinalTransformation;
		}

		if (editorPose)
		{
			_evaluatedNodes = clip.nodes;
			_evaluatedClipRoot = git;
		}
	}

	bool SkeletalAnimationComponent::GetNodeEntityFrame(const std::string& nodeName, math::Matrix& out) const
	{
		if (_lastEvalClip < 0 || _lastEvalClip >= (int32_t)_compiledClips.size())
			return false;
		const CompiledClip& clip = _compiledClips[_lastEvalClip];
		for (size_t i = 0; i < clip.nodes.size() && i < _nodeGlobals.size(); ++i)
		{
			if (clip.nodes[i].channel != nullptr && clip.nodes[i].channel->nodeName == nodeName)
			{
				out = (_lastEvalRoot * _nodeGlobals[i]).Transpose();
				return true;
			}
		}
		return false;
	}

	void SkeletalAnimationComponent::SetPhysicsPose(const std::unordered_map<std::string, math::Matrix>& frames, float weight)
	{
		if (frames.empty())
		{
			ClearPhysicsPose();
			return;
		}
		if (_physicsPose.empty() && g_pEnv != nullptr && g_pEnv->_timeManager != nullptr)
			_physicsFreezeSeconds = g_pEnv->_timeManager->_currentTime - _animationStartTime;
		_physicsPose = frames;
		_physicsWeight = std::clamp(weight, 0.0f, 1.0f);
	}

	void SkeletalAnimationComponent::ClearPhysicsPose()
	{
		if (!_physicsPose.empty() && g_pEnv != nullptr && g_pEnv->_timeManager != nullptr)
		{
			// Resume the clip where it was frozen, not with a jump.
			_animationStartTime = g_pEnv->_timeManager->_currentTime - _physicsFreezeSeconds;
		}
		_physicsPose.clear();
		_physicsWeight = 0.0f;
	}

	void SkeletalAnimationComponent::SnapshotPrevPose()
	{
		// Snapshot the pose this frame is reprojecting FROM, before overwriting it.
		// Guarded on the frame counter because Update() can run more than once per
		// rendered frame - snapshotting on every entry would make prev == current and
		// zero out the deformation velocity that the whole point of this is to provide.
		const uint32_t frame = g_pEnv->_timeManager ? (uint32_t)g_pEnv->_timeManager->_frameCount : 0u;
		if (!_prevPoseValid || frame != _prevPoseFrame)
		{
			_transformsPrev = _transforms;
			_prevPoseFrame = frame;
			_prevPoseValid = true;
		}
	}

	void SkeletalAnimationComponent::SetEditorMode(bool enabled)
	{
		_editorMode = enabled;
		if (!enabled)
		{
			_poseOverrides.clear();
			_evaluatedNodes.clear();
		}
	}

	void SkeletalAnimationComponent::SetPoseOverride(const std::string& nodeName, const AnimationUtils::NodePose& pose)
	{
		_poseOverrides[nodeName] = pose;
	}

	void SkeletalAnimationComponent::ClearPoseOverride(const std::string& nodeName)
	{
		_poseOverrides.erase(nodeName);
	}

	void SkeletalAnimationComponent::ClearPoseOverrides()
	{
		_poseOverrides.clear();
	}

	const AnimationUtils::NodePose* SkeletalAnimationComponent::GetPoseOverride(const std::string& nodeName) const
	{
		const auto it = _poseOverrides.find(nodeName);
		return it != _poseOverrides.end() ? &it->second : nullptr;
	}

	void SkeletalAnimationComponent::EvaluateNow()
	{
		if (!_animData)
			TryAutoBindFromEntityMesh();

		if (!_mesh || !_animData || _animData->_animations.empty())
		{
			_evaluatedNodes.clear();
			_nodeGlobals.clear();
			return;
		}

		SnapshotPrevPose();

		const uint32_t clip = std::min((uint32_t)_animData->_animations.size() - 1, _animIndex);
		EvaluateClip(clip, _editorTicks, _transforms, true);
		++_poseVersion;
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

	void SkeletalAnimationComponent::PlayOneShot(uint32_t idx)
	{
		if (!_animData || idx >= _animData->_animations.size())
			return;
		_oneShotClip = idx;
		_oneShotStart = g_pEnv->_timeManager->_currentTime;
		if (GetTargetAnimationIndex() != idx)
			BlendToAnimationIndex(idx);
		else
			_eventStartTime = -1.0f;	// same clip restarting: re-anchor clip events
	}

	float SkeletalAnimationComponent::GetOneShotElapsed() const
	{
		if (_oneShotClip == (uint32_t)-1)
			return 0.0f;
		return std::max(0.0f, g_pEnv->_timeManager->_currentTime - _oneShotStart);
	}

	float SkeletalAnimationComponent::GetClipLengthSeconds(uint32_t idx) const
	{
		if (!_animData || idx >= _animData->_animations.size())
			return 0.0f;
		const Animation& anim = _animData->_animations[idx];
		const float tps = AnimationUtils::GetTicksPerSecond(anim);
		return tps > 0.0f ? anim.duration / tps : 0.0f;
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

		// Callers keep this pointer and read Position / Rotation every frame
		// (bone attachments), so start refreshing those for this bone.
		_attachmentBones.set(it->second);
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
		// EvaluateClip() for the strip logic.
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