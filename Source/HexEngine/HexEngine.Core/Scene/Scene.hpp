#pragma once

#include "../Required.hpp"
#include "../Entity/Entity.hpp"
#include "../Entity/Component/Camera.hpp"
#include "../Entity/Component/Light.hpp"
#include "../Entity/Component/DirectionalLight.hpp"
#include "IEntityListener.hpp"
#include "ISceneCustomRenderer.hpp"
#include "INavMeshProvider.hpp"
#include "../Graphics/IGraphicsDevice.hpp"
#include "../GUI/DebugGUI.hpp"
#include "../Entity/Component/StaticMeshComponent.hpp"
#include "../Entity/Component/UpdateComponent.hpp"
#include "../Terrain/HeightMapGenerator.hpp"
#include "SnowFootprintSystem.hpp"
#include "OceanWaveModel.hpp"
#include <functional>
#include <limits>
#include <type_traits>
#include <unordered_set>

namespace HexEngine
{
	enum class SceneFlags
	{
		Disabled				= HEX_BITSET(0),
		Updateable				= HEX_BITSET(1),
		Renderable				= HEX_BITSET(2),
		PostProcessingEnabled	= HEX_BITSET(3),
		Utility					= HEX_BITSET(4)
	};

	DEFINE_ENUM_FLAG_OPERATORS(SceneFlags);

	class Camera;

	/*struct SceneRenderParameters
	{
		math::Vector3 cameraLocation;
		math::Matrix viewMatrix;
		math::Matrix projectionMatrix;
		math::Matrix lightViewMatrix;
		math::Matrix lightProjectionMatrix[4];
		math::Vector3 lightPosition;
		math::Vector3 lightDirection;
		float frustumSplits[4];
		Camera* camera = nullptr;
		dx::BoundingSphere frustumSliceBounds;
		
		bool isShadowPass;
		float lightMultiplier;
		int passIndex;
	};*/

	

	enum class EntityNamingPolicy
	{
		AutoRename,
		Singlular,
	};

	struct VisibilitySpheres
	{
		bool needsRebuild = true;
		dx::BoundingSphere shape;
	};

	enum SceneUpdateFlags
	{
		SceneUpdateNone				= 0,
		SceneUpdateAddedEntity		= HEX_BITSET(0),
		SceneUpdateRemovedEntity	= HEX_BITSET(1),
		SceneUpdateCameraMoved		= HEX_BITSET(2),
		SceneUpdateHasCalcedVis		= HEX_BITSET(3)
	};

	DEFINE_ENUM_FLAG_OPERATORS(SceneUpdateFlags);

	

	class HEX_API Scene : public IDebugGUICallback, public IResource
	{
	public:
		static constexpr uint32_t InvalidDenseIndex = std::numeric_limits<uint32_t>::max();

		using EntityVector = std::vector<Entity*>;
		using EntityComponentVector = std::vector<BaseComponent*>;

		using EntityMap = std::map<ComponentSignature, EntityVector>;

		void Create(bool createSkySphere, IEntityListener* listener = nullptr);
		void CreateEmpty(bool createSkySphere, IEntityListener* listener = nullptr);
		void CreateDefaultSunLight();

		void Destroy();

		EntityId CreateEntityId(const std::string& name, const math::Vector3& position = math::Vector3::Zero, const math::Quaternion& rotation = math::Quaternion::Identity, const math::Vector3& scale = math::Vector3(1.0f));
		Entity* CreateEntity(const std::string & name, const math::Vector3& position = math::Vector3::Zero, const math::Quaternion& rotation = math::Quaternion::Identity, const math::Vector3& scale = math::Vector3(1.0f));
		Entity* TryGetEntity(EntityId id) const;
		bool IsValid(EntityId id) const;

		Entity* CloneEntity(Entity* entity, const std::string& name, const math::Vector3& position = math::Vector3::Zero, const math::Quaternion& rotation = math::Quaternion::Identity, const math::Vector3& scale = math::Vector3(1.0f), bool retainHierarchy = true);

		Entity* CloneEntity(Entity* entity, bool retainHierarchy = true);

		std::vector<Entity*> MergeFrom(Scene* scene, std::vector<std::pair<Entity*, Entity*>>* outSourceToMerged = nullptr);

