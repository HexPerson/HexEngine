
#include "PVS.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Environment/TimeManager.hpp"
#include "../Terrain/ChunkManager.hpp"
#include "../Entity/Component/StaticMeshComponent.hpp"
#include "../Entity/Component/SkeletalAnimationComponent.hpp"
#include "../Environment/LogFile.hpp"
#include "../Graphics/Material.hpp"
#include "Scene.hpp"
#include "../Input/HVar.hpp"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace HexEngine
{
	HVar r_grassCullDist("r_grassCullDist", "The distance at which to cull grass", 1000.0f, 0.0f, 10000.0f);
	HVar r_hlodEnable("r_hlodEnable", "Enable runtime HLOD substitution for generated HLOD entities", true, false, true);
	HVar r_hlodClusterSize("r_hlodClusterSize", "Cluster size used when mapping source meshes to generated HLOD entities", 200.0f, 1.0f, 100000.0f);
	HVar r_hlodSwitchDistance("r_hlodSwitchDistance", "Distance from camera where runtime HLOD replaces source meshes", 400.0f, 1.0f, 100000.0f);
	HVar r_hlodDebugShowOnly("r_hlodDebugShowOnly", "Debug: render only generated HLOD entities", false, false, true);
	HVar r_hlodAffectTerrain("r_hlodAffectTerrain", "Allow runtime HLOD substitution to affect chunk-based terrain meshes", false, false, true);
	HVar r_hlodStreamEnable("r_hlodStreamEnable", "Enable runtime streaming of generated HLOD meshes", true, false, true);
	HVar r_hlodStreamHysteresis("r_hlodStreamHysteresis", "Hysteresis distance used for HLOD streaming load/unload", 75.0f, 0.0f, 100000.0f);
	extern HVar r_pvsPerfLog; // Scene.cpp

	namespace
	{
		std::unordered_map<StaticMeshComponent*, fs::path> g_hlodMeshPaths;
		std::unordered_set<StaticMeshComponent*> g_hlodPendingLoads;

		struct HlodClusterRuntimeData
		{
			math::Vector3 center = math::Vector3::Zero;
			bool hasCenter = false;
		};

		// Allocation-free: runs for every candidate on every PVS rebuild (and
		// the std::string version allocated 2-3 times per HLOD-named entity).
		bool TryExtractHlodClusterKey(const std::string& entityName, std::string_view& outClusterKey)
		{
			static constexpr size_t HlodPrefixLen = 5;

			if (entityName.size() <= HlodPrefixLen || entityName.compare(0, HlodPrefixLen, "HLOD_") != 0)
				return false;

			const std::string_view suffix(entityName.data() + HlodPrefixLen, entityName.size() - HlodPrefixLen);
			const size_t lastUnderscore = suffix.find_last_of('_');
			if (lastUnderscore == std::string_view::npos || lastUnderscore == 0)
				return false;

			const std::string_view tail = suffix.substr(lastUnderscore + 1);
			const bool hasNumericTail = !tail.empty() && std::all_of(tail.begin(), tail.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
			outClusterKey = hasNumericTail ? suffix.substr(0, lastUnderscore) : suffix;
			return !outClusterKey.empty();
		}

		// Cluster keys used to be "cx_cy_cz" strings, built with 3x std::to_string
		// per entity PER PVS REBUILD (~50k heap allocs a rebuild in a 10k-entity
		// scene). They're now packed into an int64 (21 signed bits per axis).
		int64_t PackHlodClusterCell(int32_t cx, int32_t cy, int32_t cz)
		{
			return (static_cast<int64_t>(cx & 0x1FFFFF) << 42) |
				(static_cast<int64_t>(cy & 0x1FFFFF) << 21) |
				static_cast<int64_t>(cz & 0x1FFFFF);
		}

		int64_t PackHlodClusterKeyFromPosition(const math::Vector3& pos, float clusterSize)
		{
			const int32_t cx = static_cast<int32_t>(std::floor(pos.x / clusterSize));
			const int32_t cy = static_cast<int32_t>(std::floor(pos.y / clusterSize));
			const int32_t cz = static_cast<int32_t>(std::floor(pos.z / clusterSize));
			return PackHlodClusterCell(cx, cy, cz);
		}

		// Extract the 6 world-space planes of a BoundingFrustum, normalised to the
		// OUTWARD convention (inside <=> dot(n,p)+d <= 0 for all planes). The
		// sign is verified against a probe point known to be inside (frustum
		// apex pushed to mid-depth along its orientation) rather than trusting
		// DirectXCollision's plane orientation, so a convention mix-up can't
		// silently cull everything.
		void BuildCullPlanesFromFrustum(const dx::BoundingFrustum& frustum, math::Vector4 outPlanes[6])
		{
			dx::XMVECTOR planes[6];
			frustum.GetPlanes(&planes[0], &planes[1], &planes[2], &planes[3], &planes[4], &planes[5]);

			for (int32_t i = 0; i < 6; ++i)
			{
				dx::XMFLOAT4 p;
				dx::XMStoreFloat4(&p, planes[i]);
				outPlanes[i] = math::Vector4(p.x, p.y, p.z, p.w);

				const float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
				if (len > 1e-6f)
					outPlanes[i] /= len;
			}

			const dx::XMVECTOR forward = dx::XMVector3Rotate(
				dx::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f),
				dx::XMLoadFloat4(&frustum.Orientation));
			dx::XMFLOAT3 fwd;
			dx::XMStoreFloat3(&fwd, forward);
			const float midDepth = (frustum.Near + frustum.Far) * 0.5f;
			const math::Vector3 probe(
				frustum.Origin.x + fwd.x * midDepth,
				frustum.Origin.y + fwd.y * midDepth,
				frustum.Origin.z + fwd.z * midDepth);

			float maxSigned = -FLT_MAX;
			for (int32_t i = 0; i < 6; ++i)
			{
				const math::Vector4& p = outPlanes[i];
				maxSigned = std::max(maxSigned, p.x * probe.x + p.y * probe.y + p.z * probe.z + p.w);
			}

			// Probe reported outside => the planes face inward; flip to outward.
			if (maxSigned > 0.0f)
			{
				for (int32_t i = 0; i < 6; ++i)
					outPlanes[i] = -outPlanes[i];
			}
		}

		// Parse a name-derived "cx_cy_cz" cluster key into the packed form. HLOD
		// entity names carry the key their generator built from positions, so a
		// successful parse lands on the same packed value as
		// PackHlodClusterKeyFromPosition for co-located source meshes.
		bool ParseHlodClusterKey(std::string_view clusterKey, int64_t& outPacked)
		{
			int32_t v[3] = {};
			size_t pos = 0;
			const size_t n = clusterKey.size();

			for (int32_t i = 0; i < 3; ++i)
			{
				bool negative = false;
				if (pos < n && (clusterKey[pos] == '-' || clusterKey[pos] == '+'))
				{
					negative = clusterKey[pos] == '-';
					++pos;
				}

				const size_t digitsStart = pos;
				int64_t value = 0;
				while (pos < n && clusterKey[pos] >= '0' && clusterKey[pos] <= '9')
				{
					value = value * 10 + (clusterKey[pos] - '0');
					++pos;
				}
				if (pos == digitsStart)
					return false;

				v[i] = static_cast<int32_t>(negative ? -value : value);

				if (i < 2)
				{
					if (pos >= n || clusterKey[pos] != '_')
						return false;
					++pos;
				}
			}

			if (pos != n)
				return false;

			outPacked = PackHlodClusterCell(v[0], v[1], v[2]);
			return true;
		}

		void CacheHlodMeshPath(StaticMeshComponent* component)
		{
			if (!component)
				return;

			auto mesh = component->GetMesh();
			if (!mesh)
				return;

			const fs::path path = mesh->GetFileSystemPath();
			if (!path.empty())
			{
				g_hlodMeshPaths[component] = path;
			}
		}

		void UpdateHlodStreamingState(StaticMeshComponent* component, float distanceToCluster, float switchDistance, bool forceLoad)
		{
			if (!component)
				return;

			CacheHlodMeshPath(component);

			auto mesh = component->GetMesh();
			const bool isLoaded = mesh != nullptr;
			const float hysteresis = std::max(0.0f, r_hlodStreamHysteresis._val.f32);
			const float loadDistance = std::max(0.0f, switchDistance - hysteresis);
			const float unloadDistance = std::max(0.0f, switchDistance - (hysteresis * 2.0f));

			if ((forceLoad || distanceToCluster >= loadDistance) && !isLoaded)
			{
				if (g_hlodPendingLoads.find(component) != g_hlodPendingLoads.end())
					return;

				auto pathIt = g_hlodMeshPaths.find(component);
				if (pathIt != g_hlodMeshPaths.end() && !pathIt->second.empty())
				{
					const fs::path meshPath = pathIt->second;
					g_hlodPendingLoads.insert(component);

					Mesh::CreateAsync(meshPath, [component, meshPath](std::shared_ptr<IResource> resource)
					{
						g_hlodPendingLoads.erase(component);

						auto cachedPathIt = g_hlodMeshPaths.find(component);
						if (cachedPathIt == g_hlodMeshPaths.end() || cachedPathIt->second != meshPath)
							return;

						auto mesh = std::dynamic_pointer_cast<Mesh>(resource);
						if (!mesh)
							return;

						// If the mesh is still unloaded when the async load completes, swap it in.
						if (!component->GetMesh())
						{
							// Preserve the authored component material; SetMesh() may replace it with mesh/default material.
							auto originalMaterial = component->GetMaterial();
							component->SetMesh(mesh);
							if (originalMaterial)
								component->SetMaterial(originalMaterial);
						}
					});
				}
			}
			else if (!forceLoad && distanceToCluster <= unloadDistance && isLoaded)
			{
				component->SetMesh(nullptr);
			}
		}
	}

	void PVS::ClearPVS()
	{
		std::unique_lock lock(_lock);

		auto oldSize = _pvs.size();
		_pvs.clear();
		//_pvs.reserve(oldSize);

		_forceRebuild = true;

		_totalEnts = 0;
		_totalSkeletalAnimators = 0;
	}

	const PVS::MeshInstanceMap& PVS::GetRenderables() const
	{
		return _pvs;
	}

	/*bool SortThird(const PVS::MeshEntityPair& left, const PVS::MeshEntityPair& right)
	{

	}

	bool SortSecond(const PVS::MeshEntityVector& left, const PVS::MeshEntityVector& right)
	{
		return SortThird(left. right);
	}

	bool SortFirst(const PVS::MaterialEntityVectorPair& left, const PVS::MaterialEntityVectorPair& right)
	{
		return SortSecond(left.second, right.second);
	}*/

	void PVS::ForceRebuild()
	{
		_forceRebuild = true;
	}

	bool PVS::NeedsRebuild() const
	{
		return _forceRebuild;
	}

	void PVS::ResetDidRebuild()
	{
		_didRebuild = false;
	}

	bool PVS::DidRebuild() const
	{
		return _didRebuild;
	}

	void PVS::CalculateVisibility(Scene* scene, const PVSParams& params)
	{
		if (_updatesDisabled)
			return;

		memcpy(&_currentParams, &params, sizeof(PVSParams));

		bool needsRebuild = _forceRebuild;
		const bool wasForced = _forceRebuild;

		// Hysteresis diagnostics for r_pvsPerfLog: how far the cull shape moved
		// since the shape we last built against, and (spheres) old vs new radius.
		float hystMoved = 0.0f;
		float hystOldRadius = 0.0f;
		float hystNewRadius = 0.0f;
		int32_t hystContainment = -1;

		if (_hasBuildOptimisation)
		{
			switch (params.shapeType)
			{
			case PVSParams::ShapeType::Frustum:
			{
				const auto containment = _optimisedParams.shape.frustum.sm.Contains(params.shape.frustum.sm);
				hystContainment = (int32_t)containment;
				if (containment != dx::ContainmentType::CONTAINS)
					needsRebuild = true;
				const math::Vector3 d(
					params.shape.frustum.sm.Origin.x - _optimisedParams.shape.frustum.sm.Origin.x,
					params.shape.frustum.sm.Origin.y - _optimisedParams.shape.frustum.sm.Origin.y,
					params.shape.frustum.sm.Origin.z - _optimisedParams.shape.frustum.sm.Origin.z);
				hystMoved = d.Length();
				hystOldRadius = _optimisedParams.shape.frustum.sm.Far;
				hystNewRadius = params.shape.frustum.sm.Far;
				break;
			}

			case PVSParams::ShapeType::Frustum2:
			{
				const auto containment = _optimisedParams.shape.frustum.lg.Contains(params.shape.frustum.sm);
				hystContainment = (int32_t)containment;
				if (containment != dx::ContainmentType::CONTAINS)
					needsRebuild = true;
				const math::Vector3 d(
					params.shape.frustum.sm.Origin.x - _optimisedParams.shape.frustum.sm.Origin.x,
					params.shape.frustum.sm.Origin.y - _optimisedParams.shape.frustum.sm.Origin.y,
					params.shape.frustum.sm.Origin.z - _optimisedParams.shape.frustum.sm.Origin.z);
				hystMoved = d.Length();
				hystOldRadius = _optimisedParams.shape.frustum.lg.Far;
				hystNewRadius = params.shape.frustum.sm.Far;
				break;
			}

			case PVSParams::ShapeType::Sphere:
			{
				const auto containment = _optimisedParams.shape.sphere.Contains(params.shape.sphere);
				hystContainment = (int32_t)containment;
				if (containment != dx::ContainmentType::CONTAINS)
					needsRebuild = true;
				const math::Vector3 d(
					params.shape.sphere.Center.x - _optimisedParams.shape.sphere.Center.x,
					params.shape.sphere.Center.y - _optimisedParams.shape.sphere.Center.y,
					params.shape.sphere.Center.z - _optimisedParams.shape.sphere.Center.z);
				hystMoved = d.Length();
				hystOldRadius = _optimisedParams.shape.sphere.Radius;
				hystNewRadius = params.shape.sphere.Radius;
				break;
			}
			}
		}

		if (needsRebuild)
			_needsOptimisationRebuild = true;

		if (_needsOptimisationRebuild)
		{
			memcpy(&_optimisedParams, &params, sizeof(PVSParams));

			switch (params.shapeType)
			{
			case PVSParams::ShapeType::Frustum:
			{
				//dx::BoundingSphere::CreateFromFrustum(_optimisedParams.shape.sphere, params.shape.frustum);
				//_optimisedParams.shape.sphere.Radius *= 1.05f;

				//_optimisedParams.shape.frustum.Transform(_optimisedParams.shape.frustum, math::Matrix::CreateScale(1.2f));

				//_optimisedParams.shape.frustum.Origin = params.shape.frustum.Origin;
				//_optimisedParams.shape.frustum.Orientation = params.shape.frustum.Orientation;

				//_optimisedParams.shape.frustum.Near *= 2.2f;//params.shape.frustum.Near;
				//_optimisedParams.shape.frustum.Far = params.shape.frustum.Far;

				//_optimisedParams.shape.frustum.RightSlope	*= 2.2f;
				//_optimisedParams.shape.frustum.LeftSlope	*= 2.2f;
				//_optimisedParams.shape.frustum.TopSlope		*= 2.2f;
				//_optimisedParams.shape.frustum.BottomSlope	*= 2.2f;
				//_optimisedParams.shape.frustum.Near *= 2.2f;
				////_optimisedParams.shape.frustum.Far *= 2.2f;

				//math::Vector3 o = _optimisedParams.shape.frustum.Origin;
				//math::Quaternion d = _optimisedParams.shape.frustum.Orientation;
				//o -= d.ToEuler() * 100.0f;

				//_optimisedParams.shape.frustum.Origin = o;
				
				break;
			}

			case PVSParams::ShapeType::Sphere:
				//_optimisedParams.shape.sphere.Transform(_optimisedParams.shape.sphere, math::Matrix::CreateScale(1.2f));
				_optimisedParams.shape.sphere.Radius *= 1.25f;
				break;
			}

			_needsOptimisationRebuild = false;
			_hasBuildOptimisation = true;

			_cullPlanesValid = false;
			if (params.shapeType == PVSParams::ShapeType::Frustum)
			{
				BuildCullPlanesFromFrustum(_optimisedParams.shape.frustum.sm, _cullPlanes);
				_cullPlanesValid = true;
			}
			else if (params.shapeType == PVSParams::ShapeType::Frustum2)
			{
				BuildCullPlanesFromFrustum(_optimisedParams.shape.frustum.lg, _cullPlanes);
				_cullPlanesValid = true;
			}
		}

		if (!needsRebuild)
		{
			return;
		}

		const auto rebuildStart = std::chrono::high_resolution_clock::now();
		uint32_t visibilityMessages = 0;

		// Candidate gathering runs BEFORE the PVS lock is taken: the queries
		// below take the scene lock, and acquiring it while holding the PVS lock
		// would invert the order used by entity-add paths (scene lock ->
		// PVS::FlushEntity).
		std::vector<StaticMeshComponent*> components;

		if (g_pEnv->_chunkManager->HasActiveChunks(scene))
		{
			g_pEnv->_chunkManager->CalculatePVS(scene, this, params, components);
			components.erase(
				std::remove_if(components.begin(), components.end(), [](StaticMeshComponent* smc)
					{
						return smc && smc->GetEntity() && smc->GetEntity()->GetName().rfind("HLOD_", 0) == 0;
					}),
				components.end());

			// HLOD entities are intentionally excluded from chunk membership to avoid perturbing terrain chunk visibility.
			// Add them explicitly here so they still participate in runtime PVS/rendering.
			std::unordered_set<StaticMeshComponent*> existing(components.begin(), components.end());
			std::vector<StaticMeshComponent*> allStaticMeshes;
			scene->GetComponents<StaticMeshComponent>(allStaticMeshes);
			for (auto* smc : allStaticMeshes)
			{
				if (!smc || !smc->GetEntity())
					continue;
				if (smc->GetEntity()->GetName().rfind("HLOD_", 0) != 0)
					continue;
				if (existing.insert(smc).second)
					components.push_back(smc);
			}
		}
		else
		{
			// Spatial-grid candidate gather: only components whose cells touch
			// the cull shape (plus Sky/HLOD/oversized entries, which the grid
			// always returns) instead of every static mesh in the scene. The
			// exact per-entity test below is unchanged, so the grid only has to
			// be conservative. Falls back to the full pool scan if the grid is
			// disabled or unavailable.
			bool usedSpatialQuery = false;

			switch (params.shapeType)
			{
			case PVSParams::ShapeType::Frustum:
			case PVSParams::ShapeType::Frustum2:
			{
				// IsShapeVisible culls against the (enlarged, for Frustum2)
				// cached frustum, so candidates must cover that same shape.
				const dx::BoundingFrustum cullFrustum =
					(params.shapeType == PVSParams::ShapeType::Frustum2)
					? _optimisedParams.shape.frustum.lg
					: _optimisedParams.shape.frustum.sm;

				dx::XMFLOAT3 corners[dx::BoundingFrustum::CORNER_COUNT];
				cullFrustum.GetCorners(corners);

				dx::BoundingBox queryBounds;
				dx::BoundingBox::CreateFromPoints(queryBounds, dx::BoundingFrustum::CORNER_COUNT, corners, sizeof(dx::XMFLOAT3));

				// Cell test against the cached planes (outward convention): a box
				// is outside a plane when its nearest corner is in front of it.
				// BoundingFrustum::Intersects(box) per cell was ~1us x 5k cells.
				const bool usePlanes = _cullPlanesValid;
				const math::Vector4* planes = _cullPlanes;
				usedSpatialQuery = scene->QueryStaticMeshCullingCandidates(queryBounds,
					[&cullFrustum, usePlanes, planes](const dx::BoundingBox& cellBounds)
					{
						if (!usePlanes)
							return cullFrustum.Intersects(cellBounds);

						for (int32_t i = 0; i < 6; ++i)
						{
							const math::Vector4& p = planes[i];
							const float centreDist = p.x * cellBounds.Center.x + p.y * cellBounds.Center.y + p.z * cellBounds.Center.z + p.w;
							const float projectedRadius = std::fabs(p.x) * cellBounds.Extents.x + std::fabs(p.y) * cellBounds.Extents.y + std::fabs(p.z) * cellBounds.Extents.z;
							if (centreDist > projectedRadius)
								return false;
						}
						return true;
					},
					components);
				break;
			}

			case PVSParams::ShapeType::Sphere:
			{
				const dx::BoundingSphere cullSphere = _optimisedParams.shape.sphere;

				dx::BoundingBox queryBounds;
				queryBounds.Center = cullSphere.Center;
				queryBounds.Extents = dx::XMFLOAT3(cullSphere.Radius, cullSphere.Radius, cullSphere.Radius);

				usedSpatialQuery = scene->QueryStaticMeshCullingCandidates(queryBounds,
					[&cullSphere](const dx::BoundingBox& cellBounds) { return cullSphere.Intersects(cellBounds); },
					components);
				break;
			}
			}

			if (!usedSpatialQuery)
				scene->GetComponents<StaticMeshComponent>(components);
		}

		std::unordered_map<int64_t, HlodClusterRuntimeData> hlodClusters;
		for (auto* component : components)
		{
			if (!component)
				continue;

			Entity* entity = component->GetEntity();
			if (!entity)
				continue;

			std::string_view hlodClusterKey;
			if (TryExtractHlodClusterKey(entity->GetName(), hlodClusterKey))
			{
				CacheHlodMeshPath(component);

				int64_t packedKey = 0;
				if (ParseHlodClusterKey(hlodClusterKey, packedKey))
				{
					auto& clusterData = hlodClusters[packedKey];
					if (!clusterData.hasCenter)
					{
						clusterData.center = entity->GetPosition();
						clusterData.hasCenter = true;
					}
				}
			}
		}

		// The per-entity work below (cluster lookups for every source mesh) only
		// pays off when generated HLOD actually exists in the scene.
		const bool hlodClustersActive = !hlodClusters.empty() || r_hlodDebugShowOnly._val.b;
		const auto gatherEnd = std::chrono::high_resolution_clock::now();

		// Translation hysteresis of this PVS (spheres are inflated 25% at build).
		const float lodSlack = params.shapeType == PVSParams::ShapeType::Sphere
			? params.shape.sphere.Radius * 0.25f
			: 0.0f;

		// Take the lock BEFORE tearing anything down. The old flow cleared the
		// PVS first and then try-locked: on contention it returned with an empty
		// PVS and _forceRebuild already false, leaving nothing rendered until
		// the shape check happened to fail again. Now contention keeps the old
		// content and retries next frame.
		std::unique_lock lock(_lock, std::try_to_lock);

		if (!lock.owns_lock())
		{
			_forceRebuild = true;
			return;
		}

		_forceRebuild = false;

		// In-place reset: keep every batch vector's capacity for the refill
		// below (a full _pvs.clear() re-allocated every batch on every rebuild).
		for (auto& batch : _pvs)
			batch.second.clear();

		_totalEnts = 0;
		_totalSkeletalAnimators = 0;

		for (auto&& component : components)
		{
			if (component == nullptr)
				continue;

			auto entity = component->GetEntity();

			// Defensive: skip components whose backing entity is gone or
			// marked for deletion. Scene::GetComponents<StaticMeshComponent>
			// returns whatever is currently in the pool; if entity teardown
			// left a stale entry (which the IconService preview-scene
			// teardown demonstrated could happen), the PVS rebuild would
			// pick it up and the dangling-pointer-via-GetEntity dereference
			// down this loop would crash. More importantly for icon
			// rendering, the stale component's mesh would be rasterised into
			// the next icon - producing the "prefab leaks into the next
			// icon" symptom we saw with IconService.
			if (entity == nullptr || entity->IsPendingDeletion())
				continue;

			if (entity->GetLayer() == Layer::Invisible || entity->GetLayer() == Layer::Trigger || entity->HasFlag(EntityFlags::DoNotRender))
				continue;

			// don't bother rendering entities into the shadow map that can't receive shadows
			//
			if (params.isShadow)
			{
				if (entity->GetCastsShadows() == false)
					continue;
			}

			// always draw grass if its in the grass radius
#if 0
			if (entity->GetLayer() == Layer::Grass && params.isShadow == false)
			{
				//if ((params.camera->GetEntity()->GetPosition() - entity->GetPosition()).Length() <= r_grassCullDist._val.f32)
				{
					auto material = component->GetMaterial();

					if (!material)
						continue;

					auto mesh = component->GetMesh();

					if (!mesh)
						continue;

					auto meshInstance = mesh->GetInstance();

					if (!meshInstance)
						continue;

					auto& it = _pvs[material];

					if (it.size() == 0)
						it.reserve(1000);

					_totalEnts++;

					it.push_back({ mesh, entity, component, meshInstance->GetInstanceId() });
					continue;
				}
				//else
				//	continue;
			}
#endif
			

			// early cull distance
			//if (params.camera)
			//{
			//	// because we earlier sorted the components by distance, if this check fails we can just exit the function because we know no more entities should be visible
			//	if ((params.camera->GetEntity()->GetPosition() - entity->GetPosition()).Length() - entity->GetWorldBoundingSphere().Radius >= params.camera->GetFarZ())
			//	{
			//		if (entity->IsInPVS() && params.isShadow == false)
			//		{
			//			PVSVisibilityChangedMessage pvsMsg;
			//			pvsMsg.visible = false;
			//			entity->OnMessage(&pvsMsg, nullptr);
			//		}
			//		continue;
			//	}{}
			//}

			if (component)
			{
				if (r_hlodEnable._val.b && hlodClustersActive && params.camera != nullptr)
				{
					if (!r_hlodAffectTerrain._val.b && (entity->GetChunk() != nullptr || entity->HasFlag(EntityFlags::ExcludeFromHLOD)))
					{
						// Terrain/chunk meshes have their own LOD path and can share proxy origins; skip HLOD substitution by default.
						bool ignore = false;
					}
					else
					{
						const float switchDistance = r_hlodSwitchDistance._val.f32;
						std::string_view hlodClusterKey;
						const bool isHlodEntity = TryExtractHlodClusterKey(entity->GetName(), hlodClusterKey);

						if (r_hlodDebugShowOnly._val.b)
						{
							if (!isHlodEntity)
								continue;
						}
						else
						{
							if (isHlodEntity)
							{
								int64_t packedKey = 0;
								const bool haveKey = ParseHlodClusterKey(hlodClusterKey, packedKey);
								auto hlodIt = haveKey ? hlodClusters.find(packedKey) : hlodClusters.end();
								const math::Vector3 clusterCenter = (hlodIt != hlodClusters.end() && hlodIt->second.hasCenter)
									? hlodIt->second.center
									: entity->GetPosition();
								const float distanceToCluster = (clusterCenter - params.camera->GetEntity()->GetPosition()).Length();

								if (r_hlodStreamEnable._val.b)
								{
									UpdateHlodStreamingState(component, distanceToCluster, switchDistance, r_hlodDebugShowOnly._val.b);
									if (!component->GetMesh())
										continue;
								}

								if (distanceToCluster < switchDistance)
									continue;
							}
							else
							{
								const int64_t sourceClusterKey = PackHlodClusterKeyFromPosition(entity->GetPosition(), r_hlodClusterSize._val.f32);
								auto hlodIt = hlodClusters.find(sourceClusterKey);
								if (hlodIt != hlodClusters.end())
								{
									const math::Vector3 clusterCenter = hlodIt->second.hasCenter ? hlodIt->second.center : entity->GetPosition();
									const float distanceToCluster = (clusterCenter - params.camera->GetEntity()->GetPosition()).Length();
									if (distanceToCluster >= switchDistance)
										continue;
								}
							}
						}
					}
				}

				bool visible = IsEntityVisible(entity, params);

				if (entity->IsInPVS() != visible && params.isShadow == false)
				{
					PVSVisibilityChangedMessage pvsMsg;
					pvsMsg.visible = visible;
					entity->OnMessage(&pvsMsg, nullptr);
					++visibilityMessages;
				}

				if (!visible)
					continue;

				auto mesh = component->GetMesh();

				if (!mesh)
					continue;

				if (auto lod = mesh->GetLodLevel(); lod != -1)
				{
					//if (lod < r_shadowMinimumLodThreshold._val.i32)
					//	continue;

					if (params.forceMaxLod)
					{
						if (lod < mesh->GetMaxLodLevel())
							continue;
					}

					// LOD band test widened by this PVS's translation hysteresis:
					// the set has to stay valid until the next rebuild, i.e. for
					// any camera position within lodSlack of the current one, or
					// an entity whose band changed in between would have no
					// admitted LOD and vanish. The draw loops apply the exact
					// band per frame.
					const float lodPartitions = params.lodPartition;
					const float minDistance = lodPartitions * (float)lod;
					const float maxDistance = lodPartitions * (float)(lod + 1);
					const float distance = (entity->GetPosition() - scene->GetMainCamera()->GetEntity()->GetPosition()).Length();

					if (lod < 3)
					{
						if (distance + lodSlack < minDistance || distance - lodSlack > maxDistance)
							continue;
					}
					else if (distance + lodSlack < minDistance)
					{
						continue;
					}
				}

				auto material = component->GetMaterial();

				if (!material)
					continue;

				auto meshInstance = mesh->GetInstance();

				if (!meshInstance)
					continue;

				auto& batch = _pvs[material];

				if (batch.capacity() == 0)
					batch.reserve(256);

				_totalEnts++;

				if (entity->HasA<SkeletalAnimationComponent>())
					_totalSkeletalAnimators++;

				batch.push_back({ mesh, entity, component, meshInstance->GetInstanceId() });

			}
		}

		const auto loopEnd = std::chrono::high_resolution_clock::now();

		// Drop batches whose material fell out of the visible set entirely (the
		// in-place reset above keeps them around for capacity reuse), then order
		// each surviving batch by its cached instance id so the draw loop can
		// batch consecutive same-instance entries.
		for (auto it = _pvs.begin(); it != _pvs.end();)
		{
			if (it->second.empty())
			{
				it = _pvs.erase(it);
				continue;
			}

			std::sort(it->second.begin(), it->second.end(),
				[](const MeshEntityPair& left, const MeshEntityPair& right)
				{
					return std::get<3>(left) < std::get<3>(right);
				}
			);

			++it;
		}

		_didRebuild = true;

		if (r_pvsPerfLog._val.b)
		{
			const auto now = std::chrono::high_resolution_clock::now();
			const float ms = std::chrono::duration<float, std::milli>(now - rebuildStart).count();
			const float gatherMs = std::chrono::duration<float, std::milli>(gatherEnd - rebuildStart).count();
			const float loopMs = std::chrono::duration<float, std::milli>(loopEnd - gatherEnd).count();
			const float sortMs = std::chrono::duration<float, std::milli>(now - loopEnd).count();
			LOG_INFO("PVS rebuild: frame=%lld shape=%d shadow=%d forced=%d contain=%d moved=%.1f oldR=%.1f newR=%.1f candidates=%zu kept=%u batches=%zu visMsgs=%u ms=%.2f (gather=%.2f loop=%.2f sort=%.2f)",
				(long long)(g_pEnv && g_pEnv->_timeManager ? g_pEnv->_timeManager->_frameCount : 0),
				(int32_t)params.shapeType, params.isShadow ? 1 : 0, wasForced ? 1 : 0, hystContainment, hystMoved, hystOldRadius, hystNewRadius,
				components.size(), _totalEnts, _pvs.size(), visibilityMessages, ms, gatherMs, loopMs, sortMs);
		}
	}

	bool PVS::IsEntityVisible(Entity* entity, const PVSParams& params)
	{
		if (entity->GetLayer() == Layer::Sky)
			return true;

		return IsShapeVisible(entity->GetWorldBoundingSphere(), params);
	}

	bool PVS::IsShapeVisible(const dx::BoundingBox& bbox, const PVSParams& params)
	{
		switch (params.shapeType)
		{
		case PVSParams::ShapeType::Frustum:
			return _optimisedParams.shape.frustum.sm.Intersects(bbox);

		case PVSParams::ShapeType::Frustum2:
			return _optimisedParams.shape.frustum.lg.Intersects(bbox);

		case PVSParams::ShapeType::Sphere:
			return _optimisedParams.shape.sphere.Intersects(bbox);

		default:
			return false;
		}
	}

	bool PVS::IsShapeVisible(const dx::BoundingSphere& bsphere, const PVSParams& params)
	{
		switch (params.shapeType)
		{
		case PVSParams::ShapeType::Frustum:
		case PVSParams::ShapeType::Frustum2:
		{
			if (_cullPlanesValid)
			{
				for (int32_t i = 0; i < 6; ++i)
				{
					const math::Vector4& p = _cullPlanes[i];
					if (p.x * bsphere.Center.x + p.y * bsphere.Center.y + p.z * bsphere.Center.z + p.w > bsphere.Radius)
						return false;
				}
				return true;
			}

			return params.shapeType == PVSParams::ShapeType::Frustum2
				? _optimisedParams.shape.frustum.lg.Intersects(bsphere)
				: _optimisedParams.shape.frustum.sm.Intersects(bsphere);
		}

		case PVSParams::ShapeType::Sphere:
			return _optimisedParams.shape.sphere.Intersects(bsphere);

		default:
			return false;
		}
	}

	void PVS::AddEntity(Entity* entity)
	{
		FlushEntity(entity, true);
	}

	void PVS::RefreshAllInstanceCaches()
	{
		if (_updatesDisabled)
			return;

		// No-op: see UpdateEntityInstanceCache - the draw loop's per-entry
		// transform-version check refreshes stale instance data at the moment
		// a (cached shadow) re-render actually draws the entry.
	}

	void PVS::UpdateEntityInstanceCache(Entity* entity)
	{
		// Intentionally a no-op now. Snapshot entries carry the entity
		// transform version they were pulled at and the draw loops refresh
		// stale entries on use (Scene::RenderEntities), so nothing has to scan
		// the snapshot per moving entity. Kept so the many call sites in
		// Entity.cpp stay untouched.
		(void)entity;
	}

	void PVS::FlushEntity(Entity* entity, bool recache)
	{
		/*if (_updatesDisabled)
			return;*/

		if (!entity)
			return;

		std::unique_lock lock(_lock);

		uint32_t removedFromPvs = 0;
		uint32_t removedSkeletal = 0;

		for (auto it = _pvs.begin(); it != _pvs.end();)
		{
			auto& entries = it->second;
			entries.erase(
				std::remove_if(entries.begin(), entries.end(),
					[entity, &removedFromPvs, &removedSkeletal](const MeshEntityPair& pair)
					{
						const bool shouldRemove = std::get<1>(pair) == entity;
						if (shouldRemove)
						{
							removedFromPvs++;
							if (entity->HasA<SkeletalAnimationComponent>())
								removedSkeletal++;
						}
						return shouldRemove;
					}),
				entries.end());

			if (entries.empty())
				it = _pvs.erase(it);
			else
				++it;
		}

		for (auto it = _renderableSnapshot.begin(); it != _renderableSnapshot.end();)
		{
			auto& snapshotEntries = it->second;
			snapshotEntries.erase(
				std::remove_if(snapshotEntries.begin(), snapshotEntries.end(),
					[entity](const RenderableSnapshot& snapshot)
					{
						return snapshot.entity == entity;
					}),
				snapshotEntries.end());

			if (snapshotEntries.empty())
				it = _renderableSnapshot.erase(it);
			else
				++it;
		}

		_totalEnts = _totalEnts > removedFromPvs ? _totalEnts - removedFromPvs : 0;
		_totalSkeletalAnimators = _totalSkeletalAnimators > removedSkeletal ? _totalSkeletalAnimators - removedSkeletal : 0;

		if (!recache)
			return;

		if (!_hasBuildOptimisation)
			return;

		if (entity->GetLayer() == Layer::Invisible || entity->GetLayer() == Layer::Trigger || entity->HasFlag(EntityFlags::DoNotRender))
			return;

		if (_optimisedParams.isShadow && !entity->GetCastsShadows())
			return;

		if (!IsEntityVisible(entity, _optimisedParams))
			return;

		auto scene = entity->GetScene();
		if (!scene)
			return;

		const bool hasSkeletalAnimation = entity->HasA<SkeletalAnimationComponent>();
		auto meshComponents = entity->GetComponents<StaticMeshComponent>();

		for (auto* meshComponent : meshComponents)
		{
			if (!meshComponent)
				continue;

			auto mesh = meshComponent->GetMesh();
			if (!mesh)
				continue;

			if (auto lod = mesh->GetLodLevel(); lod != -1)
			{
				if (_optimisedParams.forceMaxLod)
				{
					if (lod < mesh->GetMaxLodLevel())
						continue;
				}

				// Same slack-widened band test as CalculateVisibility.
				const float lodSlack = _optimisedParams.shapeType == PVSParams::ShapeType::Sphere
					? _optimisedParams.shape.sphere.Radius * 0.25f
					: 0.0f;
				const float lodPartitions = _optimisedParams.lodPartition;
				const float minDistance = lodPartitions * static_cast<float>(lod);
				const float maxDistance = lodPartitions * static_cast<float>(lod + 1);
				const float distance = (entity->GetPosition() - scene->GetMainCamera()->GetEntity()->GetPosition()).Length();

				if (lod < 3)
				{
					if (distance + lodSlack < minDistance || distance - lodSlack > maxDistance)
						continue;
				}
				else if (distance + lodSlack < minDistance)
				{
					continue;
				}
			}

			auto material = meshComponent->GetMaterial();
			if (!material)
				continue;

			auto meshInstance = mesh->GetInstance();
			if (!meshInstance)
				continue;

			auto& pvsBatch = _pvs[material];
			pvsBatch.push_back({ mesh, entity, meshComponent, meshInstance->GetInstanceId() });

			std::sort(pvsBatch.begin(), pvsBatch.end(),
				[](const MeshEntityPair& left, const MeshEntityPair& right)
				{
					return std::get<3>(left) < std::get<3>(right);
				});

			auto snapshotIt = std::find_if(_renderableSnapshot.begin(), _renderableSnapshot.end(),
				[&material](const auto& batch)
				{
					return batch.first == material;
				});

			if (snapshotIt == _renderableSnapshot.end())
			{
				_renderableSnapshot.push_back({ material, {} });
				snapshotIt = std::prev(_renderableSnapshot.end());
			}

			RenderableSnapshot snapshot;
			snapshot.mesh = mesh;
			snapshot.material = material;
			snapshot.instance = meshInstance;
			snapshot.simpleInstance = meshInstance->GetSimpleInstance();
			snapshot.layer = entity->GetLayer();
			snapshot.hasAnimations = mesh->HasAnimations();
			snapshot.isBoundToBone = meshComponent->IsBoundToBone();
			snapshot.shadowCullMode = meshComponent->GetShadowCullMode();
			snapshot.entity = entity;
			snapshot.component = meshComponent;
			snapshot.transformVersion = entity->GetTransformVersion();

			if (snapshot.isBoundToBone)
			{
				snapshot.shadowInstanceData.worldMatrix = entity->GetWorldTMTranspose() * meshComponent->GetOffsetMatrixTranspose();
				snapshot.instanceData.worldMatrix = snapshot.shadowInstanceData.worldMatrix;
				snapshot.instanceData.worldMatrixPrev = entity->GetWorldTMPrevTranspose();
				snapshot.instanceData.worldMatrixInverseTranspose = entity->GetWorldTMInvert();
				snapshot.instanceData.colour = material->_properties.diffuseColour;
				snapshot.instanceData.uvscale = meshComponent->GetUVScale();
			}
			else
			{
				snapshot.shadowInstanceData = meshComponent->GetCachedShadowInstanceData();
				snapshot.instanceData = meshComponent->GetCachedInstanceData(material.get());
			}

			snapshotIt->second.push_back(snapshot);
			_totalEnts++;
			if (hasSkeletalAnimation)
				_totalSkeletalAnimators++;
		}
	}

	void PVS::RemoveEntity(Entity* entity)
	{
		FlushEntity(entity);
	}

	const PVSParams& PVS::GetOptimisedParams() const
	{
		return _optimisedParams;
	}
}
