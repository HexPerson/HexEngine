
#pragma once

#include "Mesh.hpp"

namespace HexEngine
{
	class AnimatedMesh : public Mesh
	{
	public:
		friend class AssimpModelImporter;

		AnimatedMesh(std::shared_ptr<Model>& model, const std::string& name);
		AnimatedMesh(AnimatedMesh* other);

		virtual ~AnimatedMesh();

		virtual void UpdateConstantBuffer(const math::Matrix& localTM, Material* material, int32_t instanceId) override;

		void UpdateBoneTransform(Animation* animation, float TimeInSeconds, std::vector<math::Matrix>& Transforms);

		void StopAnimating();
		void SetAnimationIndex(uint32_t idx);
		void BlendToAnimationIndex(uint32_t idx);
		const std::shared_ptr<AnimationData>& GetAnimationData() const;
		BoneInfo* GetBoneInfoByName(const std::string& name);

		virtual bool HasAnimations() const override { return true; }
		bool CreateBuffers();

		void AddVertex(const AnimatedMeshVertex& vertex);

	private:
		void ReadNodeHierarchy(AnimChannel* animation, float AnimationTime, math::Matrix& ParentTransform);
		const AnimChannel* FindNodeAnim(const AnimChannel* pAnimation, const std::string& NodeName);
		void CalcInterpolatedScaling(math::Vector3& Out, float AnimationTime, const AnimChannel* pNodeAnim);
		void CalcInterpolatedPosition(math::Vector3& Out, float AnimationTime, const AnimChannel* pNodeAnim);
		void CalcInterpolatedRotation(math::Quaternion& Out, float AnimationTime, const AnimChannel* pNodeAnim);
		uint32_t FindScaling(float AnimationTime, const AnimChannel* pNodeAnim);
		uint32_t FindPosition(float AnimationTime, const AnimChannel* pNodeAnim);
		uint32_t FindRotation(float AnimationTime, const AnimChannel* pNodeAnim);

	private:
		std::vector<AnimatedMeshVertex> _vertices;
		std::vector<AnimatedMeshVertex> _transformedVertices;

		std::map<std::string, uint32_t> _boneMap;
		std::vector<BoneInfo> _boneInfo;
		math::Matrix _rootTransformation;

		std::shared_ptr<AnimationData> _animData;

		struct PerAnimationBuffer* _animationBuffer = nullptr;
	};
}