		void DestroyEntity(EntityId entityId, bool broadcast = true);
		void DestroyEntity(Entity* entity, bool broadcast = true);

		void OnEntityAddComponent(Entity* entity, ComponentSignature previousSignature, BaseComponent* component);
		void OnEntityRemoveComponent(Entity* entity, ComponentSignature previousSignature, BaseComponent* component);

		void Clear();

		void Update(float frameTime);

		void FixedUpdate(float frameTime);

		void LateUpdate(float frameTime);

		void Lock();
		void Unlock();
		bool TryLock();

		void FlushPVS(Entity* entity, bool remove=false);
		void ForceRebuildPVS();

		void RenderEntities(PVS* pvs, LayerMask layerMask, MeshRenderFlags renderFlags);
		void RenderCustom(Scene* scene, Camera* camera, MeshRenderFlags renderFlags);

		void OnGUI();


		//void RenderTransparent(const SceneRenderParameters& params);

		//void RenderParticles(const SceneRenderParameters& params);

		void RenderDebug(PVS* pvs);

		Entity* GetEntityByName(const std::string& name);
		bool RenameEntity(Entity* entity, const std::string& desiredName, std::string* outFinalName = nullptr);

		uint32_t GetNumberOfComponentsOfType(const ComponentId id);

		SceneFlags GetFlags();

		Camera* GetCameraAtIndex(uint32_t index);

		uint32_t GetNumCameras() const { return (uint32_t)_cameras.size(); }

		Camera* GetMainCamera();

		void SetMainCamera(Camera* camera);

		void SetSkySphere(Entity* skySphere);

		// Navmesh persisted with the scene (baked in editor, saved as a sidecar).
		uint32_t GetNavMeshId() const { return _navMeshId; }     // NavMeshId
		bool HasNavMesh() const { return _hasNavMesh; }
		void SetNavMeshId(uint32_t id) { _navMeshId = id; _hasNavMesh = true; }

		// Host-authoritative network replication for this scene. Lazily created on
		// first access; its Pump()/SendUpdates() no-op when no networking plugin is
		// loaded (g_pEnv->_networkSystem == null). See NetworkReplicationSystem.
		class NetworkReplicationSystem* GetNetworkReplicationSystem();

		void CalculateBounds(math::Vector3& min, math::Vector3& max);
		bool GatherStaticMeshesInBounds(const dx::BoundingBox& bounds, std::vector<StaticMeshComponent*>& outComponents, bool includeDynamic = true);
		uint64_t GetGiGeometryRevision() const;
		// Bumped on ANY meshed entity's transform change and on geometry
		// add/change - no GI exclusions, no debounce. The shadow atlas keys
		// its cache invalidation off this, never off the GI revision.
		uint64_t GetShadowGeometryRevision() const { return _shadowGeometryRevision; }
		uint64_t GetGiMaterialRevision() const;
		uint64_t GetGiLightRevision() const;
		void NotifyGiMaterialStateChanged();
		void NotifyGiLightStateChanged();
		void NotifyStaticMeshChanged(StaticMeshComponent* component, bool geometryChanged, bool materialChanged);
		void NotifyEntityTransformChanged(Entity* entity);

		// PVS culling spatial cache. Mirrors the GI spatial cache but with PVS
		// semantics: meshless components stay in (HLOD streaming needs them),
		// Sky/HLOD entries are always returned, and moving entities update their
		// grid cells in place instead of dirtying the whole cache. Returns false
		// when the cache is disabled/unusable - callers fall back to a full
		// component-pool scan. shapeIntersectsCell (optional) tightens the cell
		// walk with an exact shape-vs-cell test; queryBounds must fully contain
		// the cull shape.
		bool QueryStaticMeshCullingCandidates(const dx::BoundingBox& queryBounds,
			const std::function<bool(const dx::BoundingBox& cellBounds)>& shapeIntersectsCell,
			std::vector<StaticMeshComponent*>& outComponents);
		void MarkPvsSpatialCacheDirty();
		// In-place cell update for one entity's static mesh entries (called on
		// every transform-cache clear, including the no-notify physics writeback
		// path, so the grid tracks movers without full rebuilds).
		void UpdatePvsSpatialEntriesForEntity(Entity* entity);
		// Per-frame settle sweep for the GI motion debounce (called from Update while
		// holding _lock): re-bakes meshes that have stopped moving for a few frames.
		void UpdateGiMotionDebounce();
		void CalculateSceneStats(std::vector<math::Vector3>& vertices, std::vector<uint16_t>& indices, uint32_t& numFaces, EntityFlags excludeFlags = EntityFlags::None);
		void CalculateSceneStats_UInt32(std::vector<math::Vector3>& vertices, std::vector<uint32_t>& indices, uint32_t& numFaces, EntityFlags excludeFlags = EntityFlags::None);

