
#include "PhysUtils.hpp"
#include "RayMath.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Scene/Scene.hpp"
#include "../Scene/Mesh.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Entity/Component/RigidBody.hpp"
#include "../Entity/Component/StaticMeshComponent.hpp"
#include "../Input/HVar.hpp"
#include "../Environment/LogFile.hpp"

#include <mutex>

namespace HexEngine
{
	HVar phys_rayBvh("phys_rayBvh", "PhysUtils::RayCast: test big meshes through a cached per-mesh triangle BVH (0 = brute force over every triangle)", true, false, true);
	HVar phys_rayBvhMinTriangles("phys_rayBvhMinTriangles", "PhysUtils::RayCast: meshes with at least this many triangles get a ray BVH", 256u, 1u, 0x7FFFFFFFu);
	HVar phys_raySpatialGrid("phys_raySpatialGrid", "PhysUtils::RayCast: gather candidate meshes from the scene spatial grid instead of scanning every static mesh component", true, false, true);

	namespace PhysUtils
	{
		namespace
		{
			// ---- Per-mesh ray BVH cache --------------------------------------
			//
			// Keyed by Mesh*, validated by a weak_ptr (a freed Mesh whose address
			// is reused can't alias a stale entry) plus the mesh's geometry
			// revision and vertex/index counts (rebuilt after
			// UpdateDynamicGeometry / Add* / Clear). Built lazily on the first
			// ray that reaches the mesh's triangles. A BVH is immutable once
			// published, so rays hold a shared_ptr and test without the lock.

			struct MeshBvhEntry
			{
				std::weak_ptr<Mesh> mesh;
				uint32_t revision = 0;
				size_t vertexCount = 0;
				size_t indexCount = 0;
				std::shared_ptr<const RayMath::TriangleBVH> bvh;
			};

			std::mutex g_meshBvhMutex;
			std::unordered_map<const Mesh*, MeshBvhEntry> g_meshBvhCache;
			uint32_t g_meshBvhInsertsSincePurge = 0;

			std::shared_ptr<const RayMath::TriangleBVH> GetMeshBvh(const std::shared_ptr<Mesh>& mesh, size_t triCount)
			{
				const auto& verts = mesh->GetVertices();
				const auto& idx = mesh->GetIndices();
				const uint32_t revision = mesh->GetGeometryRevision();

				std::lock_guard<std::mutex> lock(g_meshBvhMutex);
				auto it = g_meshBvhCache.find(mesh.get());
				if (it != g_meshBvhCache.end())
				{
					const MeshBvhEntry& e = it->second;
					if (e.mesh.lock() == mesh && e.revision == revision &&
						e.vertexCount == verts.size() && e.indexCount == idx.size())
						return e.bvh;
				}

				// (Re)build. Holding the cache lock keeps two rays from building
				// the same mesh twice; builds are one-off per mesh revision.
				const auto start = std::chrono::high_resolution_clock::now();
				auto bvh = std::make_shared<RayMath::TriangleBVH>();
				bvh->Build(triCount,
					[&](size_t i) { return idx[i]; },
					verts.size(),
					[&](uint32_t vi) { const auto& p = verts[vi]._position; return math::Vector3(p.x, p.y, p.z); });
				const float ms = std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - start).count();
				if (ms > 50.0f)
				{
					LOG_INFO("PhysUtils: built ray BVH for mesh '%s': %zu triangles, %zu nodes, %.1f MB in %.1f ms",
						mesh->GetName().c_str(), bvh->TriangleCount(), bvh->NodeCount(), bvh->MemoryBytes() / (1024.0 * 1024.0), ms);
				}

				MeshBvhEntry entry;
				entry.mesh = mesh;
				entry.revision = revision;
				entry.vertexCount = verts.size();
				entry.indexCount = idx.size();
				entry.bvh = bvh;
				g_meshBvhCache[mesh.get()] = std::move(entry);

				// Drop entries of meshes that have been freed.
				if (++g_meshBvhInsertsSincePurge >= 64)
				{
					g_meshBvhInsertsSincePurge = 0;
					for (auto purge = g_meshBvhCache.begin(); purge != g_meshBvhCache.end();)
					{
						if (purge->second.mesh.expired())
							purge = g_meshBvhCache.erase(purge);
						else
							++purge;
					}
				}
				return bvh;
			}

