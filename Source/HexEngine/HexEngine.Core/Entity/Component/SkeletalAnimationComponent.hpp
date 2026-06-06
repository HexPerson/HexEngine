
#pragma once

#include "UpdateComponent.hpp"
#include "../../Scene/AnimatedMesh.hpp"

namespace HexEngine
{
	class HEX_API SkeletalAnimationComponent : public UpdateComponent
	{
	public:
		CREATE_COMPONENT_ID(SkeletalAnimationComponent);
		DEFINE_COMPONENT_CTOR(SkeletalAnimationComponent);

		virtual void Update(float deltaTime) override;

		void		SetAnimationData(std::shared_ptr<AnimatedMesh> mesh, std::shared_ptr<AnimationData> animData);
		BoneInfo*	GetBoneInfoByName(const std::string& name);

		// Pull the AnimatedMesh + AnimationData off the owning entity's
		// StaticMeshComponent if we don't already have them. Editor-driven
		// flows (mesh dragged in, asset-explorer assignment) call
		// SetAnimationData directly, but prefab deserialization does NOT -
		// it just constructs the component and runs Deserialize. Without
		// this auto-bind, prefab-instantiated characters render the right
		// mesh (because the StaticMeshComponent still loaded it) but the
		// SkeletalAnimationComponent stays detached: animations don't
		// populate in the editor dropdown and the rig doesn't animate at
		// runtime. Returns true if the bind succeeded or was already in
		// effect.
		bool		TryAutoBindFromEntityMesh();

		// Accessor for the underlying animation set; used by the editor widget
		// (and any gameplay code) to enumerate available animation names.
		std::shared_ptr<AnimationData> GetAnimationData() const { return _animData; }

	private:
		void				UpdateBoneTransform(Animation* animation, float TimeInSeconds, std::array<math::Matrix, MAX_BONES>& Transforms);
		void				ReadNodeHierarchy(const Animation* owningAnim, AnimChannel* animation, float AnimationTime, math::Matrix& ParentTransform);
		const AnimChannel*	FindNodeAnim(const AnimChannel* pAnimation, const std::string& NodeName);
		void				CalcInterpolatedScaling(math::Vector3& Out, float AnimationTime, const AnimChannel* pNodeAnim);
		void				CalcInterpolatedPosition(math::Vector3& Out, float AnimationTime, const AnimChannel* pNodeAnim);
		void				CalcInterpolatedRotation(math::Quaternion& Out, float AnimationTime, const AnimChannel* pNodeAnim);
		uint32_t			FindScaling(float AnimationTime, const AnimChannel* pNodeAnim);
		uint32_t			FindPosition(float AnimationTime, const AnimChannel* pNodeAnim);
		uint32_t			FindRotation(float AnimationTime, const AnimChannel* pNodeAnim);



	public:
		void				StopAnimating();
		void				SetAnimationIndex(uint32_t idx);
		void				BlendToAnimationIndex(uint32_t idx);
		uint32_t			GetAnimationIndex() const { return _animIndex; }

		// Root motion: when enabled, ReadNodeHierarchy strips the root bone's
		// translation from the skeleton each frame and accumulates the per-
		// frame translation delta in _rootMotionDelta (entity-local space).
		// Game code applies the delta to the owning Entity's transform via
		// ConsumeRootMotionDelta() so the mesh stays planted on the move
		// instead of sliding around relative to the entity origin.
		bool				IsRootMotionEnabled() const { return _rootMotion; }
		void				SetRootMotionEnabled(bool enabled) { _rootMotion = enabled; }

		// Returns the accumulated entity-local translation since the last
		// call, then clears the accumulator. Game/script code should call
		// this once per frame and add the value to the entity's Transform.
		math::Vector3		ConsumeRootMotionDelta();

		const std::array<math::Matrix, MAX_BONES>& GetBoneTransformArray() const;

		virtual void OnMessage(Message* message, MessageListener* sender) override;

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(class ComponentWidget* widget) override;

	private:
		std::shared_ptr<AnimatedMesh> _mesh;
		std::shared_ptr<AnimationData> _animData;
		uint32_t _animIndex = 0;
		uint32_t _nextAnimIndex = -1;
		float _blendFactor = 0.0f;
		float _animationStartTime = 0.0f;

		std::array<BoneInfo, MAX_BONES> _boneInfo;
		std::array<math::Matrix, MAX_BONES> _transforms;

		int32_t _previousTickRate = 1;

		// Root motion state. _rootMotion is the user-facing toggle, the rest
		// is per-frame bookkeeping. _rootBoneName caches the first bone whose
		// node hierarchy we visit so we only strip translation from the
		// genuine skeletal root (not arbitrary scene-graph parents).
		// _rootBoneBindPos is the FIRST sample of the root bone's animated
		// translation, treated as the bind-pose anchor; we re-stamp the
		// bone's local translation back to this every frame so the
		// skeleton's hips stay at hip-height while the per-frame delta is
		// handed off to the owning Entity via ConsumeRootMotionDelta().
		bool			_rootMotion = false;
		math::Vector3	_lastRootBonePosition = math::Vector3::Zero;
		math::Vector3	_rootBoneBindPos = math::Vector3::Zero;
		bool			_hasLastRootBonePosition = false;
		math::Vector3	_rootMotionDelta = math::Vector3::Zero;
		std::string		_rootBoneName;
	};
}