		// Gather the world-space footprints of all NavMeshBlockingVolume components,
		// for the navmesh bake to carve roads / restore crossings.
		void GatherNavMeshBlockingVolumes(std::vector<NavMeshBlockingBox>& out);

		// Gather the world-space off-mesh links from all NavMeshLinkComponent
		// components, fed to Detour at bake time as off-mesh connections.
		void GatherNavMeshLinks(std::vector<NavMeshLink>& out);

		const EntityMap& GetEntities() const;

		bool GetEntities(const ComponentSignature signature, std::vector<Entity*>& entities);
		bool GetLiveEntityIds(std::vector<EntityId>& entityIds);

		template <typename T>
		bool GetComponents(std::vector<T*>& components)
		{
			std::unique_lock lock(_lock);
			if constexpr (std::is_same_v<T, UpdateComponent>)
			{
				components.reserve(_updateComponents.size());
				for (auto* component : _updateComponents)
				{
					components.push_back(component);
				}
				return !components.empty();
			}

			const auto* pool = TryGetComponentPool(T::_GetComponentId());
			if (pool == nullptr || pool->components.empty())
				return false;

			components.reserve(pool->components.size());
			for (auto* component : pool->components)
			{
				components.push_back(static_cast<T*>(component));
			}
			return components.size() > 0;
		}

		template <typename T>
		T* GetComponent(EntityId entityId)
		{
			std::unique_lock lock(_lock);
			const auto* pool = TryGetComponentPool(T::_GetComponentId());
			if (pool == nullptr)
				return nullptr;

			return static_cast<T*>(pool->Get(entityId));
		}

		template <typename T>
		bool HasComponent(EntityId entityId)
		{
			return GetComponent<T>(entityId) != nullptr;
		}

		uint32_t GetTotalNumberOfEntities();

		//void GetLights(std::vector<Light*>& lights) const;

		DirectionalLight* GetSunLight();

		void SetFogColour(const math::Color& colour);
		void SetAmbientLight(const math::Vector4& ambient);
		void SetWeatherSurfaceParams(const WeatherSurfaceParams& params);
		const math::Color& GetFogColour() const;
		const math::Vector4& GetAmbientColour() const;
		const WeatherSurfaceParams& GetWeatherSurfaceParams() const;
		// Snow footprint ring buffer (Phase 3 Part B). Walking entities Emit()
		// into it; the renderer stamps the live prints into the deformation map.
		SnowFootprintSystem& GetSnowFootprints();

		//void RenderSkySphere();

		uint32_t GetNumberOfEntitiesDrawn();
		uint32_t GetDrawCalls();

		void AddEntityListener(IEntityListener* listener);
		void RemoveEntityListener(IEntityListener* listener);

		OceanSettings& GetOcean() { return _oceanSettings; }

		// ---- Ocean surface queries (underwater S0) ----
		// The sea is displaced on the GPU; these evaluate the SAME wave function
		// on the CPU (OceanWaveModel.hpp) with the wind/time the renderer last
		// uploaded, so gameplay and the underwater camera test agree with what
		// is on screen.

