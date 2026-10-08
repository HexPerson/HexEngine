
#pragma once

#include "Mesh.hpp"
#include "../Graphics/GpuSkinning.hpp"

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
		virtual void SetBuffers(bool isShadowMap = false) override;
		virtual uint32_t GetAnimationObjectFlags() const override;

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
		const std::vector<SimpleAnimatedMeshVertex>& GetSimpleVertices() const { return _simpleVertices; }

		// The material name stored in the .hmesh, even when loading substituted the
		// DefaultAnimated fallback (see MeshLoader). Written back on save.
		void SetFileMaterialName(const std::string& name) { _fileMaterialName = name; }
		const std::string& GetFileMaterialName() const { return _fileMaterialName; }

		// Bind-pose vertices for the GpuSkinning compute pass, created on first use.
		GpuSkinSource* GetGpuSkinSource();

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

		std::unique_ptr<GpuSkinSource> _gpuSkinSource;
		bool _gpuSkinSourceFailed = false;
		std::string _fileMaterialName;

		// Set by UpdateConstantBuffer for the draw being prepared, consumed by the
		// SetBuffers call that always follows it. The mesh is shared between entities,
		// so this is strictly per-draw state.
		GpuSkinInstance* _preSkinnedDraw = nullptr;
	};
}
