
#pragma once

#include "Mesh.hpp"

namespace HexEngine
{
	class HEX_API AnimatedMesh : public Mesh
	{
	public:
		using BoneNameMap = std::map<std::string, uint32_t>;
		using BoneInfoArray = std::array<BoneInfo, MAX_BONES>;

		friend class AssimpModelImporter;

		AnimatedMesh(const std::shared_ptr<Model>& model, const std::string& name);
		AnimatedMesh(AnimatedMesh* other);

		virtual ~AnimatedMesh();

		virtual void UpdateConstantBuffer(Entity* entity, const math::Matrix& localTM, Material* material, int32_t instanceId, bool isTransparencyPhase = false) override;

		void UpdateBoneTransform(Animation* animation, float TimeInSeconds, std::vector<math::Matrix>& Transforms);

		void StopAnimating();
		void SetAnimationIndex(uint32_t idx);
		void BlendToAnimationIndex(uint32_t idx);
		virtual std::shared_ptr<AnimationData> GetAnimationData() const override;
		virtual std::shared_ptr<AnimationData> CreateAnimationData();
		void SetAnimationData(std::shared_ptr<AnimationData> data) { _animData = data; }

		BoneInfo* GetBoneInfoByName(const std::string& name);
		const BoneInfoArray& GetAllBoneInfo() const { return _boneInfo; }
		const BoneNameMap& GetBoneMap() const { return _boneMap; }
		void SetBoneMap(uint32_t numBones, const BoneNameMap& boneMap, const BoneInfoArray& boneInfo);
		uint32_t GetNumBones() const { return _numBones; }

		virtual bool HasAnimations() const override { return true; }
		virtual bool CreateBuffers() override;

		void AddVertex(const AnimatedMeshVertex& vertex);
		void AddVertices(const std::vector<AnimatedMeshVertex>& vertex);
		const std::vector<AnimatedMeshVertex>& GetVertices() const;

		void SetRootTransformation(const math::Matrix& rootTrans);
		const math::Matrix& GetRootTransformation() const;

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
		uint32_t _numBones = 0;
		std::vector<AnimatedMeshVertex> _vertices;
		std::vector<SimpleAnimatedMeshVertex> _simpleVertices;
		std::vector<AnimatedMeshVertex> _transformedVertices;

		BoneNameMap _boneMap;
		BoneInfoArray _boneInfo;
		math::Matrix _rootTransformation;

		std::shared_ptr<AnimationData> _animData;

		struct PerAnimationBuffer* _animationBuffer = nullptr;
	};
}
