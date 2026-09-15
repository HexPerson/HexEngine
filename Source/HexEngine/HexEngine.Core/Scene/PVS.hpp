
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
	class StaticMeshComponent;

	struct PVSParams
	{
		PVSParams() :
			shapeType(ShapeType::Frustum),
			lodPartition(0.0f),
			forceMaxLod(false),
			isShadow(false),
			camera(nullptr),
			hasFineSphere(false)
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

		// Optional exact shape for the per-frame draw-time fine cull when the
		// coarse PVS shape is deliberately larger (e.g. a cascade PVS built
		// from a camera-centred, rotation-invariant sphere passes the actual
		// slice sphere here so shadow draws stay tight).
		dx::BoundingSphere fineSphere;
		bool hasFineSphere;
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
		// Source component + the entity transform version the cached instance
		// data was pulled at. The draw loops re-pull instance data lazily when
		// the entity's version has moved on (O(drawn)); this replaced
		// UpdateEntityInstanceCache's linear scan of every snapshot per moving
		// entity per PVS per frame (O(movers x snapshot x PVSes)).
		StaticMeshComponent* component = nullptr;
		uint64_t transformVersion = 0;
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
		// Last element is the mesh's MeshInstanceId, cached at insert time so the
		// per-batch sort is a plain integer compare instead of chasing
		// shared_ptr->GetInstance()->GetInstanceId() per comparison.
		using MeshEntityPair = std::tuple<std::shared_ptr<Mesh>, Entity*, BaseComponent*, uint32_t>;
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
		// The params of the most recent CalculateVisibility call (rebuild or
		// not) - i.e. the CURRENT, un-inflated cull shape. The draw loops use
		// it for the per-frame fine cull.
		const PVSParams& GetCurrentParams() const { return _currentParams; }

		uint32_t GetTotalNumberOfEnts() const { return _totalEnts; }
		uint32_t GetTotalSkeletalAnimators() const { return _totalSkeletalAnimators; }

		RenderBatchSnapshot& GetRenderableSnapshot() { return _renderableSnapshot; }

		void DisableUpdates(bool disable) { _updatesDisabled = disable; }

		void UpdateEntityInstanceCache(Entity* entity);
		// Re-pull instance data for EVERY snapshot entry. For PVS holders that
		// don't get the per-move UpdateEntityInstanceCache treatment (local
		// shadow lights - only the camera and sun do), call this before a
		// cached-shadow re-render or moved entities draw with stale
		// shadowInstanceData (slice 7, user-found: a dragged prop kept its
		// old shadow through an atlas tile refresh).
		void RefreshAllInstanceCaches();

	private:
		MeshInstanceMap _pvs;
		PVSParams _optimisedParams;
		PVSParams _currentParams;
		// Planes of the frustum actually culled against (sm for Frustum, lg for
		// Frustum2), rebuilt whenever _optimisedParams changes. The exact
		// per-entity sphere test is 6 dot products against these -
		// dx::BoundingFrustum::Intersects re-derives its planes on EVERY call,
		// which was ~1/3 of the rebuild loop at 16k candidates.
		math::Vector4 _cullPlanes[6] = {};
		bool _cullPlanesValid = false;
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
