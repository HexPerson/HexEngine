// city_kerbs: edge every paved surface that sits above the grass with a kerb.
//
// Floors, pavements, car parks and driveways are flat tiles laid a little above the
// terrain, so where paving meets grass its edge floated (a dark gap / slab side). This
// console command traces the outline of the combined paved area and lays the Universe
// kit kerb (SM_Envr_Road_Sidewalk_Edge_01) along every stretch of outline where the grass
// outside sits lower than the paving. Re-run it after adding or enlarging paved areas:
// it replaces the previous set (children of the "Kerbs" group). `city_kerbs clear`
// only removes them.
//
// Paved = flat StaticMeshComponent meshes whose file name matches kPavedMeshes (hidden
// road tiles included: entrance cuts hide the merged tile and rebuild it from pieces).
// Per outline stretch (<= 5 m) the paving height is ray-cast just inside the edge, the
// terrain (VolTerrainChunk*) just outside; no kerb where the step is flush, where the
// drop is a wall / quay (more than kMaxStep), or where something other than terrain
// stands right outside (a building, another slab, water).

#include <HexEngine.Core/HexEngine.hpp>
#include <HexEngine.Core/Entity/Entity.hpp>
#include <HexEngine.Core/Entity/Component/StaticMeshComponent.hpp>
#include <HexEngine.Core/Input/HCommand.hpp>
#include <HexEngine.Core/Input/Console.hpp>
#include <HexEngine.Core/Physics/PhysUtils.hpp>
#include <HexEngine.Core/Scene/Mesh.hpp>
#include <HexEngine.Core/Scene/Scene.hpp>
#include <HexEngine.Core/Scene/SceneManager.hpp>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
	using namespace HexEngine;

	const char* const kPavedMeshes[] = {
		"Floor_Concrete", "Road_Sidewalk", "RoadSection_", "Road_TJunction", "Road_Plane",
		"SM_Envr_Road_Sidewalk_0", "SM_Envr_Road_Sidewalk_Driveway", "SM_Envr_Road_Sidewalk_Half",
		"SM_Envr_Road_Sidewalk_Floor", "SM_Envr_Road_Suburb_Sidewalk", "SM_Envr_Airport_Apron",
		"SM_Envr_Road_Road_0",
	};
	const char* const kKerbMesh = "GameData.Meshes/Universe/Environment/Road/SM_Envr_Road_Sidewalk_Edge_01_floor714.hmesh";
	const char* const kGroupName = "Kerbs";
	constexpr float kCell = 0.25f;			// outline raster resolution (tiles sit on 0.25 m multiples)
	constexpr float kMaxPiece = 5.0f;		// the kerb mesh is 5 m long
	constexpr float kMinStep = 0.03f;		// paving this little above the grass needs no kerb
	constexpr float kMaxStep = 0.9f;		// more than this is a wall / quay edge, not a kerb
	constexpr float kKerbDrop = 0.1f;		// kerb mesh centre below the paving (top lip ~3 cm proud)
	constexpr float kKerbOut = 0.12f;		// kerb centre outside the paving edge
	constexpr float kCornerOverlap = 0.24f;	// runs run on past their ends so corners close

	bool IsPavedMesh(const std::string& fileName)
	{
		for (const char* p : kPavedMeshes)
			if (fileName.find(p) != std::string::npos)
				return true;
		return false;
	}

	struct Rect { float x0, z0, x1, z1; };

	bool CastDown(Scene* scene, float x, float z, const RayCastOptions& opt, RayHit& hit, float fromY = 400.0f)
	{
		return PhysUtils::RayCast(scene, math::Vector3(x, fromY, z), math::Vector3(0.0f, -1.0f, 0.0f), opt, &hit);
	}
}

