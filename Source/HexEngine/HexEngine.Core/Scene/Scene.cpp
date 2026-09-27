

#include "Scene.hpp"
#include "../HexEngine.hpp"
#include "NetworkReplicationSystem.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <unordered_map>

#include "../Entity/Component/Transform.hpp"
#include "../Entity/Component/StaticMeshComponent.hpp"
#include "../Entity/Component/PointLight.hpp"
#include "../Entity/Component/SpotLight.hpp"
#include "../Entity/Component/FirstPersonCameraController.hpp"
#include "../Entity/Component/InstancedStaticMeshComponent.hpp"
#include "../Entity/Component/NavMeshBlockingVolume.hpp"
#include "../Entity/Component/NavMeshLinkComponent.hpp"
#include "PVS.hpp"

namespace HexEngine
{
#if 0//defined(_DEBUG)
#define HEX_VALIDATE_SCENE_INVARIANTS() ValidateInvariants_NoLock()
#else
#define HEX_VALIDATE_SCENE_INVARIANTS() ((void)0)
#endif

	extern HVar r_debugScene;
	extern HVar r_interpolate;
	extern HVar r_lodPartition;

	HVar r_profileDisableShadowSampling("r_profileDisableShadowSampling", "Disable shadow-map sampling in static mesh materials for profiling", false, false, true);
	HVar r_profileDisableNormalMaps("r_profileDisableNormalMaps", "Disable normal map bindings in static mesh materials for profiling", false, false, true);
	HVar r_profileDisableSurfaceMaps("r_profileDisableSurfaceMaps", "Disable roughness, metallic, AO, height, emission and opacity map bindings in static mesh materials for profiling", false, false, true);
	HVar phys_debug("phys_debug", "Enable the physics debugger (very slow)", false, false, true);
	HVar r_debugRenderSkips("r_debugRenderSkips", "Log per-pass render skip counters for scene entity rendering", false, false, true);
	HVar r_gpuCullUseIndirectDraw("r_gpuCullUseIndirectDraw", "Reserved for a GPU-written ExecuteIndirect path (D3D12). Ignored on D3D11 - CPU-written per-draw indirect args were measurably slower than direct draws", false, false, true);
	HVar r_snowShellDebug("r_snowShellDebug", "Log why the snow shell draw does/doesn't fire, once/sec per material", false, false, true);

	namespace
	{
		constexpr float kGiSpatialCellSize = 32.0f;
		constexpr int32_t kGiSpatialMaxCoveredCells = 512;

		inline int32_t GiSpatialCellCoord(float value)
		{
			return static_cast<int32_t>(std::floor(value / kGiSpatialCellSize));
		}

		// PVS culling grid: much coarser cells than GI - candidates only need to
		// be conservative (the PVS does the exact sphere/frustum test per
		// entity), and coarse cells keep both the occupied-cell count and the
		// per-entry covered-cell count small, which is what the rebuild and the
		// query walk pay for.
		// The cell size is ADAPTIVE: picked at each grid rebuild from the entry
		// size distribution (90th percentile of max half-extent), so ~90% of
		// entries satisfy the loose-grid "fits in one cell" rule whatever the
		// scene's units/scale are. A fixed 256 left 15k of 21k entries ungridded
		// (and therefore returned by EVERY query) in a large-scale city scene.
		constexpr float kPvsSpatialMinCellSize = 32.0f;
		constexpr float kPvsSpatialMaxCellSize = 32768.0f;

		inline int32_t PvsSpatialCellCoord(float value, float cellSize)
		{
			return static_cast<int32_t>(std::floor(value / cellSize));
		}

		// Same plane extraction (and sign convention) as GpuVisibilityCulling::
		// BuildFrustumPlanes / GpuFrustumCull.shader: a sphere is OUTSIDE when
		// dot(plane.xyz, center) + plane.w < -radius for any plane. Building the
		// planes once per pass makes the per-renderable test 6 dot products -
		// dx::BoundingFrustum::Intersects re-derives its planes on every call.
		inline void BuildPvsFineCullPlanes(const math::Matrix& m, math::Vector4 outPlanes[6])
		{
			outPlanes[0] = math::Vector4(m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41);
			outPlanes[1] = math::Vector4(m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41);
			outPlanes[2] = math::Vector4(m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42);
			outPlanes[3] = math::Vector4(m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42);
			outPlanes[4] = math::Vector4(m._13, m._23, m._33, m._43);
			outPlanes[5] = math::Vector4(m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43);

			for (int32_t i = 0; i < 6; ++i)
			{
				const math::Vector3 n(outPlanes[i].x, outPlanes[i].y, outPlanes[i].z);
				const float len = std::max(n.Length(), 1e-5f);
				outPlanes[i] /= len;
			}
		}
	}

	HVar r_pvsSpatialGrid("r_pvsSpatialGrid", "Gather PVS culling candidates from the spatial grid instead of scanning every static mesh component", true, false, true);
	HVar r_pvsFineCull("r_pvsFineCull", "Per-renderable frustum test at draw submission (culls between PVS rebuilds)", true, false, true);
	HVar r_pvsPerfLog("r_pvsPerfLog", "Log PVS rebuild and spatial grid timings/counters", false, false, true);
	HVar r_pvsForceRebuildOnRemove("r_pvsForceRebuildOnRemove", "Legacy: force-rebuild EVERY PVS on any frame an entity was removed (removal already flushes the entity from all PVSes incrementally)", false, false, true);

	void Scene::ComponentPool::EnsureEntityCapacity(uint32_t slotCount)
	{
		if (sparseEntityToDense.size() < slotCount)
		{
			sparseEntityToDense.resize(slotCount, Scene::InvalidDenseIndex);
		}
	}

	BaseComponent* Scene::ComponentPool::Get(EntityId id) const
	{
		if (!id.IsValid() || id.index >= sparseEntityToDense.size())
			return nullptr;

		const uint32_t denseIndex = sparseEntityToDense[id.index];
		if (denseIndex == Scene::InvalidDenseIndex || denseIndex >= components.size())
			return nullptr;

		const EntityId owner = owners[denseIndex];
		if (owner != id)
			return nullptr;

		return components[denseIndex];
	}

	bool Scene::ComponentPool::Has(EntityId id) const
	{
		return Get(id) != nullptr;
	}

	uint32_t Scene::ComponentPool::Add(EntityId owner, BaseComponent* component)
	{
		EnsureEntityCapacity(owner.index + 1);

		const uint32_t existing = sparseEntityToDense[owner.index];
		if (existing != Scene::InvalidDenseIndex && existing < owners.size() && owners[existing] == owner)
		{
			components[existing] = component;
			return existing;
		}

		const uint32_t denseIndex = static_cast<uint32_t>(components.size());
		components.push_back(component);
		owners.push_back(owner);
		sparseEntityToDense[owner.index] = denseIndex;
		return denseIndex;
	}

	bool Scene::ComponentPool::Remove(EntityId owner, BaseComponent** outRemoved, EntityId* outMovedOwner, uint32_t* outMovedDenseIndex)
	{
		if (!owner.IsValid() || owner.index >= sparseEntityToDense.size())
			return false;

		const uint32_t denseIndex = sparseEntityToDense[owner.index];
		if (denseIndex == Scene::InvalidDenseIndex || denseIndex >= owners.size() || owners[denseIndex] != owner)
			return false;

		if (outRemoved != nullptr)
			*outRemoved = components[denseIndex];

		const uint32_t lastIndex = static_cast<uint32_t>(components.size() - 1);

		if (denseIndex != lastIndex)
		{
			components[denseIndex] = components[lastIndex];
			const EntityId movedOwner = owners[lastIndex];
			owners[denseIndex] = movedOwner;
			sparseEntityToDense[movedOwner.index] = denseIndex;

			if (outMovedOwner != nullptr)
				*outMovedOwner = movedOwner;
			if (outMovedDenseIndex != nullptr)
				*outMovedDenseIndex = denseIndex;
		}
		else
		{
			if (outMovedOwner != nullptr)
				*outMovedOwner = InvalidEntityId;
			if (outMovedDenseIndex != nullptr)
				*outMovedDenseIndex = InvalidDenseIndex;
		}

		components.pop_back();
		owners.pop_back();
		sparseEntityToDense[owner.index] = Scene::InvalidDenseIndex;
		return true;
	}

	Scene::ComponentPool* Scene::GetOrCreateComponentPool(ComponentId componentId)
	{
		auto [it, inserted] = _componentPools.try_emplace(componentId);
		ComponentPool& pool = it->second;
		if (inserted)
		{
			pool.EnsureEntityCapacity(static_cast<uint32_t>(_entitySlots.size()));
		}
		return &pool;
	}

	Scene::ComponentPool* Scene::TryGetComponentPool(ComponentId componentId)
	{
		auto it = _componentPools.find(componentId);
		if (it == _componentPools.end())
			return nullptr;
		return &it->second;
	}

	const Scene::ComponentPool* Scene::TryGetComponentPool(ComponentId componentId) const
	{
		auto it = _componentPools.find(componentId);
		if (it == _componentPools.end())
			return nullptr;
		return &it->second;
	}

	void Scene::EnsureSlotComponentCapacity(EntitySlot& slot, ComponentId componentId)
	{
		if (slot.componentDenseIndices.size() <= componentId)
		{
			slot.componentDenseIndices.resize(componentId + 1, InvalidDenseIndex);
		}
	}

	EntityId Scene::AllocateEntityId(Entity* entity)
	{
		uint32_t index = 0;

		if (!_freeEntitySlotIndices.empty())
		{
			index = _freeEntitySlotIndices.back();
			_freeEntitySlotIndices.pop_back();
		}
		else
		{
			index = static_cast<uint32_t>(_entitySlots.size());
			_entitySlots.emplace_back();
		}

		EntitySlot& slot = _entitySlots[index];
		slot.alive = true;
		slot.inLiveList = false;
		slot.denseEntityIndex = InvalidDenseIndex;
		slot.entity = entity;
		std::fill(slot.componentDenseIndices.begin(), slot.componentDenseIndices.end(), InvalidDenseIndex);
		for (auto& poolIt : _componentPools)
		{
			poolIt.second.EnsureEntityCapacity(static_cast<uint32_t>(_entitySlots.size()));
		}

		EntityId id;
		id.index = index;
		id.generation = slot.generation;
		return id;
	}

	void Scene::FreeEntityId(EntityId id)
	{
		if (!id.IsValid() || id.index >= _entitySlots.size())
			return;

		EntitySlot& slot = _entitySlots[id.index];
		slot.alive = false;
		slot.inLiveList = false;
		slot.denseEntityIndex = InvalidDenseIndex;
		slot.entity = nullptr;
		std::fill(slot.componentDenseIndices.begin(), slot.componentDenseIndices.end(), InvalidDenseIndex);
		++slot.generation;
		_freeEntitySlotIndices.push_back(id.index);
	}

	bool Scene::IsValid(EntityId id) const
	{
		if (!id.IsValid() || id.index >= _entitySlots.size())
			return false;

		const EntitySlot& slot = _entitySlots[id.index];
		return slot.alive && slot.generation == id.generation && slot.entity != nullptr;
	}

	Entity* Scene::TryGetEntity(EntityId id) const
	{
		if (!IsValid(id))
			return nullptr;

		return _entitySlots[id.index].entity;
	}

	void Scene::MarkEntityViewDirty()
	{
		_entityViewDirty = true;
	}

	uint64_t Scene::GetGiGeometryRevision() const
	{
		return _giGeometryRevision;
	}

	uint64_t Scene::GetGiMaterialRevision() const
	{
		return _giMaterialRevision;
	}

	uint64_t Scene::GetGiLightRevision() const
	{
		return _giLightRevision;
	}

	// Diagnostic sibling of DiffuseGI's r_giLogRebuilds: names WHO bumped a GI
	// revision, so a rebuild log showing geomRevOk=0/matRevOk=0 can be traced
	// straight to its source.
	HVar r_giLogInvalidations("r_giLogInvalidations", "Log every GI revision bump with its source", false, false, true);

	void Scene::NotifyGiMaterialStateChanged()
	{
		std::unique_lock lock(_lock);
		++_giMaterialRevision;
		if (r_giLogInvalidations._val.b)
			LOG_INFO("GI matRev bump -> %llu (NotifyGiMaterialStateChanged)", (unsigned long long)_giMaterialRevision);
	}

	void Scene::NotifyGiLightStateChanged()
	{
		std::unique_lock lock(_lock);
		++_giLightRevision;
		if (r_giLogInvalidations._val.b)
			LOG_INFO("GI lightRev bump -> %llu (NotifyGiLightStateChanged)", (unsigned long long)_giLightRevision);
	}

	void Scene::NotifyStaticMeshChanged(StaticMeshComponent* component, bool geometryChanged, bool materialChanged)
	{
		std::unique_lock lock(_lock);
		if (component == nullptr)
			return;

		if (geometryChanged)
		{
			++_giGeometryRevision;
			++_shadowGeometryRevision;
			_giSpatialCacheDirty = true;
		}

		if (materialChanged)
		{
			++_giMaterialRevision;
		}

		if (r_giLogInvalidations._val.b && (geometryChanged || materialChanged))
		{
			auto* entity = component->GetEntity();
			LOG_INFO("GI %s%sRev bump (mesh '%s')",
				geometryChanged ? "geom" : "",
				materialChanged ? (geometryChanged ? "+mat" : "mat") : "",
				entity != nullptr ? entity->GetName().c_str() : "<null>");
		}
	}

	void Scene::NotifyEntityTransformChanged(Entity* entity)
	{
		std::unique_lock lock(_lock);
		if (entity == nullptr)
			return;

		// Keep the PVS culling grid tracking movers (cheap in-place cell update,
		// not a dirty flag - a full rebuild per moving entity would defeat it).
		UpdatePvsSpatialEntriesForEntity(entity);

		// Shadow revision: UNCONDITIONAL for anything with a mesh, unlike the
		// GI revision below which inherits GI's exclusions and debounce. The
		// shadow atlas first keyed off the GI revision and objects flagged
		// ExcludeFromGI (small props) silently stopped invalidating shadow
		// tiles - a dragged trash can kept its stale shadow (user-found).
		// Per-frame bumps while dragging are fine: the atlas render budget
		// bounds the refresh cost, and a live shadow during the drag is what
		// you want anyway.
		if (auto* anyMesh = entity->GetComponent<StaticMeshComponent>();
			anyMesh != nullptr && anyMesh->GetMesh() != nullptr)
		{
			++_shadowGeometryRevision;
		}

		// GI motion debounce - deterministic-GI era. Movers STAY in the voxel
		// world: the old behaviour dropped a moving mesh from GI entirely
		// (motion-excluded until it settled), so a dragged object's bounce
		// folded to zero for the whole move and only re-baked ~6 frames after
		// release. With the sliced, snapshot-completing gather a rebake is
		// cheap enough to run at a THROTTLED cadence while the mesh moves -
		// its GI pose lags a few frames, but its light never disappears.
		if (auto* staticMesh = entity->GetComponent<StaticMeshComponent>();
			staticMesh != nullptr && staticMesh->GetMesh() != nullptr && !staticMesh->GetExcludeFromGI())
		{
			constexpr uint64_t kMovingRebakeFrames = 4ull;
			auto it = _giMovingMeshes.find(staticMesh);
			if (it == _giMovingMeshes.end())
			{
				++_giGeometryRevision;
				_giSpatialCacheDirty = true;
				if (r_giLogInvalidations._val.b)
					LOG_INFO("GI geomRev bump -> %llu (motion start '%s')",
						(unsigned long long)_giGeometryRevision, entity->GetName().c_str());
				_giMovingMeshes[staticMesh] = { _giFrameNumber, _giFrameNumber };
			}
			else
			{
				it->second.lastMotionFrame = _giFrameNumber;
				if (_giFrameNumber - it->second.lastBakeFrame >= kMovingRebakeFrames)
				{
					++_giGeometryRevision;
					_giSpatialCacheDirty = true;
					it->second.lastBakeFrame = _giFrameNumber;
					if (r_giLogInvalidations._val.b)
						LOG_INFO("GI geomRev bump -> %llu (moving rebake '%s')",
							(unsigned long long)_giGeometryRevision, entity->GetName().c_str());
				}
			}
		}

		if (entity->GetComponent<PointLight>() != nullptr ||
			entity->GetComponent<SpotLight>() != nullptr ||
			entity->GetComponent<DirectionalLight>() != nullptr)
		{
			++_giLightRevision;
			if (r_giLogInvalidations._val.b)
				LOG_INFO("GI lightRev bump -> %llu (light entity moved '%s')",
					(unsigned long long)_giLightRevision, entity->GetName().c_str());
		}
	}