			struct SceneLockGuard
			{
				explicit SceneLockGuard(Scene* inScene) : scene(inScene) { scene->Lock(); }
				~SceneLockGuard() { scene->Unlock(); }
				SceneLockGuard(const SceneLockGuard&) = delete;
				SceneLockGuard& operator=(const SceneLockGuard&) = delete;
				Scene* scene;
			};

			// Entity-level filters shared by every hit source.
			struct EntityFilter
			{
				const RayCastOptions& opt;

				bool InSet(const std::unordered_set<Entity*>& set, Entity* e) const
				{
					for (Entity* x = e; x != nullptr; x = opt.hierarchical ? x->GetParent() : nullptr)
					{
						if (set.count(x) != 0)
							return true;
					}
					return false;
				}

				bool Accept(Entity* e) const
				{
					// A collider with no owning entity has no layer and can't be in
					// an ignore list; it can only fail an `only` filter.
					if (e == nullptr)
						return opt.only.empty();
					if (e->IsPendingDeletion())
						return false;
					const uint32_t layer = static_cast<uint32_t>(e->GetLayer());
					if (layer >= 32 || (opt.layerMask & (1u << layer)) == 0)
						return false;
					if (!opt.ignore.empty() && InSet(opt.ignore, e))
						return false;
					if (!opt.only.empty() && !InSet(opt.only, e))
						return false;
					return true;
				}

				static bool Thunk(Entity* e, void* context)
				{
					return static_cast<const EntityFilter*>(context)->Accept(e);
				}
			};

			math::Vector3 ToVec3(const math::Vector4& v) { return math::Vector3(v.x, v.y, v.z); }

			math::Vector3 SafeNormalized(math::Vector3 n, const math::Vector3& fallback)
			{
				const float len = n.Length();
				if (!(len > 1e-30f) || !RayMath::IsFinite(len))
					return fallback;
				return n / len;
			}

			// Nearest triangle of one mesh entity, tested in object space. Fills
			// `out` and returns true only for a hit nearer than `limit`.
			bool RayCastMeshTriangles(Entity* entity, StaticMeshComponent* smc, const std::shared_ptr<Mesh>& meshPtr,
				const math::Vector3& o, const math::Vector3& d, float limit, const RayCastOptions& opt, RayHit& out)
			{
				Mesh& mesh = *meshPtr;
				const auto& verts = mesh.GetVertices();
				const auto& idx = mesh.GetIndices();
				const size_t triCount = std::min<size_t>(mesh.GetNumFaces(), idx.size() / 3);
				if (verts.empty() || triCount == 0)
					return false;

				math::Matrix inv;
				if (!RayMath::InvertWorld(entity->GetWorldTM(), inv))
					return false;

				const RayMath::ObjectRay ray = RayMath::ToObjectSpace(o, d, inv);
				const bool slopeLimit = opt.minNormalY > -1.0f;

				auto accept = [&](uint32_t, float, const math::Vector3& p0, const math::Vector3& p1, const math::Vector3& p2)
				{
					if (!slopeLimit)
						return true;
					const math::Vector3 n = RayMath::NormalToWorld(RayMath::FaceNormal(p0, p1, p2), inv);
					return !RayMath::RejectsSlope(n, opt.minNormalY);
				};

				RayMath::TriangleHit tri;
				std::shared_ptr<const RayMath::TriangleBVH> bvh;
				if (phys_rayBvh._val.b && triCount >= std::max<uint32_t>(1u, phys_rayBvhMinTriangles._val.ui32))
					bvh = GetMeshBvh(meshPtr, triCount);
				if (bvh)
				{
					tri = bvh->Intersect(ray, limit, accept);
				}
				else
				{
					tri = RayMath::NearestTriangle(ray, triCount,
						[&](size_t i) { return idx[i]; },
						verts.size(),
						[&](uint32_t vi) { return ToVec3(verts[vi]._position); },
						limit,
						accept);
				}

				if (!tri.hit)
					return false;

				const uint32_t i0 = idx[tri.triangle * 3 + 0];
				const uint32_t i1 = idx[tri.triangle * 3 + 1];
				const uint32_t i2 = idx[tri.triangle * 3 + 2];
				const float w0 = 1.0f - tri.u - tri.v;

				const math::Vector3 faceNormal = SafeNormalized(
					RayMath::NormalToWorld(RayMath::FaceNormal(ToVec3(verts[i0]._position), ToVec3(verts[i1]._position), ToVec3(verts[i2]._position)), inv),
					-d);
				const math::Vector3 smooth = RayMath::NormalToWorld(
					verts[i0]._normal * w0 + verts[i1]._normal * tri.u + verts[i2]._normal * tri.v, inv);

				out = RayHit{};
				out.start = o;
				out.distance = tri.t;
				out.position = o + d * tri.t;
				out.entity = entity;
				out.normal = faceNormal;
				out.smoothNormal = SafeNormalized(smooth, faceNormal);
				out.uv = verts[i0]._texcoord * w0 + verts[i1]._texcoord * tri.u + verts[i2]._texcoord * tri.v;
				out.material = smc->GetMaterial();
				out.source = RayHitSource::Mesh;
				out.backFace = RayMath::IsBackFace(faceNormal, d);
				out.triangleIndex = tri.triangle;
				return true;
			}