HEX_COMMAND(city_kerbs)
{
	(void)pressed; (void)param;
	using namespace HexEngine;
	auto sceneRef = g_pEnv->_sceneManager->GetCurrentScene();
	Scene* scene = sceneRef.get();
	if (scene == nullptr)
	{
		CON_ECHO("^rcity_kerbs: no scene");
		return;
	}

	// Previous kerbs go first (they are not paving, but they'd shadow the rays).
	Entity* group = scene->GetEntityByName(kGroupName);
	size_t removed = 0;
	if (group != nullptr)
	{
		const std::vector<Entity*> old = group->GetChildren();
		for (Entity* e : old)
		{
			if (e != nullptr && !e->IsPendingDeletion())
			{
				e->DeleteMe();
				++removed;
			}
		}
	}
	if (args != nullptr && args->Count() >= 1 && args->Get<std::string>(0) == "clear")
	{
		LOG_INFO("city_kerbs: removed %zu kerb pieces", removed);
		CON_ECHO("city_kerbs: removed %zu kerb pieces", removed);
		return;
	}

	// --- paved footprints
	std::vector<StaticMeshComponent*> meshes;
	scene->GetComponents<StaticMeshComponent>(meshes);
	std::vector<Rect> rects;
	RayCastOptions pavedOpt;
	pavedOpt.physics = false;
	pavedOpt.rigidBodyColliders = false;
	pavedOpt.includeHidden = true;
	RayCastOptions terrainOpt;
	RayCastOptions anyOpt;
	anyOpt.includeHidden = false;
	float minX = 1e30f, minZ = 1e30f, maxX = -1e30f, maxZ = -1e30f;
	for (auto* smc : meshes)
	{
		Entity* e = smc != nullptr ? smc->GetEntity() : nullptr;
		if (e == nullptr || e->IsPendingDeletion() || (group != nullptr && e->GetParent() == group))
			continue;
		if (e->GetName().rfind("VolTerrainChunk", 0) == 0)
		{
			terrainOpt.only.insert(e);
			continue;
		}
		auto mesh = smc->GetMesh();
		if (!mesh || !IsPavedMesh(mesh->GetFileSystemPath().filename().string()))
			continue;
		const dx::BoundingBox& b = e->GetWorldAABB();
		if (b.Extents.y > 0.4f || b.Extents.x < 0.4f || b.Extents.z < 0.4f)
			continue;	// not a flat tile (or a marking line)
		Rect r{ b.Center.x - b.Extents.x, b.Center.z - b.Extents.z, b.Center.x + b.Extents.x, b.Center.z + b.Extents.z };
		rects.push_back(r);
		pavedOpt.only.insert(e);
		minX = std::min(minX, r.x0); minZ = std::min(minZ, r.z0);
		maxX = std::max(maxX, r.x1); maxZ = std::max(maxZ, r.z1);
	}
	// Terrain chunks may have no mesh component (collider only): find them by name too.
	{
		std::vector<EntityId> ids;
		scene->GetLiveEntityIds(ids);
		for (const EntityId id : ids)
		{
			Entity* e = scene->TryGetEntity(id);
			if (e != nullptr && !e->IsPendingDeletion() && e->GetName().rfind("VolTerrainChunk", 0) == 0)
				terrainOpt.only.insert(e);
		}
	}
	if (rects.empty() || terrainOpt.only.empty())
	{
		CON_ECHO("^rcity_kerbs: no paved tiles (%zu) or no terrain chunks (%zu)", rects.size(), terrainOpt.only.size());
		return;
	}

	// --- rasterise the union of the footprints
	const float ox = std::floor(minX / kCell) * kCell - kCell;
	const float oz = std::floor(minZ / kCell) * kCell - kCell;
	const int nx = (int)std::ceil((maxX - ox) / kCell) + 2;
	const int nz = (int)std::ceil((maxZ - oz) / kCell) + 2;
	std::vector<uint8_t> grid((size_t)nx * (size_t)nz, 0);
	auto at = [&](int i, int k) -> uint8_t& { return grid[(size_t)k * (size_t)nx + (size_t)i]; };
	for (const Rect& r : rects)
	{
		// Cells whose centre lies inside the rectangle.
		const int i0 = std::max(0, (int)std::ceil((r.x0 - ox) / kCell - 0.5f - 1e-3f));
		const int i1 = std::min(nx - 1, (int)std::floor((r.x1 - ox) / kCell - 0.5f + 1e-3f));
		const int k0 = std::max(0, (int)std::ceil((r.z0 - oz) / kCell - 0.5f - 1e-3f));
		const int k1 = std::min(nz - 1, (int)std::floor((r.z1 - oz) / kCell - 0.5f + 1e-3f));
		for (int k = k0; k <= k1; ++k)
			for (int i = i0; i <= i1; ++i)
				at(i, k) = 1;
	}

	// --- outline runs: (line coordinate, start, end, along X?, outward sign)
	struct Run { float line, a, b; bool alongX; int out; };
	std::vector<Run> runs;
	for (int k = 0; k + 1 < nz; ++k)		// lines between rows k and k+1 (z = oz + (k+1)*cell), runs along X
	{
		int cur = 0, start = 0;
		for (int i = 0; i <= nx; ++i)
		{
			int t = 0;
			if (i < nx)
			{
				const uint8_t lo = at(i, k), hi = at(i, k + 1);
				t = (lo && !hi) ? +1 : (!lo && hi) ? -1 : 0;	// outward toward +z / -z
			}
			if (t != cur)
			{
				if (cur != 0)
					runs.push_back(Run{ oz + (k + 1) * kCell, ox + start * kCell, ox + i * kCell, true, cur });
				cur = t;
				start = i;
			}
		}
	}
	for (int i = 0; i + 1 < nx; ++i)		// lines between columns i and i+1, runs along Z
	{
		int cur = 0, start = 0;
		for (int k = 0; k <= nz; ++k)
		{
			int t = 0;
			if (k < nz)
			{
				const uint8_t lo = at(i, k), hi = at(i + 1, k);
				t = (lo && !hi) ? +1 : (!lo && hi) ? -1 : 0;
			}
			if (t != cur)
			{
				if (cur != 0)
					runs.push_back(Run{ ox + (i + 1) * kCell, oz + start * kCell, oz + k * kCell, false, cur });
				cur = t;
				start = k;
			}
		}
	}

	// --- kerb pieces
	auto kerbMesh = Mesh::Create(fs::path(kKerbMesh));
	if (!kerbMesh)
	{
		CON_ECHO("^rcity_kerbs: can't load %s", kKerbMesh);
		return;
	}
	if (group == nullptr)
		group = scene->CreateEntity(kGroupName);
	// A 13 cm kerb's shadow isn't worth a shadow-map draw per piece: the group opts
	// every kerb out (the flag is inherited by children).
	group->SetFlag(EntityFlags::DoNotCastShadows);
	const unsigned pass = (unsigned)(std::time(nullptr) % 100000);
	size_t made = 0, flush = 0, wall = 0, blocked = 0, noRay = 0;
	for (const Run& run : runs)
	{
		if (run.b - run.a < 0.49f)
			continue;
		const float a = run.a - kCornerOverlap, b = run.b + kCornerOverlap;
		const int pieces = std::max(1, (int)std::ceil((b - a) / kMaxPiece - 1e-4f));
		const float len = (b - a) / (float)pieces;
		for (int p = 0; p < pieces; ++p)
		{
			const float mid = a + (p + 0.5f) * len;
			auto pt = [&](float acrossOffset) {
				const float across = run.line + run.out * acrossOffset;
				return run.alongX ? std::make_pair(mid, across) : std::make_pair(across, mid);
			};
			// Sample within the original run (the overlap ends may hang past a corner).
			const float sampleMid = std::clamp(mid, run.a + 0.2f, run.b - 0.2f);
			auto sample = [&](float acrossOffset) {
				const float across = run.line + run.out * acrossOffset;
				return run.alongX ? std::make_pair(sampleMid, across) : std::make_pair(across, sampleMid);
			};
			RayHit paved, terrain, outside;
			const auto in = sample(-0.3f);
			const auto outPt = sample(0.6f);
			if (!CastDown(scene, in.first, in.second, pavedOpt, paved) || !CastDown(scene, outPt.first, outPt.second, terrainOpt, terrain))
			{
				++noRay;
				continue;
			}
			const float step = paved.position.y - terrain.position.y;
			if (step < kMinStep)
			{
				++flush;
				continue;
			}
			if (step > kMaxStep)
			{
				++wall;
				continue;
			}
			// Something other than grass right outside (a building, a raised slab, water).
			// Cast from just above the paving so tree canopies don't count.
			if (CastDown(scene, outPt.first, outPt.second, anyOpt, outside, paved.position.y + 1.0f) &&
				outside.entity != nullptr && terrainOpt.only.count(outside.entity) == 0 &&
				outside.position.y > terrain.position.y + 0.06f)
			{
				++blocked;
				continue;
			}
			const auto c = pt(kKerbOut);
			const float yaw = run.alongX ? 0.0f : 1.5707963f;
			const std::string name = "Kerb" + std::to_string(pass) + "_" + std::to_string(made);
			Entity* e = scene->CreateEntity(name, math::Vector3(c.first, paved.position.y - kKerbDrop, c.second),
				math::Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f), math::Vector3(len / kMaxPiece, 1.0f, 1.0f));
			if (e == nullptr)
				continue;
			e->SetParent(group, false);
			auto* smc = e->AddComponent<StaticMeshComponent>();
			smc->SetMesh(kerbMesh);
			if (kerbMesh->GetMaterial())
				smc->SetMaterial(kerbMesh->GetMaterial());
			scene->FlushPVS(e, false);
			++made;
		}
	}
	LOG_INFO("city_kerbs: %zu paved tiles, %zu outline runs -> %zu kerb pieces (removed %zu old; skipped %zu flush, %zu wall/quay, %zu blocked, %zu no ground)",
		rects.size(), runs.size(), made, removed, flush, wall, blocked, noRay);
	CON_ECHO("city_kerbs: %zu kerb pieces (removed %zu old). Save the scene to keep them.", made, removed);
}