	void Scene::UpdateGiMotionDebounce()
	{
		// Caller (Scene::Update) holds _lock. Advance the scene frame counter and
		// re-bake any mesh that has stopped moving for kSettleFrames - a single
		// revision bump apiece, versus a full GI rebuild every frame while moving.
		++_giFrameNumber;
		if (_giMovingMeshes.empty())
			return;

		constexpr uint64_t kSettleFrames = 6ull;
		for (auto it = _giMovingMeshes.begin(); it != _giMovingMeshes.end();)
		{
			if (_giFrameNumber - it->second.lastMotionFrame >= kSettleFrames)
			{
				// Final rebake at the settled pose (the throttled cadence may
				// have left the last few frames of movement unbaked).
				++_giGeometryRevision;
				_giSpatialCacheDirty = true;
				if (r_giLogInvalidations._val.b)
					LOG_INFO("GI geomRev bump -> %llu (motion settle)", (unsigned long long)_giGeometryRevision);
				it = _giMovingMeshes.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	bool Scene::IsGiStaticLayer(Layer layer) const
	{
		return
			layer == Layer::StaticGeometry ||
			layer == Layer::Decorative ||
			layer == Layer::Grass;
	}

	void Scene::RebuildGiSpatialCache_NoLock()
	{
		_giSpatialEntries.clear();
		_giSpatialCells.clear();
		_giSpatialOverflowEntries.clear();
		_giSpatialQueryStampByComponent.clear();
		_giSpatialQueryStamp = 1u;

		const auto* pool = TryGetComponentPool(StaticMeshComponent::_GetComponentId());
		if (pool == nullptr)
		{
			_giSpatialCacheDirty = false;
			return;
		}

		_giSpatialEntries.reserve(pool->components.size());
		for (uint32_t denseIndex = 0; denseIndex < pool->components.size(); ++denseIndex)
		{
			auto* component = static_cast<StaticMeshComponent*>(pool->components[denseIndex]);
			if (component == nullptr || component->GetMesh() == nullptr)
				continue;

			Entity* entity = TryGetEntity(pool->owners[denseIndex]);
			if (entity == nullptr || entity->IsPendingDeletion())
				continue;
			if (entity->HasFlag(EntityFlags::DoNotRender) && !component->GetIncludeInGIWhenHidden())
				continue;

			const Layer layer = entity->GetLayer();
			if (layer == Layer::Sky || layer == Layer::Invisible)
				continue;

			GiSpatialEntry entry = {};
			entry.component = component;
			entry.entity = entity;
			entry.worldBounds = entity->GetWorldAABB();
			entry.isStaticLayer = IsGiStaticLayer(layer);
			const uint32_t entryIndex = static_cast<uint32_t>(_giSpatialEntries.size());
			_giSpatialEntries.push_back(entry);

			const math::Vector3 center(entry.worldBounds.Center.x, entry.worldBounds.Center.y, entry.worldBounds.Center.z);
			const math::Vector3 extents(entry.worldBounds.Extents.x, entry.worldBounds.Extents.y, entry.worldBounds.Extents.z);
			const math::Vector3 min = center - extents;
			const math::Vector3 max = center + extents;
			const int32_t minCellX = GiSpatialCellCoord(min.x);
			const int32_t minCellY = GiSpatialCellCoord(min.y);
			const int32_t minCellZ = GiSpatialCellCoord(min.z);
			const int32_t maxCellX = GiSpatialCellCoord(max.x);
			const int32_t maxCellY = GiSpatialCellCoord(max.y);
			const int32_t maxCellZ = GiSpatialCellCoord(max.z);
			const int64_t spanX = static_cast<int64_t>(maxCellX) - static_cast<int64_t>(minCellX) + 1ll;
			const int64_t spanY = static_cast<int64_t>(maxCellY) - static_cast<int64_t>(minCellY) + 1ll;
			const int64_t spanZ = static_cast<int64_t>(maxCellZ) - static_cast<int64_t>(minCellZ) + 1ll;
			const int64_t coveredCells = spanX * spanY * spanZ;
			if (coveredCells <= 0ll || coveredCells > kGiSpatialMaxCoveredCells)
			{
				_giSpatialOverflowEntries.push_back(entryIndex);
				continue;
			}

			for (int32_t z = minCellZ; z <= maxCellZ; ++z)
			{
				for (int32_t y = minCellY; y <= maxCellY; ++y)
				{
					for (int32_t x = minCellX; x <= maxCellX; ++x)
					{
						_giSpatialCells[GiSpatialCellKey{ x, y, z }].push_back(entryIndex);
					}
				}
			}
		}

		_giSpatialCacheDirty = false;
	}

	void Scene::RebuildEntityViewCache() const
	{
		if (!_entityViewDirty)
			return;

		_entities.clear();

		for (const EntityId id : _liveEntities)
		{
			if (!IsValid(id))
				continue;

			Entity* entity = _entitySlots[id.index].entity;
			if (entity == nullptr)
				continue;

			_entities[entity->GetComponentSignature()].push_back(entity);
		}

		_entityViewDirty = false;
	}

	void Scene::ValidateInvariants_NoLock() const
	{
#if defined(_DEBUG)
		for (uint32_t denseIndex = 0; denseIndex < _liveEntities.size(); ++denseIndex)
		{
			const EntityId id = _liveEntities[denseIndex];
			HEX_ASSERT(IsValid(id));

			const EntitySlot& slot = _entitySlots[id.index];
			HEX_ASSERT(slot.inLiveList);
			HEX_ASSERT(slot.denseEntityIndex == denseIndex);
			HEX_ASSERT(slot.entity != nullptr);
			HEX_ASSERT(slot.entity->GetId() == id);
		}

		for (uint32_t slotIndex = 0; slotIndex < _entitySlots.size(); ++slotIndex)
		{
			const EntitySlot& slot = _entitySlots[slotIndex];
			if (slot.alive)
			{
				HEX_ASSERT(slot.entity != nullptr);

				// Legit transient state: entity slot is allocated and components are being added
				// before the entity is inserted into the dense live-entity list.
				if (slot.inLiveList)
				{
					HEX_ASSERT(slot.denseEntityIndex < _liveEntities.size());
					HEX_ASSERT(_liveEntities[slot.denseEntityIndex].index == slotIndex);
					HEX_ASSERT(_liveEntities[slot.denseEntityIndex].generation == slot.generation);
				}
				else
				{
					HEX_ASSERT(slot.denseEntityIndex == InvalidDenseIndex);
				}
			}
			else
			{
				HEX_ASSERT(slot.entity == nullptr);
				HEX_ASSERT(slot.inLiveList == false);
				HEX_ASSERT(slot.denseEntityIndex == InvalidDenseIndex);
			}
		}

		for (const auto& [componentId, pool] : _componentPools)
		{
			HEX_ASSERT(pool.components.size() == pool.owners.size());

			for (uint32_t denseIndex = 0; denseIndex < pool.components.size(); ++denseIndex)
			{
				HEX_ASSERT(pool.components[denseIndex] != nullptr);

				const EntityId owner = pool.owners[denseIndex];
				HEX_ASSERT(IsValid(owner));
				HEX_ASSERT(owner.index < pool.sparseEntityToDense.size());
				HEX_ASSERT(pool.sparseEntityToDense[owner.index] == denseIndex);

				const EntitySlot& ownerSlot = _entitySlots[owner.index];
				HEX_ASSERT(componentId < ownerSlot.componentDenseIndices.size());
				HEX_ASSERT(ownerSlot.componentDenseIndices[componentId] == denseIndex);
			}
		}

		for (uint32_t slotIndex = 0; slotIndex < _entitySlots.size(); ++slotIndex)
		{
			const EntitySlot& slot = _entitySlots[slotIndex];
			if (!slot.alive)
				continue;

			for (ComponentId componentId = 0; componentId < slot.componentDenseIndices.size(); ++componentId)
			{
				const uint32_t denseIndex = slot.componentDenseIndices[componentId];
				if (denseIndex == InvalidDenseIndex)
					continue;

				const auto poolIt = _componentPools.find(componentId);
				HEX_ASSERT(poolIt != _componentPools.end());

				const ComponentPool& pool = poolIt->second;
				HEX_ASSERT(denseIndex < pool.components.size());
				HEX_ASSERT(pool.owners[denseIndex].index == slotIndex);
				HEX_ASSERT(pool.owners[denseIndex].generation == slot.generation);
			}
		}

		HEX_ASSERT(_updateComponents.size() == _updateComponentIndices.size());
		for (uint32_t denseIndex = 0; denseIndex < _updateComponents.size(); ++denseIndex)
		{
			UpdateComponent* component = _updateComponents[denseIndex];
			HEX_ASSERT(component != nullptr);
			const auto indexIt = _updateComponentIndices.find(component);
			HEX_ASSERT(indexIt != _updateComponentIndices.end());
			HEX_ASSERT(indexIt->second == denseIndex);
		}
#endif
	}

	void Scene::AddUpdateComponent(UpdateComponent* component)
	{
		if (component == nullptr)
			return;

		if (auto it = _updateComponentIndices.find(component); it != _updateComponentIndices.end())
			return;

		const uint32_t index = static_cast<uint32_t>(_updateComponents.size());
		_updateComponents.push_back(component);
		_updateComponentIndices[component] = index;
		HEX_VALIDATE_SCENE_INVARIANTS();
	}

	void Scene::RemoveUpdateComponent(UpdateComponent* component)
	{
		if (component == nullptr)
			return;

		auto it = _updateComponentIndices.find(component);
		if (it == _updateComponentIndices.end())
			return;

		const uint32_t index = it->second;
		const uint32_t lastIndex = static_cast<uint32_t>(_updateComponents.size() - 1);
		if (index != lastIndex)
		{
			UpdateComponent* moved = _updateComponents[lastIndex];
			_updateComponents[index] = moved;
			_updateComponentIndices[moved] = index;
		}

		_updateComponents.pop_back();
		_updateComponentIndices.erase(it);
		HEX_VALIDATE_SCENE_INVARIANTS();
	}

	void Scene::Create(bool createSkySphere, IEntityListener* listener)
	{
		if (listener)
		{
			AddEntityListener(listener);
			if (auto* messageListener = dynamic_cast<MessageListener*>(listener); messageListener != nullptr)
			{
				RegisterMessageListener(messageListener);
			}
		}

#if 1//def _DEBUG
		g_pEnv->_debugGui->AddCallback(this);
#endif

		// Create the main camera
		//
		auto cameraEntity = CreateEntity("MainCamera");
		cameraEntity->SetLayer(Layer::Camera);
		
		_mainCamera = cameraEntity->AddComponent<Camera>();

		// Add an environment light (sun)
		//
		CreateDefaultSunLight();

		SetFogColour(math::Color(HEX_RGB_TO_FLOAT3(95, 95, 95)));

		_ambientLight = math::Vector4(0.14f, 0.14f, 0.145f, 1.0f);

		if (createSkySphere)
		{
			if (auto skyEnt = GetEntityByName("SkySphere"); skyEnt != nullptr)
			{
				_skySphere = skyEnt;
			}
			else
			{
				_skySphere = CreateEntity("SkySphere", math::Vector3::Zero, math::Quaternion::Identity, math::Vector3(2.0f));
				_skySphere->SetLayer(Layer::Sky);
				_skySphere->SetFlag(EntityFlags::ExcludeFromHLOD);
				auto sphereMesh = Mesh::Create("EngineData.Models/Primitives/sphere.hmesh");

				auto skyRenderer = _skySphere->AddComponent<StaticMeshComponent>();

				skyRenderer->SetMesh(sphereMesh);

				//auto material = Material::Create("Materials/SkySphere.hmat"); skyRenderer->GetMesh(0)->GetMaterial();

				//material->SetCullMode(CullingMode::FrontFace);
				//material->SetDepthState(DepthBufferState::DepthNone);
				skyRenderer->SetMaterial(Material::Create("EngineData.Materials/SkySphere.hmat"));
			}
		}
	}

	void Scene::CreateEmpty(bool createSkySphere, IEntityListener* listener)
	{
		if (listener)
		{
			AddEntityListener(listener);
			if (auto* messageListener = dynamic_cast<MessageListener*>(listener); messageListener != nullptr)
			{
				RegisterMessageListener(messageListener);
			}
		}

#if 1//def _DEBUG
		g_pEnv->_debugGui->AddCallback(this);
#endif

		SetFogColour(math::Color(HEX_RGB_TO_FLOAT3(95, 95, 95)));

		_ambientLight = math::Vector4(0.14f, 0.14f, 0.145f, 1.0f);

		if (createSkySphere)
		{
			if (auto skyEnt = GetEntityByName("SkySphere"); skyEnt != nullptr)
			{
				_skySphere = skyEnt;
			}
			else
			{
				_skySphere = CreateEntity("SkySphere", math::Vector3::Zero, math::Quaternion::Identity, math::Vector3(2.0f));
				_skySphere->SetLayer(Layer::Sky);
				_skySphere->SetFlag(EntityFlags::ExcludeFromHLOD);

				auto sphereMesh = Mesh::Create("EngineData.Models/Primitives/sphere.hmesh");

				auto skyRenderer = _skySphere->AddComponent<StaticMeshComponent>();

				skyRenderer->SetMesh(sphereMesh);
				skyRenderer->SetMaterial(Material::Create("EngineData.Materials/SkySphere.hmat"));
			}
		}
	}


	void Scene::CreateDefaultSunLight()
	{
		// look down ish
		//
		float pitch = -30.0f;

		auto rot = math::Quaternion::CreateFromYawPitchRoll(ToRadian(0.0f), ToRadian(pitch), 0.0f);

		auto lookDir = math::Vector3::Transform(math::Vector3::Forward, rot);
		lookDir.Normalize();

		auto lightPosCenter = math::Vector3(0, 0, 0);

		auto newPosition = lightPosCenter - (lookDir * 550.0f);

		auto sunEntity = CreateEntity("MainSun", newPosition, rot);
		_sunLight = sunEntity->AddComponent<DirectionalLight>();


	}

	void Scene::Clear()
	{
		_entities.clear();
		_entityViewDirty = true;
		_componentPools.clear();
		_liveEntities.clear();
		_entitySlots.clear();
		_freeEntitySlotIndices.clear();
		_entNameMap.clear();
		_cameras.clear();
		_updateComponents.clear();
		_updateComponentIndices.clear();

		_pendingAdditions.clear();
		_pendingRemovals.clear();

		_mainCamera = nullptr;
		_sunLight = nullptr;
		_skySphere = nullptr;
		HEX_VALIDATE_SCENE_INVARIANTS();
	}

	NetworkReplicationSystem* Scene::GetNetworkReplicationSystem()
	{
		if (_networkReplicationSystem == nullptr)
			_networkReplicationSystem = new NetworkReplicationSystem(this);
		return _networkReplicationSystem;
	}

	void Scene::Destroy()
	{
		std::unique_lock lock(_lock);

		if (_networkReplicationSystem != nullptr)
		{
			delete _networkReplicationSystem;
			_networkReplicationSystem = nullptr;
		}

		HandlePendingRemovals();
		const std::vector<EntityId> liveCopy = _liveEntities;
		for (const EntityId id : liveCopy)
		{
			DestroyEntity(id, false);
		}

		_entities.clear();
		_entityViewDirty = true;
		_componentPools.clear();
		_liveEntities.clear();
		_entitySlots.clear();
		_freeEntitySlotIndices.clear();
		_entNameMap.clear();
		_cameras.clear();
		_updateComponents.clear();
		_updateComponentIndices.clear();

		_pendingAdditions.clear();
		_pendingRemovals.clear();

		_sunLight = nullptr;
		_mainCamera = nullptr;
		_skySphere = nullptr;

		g_pEnv->_debugGui->RemoveCallback(this);
		HEX_VALIDATE_SCENE_INVARIANTS();
	}

	EntityId Scene::CreateEntityId(const std::string& name, const math::Vector3& position, const math::Quaternion& rotation, const math::Vector3& scale)
	{
		std::unique_lock lock(_lock);

		PROFILE();

		std::string entityName = name;

		if (auto existingEnt = GetEntityByName(name); existingEnt != nullptr)
		{
			if (_namingPolicy == EntityNamingPolicy::AutoRename)
			{
				while (true)
				{
					if (auto p = entityName.find_last_not_of("0123456789"); p != entityName.npos)
					{
						int32_t num = p < entityName.length() - 1 ? std::stoi(entityName.substr(p + 1)) : 1;
						std::string nameWithoutNum = entityName.substr(0, p + 1);

						entityName = nameWithoutNum + std::to_string(num + 1);

						existingEnt = GetEntityByName(entityName);

						if (existingEnt == nullptr)
							break;
					}
				}
			}
			else
			{
				LOG_WARN("Cannot create an entity named '%s', an existing entity already exists with this name", name.c_str());
				return existingEnt->GetId();
			}
		}

		Entity* entity = new Entity(this);
		const EntityId id = AllocateEntityId(entity);
		entity->_entityId = id;

		// Set the entity name
		//
		entity->SetName(entityName);

		// Create a transform component
		//
		entity->AddComponent<Transform>();
		entity->SetPosition(position);
		entity->SetRotation(rotation);
		entity->SetScale(scale);

		if (_insideEntityIteration)
		{
			_pendingAdditions.insert(entity);
		}
		else
		{
			AddEntityInternal(entity);
		}

		if (entity->IsCreated() == false)
			entity->Create();

		return id;
	}

	Entity* Scene::CreateEntity(const std::string& name, const math::Vector3& position, const math::Quaternion& rotation, const math::Vector3& scale)
	{
		const EntityId id = CreateEntityId(name, position, rotation, scale);
		return TryGetEntity(id);
	}

	Entity* Scene::CloneEntity(Entity* entity, const std::string& name, const math::Vector3& position, const math::Quaternion& rotation, const math::Vector3& scale, bool retainHierarchy)
	{
		std::unique_lock lock(_lock);

		Entity* clone = CreateEntity(name /*+ " (Clone)"*/, position, rotation, scale);

		clone->SetLayer(entity->GetLayer());
		// Carry the source's authored entity flags (DoNotBlockNavMesh, DoNotRender,
		// DoNotSave, ...) onto the clone - CloneEntity previously copied only the layer,
		// so these were lost on prefab spawn / duplicate. Strip the editor-only
		// selection flag so clones aren't born selected (matches Entity::Deserialize).
		clone->SetFlag(entity->GetFlags() & ~EntityFlags::SelectedInEditor);
		if (entity->IsPrefabInstance())
		{
			clone->SetPrefabSource(entity->GetPrefabSourcePath(), entity->GetPrefabRootEntityName(), entity->IsPrefabInstanceRoot());
			clone->SetPrefabNodeId(entity->EnsurePrefabNodeId());
			clone->SetPrefabPropertyOverrides(entity->GetPrefabPropertyOverrides());
			clone->SetPrefabOverridePatches(entity->GetPrefabOverridePatches());
		}
		else
		{
			clone->ClearPrefabSource();
		}

		if (retainHierarchy && entity->GetParent())
		{
			// preserveWorldPosition MUST be false here. At this point the
			// clone was just created at root with local = source's local
			// (passed into CreateEntity above), so its "world" position is
			// really just that local-space value sitting in a worldspace
			// slot. Reparenting with the default preserveWorldPosition=true
			// would re-derive a new local from that bogus world via
			// parent.worldInverse, placing the duplicate at roughly
			// (source.local - parent.translation) instead of alongside
			// source. We want the clone's existing local kept as-is and
			// just attached to the source's parent, which is exactly what
			// preserveWorldPosition=false does.
			clone->SetParent(entity->GetParent(), false);
		}

		for (auto& comp : entity->GetAllComponents())
		{
			// already has a transform so skip
			if (comp->GetComponentId() == Transform::_GetComponentId())
				continue;

			auto cls = g_pEnv->_classRegistry->Find(comp->GetComponentName());

			if (!cls)
			{
				LOG_CRIT("Could not find an corresponding entry in the class registry for '%s'", comp->GetComponentName());
				DestroyEntity(clone);
				return nullptr;
			}

			auto clonedComponent = cls->cloneInstanceFn(clone, comp);

			clone->AddComponent(clonedComponent);
		}

		// Even though the PVS was already flushed in CreateEntity we have to flush it again because it has had all its components added now
		FlushPVS(entity);

		return clone;
	}

	Entity* Scene::CloneEntity(Entity* entity, bool retainHierarchy)
	{
		return CloneEntity(entity, entity->GetName(), entity->GetPosition(), entity->GetRotation(), entity->GetScale(), retainHierarchy);
	}

	std::vector<Entity*> Scene::MergeFrom(Scene* scene, std::vector<std::pair<Entity*, Entity*>>* outSourceToMerged)
	{
		std::vector<std::tuple<Entity*, Entity*, std::string, std::string>> renamedEnts;

		std::vector<Entity*> newEnts;

		for (auto& map : scene->GetEntities())
		{
			for (auto& ent : map.second)
			{
				auto newEnt = CloneEntity(ent, false);

				renamedEnts.push_back({ newEnt, ent, newEnt->GetName(), ent->GetName() });
				if (outSourceToMerged != nullptr)
				{
					outSourceToMerged->push_back({ ent, newEnt });
				}

				newEnts.push_back(newEnt);
			}
		}

		auto findRenamedParent = [renamedEnts](const std::string& name)
		{
			for (auto& r : renamedEnts)
			{
				auto clonedEnt = std::get<0>(r);
				auto originalEnt = std::get<1>(r);
				auto clonedName = std::get<2>(r);
				auto originalName = std::get<3>(r);

				if (originalName == name)
					return clonedEnt;
			}
			return (Entity*)nullptr;
		};

		// manually run back through and fix the parenting
		for (auto& r : renamedEnts)
		{
			auto clonedEnt = std::get<0>(r);
			auto originalEnt = std::get<1>(r);
			auto clonedName = std::get<2>(r);
			auto originalName = std::get<3>(r);

			if (auto parent = originalEnt->GetParent(); parent != nullptr)
			{
				auto clonedParent = findRenamedParent(parent->GetName());

				if (clonedParent)
				{
					// preserveWorldPosition=false: the cloned entity's current local
					// position IS the source's authored local (CloneEntity copied it
					// into _current). Letting SetParent's world-preservation recompute
					// run here would treat that value as the entity's current WORLD
					// position (the clone has no parent yet, so world == local), then
					// compute new_local = source.local - parent.world - giving a
					// completely different value from what the prefab author intended.
					// The new param keeps the cloned local as-is and only changes the
					// parent link.
					clonedEnt->SetParent(clonedParent, false);
				}
			}
		}

		ForceRebuildPVS();
		return newEnts;
	}

	void Scene::AddEntityInternal(Entity* entity)
	{
		std::unique_lock lock(_lock);
		if (entity == nullptr)
			return;

		const EntityId id = entity->GetId();
		if (!IsValid(id))
			return;

		EntitySlot& slot = _entitySlots[id.index];
		if (slot.inLiveList)
			return;

		slot.denseEntityIndex = static_cast<uint32_t>(_liveEntities.size());
		_liveEntities.push_back(id);
		slot.inLiveList = true;

		for (auto&& listener : _entityListeners)
		{
			listener->OnAddEntity(entity);
		}

		_updateFlags |= SceneUpdateAddedEntity;

		_entNameMap[entity->GetName()] = entity;
		MarkEntityViewDirty();

		FlushPVS(entity);
		HEX_VALIDATE_SCENE_INVARIANTS();
	}

	void Scene::FlushPVS(Entity* entity, bool remove)
	{
		for (auto& camera : _cameras)
		{
			if(remove)
				camera->GetPVS()->RemoveEntity(entity);
			else
				camera->GetPVS()->AddEntity(entity);

			// We have to make the camera think it moved otherwise it won't refresh the PVS
			/*TransformChangedMessage message;
			message._flags = TransformChangedMessage::ChangeFlags::PositionChanged;
			message._position = camera->GetEntity()->GetPosition();

			camera->OnMessage(&message, nullptr);*/
		}

		for (auto& caster : g_pEnv->_sceneRenderer->GetShadowCasters())
		{
			if (caster->GetEntity() == entity)
				continue;

			for (auto i = 0; i < caster->GetMaxSupportedShadowCascades(); ++i)
			{
				if(remove)
					caster->GetPVS(i)->RemoveEntity(entity);
				else
					caster->GetPVS(i)->AddEntity(entity);
			}
		}
	}

	void Scene::OnGUI()
	{
		if (_mainCamera)
		{
			std::unique_lock lock(_lock);

			for (auto& renderable : _mainCamera->GetPVS()->GetRenderables())
			{
				for (auto& tuple : renderable.second)
				{
					auto ent = std::get<1>(tuple);

					ent->OnGUI();
				}
			}
		}
	}

	void Scene::ForceRebuildPVS()
	{
		for (auto& camera : _cameras)
		{
			camera->GetPVS()->ForceRebuild();
		}

		for (auto& caster : g_pEnv->_sceneRenderer->GetShadowCasters())
		{
			for (auto i = 0; i < caster->GetMaxSupportedShadowCascades(); ++i)
			{
				caster->GetPVS(i)->ForceRebuild();
			}
		}
	}

	void Scene::BroadcastMessage(Message* message)
	{
		for (auto& listener : _auxMessageListeners)
		{
			listener->OnMessage(message, nullptr);
		}
	}

	void Scene::RegisterMessageListener(MessageListener* listener)
	{
		_auxMessageListeners.push_back(listener);
	}

	void Scene::RegisterCustomRenderer(ISceneCustomRenderer* renderer)
	{
		if (renderer == nullptr)
		{
			return;
		}

		std::unique_lock lock(_lock);
		if (std::find(_customRenderers.begin(), _customRenderers.end(), renderer) == _customRenderers.end())
		{
			_customRenderers.push_back(renderer);
		}
	}

	void Scene::UnregisterMessageListener(MessageListener* listener)
	{
		_auxMessageListeners.erase(std::remove(_auxMessageListeners.begin(), _auxMessageListeners.end(), listener));
	}

	void Scene::UnregisterCustomRenderer(ISceneCustomRenderer* renderer)
	{
		std::unique_lock lock(_lock);
		_customRenderers.erase(std::remove(_customRenderers.begin(), _customRenderers.end(), renderer), _customRenderers.end());
	}

	void Scene::RemoveEntityInternal(Entity* entity)
	{
		std::unique_lock lock(_lock);		
		if (entity == nullptr)
			return;

		const EntityId id = entity->GetId();

		if (entity->GetComponent<DirectionalLight>() == _sunLight)
		{
			_sunLight = nullptr;
		}

		_pendingAdditions.erase(entity);
		_pendingRemovals.erase(entity);

		for (auto&& listener : _entityListeners)
		{
			listener->OnRemoveEntity(entity);
		}

		_updateFlags |= SceneUpdateRemovedEntity;

		_entNameMap.erase(entity->GetName());

		FlushPVS(entity, true);
		MarkEntityViewDirty();

		if (IsValid(id))
		{
			EntitySlot& slot = _entitySlots[id.index];
			if (slot.inLiveList && slot.denseEntityIndex < _liveEntities.size())
			{
				const uint32_t removeDenseIndex = slot.denseEntityIndex;
				const uint32_t lastDenseIndex = static_cast<uint32_t>(_liveEntities.size() - 1);
				if (removeDenseIndex != lastDenseIndex)
				{
					const EntityId movedId = _liveEntities[lastDenseIndex];
					_liveEntities[removeDenseIndex] = movedId;
					_entitySlots[movedId.index].denseEntityIndex = removeDenseIndex;
				}

				_liveEntities.pop_back();
				slot.inLiveList = false;
				slot.denseEntityIndex = InvalidDenseIndex;
			}
		}

		delete entity;

		if (id.IsValid())
		{
			FreeEntityId(id);
		}
		HEX_VALIDATE_SCENE_INVARIANTS();
	}

	uint32_t Scene::GetTotalNumberOfEntities()
	{
		std::unique_lock lock(_lock);
		return static_cast<uint32_t>(_liveEntities.size());
	}

	void Scene::DestroyEntity(EntityId entityId, bool broadcast)
	{
		std::unique_lock lock(_lock);
		Entity* entity = TryGetEntity(entityId);
		if (entity == nullptr)
			return;

		DestroyEntity(entity, broadcast);
	}

	void Scene::DestroyEntity(Entity* entity, bool broadcast)
	{
		std::unique_lock lock(_lock);

		PROFILE();

		if (entity == nullptr)
			return;

		LOG_DEBUG("Removing entity [%p] %s. There will be %d entities remaining in the scene", entity, entity->GetName().c_str(), GetTotalNumberOfEntities() > 0 ? GetTotalNumberOfEntities() - 1 : 0);

		if (entity == _skySphere)
		{
			_skySphere = nullptr;
		}

		if(entity->HasFlag(EntityFlags::IsPendingRemoval) == false)
			entity->DeleteMe(broadcast);

		const auto children = entity->GetChildren();
		for (auto* child : children)
		{
			DestroyEntity(child, broadcast);
		}

		if (_insideEntityIteration)
		{
			LOG_DEBUG("Entity [%p] '%s' cannot be removed immediately because iteration is in progress, but will be removed next tick", entity, entity->GetName().c_str());

			_pendingRemovals.insert(entity);			
		}
		else
		{
			RemoveEntityInternal(entity);			
		}	
	}

	void Scene::OnEntityAddComponent(Entity* entity, ComponentSignature previousSignature, BaseComponent* component)
	{
		std::unique_lock lock(_lock);
		(void)previousSignature;
		if (entity == nullptr || component == nullptr)
			return;

		const EntityId ownerId = entity->GetId();
		if (!IsValid(ownerId))
			return;

		const ComponentId componentId = component->GetComponentId();
		EntitySlot& slot = _entitySlots[ownerId.index];
		EnsureSlotComponentCapacity(slot, componentId);

		ComponentPool* pool = GetOrCreateComponentPool(componentId);
		pool->EnsureEntityCapacity(static_cast<uint32_t>(_entitySlots.size()));
		uint32_t denseIndex = pool->Add(ownerId, component);
		slot.componentDenseIndices[componentId] = denseIndex;
		MarkEntityViewDirty();

		if (auto* updateComponent = component->CastAs<UpdateComponent>(); updateComponent != nullptr)
		{
			AddUpdateComponent(updateComponent);
		}

		// attempt to automatigally set the main camera, if it hasn't already been set
		if (component->GetComponentId() == Camera::_GetComponentId())
		{
			if(_mainCamera == nullptr)
				_mainCamera = component->CastAs<Camera>();

			Camera* camera = component->CastAs<Camera>();
			if (std::find(_cameras.begin(), _cameras.end(), camera) == _cameras.end())
				_cameras.push_back(camera);
		}

		if (component->GetComponentId() == DirectionalLight::_GetComponentId() && _sunLight == nullptr)
		{
			_sunLight = component->CastAs<DirectionalLight>();
		}

		if (component->GetComponentId() == StaticMeshComponent::_GetComponentId())
		{
			++_giGeometryRevision;
			++_giMaterialRevision;
			_giSpatialCacheDirty = true;
			// Incremental, NOT a dirty flag: transient per-frame helper entities
			// add/remove mesh components constantly, and a dirty flag here meant
			// a full grid rebuild on every PVS query.
			AddPvsSpatialEntry_NoLock(entity, component->CastAs<StaticMeshComponent>());
		}
		else if (component->CastAs<Light>() != nullptr)
		{
			++_giLightRevision;
		}

		for (auto&& listener : _entityListeners)
		{
			listener->OnAddComponent(entity, component);
		}
		HEX_VALIDATE_SCENE_INVARIANTS();
	}

	void Scene::OnEntityRemoveComponent(Entity* entity, ComponentSignature previousSignature, BaseComponent* component)
	{
		std::unique_lock lock(_lock);
		(void)previousSignature;

		if (entity == nullptr || component == nullptr)
			return;

		const bool isEntityPendingDeletion = entity->IsPendingDeletion();

		const EntityId ownerId = entity->GetId();
		const ComponentId componentId = component->GetComponentId();
		if (IsValid(ownerId))
		{
			if (auto* pool = TryGetComponentPool(componentId); pool != nullptr)
			{
				EntityId movedOwner = InvalidEntityId;
				uint32_t movedDenseIndex = InvalidDenseIndex;
				pool->Remove(ownerId, nullptr, &movedOwner, &movedDenseIndex);

				if (movedOwner.IsValid() && IsValid(movedOwner))
				{
					EntitySlot& movedSlot = _entitySlots[movedOwner.index];
					EnsureSlotComponentCapacity(movedSlot, componentId);
					movedSlot.componentDenseIndices[componentId] = movedDenseIndex;
				}

				if (pool->components.empty())
				{
					_componentPools.erase(componentId);
				}
			}

			EntitySlot& slot = _entitySlots[ownerId.index];
			EnsureSlotComponentCapacity(slot, componentId);
			slot.componentDenseIndices[componentId] = InvalidDenseIndex;
		}

		if (auto* updateComponent = component->CastAs<UpdateComponent>(); updateComponent != nullptr)
		{
			RemoveUpdateComponent(updateComponent);
		}

		if (component->GetComponentId() == Camera::_GetComponentId())
		{
			_cameras.erase(std::remove(_cameras.begin(), _cameras.end(), component->CastAs<Camera>()), _cameras.end());

			if (_mainCamera == component->CastAs<Camera>())
				_mainCamera = nullptr;
		}

		if (component->GetComponentId() == DirectionalLight::_GetComponentId() && _sunLight == component->CastAs<DirectionalLight>())
		{
			_sunLight = nullptr;
		}

		if (auto* light = component->CastAs<Light>(); light != nullptr && light->GetDoesCastShadows())
		{
			g_pEnv->_sceneRenderer->RemoveShadowCaster(light);
		}

		if (component->GetComponentId() == StaticMeshComponent::_GetComponentId())
		{
			// Drop from the GI motion-debounce set so the sweep never touches a
			// destroyed component (pointers here are erased before the mesh dies).
			_giMovingMeshes.erase(component->CastAs<StaticMeshComponent>());
			++_giGeometryRevision;
			++_giMaterialRevision;
			_giSpatialCacheDirty = true;
			// Incremental tombstone (see the add-side comment).
			RemovePvsSpatialEntry_NoLock(component->CastAs<StaticMeshComponent>());
		}
		else if (component->CastAs<Light>() != nullptr)
		{
			++_giLightRevision;
		}
		MarkEntityViewDirty();

		if (!isEntityPendingDeletion)
		{
			for (auto&& listener : _entityListeners)
			{
				listener->OnRemoveComponent(entity, component);
			}
		}
		HEX_VALIDATE_SCENE_INVARIANTS();
	}

	void Scene::HandlePendingRemovals()
	{
		std::unique_lock lock(_lock);

		bool removedAny = false;

		// Drain in batches: RemoveEntityInternal mutates _pendingRemovals, so iterating
		// the set directly can invalidate iterators (and crash). Snapshot then clear first.
		while (_pendingRemovals.size() > 0)
		{
			std::vector<EntityId> pendingBatch;
			pendingBatch.reserve(_pendingRemovals.size());
			for (auto* ent : _pendingRemovals)
			{
				if (ent != nullptr)
					pendingBatch.push_back(ent->GetId());
			}

			_pendingRemovals.clear();

			for (const EntityId id : pendingBatch)
			{
				Entity* ent = TryGetEntity(id);
				if (ent == nullptr)
					continue;

				LOG_DEBUG("Entity [%p] was deferred removed", ent);

				removedAny = true;
				RemoveEntityInternal(ent);
			}
		}

		// RemoveEntityInternal -> FlushPVS already pulls the entity out of every
		// PVS batch list AND render snapshot, so this blanket force-rebuild was
		// redundant - and with transient helper entities dying most frames it
		// meant a full rebuild of the camera + every cascade PVS nearly every
		// frame, bypassing the frustum hysteresis entirely. Kept as an opt-in
		// escape hatch only.
		if (removedAny && r_pvsForceRebuildOnRemove._val.b)
			ForceRebuildPVS();
	}

	void Scene::HandlePendingAdditions()
	{
		std::unique_lock lock(_lock);

		if (_pendingAdditions.size() > 0)
		{
			for (auto& pending : _pendingAdditions)
				AddEntityInternal(pending);

			_pendingAdditions.clear();
		}
	}

	void Scene::FixedUpdate(float frameTime)
	{
		std::unique_lock lock(_lock);

		PROFILE();

		HandlePendingRemovals();
		HandlePendingAdditions();		

		_insideEntityIteration = true;

		std::vector<UpdateComponent*> updateSet;

		if (GetComponents<UpdateComponent>(updateSet))
		{
			for (auto&& component : updateSet)
			{
				auto updateComponent = component->CastAs<UpdateComponent>();

				if (!updateComponent)
					continue;

				const EntityId ownerId = updateComponent->GetOwnerId();
				Entity* owner = TryGetEntity(ownerId);
				if (owner == nullptr || owner->IsPendingDeletion())
				{
					DestroyEntity(ownerId);
					continue;
				}

				updateComponent->FixedUpdate(frameTime);
			}
		}

		_insideEntityIteration = false;

		HandlePendingRemovals();
	}

	void Scene::Update(float frameTime)
	{		
		std::unique_lock lock(_lock);

		_drawCalls = 0;
		_didAnyDrawnItemReflect = false;

		PROFILE();

		HandlePendingRemovals();
		HandlePendingAdditions();

		// Re-bake GI meshes that have stopped moving (see NotifyEntityTransformChanged).
		UpdateGiMotionDebounce();

		_insideEntityIteration = true;
		
		std::vector<UpdateComponent*> updateSet;

		if (GetComponents<UpdateComponent>(updateSet))
		{
			for (auto&& component : updateSet)
			{
				auto updateComponent = component->CastAs<UpdateComponent>();

				if (!updateComponent)
					continue;

				if (updateComponent->CanUpdate())
				{
					float timeOffset = updateComponent->GetTickRate() > 1 ? g_pEnv->_timeManager->_currentTime - updateComponent->GetLastUpdateTime() : 0.0f;
					updateComponent->Update(frameTime + timeOffset);
				}
			}
		}

		std::vector<Transform*> transformSet;

		if (GetComponents<Transform>(transformSet))
		{
			for (auto&& transform : transformSet)
			{
				if (transform != nullptr)
					transform->UpdateInterpolatedPosition(r_interpolate._val.b);
			}
		}

		if (GetMainCamera())
		{
			if (GetMainCamera()->HasMovedThisFrame())
				_updateFlags |= SceneUpdateCameraMoved;

			// Re-centre the sky dome on the camera EVERY frame, not only when
			// the camera reports movement. _hasMovedThisFrame is set on
			// rotation (mouse-look) and on a PositionChanged transform message,
			// but a player walking a straight line - or standing still while
			// only the world updates - could leave it false, so the sky dome
			// was left behind and the sky went dark. The editor fly-cam trips
			// the flag constantly, which is why it only showed up in-game.
			// Following is a single SetPosition; there is no reason to gate it.
			UpdateSkySphereMatrix();
		}

		_insideEntityIteration = false;

		HandlePendingRemovals();
	}

	void Scene::LateUpdate(float frameTime)
	{
		std::unique_lock lock(_lock);

		PROFILE();

		_insideEntityIteration = true;

		std::vector<UpdateComponent*> updateSet;

		if (GetComponents<UpdateComponent>(updateSet))
		{
			for (auto&& component : updateSet)
			{
				auto updateComponent = component->CastAs<UpdateComponent>();

				if (!updateComponent)
					continue;

				const EntityId ownerId = updateComponent->GetOwnerId();
				Entity* owner = TryGetEntity(ownerId);
				if (owner == nullptr || owner->IsPendingDeletion())
				{
					DestroyEntity(ownerId);
					continue;
				}

				if (updateComponent->CanUpdate())
					updateComponent->LateUpdate(frameTime);
			}
		}

		_insideEntityIteration = false;
		_wasPvsReset = false;

		_updateFlags = SceneUpdateNone;

		if(GetMainCamera())
			GetMainCamera()->ResetHasMovedThisFrame();

		//_didDeleteEnts = false;
		//_flushEnts = false;

		g_pEnv->_chunkManager->ChunkLoader();
	}

	void Scene::SetFogColour(const math::Color& colour)
	{
		_fogColour = colour;
	}

	void Scene::SetAmbientLight(const math::Vector4& ambient)
	{
		_ambientLight = ambient;
	}

	void Scene::SetWeatherSurfaceParams(const WeatherSurfaceParams& params)
	{
		_weatherSurfaceParams = params;
	}

	const math::Color& Scene::GetFogColour() const
	{
		return _fogColour;
	}

	const math::Vector4& Scene::GetAmbientColour() const
	{
		return _ambientLight;
	}

	const WeatherSurfaceParams& Scene::GetWeatherSurfaceParams() const
	{
		return _weatherSurfaceParams;
	}

	SnowFootprintSystem& Scene::GetSnowFootprints()
	{
		return _snowFootprints;
	}

	void Scene::Lock()
	{
		_lock.lock();
	}

	void Scene::Unlock()
	{
		_lock.unlock();
	}

	bool Scene::TryLock()
	{
		return _lock.try_lock();
	}

	const std::wstring& Scene::GetName() const
	{
		return _name;
	}

	void Scene::SetName(const std::wstring& name)
	{
		_name = name;
	}

	void Scene::SetSkySphere(Entity* skySphere)
	{
		_skySphere = skySphere;
	}

	void Scene::UpdateSkySphereMatrix()
	{
		//_skySphereMatrix = math::Matrix::CreateScale(math::Vector3(2.0f)) * math::Matrix::CreateTranslation(_mainCamera->GetEntity()->GetPosition());

		if(_skySphere)
			_skySphere->SetPosition(_mainCamera->GetEntity()->GetPosition() + _mainCamera->GetViewOffset());
	}

	void Scene::OnDebugGUI()
	{
		if (r_debugScene._val.b && _mainCamera)
		{
			auto renderer = g_pEnv->GetUIManager().GetRenderer();
			auto width = g_pEnv->GetScreenWidth();
			int32_t x = width - 20;
			int32_t y = 20;
			auto font = g_pEnv->GetUIManager().GetRenderer()->_style.font;
			auto cameraTransform = _mainCamera->GetEntity()->GetComponent<Transform>();
			const auto& cameraPos = cameraTransform->GetPosition();
			auto pvs = _mainCamera->GetPVS();
			const auto& optimisedPvs = pvs->GetOptimisedParams();
			const auto& frustum = _mainCamera->GetFrustum();
			const auto& frustumSphere = optimisedPvs.shape.sphere;// _mainCamera->GetFrustumSphere();

			if (_updateFlags != 0)
			{
				renderer->PrintText(font.get(), 14, x, y, math::Color(1, 0, 0, 1), FontAlign::Right, L"Flushing entities"); y += 15;
			}
			else
			{
				renderer->PrintText(font.get(), 14, x, y, math::Color(0, 01, 0, 1), FontAlign::Right, L"Not flushing entities"); y += 15;
			}

			renderer->PrintText(font.get(), 14, x, y, math::Color(1, 1, 0.5f, 1), FontAlign::Right, std::format(L"Camera pos {:.2f} {:.2f} {:.2f} ", cameraPos.x, cameraPos.y, cameraPos.z));
			y += 15;

			renderer->PrintText(font.get(), 14, x, y, math::Color(1, 1, 0.5f, 1), FontAlign::Right, std::format(L"Frustum centre pos {:.2f} {:.2f} {:.2f}", frustum.Origin.x, frustum.Origin.y, frustum.Origin.z));
			y += 15;

			renderer->PrintText(font.get(), 14, x, y, math::Color(1, 1, 0.5f, 1), FontAlign::Right, std::format(L"Frustum sphere pos {:.2f} {:.2f} {:.2f} Radius {:.2f}", frustumSphere.Center.x, frustumSphere.Center.y, frustumSphere.Center.z, frustumSphere.Radius));
			y += 15;

#if 0
			for (int32_t i = 0; i < 7; ++i)
			{
				int32_t drawn = 0;
				for (auto& renderSet : _renderables[i])
				{
					drawn += renderSet.second.size();
				}
				renderer->PrintText(font, 14, x, y, math::Color(1, 1, 1, 1), FontAlign::Right, std::format(L"VisShape[{:d}] pos {:.2f} {:.2f} {:.2f} Ents {:d}", i, _visSpheres[i].shape.Center.x, _visSpheres[i].shape.Center.y, _visSpheres[i].shape.Center.z, drawn));
				y += 15;
			}
#endif

			renderer->PrintText(font.get(), 14, x, y, math::Color(1, 1, 1, 1), FontAlign::Right, std::format(L"Drawn entities {:d}", pvs->GetTotalNumberOfEnts())); y += 15;
			renderer->PrintText(font.get(), 14, x, y, math::Color(1, 1, 1, 1), FontAlign::Right, std::format(L"Drawn skeletal animators {:d}", pvs->GetTotalSkeletalAnimators())); y += 15;
			renderer->PrintText(font.get(), 14, x, y, math::Color(1, 1, 1, 1), FontAlign::Right, std::format(L"Draw calls {:d}", _drawCalls)); y += 15;
			renderer->PrintText(font.get(), 14, x, y, math::Color(1, 1, 1, 1), FontAlign::Right, std::format(L"Chunks visible {:d}", g_pEnv->_chunkManager->GetNumChunksVisible())); y += 15;
			if (g_pEnv && g_pEnv->_sceneRenderer)
			{
				if (auto* gpuCulling = g_pEnv->_sceneRenderer->GetGpuVisibilityCulling(); gpuCulling != nullptr)
				{
					const auto& cullStats = gpuCulling->GetStats();
					renderer->PrintText(font.get(), 14, x, y, math::Color(0.75f, 1.0f, 0.85f, 1.0f), FontAlign::Right, std::format(L"GPU Cull cand {:d} vis {:d}", cullStats.totalCandidates, cullStats.visibleInstances)); y += 15;
					renderer->PrintText(font.get(), 14, x, y, math::Color(0.75f, 1.0f, 0.85f, 1.0f), FontAlign::Right, std::format(L"GPU Cull reject F {:d} O {:d}", cullStats.frustumRejected, cullStats.occlusionRejected)); y += 15;
					renderer->PrintText(font.get(), 14, x, y, math::Color(0.75f, 1.0f, 0.85f, 1.0f), FontAlign::Right, std::format(L"GPU Cull ms CPU {:.2f} F {:.2f} O {:.2f}", cullStats.cpuBuildMs, cullStats.gpuFrustumMs, cullStats.gpuOcclusionMs)); y += 15;
				}
			}
		}
	}

	namespace
	{
		bool IsMaterialTransparent(const Material* material)
		{
			if (!material)
				return false;

			if (material->_properties.hasTransparency == 1 || material->_properties.isWater == 1)
				return true;

			return material->GetBlendState() != BlendState::Opaque;
		}

		bool PrepareMeshRender(Entity* entity, Mesh* mesh, Material* material, MeshRenderFlags flags, int32_t instanceId, CullingMode shadowCullMode)
		{
			if (!mesh || !material)
				return false;

			auto shader = material->GetStandardShader();
			const bool isShadowMap = (flags & MeshRenderFlags::MeshRenderShadowMap) != 0;
			const bool disableShadowSampling = r_profileDisableShadowSampling._val.b;
			const bool disableNormalMaps = r_profileDisableNormalMaps._val.b;
			const bool disableSurfaceMaps = r_profileDisableSurfaceMaps._val.b;
			static auto defaultTexture = ITexture2D::GetDefaultTexture();

			if (isShadowMap)
			{
				auto shadowShader = material->GetShadowMapShader();
				if (!shadowShader)
					shadowShader = IShader::Create("EngineData.Shaders/ShadowMapGeometry.hcs"); // it should never get here, hard fallback
				if (shadowShader)
					shader = shadowShader;
			}

			if (!shader)
			{
				LOG_WARN("Cannot render a mesh without a valid shader, please check the material has a shader applied!");
				return false;
			}

			auto graphicsDevice = g_pEnv->_graphicsDevice;
			graphicsDevice->SetPixelShader(shader->GetShaderStage(ShaderStage::PixelShader));
			graphicsDevice->SetVertexShader(shader->GetShaderStage(ShaderStage::VertexShader));
			graphicsDevice->SetInputLayout(shader->GetInputLayout());

			const bool isTransparency = (flags & MeshRenderFlags::MeshRenderTransparency) != 0;
			const bool isWaterMaterial = material->_properties.isWater == 1;
			BlendState effectiveBlendState = material->GetBlendState();
			if (isTransparency)
			{
				// Water shader already composites scene lighting/refraction into its output;
				// keep it opaque to avoid double alpha blending in the transparent pass.
				if (isWaterMaterial)
				{
					effectiveBlendState = BlendState::Opaque;
				}
				else if (effectiveBlendState == BlendState::Opaque || effectiveBlendState == BlendState::Transparency)
				{
					effectiveBlendState = BlendState::TransparencyPreserveAlpha;
				}
			}
			const DepthBufferState effectiveDepthState =
				(isTransparency && !isWaterMaterial && material->GetDepthState() == DepthBufferState::DepthDefault)
				? DepthBufferState::DepthRead
				: material->GetDepthState();

			material->SaveRenderState();
			graphicsDevice->SetBlendState(effectiveBlendState);
			graphicsDevice->SetDepthBufferState(effectiveDepthState);
			// Water is DOUBLE-SIDED (underwater S2): with back-face culling the
			// sea simply was not drawn from underneath - a submerged camera saw
			// only the odd wavelet steep enough to present its front face (they
			// showed as stray bright flecks). From above a height-field has no
			// visible back faces, so this costs nothing there; Water.shader picks
			// the air-side or the Snell's-window shading from the camera depth
			// (g_oceanConfig3.z), not from the triangle facing.
			const CullingMode materialCullMode = isWaterMaterial ? CullingMode::NoCulling : material->GetCullMode();
			graphicsDevice->SetCullingMode(isShadowMap ? shadowCullMode : materialCullMode);

			mesh->UpdateConstantBuffer(entity, math::Matrix::Identity, material, instanceId, isTransparency);

			auto requirements = shader->GetRequirements();
			if (HEX_HASFLAG(requirements, ShaderRequirements::RequiresGBuffer))
				g_pEnv->_sceneRenderer->GetGBuffer()->BindAsShaderResource();

			if (HEX_HASFLAG(requirements, ShaderRequirements::RequiresShadowMaps))
			{
				if (disableShadowSampling)
					graphicsDevice->SetTexture2D(nullptr);
				else
					g_pEnv->_sceneRenderer->GetCurrentShadowMap()->BindAsShaderResource();
			}

			if (HEX_HASFLAG(requirements, ShaderRequirements::RequiresBeauty))
				graphicsDevice->SetTexture2D(g_pEnv->_sceneRenderer->GetBeautyTexture());

			uint32_t slotIdx = graphicsDevice->GetBoundResourceIndex();

			std::vector<ITexture2D*> textures = {
				material->GetTexture(MaterialTexture::Albedo).get(),
				disableNormalMaps ? defaultTexture.get() : material->GetTexture(MaterialTexture::Normal).get(),
				disableSurfaceMaps ? defaultTexture.get() : material->GetTexture(MaterialTexture::Roughness).get(),
				disableSurfaceMaps ? defaultTexture.get() : material->GetTexture(MaterialTexture::Metallic).get(),
				disableSurfaceMaps ? defaultTexture.get() : material->GetTexture(MaterialTexture::Height).get(),
				disableSurfaceMaps ? defaultTexture.get() : material->GetTexture(MaterialTexture::Emission).get(),
				disableSurfaceMaps ? defaultTexture.get() : material->GetTexture(MaterialTexture::Opacity).get(),
				disableSurfaceMaps ? defaultTexture.get() : material->GetTexture(MaterialTexture::AmbientOcclusion).get()
			};

			graphicsDevice->SetTexture2DArray(slotIdx, textures);

			mesh->SetBuffers(isShadowMap);

			// Tessellation opt-in (Phase 3 snow displacement). This batched
			// snapshot path - not StaticMeshComponent::RenderMesh - is what
			// actually draws environment meshes, so the same HS/DS + patch
			// topology switch has to live here too: a tessellation VS emits
			// control points (no SV_Position), so without the hull/domain
			// stages bound the mesh rasterises nothing (the "road vanished"
			// symptom). Mirrors RenderMesh exactly; redundancy-cached clears
			// keep non-tessellated draws free.
			IShaderStage* hullStage   = isShadowMap ? nullptr : shader->GetShaderStage(ShaderStage::HullShader);
			IShaderStage* domainStage = isShadowMap ? nullptr : shader->GetShaderStage(ShaderStage::DomainShader);
			if (hullStage != nullptr && domainStage != nullptr)
			{
				graphicsDevice->SetHullShader(hullStage);
				graphicsDevice->SetDomainShader(domainStage);
				auto* perFrame = graphicsDevice->GetEngineConstantBuffer(EngineConstantBuffer::PerFrameBuffer);
				graphicsDevice->SetConstantBufferHS(0, perFrame);
				graphicsDevice->SetConstantBufferDS(0, perFrame);
				graphicsDevice->SetTopology(HexEngine::PrimitiveTopology::ControlPointPatchList3);
			}
			else
			{
				graphicsDevice->SetHullShader(nullptr);
				graphicsDevice->SetDomainShader(nullptr);
			}
			return true;
		}
	}

	// B5 D3D12 visual-parity debug: `r_dumpMeshDraws N` logs the next N
	// instanced scene draws with mesh + material identity. Pairs with the
	// D3D12 plugin's r_d3d12DrawDump (match on indices/instances) to name the
	// meshes behind corrupt draw ordinals. Self-clears when the count runs out.
	HVar r_dumpMeshDraws("r_dumpMeshDraws", "Log mesh/material identity for the next N instanced draws (0 = off)", 0, 0, 100000);

	// Lazy-loaded snow shell shader (Phase 3 tess slice 4). Null until first
	// use / if the .hcs is missing, in which case the shell silently no-ops.
	IShader* GetSnowShellShader()
	{
		static std::shared_ptr<IShader> s_shell;
		static bool s_tried = false;
		if (!s_tried)
		{
			s_tried = true;
			s_shell = IShader::Create("EngineData.Shaders/SnowShell.hcs");
			if (!s_shell)
				LOG_WARN("SnowShell.hcs failed to load - snow shell disabled");
		}
		return s_shell.get();
	}

	template <typename T>
	void RenderInstance(T* instance, uint32_t numInstances, Material* material, bool& rendered, bool allowShell = false)
	{
		if (instance)
		{
			instance->Finish();

			if (numInstances > 0)
			{
				const uint32_t indexCount = instance->GetMesh()->GetNumIndices();
				if (r_dumpMeshDraws._val.i32 > 0)
				{
					--r_dumpMeshDraws._val.i32;
					LOG_INFO("MeshDraw: indices=%u instances=%u mesh='%s' material='%s'",
						indexCount, numInstances,
						instance->GetMesh()->GetName().c_str(),
						material ? material->GetName().c_str() : "(null)");
				}
				// Indirect-draw path is hard-coded to D3D11 (ID3D11Buffer,
				// D3D11_DRAW_INDEXED_INSTANCED_INDIRECT_ARGS, UpdateSubresource).
				// Under D3D12 the reinterpret_cast of GetNativeDevice() to
				// ID3D11Device* lands on a totally different vtable slot - the
				// resulting `device->CreateBuffer` dispatches to ID3D12Device's
				// GetPrivateData and the debug layer flags
				// CORRUPTED_PARAMETER2. Fall back to the direct DrawIndexed
				// path under non-D3D11 backends until a per-backend indirect
				// path (ID3D12CommandSignature + ExecuteIndirect) lands.
				// Indirect submission is DISABLED on D3D11 regardless of the cvar:
				// D3D11 has no multi-draw-indirect, so this path issued exactly
				// as many draw calls as the direct one while adding an
				// UpdateSubresource of a single shared args buffer per draw -
				// a write-after-read hazard on every call that the driver has
				// to stall or rename around. Measured slower than direct. The
				// cvar is kept for a future D3D12 ExecuteIndirect path where
				// the GPU writes the args and many draws go out per call.
				const bool useIndirect = false;
				if (useIndirect)
				{
					ID3D11Device* device = reinterpret_cast<ID3D11Device*>(g_pEnv->_graphicsDevice->GetNativeDevice());
					ID3D11DeviceContext* context = reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext());
					if (device != nullptr && context != nullptr)
					{
						static ID3D11Buffer* s_indirectArgsBuffer = nullptr;
						if (s_indirectArgsBuffer == nullptr)
						{
							D3D11_BUFFER_DESC argsDesc = {};
							argsDesc.ByteWidth = sizeof(D3D11_DRAW_INDEXED_INSTANCED_INDIRECT_ARGS);
							argsDesc.Usage = D3D11_USAGE_DEFAULT;
							argsDesc.BindFlags = 0;
							argsDesc.CPUAccessFlags = 0;
							argsDesc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
							device->CreateBuffer(&argsDesc, nullptr, &s_indirectArgsBuffer);
						}

						if (s_indirectArgsBuffer != nullptr)
						{
							D3D11_DRAW_INDEXED_INSTANCED_INDIRECT_ARGS args = {};
							args.IndexCountPerInstance = indexCount;
							args.InstanceCount = static_cast<UINT>(numInstances);
							args.StartIndexLocation = 0u;
							args.BaseVertexLocation = 0u;
							args.StartInstanceLocation = 0u;
							context->UpdateSubresource(s_indirectArgsBuffer, 0, nullptr, &args, 0, 0);
							g_pEnv->_graphicsDevice->DrawIndexedInstancedIndirect(s_indirectArgsBuffer, 0);
						}
						else
						{
							g_pEnv->_graphicsDevice->DrawIndexedInstanced(indexCount, static_cast<uint32_t>(numInstances));
						}
					}
					else
					{
						g_pEnv->_graphicsDevice->DrawIndexedInstanced(indexCount, static_cast<uint32_t>(numInstances));
					}
				}
				else
				{
					g_pEnv->_graphicsDevice->DrawIndexedInstanced(indexCount, static_cast<uint32_t>(numInstances));
				}

				// Snow shell (Phase 3 tess slice 4): a SECOND draw of the same
				// instances, extruded up and clipped, layered on top of the
				// rigid surface just drawn - so snow reads as accumulation ON
				// the concrete, not the concrete deforming. Opt-in per material
				// (_receivesSnow, ticked on ground materials), never in the
				// shadow pass (allowShell false there), D3D11 only. The base
				// draw already bound this mesh's per-object buffer, textures,
				// depth/blend/cull and the compatible input layout - the shell
				// reuses all of it and only swaps in its own stages + patch
				// topology + the per-frame cbuffer the HS/DS read.
				if (r_snowShellDebug._val.b)
				{
					const uint64_t frame = g_pEnv->_timeManager ? g_pEnv->_timeManager->_frameCount : 0;
					// (a) Shell-shader availability, material-independent.
					static uint64_t sLastAny = 0;
					if (frame - sLastAny >= 60)
					{
						sLastAny = frame;
						IShader* dbgShell = GetSnowShellShader();
						LOG_INFO("SnowShell: shellLoaded=%d hs=%d ds=%d backendD3D11=%d",
							dbgShell ? 1 : 0,
							(dbgShell && dbgShell->GetShaderStage(ShaderStage::HullShader)) ? 1 : 0,
							(dbgShell && dbgShell->GetShaderStage(ShaderStage::DomainShader)) ? 1 : 0,
							g_pEnv->_graphicsDevice->GetBackend() == GraphicsBackend::D3D11 ? 1 : 0);
					}
					// (b) Any receivesSnow material actually reaching the draw.
					static uint64_t sLastSnow = 0;
					if (material && material->GetReceivesSnow() && frame - sLastSnow >= 60)
					{
						sLastSnow = frame;
						LOG_INFO("SnowShell: receivesSnow mat='%s' allowShell=%d instances=%u REACHED draw",
							material->GetName().c_str(), allowShell ? 1 : 0, numInstances);
					}
				}

				if (allowShell && material && material->GetReceivesSnow() &&
					g_pEnv->_graphicsDevice->GetBackend() == GraphicsBackend::D3D11)
				{
					IShader* shell = GetSnowShellShader();
					IShaderStage* hs = shell ? shell->GetShaderStage(ShaderStage::HullShader) : nullptr;
					IShaderStage* ds = shell ? shell->GetShaderStage(ShaderStage::DomainShader) : nullptr;
					if (shell && hs && ds)
					{
						auto* gd = g_pEnv->_graphicsDevice;
						gd->SetVertexShader(shell->GetShaderStage(ShaderStage::VertexShader));
						gd->SetPixelShader(shell->GetShaderStage(ShaderStage::PixelShader));
						// Bind the SHELL vertex shader's own input layout. The base
						// draw set the concrete material's layout, which for a graph
						// material need not match the shell VS input signature - a
						// mismatch feeds the IA wrong vertex data and the domain
						// shader gets garbage positions (nothing rasterises). This
						// was the "shell draws but shows nothing" cause.
						gd->SetInputLayout(shell->GetInputLayout());
						gd->SetHullShader(hs);
						gd->SetDomainShader(ds);
						auto* perFrame = gd->GetEngineConstantBuffer(EngineConstantBuffer::PerFrameBuffer);
						gd->SetConstantBufferHS(0, perFrame);
						gd->SetConstantBufferDS(0, perFrame);
						gd->SetTopology(HexEngine::PrimitiveTopology::ControlPointPatchList3);
						gd->DrawIndexedInstanced(indexCount, static_cast<uint32_t>(numInstances));
						gd->SetHullShader(nullptr);
						gd->SetDomainShader(nullptr);
						gd->SetTopology(HexEngine::PrimitiveTopology::TriangleList);
					}
				}

				if (material)
					material->RestoreRenderState();

				rendered = false;
			}
		}
	}

	void Scene::RenderEntities(PVS* pvs, LayerMask layerMask, MeshRenderFlags renderFlags)
	{
		PROFILE();

		// Perf gate for the accumulation shell: it issues a SECOND tessellated
		// draw per _receivesSnow material. The shell now renders EITHER snow or
		// wind-blown sand (whichever weather dominates), so it's needed when
		// either snow OR dust is active; otherwise it's skipped entirely.
		const WeatherSurfaceParams& _shellWx = GetWeatherSurfaceParams();
		const bool snowActive = _shellWx.snowCoverage > 0.001f || _shellWx.dirtAmount > 0.001f;

		auto& snapshot = pvs->GetRenderableSnapshot();
		uint32_t totalCandidates = 0;
		uint32_t skippedNullMeshOrInstance = 0;
		uint32_t skippedTransparencyGate = 0;
		uint32_t skippedLayerMask = 0;
		uint32_t skippedLod = 0;
		uint32_t skippedPrepareRender = 0;
		uint32_t skippedGpuCulling = 0;
		uint32_t skippedFrustumFine = 0;
		uint32_t drawnInstancesTotal = 0;

		if(pvs->DidRebuild())
		{
			PROFILE();

			// Rebuild the snapshot IN PLACE: batch slots (and their vectors'
			// capacity) are reused positionally, and each RenderableSnapshot is
			// emplaced and filled where it lives instead of being built on the
			// stack and copied in (~500 bytes + two shared_ptr bumps per copy).
			// _pvs is an ordered map, so consecutive rebuilds with a stable
			// material set line up near-perfectly with the previous layout.
			const auto& pvsRenderables = pvs->GetRenderables();
			size_t batchCount = 0;

			for (const auto& renderableBatch : pvsRenderables)
			{
				PROFILE();

				auto material = renderableBatch.first;
				if (!material)
					continue;

				if (batchCount < snapshot.size())
				{
					snapshot[batchCount].first = material;
					snapshot[batchCount].second.clear();
				}
				else
				{
					snapshot.emplace_back(material, std::vector<RenderableSnapshot>());
				}

				auto& batch = snapshot[batchCount].second;
				batch.reserve(renderableBatch.second.size());
				++batchCount;

				for (const auto& meshEntityPair : renderableBatch.second)
				{
					PROFILE();

					auto mesh = std::get<0>(meshEntityPair);
					auto entity = std::get<1>(meshEntityPair);
					auto meshComponent = (StaticMeshComponent*)std::get<2>(meshEntityPair);
					if (!mesh || !entity || !meshComponent)
						continue;

					auto instance = mesh->GetInstance();
					if (!instance)
						continue;

					RenderableSnapshot& entry = batch.emplace_back();
					entry.mesh = mesh;
					entry.material = material;
					entry.instance = instance;
					entry.simpleInstance = instance->GetSimpleInstance();
					entry.layer = entity->GetLayer();
					entry.hasAnimations = mesh->HasAnimations();
					entry.isBoundToBone = meshComponent->IsBoundToBone();
					entry.shadowCullMode = meshComponent->GetShadowCullMode();
					entry.entity = entity;
					entry.component = meshComponent;
					entry.transformVersion = entity->GetTransformVersion();

					if (entry.isBoundToBone)
					{
						entry.shadowInstanceData.worldMatrix = entity->GetWorldTMTranspose() * meshComponent->GetOffsetMatrixTranspose();
						entry.instanceData.worldMatrix = entry.shadowInstanceData.worldMatrix;
						entry.instanceData.worldMatrixPrev = entity->GetWorldTMPrevTranspose();
						entry.instanceData.worldMatrixInverseTranspose = entity->GetWorldTMInvert();
						entry.instanceData.colour = material->_properties.diffuseColour;
						entry.instanceData.uvscale = meshComponent->GetUVScale();
					}
					else
					{
						entry.shadowInstanceData = meshComponent->GetCachedShadowInstanceData();
						entry.instanceData = meshComponent->GetCachedInstanceData(material.get());
					}
				}
			}

			snapshot.resize(batchCount);

			_wasPvsReset = true;
			pvs->ResetDidRebuild();

			if (g_pEnv && g_pEnv->_sceneRenderer)
			{
				if (auto* gpuCulling = g_pEnv->_sceneRenderer->GetGpuVisibilityCulling(); gpuCulling != nullptr)
					gpuCulling->NotifySnapshotRebuilt();
			}
		}

		bool isShadowMap = (renderFlags & MeshRenderFlags::MeshRenderShadowMap) != 0;
		bool isTransparency = (renderFlags & MeshRenderFlags::MeshRenderTransparency) != 0;
		bool isNormalRender = !isShadowMap && !isTransparency;

		// Fine-grained CPU culling: the PVS admits everything in the ENLARGED
		// hysteresis frustum and only refreshes on rebuild, so without this the
		// draw loops submit the whole enlarged set every pass. Test each
		// renderable's (entity-cached) world sphere against the owning camera's
		// CURRENT frustum. Camera-shaped PVSes only - sphere-shaped (shadow)
		// PVSes already bound their light's reach, and cached shadow tiles need
		// stable draw sets. Sky always passes; bone-bound attachments are
		// skipped because their entity bounds can lag the bone pose.
		// Two fine-cull modes:
		//  - camera passes: the owning camera's CURRENT frustum planes (the PVS
		//    coarse set is a rotation-invariant sphere, so this is what applies
		//    the actual frustum each frame);
		//  - shadow passes whose PVS carries a fine sphere (sun cascades): the
		//    actual slice sphere, so the camera-centred coarse set doesn't
		//    inflate the shadow draw list.
		bool fineCullPlanesActive = false;
		bool fineCullSphereActive = false;
		math::Vector4 fineCullPlanes[6];
		dx::BoundingSphere fineCullSphere;
		if (r_pvsFineCull._val.b && pvs != nullptr)
		{
			const auto& optimisedParams = pvs->GetOptimisedParams();
			const auto& currentParams = pvs->GetCurrentParams();
			if (!isShadowMap && !optimisedParams.isShadow && optimisedParams.camera != nullptr)
			{
				BuildPvsFineCullPlanes(
					optimisedParams.camera->GetViewProjectionMatrix(),
					fineCullPlanes);
				fineCullPlanesActive = true;
			}
			else if (isShadowMap && currentParams.hasFineSphere)
			{
				fineCullSphere = currentParams.fineSphere;
				fineCullSphereActive = true;
			}
		}

		auto isFineCulled = [&](const RenderableSnapshot& renderable) -> bool
		{
			if (!fineCullPlanesActive && !fineCullSphereActive)
				return false;
			if (renderable.layer == Layer::Sky)
				return false;
			if (renderable.isBoundToBone)
				return false;
			if (renderable.entity == nullptr)
				return false;

			const auto& worldSphere = renderable.entity->GetWorldBoundingSphere();
			if (worldSphere.Radius <= 0.0f)
				return false;

			if (fineCullPlanesActive)
			{
				for (int32_t i = 0; i < 6; ++i)
				{
					const math::Vector4& plane = fineCullPlanes[i];
					if (plane.x * worldSphere.Center.x + plane.y * worldSphere.Center.y + plane.z * worldSphere.Center.z + plane.w < -worldSphere.Radius)
						return true;
				}
				return false;
			}

			const float dx = worldSphere.Center.x - fineCullSphere.Center.x;
			const float dy = worldSphere.Center.y - fineCullSphere.Center.y;
			const float dz = worldSphere.Center.z - fineCullSphere.Center.z;
			const float radii = worldSphere.Radius + fineCullSphere.Radius;
			return (dx * dx + dy * dy + dz * dz) > (radii * radii);
		};
		const bool isOpaqueLayerSet = (layerMask & (LAYERMASK(Layer::StaticGeometry) | LAYERMASK(Layer::DynamicGeometry) | LAYERMASK(Layer::Grass))) != 0 &&
			(layerMask & LAYERMASK(Layer::Sky)) == 0;

		bool usedGpuCulling = false;
		if (isNormalRender && isOpaqueLayerSet && g_pEnv && g_pEnv->_sceneRenderer)
		{
			Camera* cullCamera = GetMainCamera();
			if (pvs != nullptr)
			{
				if (auto* pvsCamera = pvs->GetOptimisedParams().camera; pvsCamera != nullptr)
				{
					cullCamera = pvsCamera;
				}
			}

			if (auto* gpuCulling = g_pEnv->_sceneRenderer->GetGpuVisibilityCulling(); gpuCulling != nullptr)
			{
				// Hand the CPU fine cull to the GPU stage so it only receives
				// (and only occlusion-tests) what survived the frustum.
				const GpuVisibilityCulling::CpuCullPredicate cpuCulled = isFineCulled;
				usedGpuCulling = gpuCulling->CullOpaqueRenderables(snapshot, cullCamera, layerMask, renderFlags,
					(fineCullPlanesActive || fineCullSphereActive) ? &cpuCulled : nullptr);
			}
		}

		if (isNormalRender)
		{
			_drawnEntities = 0;
			_drawCalls = 0;
		}

		if (isTransparency)
		{
			struct TransparentRenderItem
			{
				std::shared_ptr<Material> material;
				RenderableSnapshot* renderable = nullptr;
				float distSq = 0.0f;
			};

			std::vector<TransparentRenderItem> transparentItems;
			transparentItems.reserve(256);
			const math::Vector3 cameraPosition = GetMainCamera()->GetEntity()->GetPosition();

			for (auto& batch : snapshot)
			{
				auto material = batch.first;

				for (auto& renderable : batch.second)
				{
					totalCandidates++;

					const auto& mesh = renderable.mesh;
					auto instance = renderable.instance;

					if (!mesh || !instance)
					{
						skippedNullMeshOrInstance++;
						continue;
					}

					if (usedGpuCulling && !renderable.gpuVisible)
					{
						skippedGpuCulling++;
						continue;
					}

					if (isFineCulled(renderable))
					{
						skippedFrustumFine++;
						continue;
					}

					if (!IsMaterialTransparent(material.get()))
					{
						skippedTransparencyGate++;
						continue;
					}

					if ((layerMask & LAYERMASK(renderable.layer)) == 0)
					{
						skippedLayerMask++;
						continue;
					}

					if (mesh->GetLodLevel() != -1)
					{
						const float lodPartitions = r_lodPartition._val.f32;
						const float minDistance = lodPartitions * static_cast<float>(mesh->GetLodLevel());
						const float maxDistance = lodPartitions * static_cast<float>(mesh->GetLodLevel() + 1);
						const float distance = (renderable.instanceData.worldMatrix.Translation() - cameraPosition).Length();

						if (mesh->GetLodLevel() < 3)
						{
							if (distance < minDistance || distance > maxDistance)
							{
								skippedLod++;
								continue;
							}
						}
						else if (distance < minDistance)
						{
							skippedLod++;
							continue;
						}
					}

					const math::Vector3 objectPosition = renderable.instanceData.worldMatrix.Translation();
					TransparentRenderItem item;
					item.material = material;
					item.renderable = &renderable;
					item.distSq = (objectPosition - cameraPosition).LengthSquared();
					transparentItems.push_back(item);
				}
			}

			std::sort(
				transparentItems.begin(),
				transparentItems.end(),
				[](const TransparentRenderItem& left, const TransparentRenderItem& right)
				{
					return left.distSq > right.distSq;
				});

			for (auto& item : transparentItems)
			{
				auto* renderable = item.renderable;
				if (!renderable)
					continue;

				auto material = item.material;
				auto mesh = renderable->mesh;
				auto* instance = renderable->instance;

				if (!mesh || !instance || !material)
					continue;

				instance->Start();
				if (!PrepareMeshRender(renderable->entity, mesh.get(), material.get(), renderFlags, _drawnEntities, renderable->shadowCullMode))
				{
					instance->Finish();
					skippedPrepareRender++;
					continue;
				}

				if (!renderable->isBoundToBone && renderable->component != nullptr && renderable->entity != nullptr)
				{
					const uint64_t transformVersion = renderable->entity->GetTransformVersion();
					if (transformVersion != renderable->transformVersion)
					{
						renderable->instanceData = renderable->component->GetCachedInstanceData(material.get());
						renderable->shadowInstanceData = renderable->component->GetCachedShadowInstanceData();
						renderable->transformVersion = transformVersion;
					}
				}

				if (renderable->layer == Layer::Sky)
				{
					renderable->instanceData.worldMatrix = renderable->entity->GetWorldTMTranspose();
					renderable->instanceData.worldMatrixPrev = renderable->entity->GetWorldTMPrevTranspose();
					renderable->instanceData.worldMatrixInverseTranspose = renderable->entity->GetWorldTMInvert();
				}

				// Bone-bound attachments need their world matrix refreshed
				// every draw, not just on PVS rebuild. PVS only re-snapshots
				// when the camera / static-mesh population changes, so a
				// static camera would freeze the attachment at whatever
				// pose the bone had at snapshot time. Recomputing here from
				// the live bone state (via GetOffsetMatrixTranspose, which
				// builds the matrix from the SkeletalAnimationComponent's
				// per-frame Position/Rotation updates) keeps hats/weapons/
				// scabbards tracking the parent animation regardless of
				// camera movement.
				if (renderable->isBoundToBone)
				{
					if (auto* smc = renderable->entity->GetComponent<StaticMeshComponent>(); smc != nullptr)
					{
						renderable->instanceData.worldMatrix              = renderable->entity->GetWorldTMTranspose() * smc->GetOffsetMatrixTranspose();
						// Motion vector: pair the entity's previous world with the
						// bone's PREVIOUS offset. Using the entity prev alone (no
						// offset) reported a full bone-offset-sized velocity every
						// frame - attachments smeared under motion blur / ghosted.
						renderable->instanceData.worldMatrixPrev          = renderable->entity->GetWorldTMPrevTranspose() * smc->GetOffsetMatrixPrevTranspose();
						renderable->instanceData.worldMatrixInverseTranspose = renderable->entity->GetWorldTMInvert();
						renderable->shadowInstanceData.worldMatrix        = renderable->instanceData.worldMatrix;
					}
				}

				instance->Render(renderable->instanceData);
				instance->Finish();
				g_pEnv->_graphicsDevice->DrawIndexedInstanced(instance->GetMesh()->GetNumIndices(), 1);
				material->RestoreRenderState();

				++drawnInstancesTotal;
				++_drawCalls;
			}
		}
		else
		{
			for (auto it = snapshot.begin(); it != snapshot.end(); it++)
			{
				auto material = it->first;

				bool rendered = false;

				int drawnInstances = 0;
				MeshInstance* currentInstance = nullptr;
				MeshInstance* lastInstance = nullptr;

				for (auto&& renderable : it->second)
				{
					totalCandidates++;

					const auto& mesh = renderable.mesh;
					auto instance = renderable.instance;
					SimpleMeshInstance* simpleInstance = renderable.simpleInstance;

					if (!mesh || !instance)
					{
						skippedNullMeshOrInstance++;
						continue;
					}

					if (usedGpuCulling && !renderable.gpuVisible)
					{
						skippedGpuCulling++;
						continue;
					}

					if (isFineCulled(renderable))
					{
						skippedFrustumFine++;
						continue;
					}

					currentInstance = instance;

					if (isShadowMap)
						currentInstance = (MeshInstance*)simpleInstance;

					if (!IsMaterialTransparent(material.get()))
					{
						if (material->DoesHaveAnyReflectivity())
							_didAnyDrawnItemReflect = true;
					}
					else
					{
						skippedTransparencyGate++;
						continue;
					}

					if (currentInstance != lastInstance && lastInstance != nullptr || renderable.hasAnimations)
					{
						if (isNormalRender)
							++_drawCalls;

						if (isShadowMap)
							RenderInstance((SimpleMeshInstance*)lastInstance, drawnInstances, material.get(), rendered);
						else
							RenderInstance(lastInstance, drawnInstances, material.get(), rendered, /*allowShell*/ snowActive);

						drawnInstances = 0;

						rendered = false;
					}

					// check this entity is in the layer mask we want
					if ((layerMask & LAYERMASK(renderable.layer)) == 0)
					{
						skippedLayerMask++;
						continue;
					}

					// Check for LOD
#if 1
					if (mesh->GetLodLevel() != -1)
					{
						const float lodPartitions = r_lodPartition._val.f32;

						float minDistance = lodPartitions * (float)(mesh->GetLodLevel());
						float maxDistance = lodPartitions * (float)(mesh->GetLodLevel() + 1);

						float distance = (renderable.instanceData.worldMatrix.Translation() - GetMainCamera()->GetEntity()->GetPosition()).Length();

						if (mesh->GetLodLevel() < 3)
						{
							if (distance < minDistance || distance > maxDistance)
							{
								skippedLod++;
								continue;
							}
						}
						else
						{
							if (distance < minDistance)
							{
								skippedLod++;
								continue;
							}
						}
					}
#endif

					if (!rendered)
					{
						if (isShadowMap)
							simpleInstance->Start();
						else
							instance->Start();

						if (PrepareMeshRender(renderable.entity, mesh.get(), material.get(), renderFlags, _drawnEntities, renderable.shadowCullMode) == false)
						{
							LOG_WARN("Failed to prepare mesh render state, ignoring this entity");
							skippedPrepareRender++;
							continue;
						}
						rendered = true;
					}

					drawnInstances++;
					drawnInstancesTotal++;
					lastInstance = currentInstance;

					// Lazy instance-data refresh: only entries that are actually
					// drawn AND whose entity moved since they were snapshotted
					// get re-pulled (the component caches by transform version).
					if (!renderable.isBoundToBone && renderable.component != nullptr && renderable.entity != nullptr)
					{
						const uint64_t transformVersion = renderable.entity->GetTransformVersion();
						if (transformVersion != renderable.transformVersion)
						{
							renderable.instanceData = renderable.component->GetCachedInstanceData(material.get());
							renderable.shadowInstanceData = renderable.component->GetCachedShadowInstanceData();
							renderable.transformVersion = transformVersion;
						}
					}

					// Per-draw refresh of bone-bound attachments. PVS snapshots
					// freeze the offset matrix; the live bone pose updates
					// every animation tick, so we have to pull it again here
					// for hats/weapons/scabbards to keep up. Done BEFORE the
					// shadow vs main split so shadow-map rendering of bound
					// attachments tracks the bone too.
					if (renderable.isBoundToBone)
					{
						if (auto* smc = renderable.entity->GetComponent<StaticMeshComponent>(); smc != nullptr)
						{
							renderable.instanceData.worldMatrix              = renderable.entity->GetWorldTMTranspose() * smc->GetOffsetMatrixTranspose();
							// See the PVS path above: prev world x prev bone offset,
							// or the attachment reports a bone-offset-sized velocity
							// every frame and smears.
							renderable.instanceData.worldMatrixPrev          = renderable.entity->GetWorldTMPrevTranspose() * smc->GetOffsetMatrixPrevTranspose();
							renderable.instanceData.worldMatrixInverseTranspose = renderable.entity->GetWorldTMInvert();
							renderable.shadowInstanceData.worldMatrix        = renderable.instanceData.worldMatrix;
						}
					}

					if (isShadowMap)
					{
						simpleInstance->Render(renderable.shadowInstanceData);
					}
					else
					{
						// Fix so sky doesn't used cached position
						if (renderable.layer == Layer::Sky)
						{
							renderable.instanceData.worldMatrix = renderable.entity->GetWorldTMTranspose();
							renderable.instanceData.worldMatrixPrev = renderable.entity->GetWorldTMPrevTranspose();
							renderable.instanceData.worldMatrixInverseTranspose = renderable.entity->GetWorldTMInvert();
						}

						instance->Render(renderable.instanceData);
					}

					if (isNormalRender)
						_drawnEntities += 1;
				}

				if (currentInstance && drawnInstances > 0)
				{
					if (isNormalRender)
						++_drawCalls;

					if (isShadowMap)
						RenderInstance((SimpleMeshInstance*)currentInstance, drawnInstances, material.get(), rendered);
					else
						RenderInstance(currentInstance, drawnInstances, material.get(), rendered, /*allowShell*/ snowActive);
				}
			}
		}

		if (r_debugRenderSkips._val.b)
		{
			static std::unordered_map<uint64_t, uint64_t> lastLoggedFrameByPass;
			const uint64_t frame = g_pEnv && g_pEnv->_timeManager ? g_pEnv->_timeManager->_frameCount : 0;
			const uint64_t passKey = (static_cast<uint64_t>(static_cast<uint32_t>(renderFlags)) << 32) | static_cast<uint64_t>(layerMask);
			const auto lastLogged = lastLoggedFrameByPass.find(passKey);
			if ((lastLogged == lastLoggedFrameByPass.end() || lastLogged->second != frame) && (frame % 60) == 0)
			{
				lastLoggedFrameByPass[passKey] = frame;
				LOG_INFO(
					"RenderEntities pass flags=%u layerMask=0x%08X candidates=%u drawn=%u skip(null=%u,trans=%u,layer=%u,lod=%u,gpu=%u,fine=%u,prep=%u) pvsRebuilt=%d snapshotBatches=%zu gpuCull=%d",
					(uint32_t)renderFlags,
					layerMask,
					totalCandidates,
					drawnInstancesTotal,
					skippedNullMeshOrInstance,
					skippedTransparencyGate,
					skippedLayerMask,
					skippedLod,
					skippedGpuCulling,
					skippedFrustumFine,
					skippedPrepareRender,
					pvs->DidRebuild() ? 1 : 0,
					snapshot.size(),
					usedGpuCulling ? 1 : 0);
			}
		}

		if (usedGpuCulling && g_pEnv && g_pEnv->_sceneRenderer)
		{
			if (auto* gpuCulling = g_pEnv->_sceneRenderer->GetGpuVisibilityCulling(); gpuCulling != nullptr)
			{
				gpuCulling->ReportSubmission(_drawCalls, drawnInstancesTotal);
			}
		}
	}

	void Scene::RenderCustom(Scene* scene, Camera* camera, MeshRenderFlags renderFlags)
	{
		(void)scene;

		std::vector<ISceneCustomRenderer*> renderers;
		{
			std::unique_lock lock(_lock);
			renderers = _customRenderers;
		}

		for (ISceneCustomRenderer* renderer : renderers)
		{
			if (renderer != nullptr)
			{
				renderer->RenderCustom(this, camera, renderFlags);
			}
		}
	}

	void Scene::RenderDebug(PVS* pvs)
	{
		std::unique_lock lock(_lock);

		PROFILE();

		for (auto& set : GetEntities())
		{
			for (auto& ent : set.second)
			{
				ent->DebugRender();
			}
		}

		// Allow the game extension to render the debug info (if they want)
		for (auto& extension : g_pEnv->GetGameExtensions())
		{
			extension->OnDebugRender();
		}

		g_pEnv->_chunkManager->DebugRender();

		if(phys_debug._val.b)
			g_pEnv->_physicsSystem->DebugRender();

		if (g_pEnv && g_pEnv->_sceneRenderer)
		{
			if (auto* gpuCulling = g_pEnv->_sceneRenderer->GetGpuVisibilityCulling(); gpuCulling != nullptr)
			{
				gpuCulling->DebugDraw(pvs->GetRenderableSnapshot());
			}
		}

		g_pEnv->_debugRenderer->FlushBuffers();

		//g_pEnv->_graphicsDevice->SetDepthBufferState(DepthBufferState::DepthDefault);
	}

	SceneFlags Scene::GetFlags()
	{
		return _flags;
	}

	uint32_t Scene::GetNumberOfEntitiesDrawn()
	{
		return _drawnEntities;
	}

	uint32_t Scene::GetDrawCalls()
	{
		return _drawCalls;
	}

	DirectionalLight* Scene::GetSunLight()
	{
		return _sunLight;
	}

	/*void Scene::AddCamera(Camera* camera)
	{
		_cameras.push_back(camera);
		AddEntity(camera);
	}*/

	Camera* Scene::GetCameraAtIndex(uint32_t index)
	{
		return _cameras.at(index);
	}

	Camera* Scene::GetMainCamera()
	{
		return _mainCamera;
	}

	void Scene::SetMainCamera(Camera* camera)
	{
		_mainCamera = camera;
	}


	const Scene::EntityMap& Scene::GetEntities() const
	{
		RebuildEntityViewCache();
		return _entities;
	}

	bool Scene::GetEntities(const ComponentSignature signature, std::vector<Entity*>& entities)
	{
		std::unique_lock lock(_lock);
		if (signature == 0)
			return false;

		if ((signature & (signature - 1)) == 0)
		{
			ComponentId componentId = 0;
			ComponentSignature mask = signature;
			while ((mask >>= 1) != 0)
			{
				++componentId;
			}

			if (const auto* pool = TryGetComponentPool(componentId); pool != nullptr)
			{
				entities.reserve(entities.size() + pool->owners.size());
				for (const EntityId owner : pool->owners)
				{
					if (!IsValid(owner))
						continue;

					Entity* entity = _entitySlots[owner.index].entity;
					if (entity != nullptr)
						entities.push_back(entity);
				}
				return !entities.empty();
			}
		}

		for (const EntityId id : _liveEntities)
		{
			if (!IsValid(id))
				continue;

			Entity* entity = _entitySlots[id.index].entity;
			if (entity == nullptr)
				continue;

			if ((entity->GetComponentSignature() & signature) != 0)
			{
				entities.push_back(entity);
			}
		}
		
		return entities.size() > 0;
	}

	bool Scene::GetLiveEntityIds(std::vector<EntityId>& entityIds)
	{
		std::unique_lock lock(_lock);
		entityIds.insert(entityIds.end(), _liveEntities.begin(), _liveEntities.end());
		return !entityIds.empty();
	}

	Entity* Scene::GetEntityByName(const std::string& name)
	{
		std::unique_lock lock(_lock);

		auto it = _entNameMap.find(name);
		if (it != _entNameMap.end())
			return it->second;

		/*for (auto& ent : _entities)
		{
			for (auto& entp : ent.second)
			{
				if (entp->GetName() == name)
					return entp;
			}
		}*/

		return nullptr;
	}

	bool Scene::RenameEntity(Entity* entity, const std::string& desiredName, std::string* outFinalName)
	{
		std::unique_lock lock(_lock);

		if (entity == nullptr || entity->GetScene() != this || desiredName.empty())
			return false;

		const std::string oldName = entity->GetName();
		if (oldName == desiredName)
		{
			if (outFinalName != nullptr)
				*outFinalName = oldName;
			return true;
		}

		auto oldNameIt = _entNameMap.find(oldName);
		if (oldNameIt != _entNameMap.end() && oldNameIt->second == entity)
		{
			_entNameMap.erase(oldNameIt);
		}

		std::string resolvedName = desiredName;
		auto existingIt = _entNameMap.find(resolvedName);
		if (existingIt != _entNameMap.end() && existingIt->second != entity)
		{
			if (_namingPolicy == EntityNamingPolicy::AutoRename)
			{
				std::string baseName = resolvedName;
				int32_t suffix = 1;

				if (auto p = baseName.find_last_not_of("0123456789"); p != baseName.npos)
				{
					if (p < baseName.length() - 1)
					{
						suffix = std::stoi(baseName.substr(p + 1));
						baseName = baseName.substr(0, p + 1);
					}
				}

				do
				{
					resolvedName = baseName + std::to_string(++suffix);
					existingIt = _entNameMap.find(resolvedName);
				} while (existingIt != _entNameMap.end() && existingIt->second != entity);
			}
			else
			{
				_entNameMap[oldName] = entity;
				return false;
			}
		}

		entity->SetName(resolvedName);
		_entNameMap[resolvedName] = entity;

		if (outFinalName != nullptr)
			*outFinalName = resolvedName;

		return true;
	}

	uint32_t Scene::GetNumberOfComponentsOfType(const ComponentId id)
	{
		std::unique_lock lock(_lock);
		if (const auto* pool = TryGetComponentPool(id); pool != nullptr)
			return static_cast<uint32_t>(pool->components.size());
		return 0;
	}

	/*bool Scene::GetComponents(ComponentId id, std::vector<BaseComponent*>& components)
	{
		std::unique_lock lock(_lock);

		components = _components[id];

		return components.size() > 0;
	}*/

	void Scene::AddEntityListener(IEntityListener* listener)
	{
		std::unique_lock lock(_lock);

		_entityListeners.push_back(listener);
	}

	void Scene::RemoveEntityListener(IEntityListener* listener)
	{
		std::unique_lock lock(_lock);

		_entityListeners.erase(std::remove(_entityListeners.begin(), _entityListeners.end(), listener), _entityListeners.end());
	}

	void Scene::SetFlags(SceneFlags flags)
	{
		_flags = flags;
	}

	/*void Scene::PushTerrainParams(const TerrainGenerationParams& params)
	{
		_terrainParams.push_back(params);
	}

	const std::vector<TerrainGenerationParams>& Scene::GetTerrainParams() const
	{
		return _terrainParams;
	}*/

	// ---- Ocean surface queries (underwater S0) -------------------------------

	void Scene::RefreshSeaLevel()
	{
		// The ocean is ordinary water-material tile entities from the "Add
		// ocean" tool, all flat grids at one height - so sea level IS their
		// world Y. Tiles at more than one height (a lake above the sea) vote:
		// the most common centimetre bucket wins. A per-water-body query is a
		// later slice; one sea level is what every current consumer needs.
		std::unique_lock lock(_lock);

		const auto* pool = TryGetComponentPool(StaticMeshComponent::_GetComponentId());
		if (pool == nullptr)
		{
			_hasOcean = false;
			return;
		}

		std::unordered_map<int32_t, uint32_t> votes;
		for (uint32_t denseIndex = 0; denseIndex < pool->components.size(); ++denseIndex)
		{
			auto* smc = static_cast<StaticMeshComponent*>(pool->components[denseIndex]);
			if (smc == nullptr)
				continue;

			auto material = smc->GetMaterial();
			if (!material || material->_properties.isWater != 1)
				continue;

			Entity* entity = TryGetEntity(pool->owners[denseIndex]);
			if (entity == nullptr)
				continue;

			const float y = entity->GetWorldTM().Translation().y;
			++votes[(int32_t)std::lround(y * 100.0f)];
		}

		if (votes.empty())
		{
			_hasOcean = false;
			return;
		}

		int32_t bestBucket = 0;
		uint32_t bestCount = 0;
		for (const auto& [bucket, count] : votes)
		{
			if (count > bestCount)
			{
				bestCount = count;
				bestBucket = bucket;
			}
		}

		_hasOcean = true;
		_seaLevel = (float)bestBucket / 100.0f;
	}

	float Scene::GetWaterHeight(float x, float z) const
	{
		if (!HasOcean())
			return GetSeaLevel();

		// The band-limit the GPU applied to the displaced detail waves depends
		// on the tessellation density there, i.e. distance from the camera.
		const float dx = x - _oceanCameraPosition.x;
		const float dz = z - _oceanCameraPosition.z;
		const float distance = std::sqrt(dx * dx + dz * dz);
		return GetSeaLevel() + OceanWaves::SurfaceOffset(x, z, _oceanWaveInputs, distance);
	}

	float Scene::GetDepthBelowWater(const math::Vector3& position) const
	{
		return GetWaterHeight(position.x, position.z) - position.y;
	}

	float Scene::GetWaveAmplitudeBound() const
	{
		return OceanWaves::AmplitudeBound(_oceanWaveInputs);
	}

	void Scene::Save(json& data, JsonFile* file)
	{
		file->Serialize<OceanSettings>(data, "_oceanSettings", _oceanSettings);
		// Beside _oceanSettings, not inside it: that struct is uploaded mid-
		// cbuffer and must not grow.
		data["_seaLevelOverrideEnabled"] = _seaLevelOverrideEnabled;
		data["_seaLevelOverride"] = _seaLevelOverride;

		// Persist the navmesh as a binary sidecar (<scene>.navmesh) next to the .hscene,
		// so it loads with the scene instead of being re-baked at runtime.
		if (_hasNavMesh && g_pEnv != nullptr && g_pEnv->_navMeshProvider != nullptr)
		{
			std::vector<uint8_t> bytes;
			if (g_pEnv->_navMeshProvider->GetNavMeshBytes(_navMeshId, bytes) && !bytes.empty())
			{
				fs::path sidecar = file->GetAbsolutePath();
				sidecar.replace_extension(L".navmesh");
				std::ofstream out(sidecar, std::ios::binary | std::ios::trunc);
				if (out.is_open())
				{
					out.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize)bytes.size());
					data["_hasNavMesh"] = true;
				}
			}
		}
	}