		/** @brief True when the scene contains water-material ocean tiles (or a sea-level override). */
		bool HasOcean() const { return _hasOcean || _seaLevelOverrideEnabled; }
		/** @brief World Y of the UNDISPLACED sea plane. */
		float GetSeaLevel() const { return _seaLevelOverrideEnabled ? _seaLevelOverride : _seaLevel; }
		/** @brief World Y of the wave surface above (x, z). Falls back to sea level with no ocean. */
		float GetWaterHeight(float x, float z) const;
		/** @brief Depth of a point below the wave surface in metres (> 0 = submerged, < 0 = above). */
		float GetDepthBelowWater(const math::Vector3& position) const;
		bool IsUnderwater(const math::Vector3& position) const { return HasOcean() && GetDepthBelowWater(position) > 0.0f; }
		/** @brief Upper bound on wave height above sea level for the current sea state. */
		float GetWaveAmplitudeBound() const;

		/** @brief Per-scene sea-level override (the auto-detected tile height is used when disabled). */
		void SetSeaLevelOverride(bool enabled, float level) { _seaLevelOverrideEnabled = enabled; _seaLevelOverride = level; }

		/** @brief Re-detects sea level from the water-material tiles. Cheap pool walk; the renderer calls it every couple of seconds. */
		void RefreshSeaLevel();
		/** @brief Renderer-only: records the exact wave inputs uploaded to the GPU this frame. */
		void SetOceanWaveInputs(const OceanWaves::WaveInputs& inputs, const math::Vector3& cameraPosition)
		{
			_oceanWaveInputs = inputs;
			_oceanCameraPosition = cameraPosition;
		}

		void UpdateSkySphereMatrix();

		void SetEntityNamingPolicy(EntityNamingPolicy policy);

		virtual void OnDebugGUI() override;

		void SetFlags(SceneFlags flags);

		//void PushTerrainParams(const TerrainGenerationParams& params);
		//const std::vector<TerrainGenerationParams>& GetTerrainParams() const;
		//void ClearTerrainParams();

		void RegisterMessageListener(MessageListener* listener);
		void UnregisterMessageListener(MessageListener* listener);
		void RegisterCustomRenderer(ISceneCustomRenderer* renderer);
		void UnregisterCustomRenderer(ISceneCustomRenderer* renderer);
		void BroadcastMessage(Message* message);

		void Save(json& key, JsonFile* file);
		void Load(json& key, JsonFile* file);

		const std::wstring& GetName() const;
		void SetName(const std::wstring& name);

		bool DidAnyDrawnItemReflect() const { return _didAnyDrawnItemReflect; }

		bool DidPvsReset() const { return _wasPvsReset; }

		// Drains entities that DestroyEntity deferred into _pendingRemovals
		// (i.e. destroyed during iteration). Normally happens automatically
		// at the next Update; expose it so callers that need a synchronous
		// "this scene is clean RIGHT NOW" guarantee (e.g. IconService
		// resetting its preview scene between two icon renders in the
		// same frame) can force the drain immediately. Safe to call when
		// the pending set is empty.
		void DrainPendingRemovals() { HandlePendingRemovals(); }

	private:
		struct ComponentPool
		{
			std::vector<BaseComponent*> components;
			std::vector<EntityId> owners;
			std::vector<uint32_t> sparseEntityToDense;

			void EnsureEntityCapacity(uint32_t slotCount);
			BaseComponent* Get(EntityId id) const;
			bool Has(EntityId id) const;
			uint32_t Add(EntityId owner, BaseComponent* component);
			bool Remove(EntityId owner, BaseComponent** outRemoved = nullptr, EntityId* outMovedOwner = nullptr, uint32_t* outMovedDenseIndex = nullptr);
		};

		struct EntitySlot
		{
			// Lifetime phases:
			// 1) allocated-not-live: alive=true, inLiveList=false, denseEntityIndex=InvalidDenseIndex.
			//    Components may already be attached during CreateEntityId before AddEntityInternal.
			// 2) live: alive=true, inLiveList=true, denseEntityIndex points into _liveEntities.
			// 3) free: alive=false, entity=nullptr, inLiveList=false, denseEntityIndex=InvalidDenseIndex.
			uint32_t generation = 1;
			uint32_t denseEntityIndex = InvalidDenseIndex;
			Entity* entity = nullptr;
			bool alive = false;
			bool inLiveList = false;
			std::vector<uint32_t> componentDenseIndices;
		};

