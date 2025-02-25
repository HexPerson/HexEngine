
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

		void SetAnimationData(std::shared_ptr<AnimatedMesh> mesh, std::shared_ptr<AnimationData> animData);

	private:
		void				UpdateBoneTransform(Animation* animation, float TimeInSeconds, std::array<math::Matrix, MAX_BONES>& Transforms);
		void				ReadNodeHierarchy(AnimChannel* animation, float AnimationTime, math::Matrix& ParentTransform);
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

		const std::array<math::Matrix, MAX_BONES>& GetBoneTransformArray() const;

		virtual void OnMessage(Message* message, MessageListener* sender) override;

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
	};
}