	void Scene::Load(json& data, JsonFile* file)
	{
		if (data.find("_oceanSettings") != data.end())
		{
			file->Deserialize<OceanSettings>(data, "_oceanSettings", _oceanSettings);
		}
		if (auto it = data.find("_seaLevelOverrideEnabled"); it != data.end() && it->is_boolean())
			_seaLevelOverrideEnabled = it->get<bool>();
		if (auto it = data.find("_seaLevelOverride"); it != data.end() && it->is_number())
			_seaLevelOverride = it->get<float>();

		// Load the navmesh sidecar (if the scene was saved with one).
		if (data.find("_hasNavMesh") != data.end() && data["_hasNavMesh"].get<bool>() &&
			g_pEnv != nullptr && g_pEnv->_navMeshProvider != nullptr)
		{
			fs::path sidecar = file->GetAbsolutePath();
			sidecar.replace_extension(L".navmesh");
			std::ifstream in(sidecar, std::ios::binary | std::ios::ate);
			if (in.is_open())
			{
				const std::streamsize sz = in.tellg();
				if (sz > 0)
				{
					in.seekg(0);
					std::vector<uint8_t> bytes((size_t)sz);
					in.read(reinterpret_cast<char*>(bytes.data()), sz);
					NavMeshId id = 0;
					if (g_pEnv->_navMeshProvider->LoadNavMeshFromBytes(this, bytes, &id))
						SetNavMeshId(id);
				}
			}
		}
	}