		void HandlePendingRemovals();
		void HandlePendingAdditions();
		EntityId AllocateEntityId(Entity* entity);
		void FreeEntityId(EntityId id);
		void EnsureSlotComponentCapacity(EntitySlot& slot, ComponentId componentId);
		ComponentPool* GetOrCreateComponentPool(ComponentId componentId);
		ComponentPool* TryGetComponentPool(ComponentId componentId);
		const ComponentPool* TryGetComponentPool(ComponentId componentId) const;
		void MarkEntityViewDirty();
		void RebuildEntityViewCache() const;
		void AddUpdateComponent(UpdateComponent* component);
		void RemoveUpdateComponent(UpdateComponent* component);
		void ValidateInvariants_NoLock() const;

		void AddEntityInternal(Entity* entity);
		void RemoveEntityInternal(Entity* entity);
		bool IsGiStaticLayer(Layer layer) const;
		void RebuildGiSpatialCache_NoLock();
		void RebuildPvsSpatialCache_NoLock();
		void InsertPvsSpatialEntryIntoCells_NoLock(uint32_t entryIndex);
		void RemovePvsSpatialEntryFromCells_NoLock(uint32_t entryIndex);
		void AddPvsSpatialEntry_NoLock(Entity* entity, StaticMeshComponent* component);
		void RemovePvsSpatialEntry_NoLock(StaticMeshComponent* component);
		void ProcessPvsSpatialPendingUpdates_NoLock();

	private:
		struct GiSpatialCellKey
		{
			int32_t x = 0;
			int32_t y = 0;
			int32_t z = 0;

			bool operator==(const GiSpatialCellKey& other) const
			{
				return x == other.x && y == other.y && z == other.z;
			}
		};

		struct GiSpatialCellKeyHash
		{
			size_t operator()(const GiSpatialCellKey& key) const
			{
				size_t hash = std::hash<int32_t>{}(key.x);
				hash ^= std::hash<int32_t>{}(key.y) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
				hash ^= std::hash<int32_t>{}(key.z) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
				return hash;
			}
		};

		struct GiSpatialEntry
		{
			StaticMeshComponent* component = nullptr;
			Entity* entity = nullptr;
			dx::BoundingBox worldBounds = {};
			bool isStaticLayer = false;
		};

		std::wstring _name;
		EntityNamingPolicy _namingPolicy = EntityNamingPolicy::AutoRename;

		SceneFlags _flags = (SceneFlags::Updateable | SceneFlags::Renderable | SceneFlags::PostProcessingEnabled);

		mutable EntityMap _entities;
		mutable bool _entityViewDirty = true;
		std::vector<EntitySlot> _entitySlots;
		std::vector<uint32_t> _freeEntitySlotIndices;
		std::vector<EntityId> _liveEntities;
		std::unordered_map<ComponentId, ComponentPool> _componentPools;
		std::vector<UpdateComponent*> _updateComponents;
		std::unordered_map<UpdateComponent*, uint32_t> _updateComponentIndices;
		std::map<std::string, Entity*> _entNameMap;

		std::set<Entity*> _pendingAdditions;
		std::set<Entity*> _pendingRemovals;

		volatile bool _insideEntityIteration = false;
		uint32_t _drawnEntities = 0;
		uint32_t _drawCalls = 0;

		DirectionalLight* _sunLight = nullptr;

		Camera* _mainCamera = nullptr;
		std::vector<Camera*> _cameras;

		uint32_t _navMeshId = 0;        // NavMeshId of this scene's navmesh (when _hasNavMesh)
		bool _hasNavMesh = false;
		class NetworkReplicationSystem* _networkReplicationSystem = nullptr; // lazily created, freed in Destroy()

		SceneUpdateFlags _updateFlags = SceneUpdateNone;

		math::Color _fogColour;
		Entity* _skySphere = nullptr;

		std::thread _updateThread;

		math::Vector4 _ambientLight;

		std::vector<IEntityListener*> _entityListeners;

		OceanSettings _oceanSettings;
		// Ocean surface state (see the query block above). _seaLevel is
		// detected from the ocean tiles; the override is serialized beside
		// _oceanSettings (NOT inside it - that struct sits mid-cbuffer and its
		// size is part of the GPU layout).
		bool _hasOcean = false;
		float _seaLevel = 0.0f;
		bool _seaLevelOverrideEnabled = false;
		float _seaLevelOverride = 0.0f;
		OceanWaves::WaveInputs _oceanWaveInputs;
		math::Vector3 _oceanCameraPosition = math::Vector3::Zero;
		WeatherSurfaceParams _weatherSurfaceParams;
		SnowFootprintSystem _snowFootprints;