			RayCastOptions LegacyOptions(float maxDistance, LayerMask mask, const std::vector<Entity*>& entsToIgnore)
			{
				RayCastOptions opt;
				opt.maxDistance = maxDistance;
				opt.layerMask = mask;
				opt.meshes = true;
				opt.rigidBodyColliders = true;
				opt.physics = false; // legacy callers were never given scene colliders (CCT capsules etc.)
				opt.includeHidden = false;
				opt.hierarchical = true;
				for (Entity* e : entsToIgnore)
				{
					if (e != nullptr)
						opt.ignore.insert(e);
				}
				return opt;
			}

			std::shared_ptr<Scene> CurrentScene()
			{
				if (g_pEnv == nullptr || g_pEnv->_sceneManager == nullptr)
					return nullptr;
				return g_pEnv->_sceneManager->GetCurrentScene();
			}
		}

		bool HEX_API RayCast(Scene* scene, const math::Vector3& origin, const math::Vector3& direction, const RayCastOptions& opt, RayHit* hitInfo)
		{
			if (scene == nullptr)
				return false;

			const float dirLen = direction.Length();
			if (!(dirLen > 1e-6f) || !RayMath::IsFinite(dirLen))
				return false;
			const math::Vector3 d = direction / dirLen;
			const math::Vector3& o = origin;

			const float maxDist = (opt.maxDistance > 0.0f)
				? std::min(opt.maxDistance, RayCastOptions::kUnlimitedDistance)
				: RayCastOptions::kUnlimitedDistance;

			SceneLockGuard sceneLock(scene);

			const EntityFilter filter{ opt };
			IPhysicsSystem* physicsSystem = g_pEnv != nullptr ? g_pEnv->_physicsSystem : nullptr;

			RayHit best;
			bool hasBest = false;
			auto limit = [&]() { return hasBest ? best.distance : maxDist; };

			if (opt.meshes)
			{
				const DirectX::XMVECTOR xo = DirectX::XMVectorSet(o.x, o.y, o.z, 1.0f);
				const DirectX::XMVECTOR xd = DirectX::XMVectorSet(d.x, d.y, d.z, 0.0f);

				// Broad phase: the scene's (loose) spatial grid, walking only the
				// cells the ray segment passes through. Falls back to every
				// static mesh component when the grid is off or unavailable.
				std::vector<StaticMeshComponent*> meshComponents;
				bool gathered = false;
				if (phys_raySpatialGrid._val.b)
				{
					const math::Vector3 end = o + d * maxDist;
					dx::BoundingBox segmentBounds;
					dx::BoundingBox::CreateFromPoints(segmentBounds, o, end);
					gathered = scene->QueryStaticMeshCullingCandidates(segmentBounds,
						[&](const dx::BoundingBox& cell)
						{
							float cellDist = 0.0f;
							return cell.Intersects(xo, xd, cellDist) && cellDist <= maxDist;
						},
						meshComponents);
				}
				if (!gathered)
				{
					meshComponents.clear();
					scene->GetComponents<StaticMeshComponent>(meshComponents);
				}

				for (StaticMeshComponent* smc : meshComponents)
				{
					if (!(limit() > 0.0f))
						break; // a hit at the origin can't be beaten

					Entity* entity = smc != nullptr ? smc->GetEntity() : nullptr;
					if (entity == nullptr)
						continue;
					if (!opt.includeHidden && entity->HasFlag(EntityFlags::DoNotRender))
						continue;

					// Broad phase. A ray starting INSIDE the box reports 0 and is
					// kept (the old code dropped it, so nothing was ever hit from
					// inside a building's bounds).
					float boxDist = 0.0f;
					if (!entity->GetWorldAABB().Intersects(xo, xd, boxDist))
						continue;
					if (boxDist > limit())
						continue;

					if (!filter.Accept(entity))
						continue;

					if (opt.rigidBodyColliders && physicsSystem != nullptr)
					{
						if (auto* rigidBody = entity->GetComponent<RigidBody>(); rigidBody != nullptr && rigidBody->GetIRigidBody() != nullptr)
						{
							RayHit h;
							if (physicsSystem->RayCast(o, d, limit(), rigidBody->GetIRigidBody(), &h) == 0 || !(h.distance < limit()))
								continue; // the collider IS this entity's shape for gameplay rays

							h.normal = SafeNormalized(h.normal, -d);
							if (!RayMath::RejectsSlope(h.normal, opt.minNormalY))
							{
								h.start = o;
								h.entity = entity;
								h.smoothNormal = h.normal;
								h.uv = math::Vector2(0.0f, 0.0f);
								h.material = smc->GetMaterial();
								h.source = RayHitSource::Collider;
								h.backFace = RayMath::IsBackFace(h.normal, d);
								h.triangleIndex = UINT32_MAX;
								best = h;
								hasBest = true;
								continue;
							}
							// Too steep: fall through to the triangles, which can
							// see a flatter surface of the same entity behind it.
						}
					}

					auto mesh = smc->GetMesh();
					if (!mesh || mesh->HasAnimations())
						continue;

					RayHit h;
					if (RayCastMeshTriangles(entity, smc, mesh, o, d, limit(), opt, h))
					{
						best = h;
						hasBest = true;
					}
				}
			}

			if (opt.physics && physicsSystem != nullptr)
			{
				RaySceneFilter sceneFilter;
				sceneFilter.acceptEntity = &EntityFilter::Thunk;
				sceneFilter.context = const_cast<EntityFilter*>(&filter);
				sceneFilter.minNormalY = opt.minNormalY;

				RayHit h;
				const float physLimit = limit();
				if (physLimit > 0.0f &&
					physicsSystem->RayCastSceneFiltered(o, d, physLimit, &h, sceneFilter) > 0 &&
					h.distance < physLimit)
				{
					h.start = o;
					h.normal = SafeNormalized(h.normal, -d);
					h.smoothNormal = h.normal;
					h.backFace = RayMath::IsBackFace(h.normal, d);
					h.source = RayHitSource::PhysicsScene;
					if (h.entity != nullptr)
					{
						if (auto* smc = h.entity->GetComponent<StaticMeshComponent>(); smc != nullptr)
							h.material = smc->GetMaterial();
					}
					best = h;
					hasBest = true;
				}
			}

			if (hasBest && hitInfo != nullptr)
				*hitInfo = best;
			return hasBest;
		}

		bool HEX_API RayCast(const math::Vector3& from, const math::Vector3& to, LayerMask mask, RayHit* hitInfo, const std::vector<Entity*>& entsToIgnore)
		{
			const math::Vector3 delta = to - from;
			const float distance = delta.Length();
			if (!(distance > FLT_EPSILON))
				return false;

			auto scene = CurrentScene();
			if (scene == nullptr)
				return false;

			return RayCast(scene.get(), from, delta, LegacyOptions(distance, mask, entsToIgnore), hitInfo);
		}

		bool HEX_API RayCast(const math::Ray& ray, float maxDistance, LayerMask mask, RayHit* hitInfo, const std::vector<Entity*>& entsToIgnore)
		{
			auto scene = CurrentScene();
			if (scene == nullptr)
				return false;

			return RayCast(scene.get(), ray.position, ray.direction, LegacyOptions(maxDistance, mask, entsToIgnore), hitInfo);
		}
	}
}