	void Scene::CalculateBounds(math::Vector3& min, math::Vector3& max)
	{
		std::unique_lock lock(_lock);

		min = math::Vector3(FLT_MAX);
		max = math::Vector3(FLT_MIN);

		const auto* pool = TryGetComponentPool(StaticMeshComponent::_GetComponentId());
		if (pool != nullptr)
		{
			for (const EntityId ownerId : pool->owners)
			{
				Entity* ent = TryGetEntity(ownerId);
				if (ent == nullptr || ent->IsPendingDeletion())
					continue;

				const auto& worldAABB = ent->GetWorldAABB();

				math::Vector3 bbCentre(worldAABB.Center);
				math::Vector3 bbExtents(worldAABB.Extents);
				math::Vector3 bbMin = bbCentre - bbExtents;
				math::Vector3 bbMax = bbCentre + bbExtents;

				for (int i = 0; i < 3; ++i)
				{
					if (((float*)&bbMin.x)[i] < ((float*)&min.x)[i])
						((float*)&min.x)[i] = ((float*)&bbMin.x)[i];

					if (((float*)&bbMax.x)[i] > ((float*)&max.x)[i])
						((float*)&max.x)[i] = ((float*)&bbMax.x)[i];
				}
			}
		}
	}

	bool Scene::GatherStaticMeshesInBounds(const dx::BoundingBox& bounds, std::vector<StaticMeshComponent*>& outComponents, bool includeDynamic)
	{
		std::unique_lock lock(_lock);

		outComponents.clear();

		if (_giSpatialCacheDirty)
		{
			RebuildGiSpatialCache_NoLock();
		}
		if (_giSpatialEntries.empty())
			return false;

		auto appendEntry = [&](uint32_t entryIndex)
		{
			if (entryIndex >= _giSpatialEntries.size())
				return;

			const GiSpatialEntry& entry = _giSpatialEntries[entryIndex];
			if (entry.component == nullptr || entry.entity == nullptr)
				return;
			if (!includeDynamic && !entry.isStaticLayer)
				return;
			if (!bounds.Intersects(entry.worldBounds))
				return;

			uint32_t& stamp = _giSpatialQueryStampByComponent[entry.component];
			if (stamp == _giSpatialQueryStamp)
				return;
			stamp = _giSpatialQueryStamp;
			outComponents.push_back(entry.component);
		};

		if (_giSpatialQueryStamp == std::numeric_limits<uint32_t>::max())
		{
			_giSpatialQueryStampByComponent.clear();
			_giSpatialQueryStamp = 1u;
		}
		else
		{
			++_giSpatialQueryStamp;
		}

		const math::Vector3 center(bounds.Center.x, bounds.Center.y, bounds.Center.z);
		const math::Vector3 extents(bounds.Extents.x, bounds.Extents.y, bounds.Extents.z);
		const math::Vector3 min = center - extents;
		const math::Vector3 max = center + extents;
		const int32_t minCellX = GiSpatialCellCoord(min.x);
		const int32_t minCellY = GiSpatialCellCoord(min.y);
		const int32_t minCellZ = GiSpatialCellCoord(min.z);
		const int32_t maxCellX = GiSpatialCellCoord(max.x);
		const int32_t maxCellY = GiSpatialCellCoord(max.y);
		const int32_t maxCellZ = GiSpatialCellCoord(max.z);

		for (int32_t z = minCellZ; z <= maxCellZ; ++z)
		{
			for (int32_t y = minCellY; y <= maxCellY; ++y)
			{
				for (int32_t x = minCellX; x <= maxCellX; ++x)
				{
					const auto it = _giSpatialCells.find(GiSpatialCellKey{ x, y, z });
					if (it == _giSpatialCells.end())
						continue;

					for (uint32_t entryIndex : it->second)
					{
						appendEntry(entryIndex);
					}
				}
			}
		}

		for (uint32_t entryIndex : _giSpatialOverflowEntries)
		{
			appendEntry(entryIndex);
		}

		return !outComponents.empty();
	}