		std::recursive_mutex _lock;

		std::vector<MessageListener*> _auxMessageListeners;
		std::vector<ISceneCustomRenderer*> _customRenderers;

		bool _didAnyDrawnItemReflect = false;
		bool _wasPvsReset = true;
		uint64_t _giGeometryRevision = 1ull;
		uint64_t _shadowGeometryRevision = 1ull;
		uint64_t _giMaterialRevision = 1ull;
		uint64_t _giLightRevision = 1ull;
		bool _giSpatialCacheDirty = true;
		// GI motion debounce: meshes currently moving (component -> last-motion
		// frame). While present they're excluded from the voxel triangle list so
		// per-frame motion doesn't force a full rebuild; on settle they're re-baked.
		uint64_t _giFrameNumber = 0ull;
		struct GiMovingMeshState
		{
			uint64_t lastMotionFrame = 0ull;
			uint64_t lastBakeFrame = 0ull;
		};
		std::unordered_map<StaticMeshComponent*, GiMovingMeshState> _giMovingMeshes;
		std::vector<GiSpatialEntry> _giSpatialEntries;
		std::unordered_map<GiSpatialCellKey, std::vector<uint32_t>, GiSpatialCellKeyHash> _giSpatialCells;
		std::vector<uint32_t> _giSpatialOverflowEntries;
		std::unordered_map<StaticMeshComponent*, uint32_t> _giSpatialQueryStampByComponent;
		uint32_t _giSpatialQueryStamp = 1u;

		// PVS culling spatial cache (see QueryStaticMeshCullingCandidates).
		// LOOSE grid: every gridded entry lives in exactly ONE cell (by AABB
		// centre) and is guaranteed to extend at most one cell beyond it, so a
		// query just inflates its range by a cell. Occupied cells therefore
		// never exceed the entry count (the multi-cell version hit 164k cells
		// for 16k entities and each query walked all of them).
		struct PvsSpatialEntry
		{
			StaticMeshComponent* component = nullptr;
			Entity* entity = nullptr;
			dx::BoundingBox worldBounds = {};
			GiSpatialCellKey cell = {};
			// Not stored in cells: Sky/HLOD proxies (must always be returned) and
			// entries too large for the loose-grid guarantee.
			bool ungridded = false;
			bool alwaysInclude = false;
			uint32_t lastQueryStamp = 0u;
		};
		struct PvsSpatialCell
		{
			GiSpatialCellKey key = {};
			std::vector<uint32_t> entries;
		};
		std::vector<PvsSpatialEntry> _pvsSpatialEntries;
		// Dense cell storage (the query's wide-range fallback walks this
		// linearly) + a key->index map for range lookups.
		std::vector<PvsSpatialCell> _pvsSpatialCellList;
		std::unordered_map<GiSpatialCellKey, uint32_t, GiSpatialCellKeyHash> _pvsSpatialCellIndex;
		std::vector<uint32_t> _pvsSpatialUngriddedEntries;
		std::unordered_map<Entity*, std::vector<uint32_t>> _pvsSpatialEntriesByEntity;
		std::unordered_map<StaticMeshComponent*, uint32_t> _pvsSpatialEntryByComponent;
		// Movers queue here (cheap set insert per transform event) and are
		// re-placed lazily at the next query - queries only happen on PVS
		// rebuilds, so per-frame motion costs no bounds recomputes at all.
		std::unordered_set<Entity*> _pvsSpatialPendingUpdates;
		bool _pvsSpatialCacheDirty = true;
		uint32_t _pvsSpatialQueryStamp = 0u;
		uint32_t _pvsSpatialDeadEntries = 0u;
		// Adaptive loose-grid cell size (see RebuildPvsSpatialCache_NoLock).
		float _pvsSpatialCellSize = 256.0f;
		size_t _pvsSpatialAlwaysIncludeCount = 0;
		size_t _pvsSpatialPoolSizeAtBuild = 0;
		
	};
}
