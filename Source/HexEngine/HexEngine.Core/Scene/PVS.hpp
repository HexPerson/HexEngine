
#pragma once

#include "../Required.hpp"
#include "../Entity/Entity.hpp"

namespace HexEngine
{
	class MeshInstance;
	class Mesh;
	class Entity;
	class Scene;
	class Material;
	class BaseComponent;
	class Camera;

	struct PVSParams
	{
		PVSParams() :
			shapeType(ShapeType::Frustum),
			lodPartition(0.0f),
			forceMaxLod(false),
			isShadow(false),
			camera(nullptr)
		{}

		enum class ShapeType
		{
			Sphere,
			Frustum,
			Frustum2
		};

		float lodPartition;
		bool forceMaxLod;

		union Shape
		{
			constexpr Shape() {}

			dx::BoundingSphere sphere;

			struct
			{
				dx::BoundingFrustum sm;
				dx::BoundingFrustum lg;
			} frustum;

		} shape;

		math::Matrix shadowViewMatrix;
		Camera* camera;
		ShapeType shapeType;
		bool isShadow;
	};

	struct RenderableSnapshot
	{
		std::shared_ptr<Mesh> mesh;
		std::shared_ptr<Material> material;
		MeshInstance* instance = nullptr;
		SimpleMeshInstance* simpleInstance = nullptr;
		Layer layer = Layer::Invisible;
		bool hasAnimations = false;
		bool isBoundToBone = false;
		CullingMode shadowCullMode = CullingMode::FrontFace;
		MeshInstanceData instanceData = {};
		SimpleMeshInstanceData shadowInstanceData = {};
		Entity* entity = nullptr;
		uint32_t stableIndex = 0;
		bool cullEligible = false;
		bool forceVisible = true;
		bool gpuVisible = true;
		bool culledByFrustum = false;
		bool culledByOcclusion = false;
	};

	using RenderBatchSnapshot = std::vector<std::pair<std::shared_ptr<Material>, std::vector<RenderableSnapshot>>>;

	class HEX_API PVS
	{
	public:
		using MeshEntityPair = std::tuple<std::shared_ptr<Mesh>, Entity*, BaseComponent*>;
		using MeshEntityVector = std::vector<MeshEntityPair>;
		using MeshInstanceMap = std::map<std::shared_ptr<Material>, MeshEntityVector>;

		void ClearPVS(); 
		void ForceRebuild();
		bool NeedsRebuild() const;
		void ResetDidRebuild();
		bool DidRebuild() const;
		const MeshInstanceMap& GetRenderables() const;

		void CalculateVisibility(Scene* scene, const PVSParams& params);

		bool IsEntityVisible(Entity* entity, const PVSParams& params);
		bool IsShapeVisible(const dx::BoundingBox& bbox, const PVSParams& params);
		bool IsShapeVisible(const dx::BoundingSphere& bsphere, const PVSParams& params);

		void AddEntity(Entity* entity);
		void FlushEntity(Entity* entity, bool recache = false);
		void RemoveEntity(Entity* entity);

		const PVSParams& GetOptimisedParams() const;

		uint32_t GetTotalNumberOfEnts() const { return _totalEnts; }
		uint32_t GetTotalSkeletalAnimators() const { return _totalSkeletalAnimators; }

		RenderBatchSnapshot& GetRenderableSnapshot() { return _renderableSnapshot; }

		void DisableUpdates(bool disable) { _updatesDisabled = disable; }

		void UpdateEntityInstanceCache(Entity* entity);

	private:
		MeshInstanceMap _pvs;
		PVSParams _optimisedParams;
		bool _needsOptimisationRebuild = true;
		bool _hasBuildOptimisation = false;
		bool _forceRebuild = true;
		bool _didRebuild = false;
		bool _updatesDisabled = false;
		std::recursive_mutex _lock;

		uint32_t _totalEnts = 0;
		uint32_t _totalSkeletalAnimators = 0;

		RenderBatchSnapshot _renderableSnapshot;
	};
}