	void Scene::MarkPvsSpatialCacheDirty()
	{
		std::unique_lock lock(_lock);
		_pvsSpatialCacheDirty = true;
	}

	void Scene::InsertPvsSpatialEntryIntoCells_NoLock(uint32_t entryIndex)
	{
		PvsSpatialEntry& entry = _pvsSpatialEntries[entryIndex];

		// Loose-grid guarantee: a gridded entry's AABB may extend at most one
		// cell beyond its centre cell, i.e. half-extents <= cell size. Anything
		// bigger (and Sky/HLOD) is always returned and left to the exact test.
		const float cellSize = _pvsSpatialCellSize;
		const bool tooLarge =
			!(entry.worldBounds.Extents.x <= cellSize &&
				entry.worldBounds.Extents.y <= cellSize &&
				entry.worldBounds.Extents.z <= cellSize); // also catches NaN

		if (entry.alwaysInclude || tooLarge)
		{
			entry.ungridded = true;
			_pvsSpatialUngriddedEntries.push_back(entryIndex);
			return;
		}

		entry.ungridded = false;
		entry.cell = {
			PvsSpatialCellCoord(entry.worldBounds.Center.x, cellSize),
			PvsSpatialCellCoord(entry.worldBounds.Center.y, cellSize),
			PvsSpatialCellCoord(entry.worldBounds.Center.z, cellSize) };

		auto it = _pvsSpatialCellIndex.find(entry.cell);
		if (it == _pvsSpatialCellIndex.end())
		{
			it = _pvsSpatialCellIndex.emplace(entry.cell, static_cast<uint32_t>(_pvsSpatialCellList.size())).first;
			_pvsSpatialCellList.push_back({ entry.cell, {} });
		}

		_pvsSpatialCellList[it->second].entries.push_back(entryIndex);
	}

	void Scene::RemovePvsSpatialEntryFromCells_NoLock(uint32_t entryIndex)
	{
		PvsSpatialEntry& entry = _pvsSpatialEntries[entryIndex];

		if (entry.ungridded)
		{
			auto& ungridded = _pvsSpatialUngriddedEntries;
			ungridded.erase(std::remove(ungridded.begin(), ungridded.end(), entryIndex), ungridded.end());
			return;
		}

		const auto it = _pvsSpatialCellIndex.find(entry.cell);
		if (it == _pvsSpatialCellIndex.end())
			return;

		auto& entries = _pvsSpatialCellList[it->second].entries;
		entries.erase(std::remove(entries.begin(), entries.end(), entryIndex), entries.end());
	}

	void Scene::RebuildPvsSpatialCache_NoLock()
	{
		const auto rebuildStart = std::chrono::high_resolution_clock::now();

		_pvsSpatialEntries.clear();
		_pvsSpatialCellList.clear();
		_pvsSpatialCellIndex.clear();
		_pvsSpatialUngriddedEntries.clear();
		_pvsSpatialEntriesByEntity.clear();
		_pvsSpatialEntryByComponent.clear();
		_pvsSpatialPendingUpdates.clear();
		_pvsSpatialQueryStamp = 0u;
		_pvsSpatialDeadEntries = 0u;
		_pvsSpatialPoolSizeAtBuild = 0;
		_pvsSpatialCacheDirty = false;

		const auto* pool = TryGetComponentPool(StaticMeshComponent::_GetComponentId());
		if (pool == nullptr)
			return;

		_pvsSpatialPoolSizeAtBuild = pool->components.size();
		_pvsSpatialEntries.reserve(pool->components.size());
		_pvsSpatialEntriesByEntity.reserve(pool->components.size());
		_pvsSpatialEntryByComponent.reserve(pool->components.size());

		for (uint32_t denseIndex = 0; denseIndex < pool->components.size(); ++denseIndex)
		{
			auto* component = static_cast<StaticMeshComponent*>(pool->components[denseIndex]);
			if (component == nullptr)
				continue;

			Entity* entity = TryGetEntity(pool->owners[denseIndex]);
			if (entity == nullptr || entity->IsPendingDeletion())
				continue;

			PvsSpatialEntry entry;
			entry.component = component;
			entry.entity = entity;
			entry.worldBounds = entity->GetWorldAABB();
			// Sky is unconditionally visible in PVS terms, and HLOD proxies must
			// be globally known (cluster map + streaming decisions) - never let
			// the grid hide either. Meshless components stay in too: an unloaded
			// HLOD mesh has to keep being considered so it can stream back in.
			entry.alwaysInclude =
				entity->GetLayer() == Layer::Sky ||
				entity->GetName().rfind("HLOD_", 0) == 0;

			const uint32_t entryIndex = static_cast<uint32_t>(_pvsSpatialEntries.size());
			_pvsSpatialEntries.push_back(entry);
			_pvsSpatialEntriesByEntity[entity].push_back(entryIndex);
			_pvsSpatialEntryByComponent[component] = entryIndex;
		}

		// Pick the cell size from the size distribution of the entries that
		// would actually be gridded: 90th percentile of max half-extent, so the
		// loose-grid fit rule holds for ~90% of them.
		{
			std::vector<float> halfExtents;
			halfExtents.reserve(_pvsSpatialEntries.size());
			size_t alwaysIncludeCount = 0;
			for (const auto& entry : _pvsSpatialEntries)
			{
				if (entry.alwaysInclude)
				{
					++alwaysIncludeCount;
					continue;
				}
				const float he = std::max({ entry.worldBounds.Extents.x, entry.worldBounds.Extents.y, entry.worldBounds.Extents.z });
				if (he == he && he < kPvsSpatialMaxCellSize) // skip NaN / absurd
					halfExtents.push_back(he);
			}

			float cellSize = kPvsSpatialMinCellSize;
			if (!halfExtents.empty())
			{
				const size_t p90 = (halfExtents.size() * 9) / 10;
				std::nth_element(halfExtents.begin(), halfExtents.begin() + p90, halfExtents.end());
				cellSize = std::clamp(halfExtents[p90], kPvsSpatialMinCellSize, kPvsSpatialMaxCellSize);
			}
			_pvsSpatialCellSize = cellSize;
			_pvsSpatialAlwaysIncludeCount = alwaysIncludeCount;
		}

		for (uint32_t entryIndex = 0; entryIndex < _pvsSpatialEntries.size(); ++entryIndex)
			InsertPvsSpatialEntryIntoCells_NoLock(entryIndex);

		if (r_pvsPerfLog._val.b)
		{
			const float ms = std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - rebuildStart).count();
			const size_t ungridded = _pvsSpatialUngriddedEntries.size();
			LOG_INFO("PVS grid rebuild: entries=%zu cells=%zu cellSize=%.1f ungridded=%zu (alwaysInclude=%zu tooLarge=%zu) ms=%.2f",
				_pvsSpatialEntries.size(), _pvsSpatialCellList.size(), _pvsSpatialCellSize,
				ungridded, _pvsSpatialAlwaysIncludeCount,
				ungridded - std::min(ungridded, _pvsSpatialAlwaysIncludeCount), ms);
		}
	}

	void Scene::UpdatePvsSpatialEntriesForEntity(Entity* entity)
	{
		if (entity == nullptr)
			return;

		std::unique_lock lock(_lock);

		// A dirty cache rebuilds wholesale on the next query; nothing to patch.
		if (_pvsSpatialCacheDirty || _pvsSpatialEntries.empty())
			return;

		// Just queue: the grid is only READ at query time (PVS rebuilds), so
		// re-placing lazily there is both correct and free between rebuilds.
		// The eager version recomputed the world AABB for every transform event
		// (up to several per moving entity per frame) - measurably worse than
		// the full scans it replaced in mover-heavy scenes.
		if (_pvsSpatialEntriesByEntity.find(entity) != _pvsSpatialEntriesByEntity.end())
		{
			_pvsSpatialPendingUpdates.insert(entity);
		}
	}

	void Scene::ProcessPvsSpatialPendingUpdates_NoLock()
	{
		if (_pvsSpatialPendingUpdates.empty())
			return;

		// A large backlog (mass teleports, scene shuffles) is cheaper as one
		// full rebuild than as thousands of cell moves.
		if (_pvsSpatialPendingUpdates.size() * 4 > _pvsSpatialEntries.size())
		{
			RebuildPvsSpatialCache_NoLock();
			return;
		}

		for (Entity* entity : _pvsSpatialPendingUpdates)
		{
			auto it = _pvsSpatialEntriesByEntity.find(entity);
			if (it == _pvsSpatialEntriesByEntity.end())
				continue;

			for (uint32_t entryIndex : it->second)
			{
				PvsSpatialEntry& entry = _pvsSpatialEntries[entryIndex];
				if (entry.component == nullptr)
					continue;

				const dx::BoundingBox newBounds = entity->GetWorldAABB();

				if (entry.alwaysInclude)
				{
					entry.worldBounds = newBounds;
					continue;
				}

				const GiSpatialCellKey newCell{
					PvsSpatialCellCoord(newBounds.Center.x, _pvsSpatialCellSize),
					PvsSpatialCellCoord(newBounds.Center.y, _pvsSpatialCellSize),
					PvsSpatialCellCoord(newBounds.Center.z, _pvsSpatialCellSize) };
				const bool stillFits =
					newBounds.Extents.x <= _pvsSpatialCellSize &&
					newBounds.Extents.y <= _pvsSpatialCellSize &&
					newBounds.Extents.z <= _pvsSpatialCellSize;

				if (!entry.ungridded && stillFits && newCell == entry.cell)
				{
					entry.worldBounds = newBounds;
					continue;
				}

				// Re-place (also promotes a temporarily ungridded entry - e.g.
				// one added before its transform existed - into a real cell).
				RemovePvsSpatialEntryFromCells_NoLock(entryIndex);
				entry.worldBounds = newBounds;
				InsertPvsSpatialEntryIntoCells_NoLock(entryIndex);
			}
		}

		_pvsSpatialPendingUpdates.clear();
	}

	void Scene::AddPvsSpatialEntry_NoLock(Entity* entity, StaticMeshComponent* component)
	{
		if (entity == nullptr || component == nullptr)
			return;

		// Cache not built yet (or queued for rebuild): the rebuild will pick the
		// component up from the pool.
		if (_pvsSpatialCacheDirty)
			return;

		if (_pvsSpatialEntryByComponent.find(component) != _pvsSpatialEntryByComponent.end())
			return;

		PvsSpatialEntry entry;
		entry.component = component;
		entry.entity = entity;
		entry.alwaysInclude =
			entity->GetLayer() == Layer::Sky ||
			entity->GetName().rfind("HLOD_", 0) == 0;

		// Components are often added before the entity's transform is wired up;
		// use whatever bounds exist and queue a re-place for the next query.
		if (entity->GetComponent<Transform>() != nullptr)
		{
			entry.worldBounds = entity->GetWorldAABB();
		}

		const uint32_t entryIndex = static_cast<uint32_t>(_pvsSpatialEntries.size());
		_pvsSpatialEntries.push_back(entry);
		_pvsSpatialEntriesByEntity[entity].push_back(entryIndex);
		_pvsSpatialEntryByComponent[component] = entryIndex;
		InsertPvsSpatialEntryIntoCells_NoLock(entryIndex);
		_pvsSpatialPendingUpdates.insert(entity);
		++_pvsSpatialPoolSizeAtBuild;
	}

	void Scene::RemovePvsSpatialEntry_NoLock(StaticMeshComponent* component)
	{
		if (component == nullptr)
			return;

		if (_pvsSpatialCacheDirty)
			return;

		auto it = _pvsSpatialEntryByComponent.find(component);
		if (it == _pvsSpatialEntryByComponent.end())
		{
			// The pool shrank but we never tracked this component - fall back to
			// a rebuild so the pool-size safety check can't loop forever.
			_pvsSpatialCacheDirty = true;
			return;
		}

		const uint32_t entryIndex = it->second;
		PvsSpatialEntry& entry = _pvsSpatialEntries[entryIndex];

		RemovePvsSpatialEntryFromCells_NoLock(entryIndex);

		if (Entity* entity = entry.entity; entity != nullptr)
		{
			if (auto byEntity = _pvsSpatialEntriesByEntity.find(entity); byEntity != _pvsSpatialEntriesByEntity.end())
			{
				auto& indices = byEntity->second;
				indices.erase(std::remove(indices.begin(), indices.end(), entryIndex), indices.end());
				if (indices.empty())
				{
					_pvsSpatialEntriesByEntity.erase(byEntity);
					_pvsSpatialPendingUpdates.erase(entity);
				}
			}
		}

		// Tombstone: entry indices are baked into cell lists, so the slot can't
		// be reused. appendEntry skips component == nullptr. Compact via full
		// rebuild once dead slots pile up.
		entry.component = nullptr;
		entry.entity = nullptr;
		entry.ungridded = false;
		_pvsSpatialEntryByComponent.erase(it);
		if (_pvsSpatialPoolSizeAtBuild > 0)
			--_pvsSpatialPoolSizeAtBuild;

		++_pvsSpatialDeadEntries;
		if (_pvsSpatialDeadEntries > 256u && _pvsSpatialDeadEntries * 4u > _pvsSpatialEntries.size())
		{
			_pvsSpatialCacheDirty = true;
		}
	}

	bool Scene::QueryStaticMeshCullingCandidates(const dx::BoundingBox& queryBounds,
		const std::function<bool(const dx::BoundingBox& cellBounds)>& shapeIntersectsCell,
		std::vector<StaticMeshComponent*>& outComponents)
	{
		if (!r_pvsSpatialGrid._val.b)
			return false;

		std::unique_lock lock(_lock);

		const auto queryStart = std::chrono::high_resolution_clock::now();

		const auto* pool = TryGetComponentPool(StaticMeshComponent::_GetComponentId());
		const size_t poolSize = pool != nullptr ? pool->components.size() : 0;

		// Safety net: a structural change that slipped past the dirty hooks
		// shows up as a pool-size mismatch - rebuild rather than cull against a
		// stale index.
		if (_pvsSpatialCacheDirty || poolSize != _pvsSpatialPoolSizeAtBuild)
			RebuildPvsSpatialCache_NoLock();

		// Re-place everything that moved since the last query (deferred from the
		// per-transform-event hooks; between queries the grid is never read).
		const size_t pendingProcessed = _pvsSpatialPendingUpdates.size();
		ProcessPvsSpatialPendingUpdates_NoLock();

		if (_pvsSpatialEntries.empty())
		{
			// An empty result is only trustworthy when the scene really has no
			// static mesh components; otherwise fall back to the pool scan.
			return poolSize == 0;
		}

		if (++_pvsSpatialQueryStamp == 0u)
		{
			for (auto& entry : _pvsSpatialEntries)
				entry.lastQueryStamp = 0u;
			_pvsSpatialQueryStamp = 1u;
		}
		const uint32_t stamp = _pvsSpatialQueryStamp;

		auto appendEntry = [&](uint32_t entryIndex)
		{
			PvsSpatialEntry& entry = _pvsSpatialEntries[entryIndex];
			if (entry.lastQueryStamp == stamp)
				return;
			entry.lastQueryStamp = stamp;

			if (entry.component != nullptr)
				outComponents.push_back(entry.component);
		};

		// A cell's entries may poke up to one cell size past the cell in every
		// direction (loose-grid guarantee), so the bounds handed to the shape
		// test are the cell inflated by that margin.
		const float cellSize = _pvsSpatialCellSize;
		auto looseCellBoundsFor = [cellSize](const GiSpatialCellKey& key)
		{
			dx::BoundingBox cellBounds;
			cellBounds.Center = dx::XMFLOAT3(
				(static_cast<float>(key.x) + 0.5f) * cellSize,
				(static_cast<float>(key.y) + 0.5f) * cellSize,
				(static_cast<float>(key.z) + 0.5f) * cellSize);
			const float looseHalf = cellSize * 1.5f;
			cellBounds.Extents = dx::XMFLOAT3(looseHalf, looseHalf, looseHalf);
			return cellBounds;
		};

		// Query range inflated by one cell for the same reason.
		const math::Vector3 center(queryBounds.Center.x, queryBounds.Center.y, queryBounds.Center.z);
		const math::Vector3 extents(queryBounds.Extents.x + cellSize, queryBounds.Extents.y + cellSize, queryBounds.Extents.z + cellSize);
		const math::Vector3 min = center - extents;
		const math::Vector3 max = center + extents;
		const int32_t minCellX = PvsSpatialCellCoord(min.x, cellSize);
		const int32_t minCellY = PvsSpatialCellCoord(min.y, cellSize);
		const int32_t minCellZ = PvsSpatialCellCoord(min.z, cellSize);
		const int32_t maxCellX = PvsSpatialCellCoord(max.x, cellSize);
		const int32_t maxCellY = PvsSpatialCellCoord(max.y, cellSize);
		const int32_t maxCellZ = PvsSpatialCellCoord(max.z, cellSize);

		const int64_t spanX = static_cast<int64_t>(maxCellX) - static_cast<int64_t>(minCellX) + 1ll;
		const int64_t spanY = static_cast<int64_t>(maxCellY) - static_cast<int64_t>(minCellY) + 1ll;
		const int64_t spanZ = static_cast<int64_t>(maxCellZ) - static_cast<int64_t>(minCellZ) + 1ll;
		const int64_t coveredCells = spanX * spanY * spanZ;

		size_t cellsTested = 0;

		if (coveredCells > 0ll && coveredCells <= static_cast<int64_t>(_pvsSpatialCellList.size()))
		{
			for (int32_t z = minCellZ; z <= maxCellZ; ++z)
			{
				for (int32_t y = minCellY; y <= maxCellY; ++y)
				{
					for (int32_t x = minCellX; x <= maxCellX; ++x)
					{
						const GiSpatialCellKey key{ x, y, z };
						const auto it = _pvsSpatialCellIndex.find(key);
						if (it == _pvsSpatialCellIndex.end())
							continue;

						const auto& cell = _pvsSpatialCellList[it->second];
						if (cell.entries.empty())
							continue;

						++cellsTested;
						if (shapeIntersectsCell && !shapeIntersectsCell(looseCellBoundsFor(key)))
							continue;

						for (uint32_t entryIndex : cell.entries)
							appendEntry(entryIndex);
					}
				}
			}
		}
		else
		{
			// The query range covers more cells than exist (a camera frustum's
			// AABB easily does) - walk the dense cell list instead. Integer
			// range rejection first; the shape-vs-cell callback only runs for
			// cells inside the query's range.
			for (const auto& cell : _pvsSpatialCellList)
			{
				if (cell.entries.empty())
					continue;

				if (cell.key.x < minCellX || cell.key.x > maxCellX ||
					cell.key.y < minCellY || cell.key.y > maxCellY ||
					cell.key.z < minCellZ || cell.key.z > maxCellZ)
					continue;

				++cellsTested;
				if (shapeIntersectsCell && !shapeIntersectsCell(looseCellBoundsFor(cell.key)))
					continue;

				for (uint32_t entryIndex : cell.entries)
					appendEntry(entryIndex);
			}
		}

		for (uint32_t entryIndex : _pvsSpatialUngriddedEntries)
			appendEntry(entryIndex);

		if (r_pvsPerfLog._val.b)
		{
			const float ms = std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - queryStart).count();
			LOG_INFO("PVS grid query: frame=%lld cellSize=%.1f coveredCells=%lld occupiedCells=%zu cellsTested=%zu ungridded=%zu (alwaysInclude=%zu) returned=%zu pending=%zu ms=%.2f",
				(long long)(g_pEnv && g_pEnv->_timeManager ? g_pEnv->_timeManager->_frameCount : 0), _pvsSpatialCellSize, (long long)coveredCells, _pvsSpatialCellList.size(), cellsTested, _pvsSpatialUngriddedEntries.size(), _pvsSpatialAlwaysIncludeCount, outComponents.size(), pendingProcessed, ms);
		}

		return true;
	}


	void Scene::CalculateSceneStats(std::vector<math::Vector3>& vertices, std::vector<uint16_t>& indices, uint32_t& numFaces, EntityFlags excludeFlags)
	{
		std::unique_lock lock(_lock);
		numFaces = 0;

		const auto* pool = TryGetComponentPool(StaticMeshComponent::_GetComponentId());
		if (pool != nullptr)
		{
			for (uint32_t denseIndex = 0; denseIndex < pool->components.size(); ++denseIndex)
			{
				auto* smc = static_cast<StaticMeshComponent*>(pool->components[denseIndex]);
				if (smc == nullptr)
					continue;

				auto mesh = smc->GetMesh();

				if (!mesh)
					continue;

				Entity* entity = TryGetEntity(pool->owners[denseIndex]);

				if (!entity)
					continue;

				if (entity->HasFlag(excludeFlags))
					continue;

				// Water-material meshes (ocean tiles from the "Add ocean" tool) are a
				// rendered surface, not walkable ground — keep them out of navmesh
				// bakes regardless of whether the entity carries DoNotBlockNavMesh.
				if (auto material = smc->GetMaterial(); material && material->_properties.isWater)
					continue;

				auto verts = mesh->GetVertices();
				auto inds = mesh->GetIndices();

				numFaces += mesh->GetNumFaces();

				for (auto& v : verts)
				{
					vertices.push_back(*(math::Vector3*)&v._position.x);
				}

				indices.insert(indices.end(), inds.begin(), inds.end());
			}
		}
	}

	void Scene::GatherNavMeshBlockingVolumes(std::vector<NavMeshBlockingBox>& out)
	{
		std::unique_lock lock(_lock);

		const auto* pool = TryGetComponentPool(NavMeshBlockingVolume::_GetComponentId());
		if (pool == nullptr)
			return;

		for (uint32_t denseIndex = 0; denseIndex < pool->components.size(); ++denseIndex)
		{
			auto* volume = static_cast<NavMeshBlockingVolume*>(pool->components[denseIndex]);
			if (volume == nullptr)
				continue;

			Entity* entity = TryGetEntity(pool->owners[denseIndex]);
			if (entity == nullptr)
				continue;

			NavMeshBlockingBox box;
			volume->GetWorldFootprint(box);
			out.push_back(box);
		}
	}

	void Scene::GatherNavMeshLinks(std::vector<NavMeshLink>& out)
	{
		std::unique_lock lock(_lock);

		const auto* pool = TryGetComponentPool(NavMeshLinkComponent::_GetComponentId());
		if (pool == nullptr)
			return;

		for (uint32_t denseIndex = 0; denseIndex < pool->components.size(); ++denseIndex)
		{
			auto* link = static_cast<NavMeshLinkComponent*>(pool->components[denseIndex]);
			if (link == nullptr)
				continue;

			Entity* entity = TryGetEntity(pool->owners[denseIndex]);
			if (entity == nullptr)
				continue;

			NavMeshLink worldLink;
			link->GetWorldLink(worldLink);
			out.push_back(worldLink);
		}
	}

	void Scene::CalculateSceneStats_UInt32(std::vector<math::Vector3>& vertices, std::vector<uint32_t>& indices, uint32_t& numFaces, EntityFlags excludeFlags)
	{
		std::unique_lock lock(_lock);
		numFaces = 0;

		const auto* pool = TryGetComponentPool(StaticMeshComponent::_GetComponentId());
		if (pool != nullptr)
		{
			uint32_t indexOffset = 0;

			for (uint32_t denseIndex = 0; denseIndex < pool->components.size(); ++denseIndex)
			{
				auto* smc = static_cast<StaticMeshComponent*>(pool->components[denseIndex]);
				if (smc == nullptr)
					continue;

				auto mesh = smc->GetMesh();

				if (!mesh)
					continue;

				Entity* entity = TryGetEntity(pool->owners[denseIndex]);

				if (!entity)
					continue;

				if (entity->HasFlag(excludeFlags))
					continue;

				// Water-material meshes (ocean tiles from the "Add ocean" tool) are a
				// rendered surface, not walkable ground — keep them out of navmesh
				// bakes regardless of whether the entity carries DoNotBlockNavMesh.
				if (auto material = smc->GetMaterial(); material && material->_properties.isWater)
					continue;

				auto verts = mesh->GetVertices();
				auto inds = mesh->GetIndices();

				numFaces += mesh->GetNumFaces();

				const auto& worldTM = entity->GetWorldTM();

				for (auto& v : verts)
				{
					const math::Vector3 localPos = *(math::Vector3*)&v._position.x;
					vertices.push_back(math::Vector3::Transform(localPos, worldTM));
				}				

				for (auto& i : inds)
				{
					indices.push_back((int)i + indexOffset);
				}

				indexOffset += (uint32_t)verts.size();
			}
		}
	}
}
