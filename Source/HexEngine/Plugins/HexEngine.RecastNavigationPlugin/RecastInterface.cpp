

#include "DebugUtils/Include/RecastDebugDraw.h"
#include "DebugUtils/Include/DetourDebugDraw.h"
#include "DetourCrowd/Include/DetourPathCorridor.h"
#include "Detour/Include/DetourCommon.h"

#include "RecastInterface.hpp"
#include <algorithm>
#include <cstring>

enum PolyAreas
{
	POLYAREA_GROUND,
	POLYAREA_WATER,
	POLYAREA_ROAD,
	POLYAREA_DOOR,
	POLYAREA_GRASS,
	POLYAREA_JUMP
};

enum PolyFlags
{
	POLYFLAGS_WALK = 0x01,		// Ability to walk (ground, grass, road)
	POLYFLAGS_SWIM = 0x02,		// Ability to swim (water).
	POLYFLAGS_DOOR = 0x04,		// Ability to move through doors.
	POLYFLAGS_JUMP = 0x08,		// Ability to jump.
	POLYFLAGS_DISABLED = 0x10,		// Disabled polygon
	POLYFLAGS_ALL = 0xffff	// All abilities.
};

namespace
{
	// Standard even-odd point-in-polygon on the XZ plane (verts are float[n*3]).
	bool PointInXZPoly(int32_t nverts, const float* verts, float px, float pz)
	{
		bool inside = false;
		for (int32_t i = 0, j = nverts - 1; i < nverts; j = i++)
		{
			const float* vi = &verts[i * 3];
			const float* vj = &verts[j * 3];
			if (((vi[2] > pz) != (vj[2] > pz)) &&
				(px < (vj[0] - vi[0]) * (pz - vi[2]) / (vj[2] - vi[2]) + vi[0]))
				inside = !inside;
		}
		return inside;
	}

	// Like rcMarkConvexPolyArea, but force-sets matching spans to RC_WALKABLE_AREA
	// even where they were already carved to RC_NULL_AREA. rcMarkConvexPolyArea skips
	// null spans, so it can't restore walkability over a block volume - this can,
	// letting a ForceWalkable (crossing) volume win over an overlapping Block (road).
	void ForceWalkableConvexPoly(const float* verts, int32_t nverts, float hmin, float hmax, rcCompactHeightfield& chf)
	{
		float bmin[3], bmax[3];
		bmin[0] = bmax[0] = verts[0];
		bmin[2] = bmax[2] = verts[2];
		for (int32_t i = 1; i < nverts; ++i)
		{
			bmin[0] = std::min(bmin[0], verts[i * 3 + 0]);
			bmax[0] = std::max(bmax[0], verts[i * 3 + 0]);
			bmin[2] = std::min(bmin[2], verts[i * 3 + 2]);
			bmax[2] = std::max(bmax[2], verts[i * 3 + 2]);
		}
		bmin[1] = hmin;
		bmax[1] = hmax;

		int32_t minx = (int32_t)((bmin[0] - chf.bmin[0]) / chf.cs);
		int32_t miny = (int32_t)((bmin[1] - chf.bmin[1]) / chf.ch);
		int32_t minz = (int32_t)((bmin[2] - chf.bmin[2]) / chf.cs);
		int32_t maxx = (int32_t)((bmax[0] - chf.bmin[0]) / chf.cs);
		int32_t maxy = (int32_t)((bmax[1] - chf.bmin[1]) / chf.ch);
		int32_t maxz = (int32_t)((bmax[2] - chf.bmin[2]) / chf.cs);

		if (maxx < 0 || minx >= chf.width || maxz < 0 || minz >= chf.height)
			return;
		if (minx < 0) minx = 0;
		if (maxx >= chf.width) maxx = chf.width - 1;
		if (minz < 0) minz = 0;
		if (maxz >= chf.height) maxz = chf.height - 1;

		for (int32_t z = minz; z <= maxz; ++z)
		{
			for (int32_t x = minx; x <= maxx; ++x)
			{
				const rcCompactCell& cell = chf.cells[x + z * chf.width];
				for (int32_t i = (int32_t)cell.index, ni = (int32_t)(cell.index + cell.count); i < ni; ++i)
				{
					const rcCompactSpan& s = chf.spans[i];
					if ((int32_t)s.y < miny || (int32_t)s.y > maxy)
						continue;

					const float px = chf.bmin[0] + (x + 0.5f) * chf.cs;
					const float pz = chf.bmin[2] + (z + 0.5f) * chf.cs;
					if (PointInXZPoly(nverts, verts, px, pz))
						chf.areas[i] = RC_WALKABLE_AREA;
				}
			}
		}
	}
}


bool RecastInterface::Create()
{
	return true;
}

void RecastInterface::Destroy()
{
	CleanupBuildData();

	for (auto& navMesh : _navMeshes)
	{
		FreeNavMesh(navMesh);
	}
	_navMeshes.clear();
}

void RecastInterface::CleanupBuildData()
{
	if (_heightField != nullptr)
	{
		rcFreeHeightField(_heightField);
		_heightField = nullptr;
	}

	if (_compactHF != nullptr)
	{
		rcFreeCompactHeightfield(_compactHF);
		_compactHF = nullptr;
	}

	if (_cset != nullptr)
	{
		rcFreeContourSet(_cset);
		_cset = nullptr;
	}

	if (_polyMesh != nullptr)
	{
		rcFreePolyMesh(_polyMesh);
		_polyMesh = nullptr;
	}

	if (_detailMesh != nullptr)
	{
		rcFreePolyMeshDetail(_detailMesh);
		_detailMesh = nullptr;
	}
}

void RecastInterface::FreeNavMesh(NavMeshes& meshData)
{
	if (meshData.navMeshQuery != nullptr)
	{
		dtFreeNavMeshQuery(meshData.navMeshQuery);
		meshData.navMeshQuery = nullptr;
	}

	if (meshData.navMesh != nullptr)
	{
		dtFreeNavMesh(meshData.navMesh);
		meshData.navMesh = nullptr;
		meshData.navMeshData = nullptr;
	}

	if (meshData.navMeshData != nullptr)
	{
		dtFree(meshData.navMeshData);
		meshData.navMeshData = nullptr;
	}
}

bool RecastInterface::CreateNavMeshForScene(HexEngine::Scene* scene, const NavMeshCreationParams& params, HexEngine::NavMeshId* navMesh)
{
	int32_t replaceIndex = -1;
	for (size_t i = 0; i < _navMeshes.size(); ++i)
	{
		if (_navMeshes[i].scene == scene && _navMeshes[i].chunk == nullptr)
		{
			replaceIndex = (int32_t)i;
			break;
		}
	}

	return CreateNavMeshInternal(scene, nullptr, params, navMesh, replaceIndex);
}

bool RecastInterface::CreateNavMeshForChunk(HexEngine::Scene* scene, HexEngine::Chunk* chunk, const NavMeshCreationParams& params, HexEngine::NavMeshId* navMesh)
{
	int32_t replaceIndex = -1;
	for (size_t i = 0; i < _navMeshes.size(); ++i)
	{
		if (_navMeshes[i].scene == scene && _navMeshes[i].chunk == chunk)
		{
			replaceIndex = (int32_t)i;
			break;
		}
	}

	return CreateNavMeshInternal(scene, chunk, params, navMesh, replaceIndex);
}

bool RecastInterface::RebuildMesh(HexEngine::NavMeshId id)
{
	if (id >= _navMeshes.size())
	{
		LOG_WARN("RebuildMesh called with invalid navmesh id: %u", id);
		return false;
	}

	auto& existing = _navMeshes[id];
	if (existing.scene == nullptr)
	{
		LOG_WARN("Cannot rebuild navmesh %u because the source scene is no longer valid", id);
		return false;
	}

	return CreateNavMeshInternal(existing.scene, existing.chunk, existing.params, &id, (int32_t)id);
}

bool RecastInterface::CreateNavMeshInternal(HexEngine::Scene* scene, HexEngine::Chunk* chunk, const NavMeshCreationParams& params, HexEngine::NavMeshId* navMesh, int32_t replaceIndex)
{
	if (scene == nullptr || navMesh == nullptr)
	{
		LOG_WARN("CreateNavMeshInternal called with invalid arguments");
		return false;
	}

	CleanupBuildData();

	int32_t width = 0;
	int32_t height = 0;

	memcpy(&_creationParams, &params, sizeof(_creationParams));

	this->enableLog(true);
	this->log(rcLogCategory::RC_LOG_PROGRESS, "Building NavMesh using recast");

	math::Vector3 min, max;

	if (chunk != nullptr)
	{
		min = math::Vector3(chunk->GetBoundingVolume().Center) - math::Vector3(chunk->GetBoundingVolume().Extents);
		max = math::Vector3(chunk->GetBoundingVolume().Center) + math::Vector3(chunk->GetBoundingVolume().Extents);
	}
	else
	{
		scene->CalculateBounds(min, max);		
	}

	rcCalcGridSize(&min.x, &max.x, params.cs, &width, &height);

	_heightField = rcAllocHeightfield();

	if (!_heightField)
	{
		LOG_CRIT("Failed to create Recast heightfield");
		return false;
	}

	if (!rcCreateHeightfield(this, *_heightField, width, height, &min.x, &max.x, params.cs, params.ch))
	{
		LOG_CRIT("buildNavigation: Could not create solid heightfield.");
		CleanupBuildData();
		return false;
	}

	uint32_t numFaces = 0;
	std::vector<math::Vector3> vertices;
	std::vector<uint32_t> indices;

	if (chunk != nullptr)
	{
		chunk->CalculateChunkStats_UInt32(vertices, indices, numFaces, HexEngine::EntityFlags::DoNotBlockNavMesh);
	}
	else
	{
		scene->CalculateSceneStats_UInt32(vertices, indices, numFaces, HexEngine::EntityFlags::DoNotBlockNavMesh);
	}

	if (numFaces == 0 || vertices.empty() || indices.empty())
	{
		LOG_WARN("Navigation mesh build skipped because no valid geometry was found");
		CleanupBuildData();
		return false;
	}

	std::vector<uint8_t> triAreas(numFaces);

	// Find triangles which are walkable based on their slope and rasterize them.
	// If your input data is multiple meshes, you can transform them here, calculate
	// the are type for each of the meshes and rasterize them.
	memset(triAreas.data(), 0, numFaces * sizeof(unsigned char));
	rcMarkWalkableTriangles(this, params.walkableSlopeAngle, &vertices.data()->x, (int)vertices.size(), (int*)indices.data(), (int)numFaces, triAreas.data());

	if (!rcRasterizeTriangles(this, &vertices.data()->x, (int)vertices.size(), (int*)indices.data(), triAreas.data(), (int)numFaces, *_heightField, params.walkableClimb))
	{
		LOG_CRIT("buildNavigation: Could not rasterize triangles.");
		CleanupBuildData();
		return false;
	}

	//if (m_filterLowHangingObstacles)
	rcFilterLowHangingWalkableObstacles(this, params.walkableClimb, *_heightField);
	//if (m_filterLedgeSpans)
	rcFilterLedgeSpans(this, params.walkableHeight, params.walkableClimb, *_heightField);
	//if (m_filterWalkableLowHeightSpans)
	rcFilterWalkableLowHeightSpans(this, params.walkableHeight, *_heightField);

	_compactHF = rcAllocCompactHeightfield();
	if (!_compactHF)
	{
		LOG_CRIT("buildNavigation: Out of memory 'chf'.");
		CleanupBuildData();
		return false;
	}
	if (!rcBuildCompactHeightfield(this, params.walkableHeight, params.walkableClimb, *_heightField, *_compactHF))
	{
		LOG_CRIT("buildNavigation: Could not build compact data.");
		CleanupBuildData();
		return false;
	}

	rcFreeHeightField(_heightField);
	_heightField = 0;

	// Erode the walkable area by agent radius.
	if (!rcErodeWalkableArea(this, params.walkableRadius, *_compactHF))
	{
		LOG_CRIT("buildNavigation: Could not erode.");
		CleanupBuildData();
		return false;
	}

	// Apply NavMeshBlockingVolume footprints. Carve every Block volume out first
	// (RC_NULL_AREA), then restore every ForceWalkable volume - so a crossing always
	// wins over an overlapping road block. Runs for both scene and chunk bakes; the
	// mark calls clip to this heightfield's bounds.
	{
		std::vector<HexEngine::NavMeshBlockingBox> navVolumes;
		scene->GatherNavMeshBlockingVolumes(navVolumes);

		for (const auto& v : navVolumes)
		{
			if (v.mode != 0) // 0 = Block
				continue;

			const float verts[12] =
			{
				v.corners[0].x, v.corners[0].y, v.corners[0].z,
				v.corners[1].x, v.corners[1].y, v.corners[1].z,
				v.corners[2].x, v.corners[2].y, v.corners[2].z,
				v.corners[3].x, v.corners[3].y, v.corners[3].z,
			};
			rcMarkConvexPolyArea(this, verts, 4, v.ymin, v.ymax, RC_NULL_AREA, *_compactHF);
		}

		for (const auto& v : navVolumes)
		{
			if (v.mode != 1) // 1 = ForceWalkable
				continue;

			const float verts[12] =
			{
				v.corners[0].x, v.corners[0].y, v.corners[0].z,
				v.corners[1].x, v.corners[1].y, v.corners[1].z,
				v.corners[2].x, v.corners[2].y, v.corners[2].z,
				v.corners[3].x, v.corners[3].y, v.corners[3].z,
			};
			ForceWalkableConvexPoly(verts, 4, v.ymin, v.ymax, *_compactHF);
		}
	}


	// waterhsed partitioning:
	// Prepare for region partitioning, by calculating distance field along the walkable surface.
	//if (!rcBuildDistanceField(this, *_compactHF))
	//{
	//	LOG_CRIT("buildNavigation: Could not build distance field.");
	//	return false;
	//}

	//// Partition the walkable surface into simple regions without holes.
	//if (!rcBuildRegions(this, *_compactHF, 0, params.minRegionArea, params.mergeRegionArea))
	//{
	//	LOG_CRIT("buildNavigation: Could not build watershed regions.");
	//	return false;
	//}

	if (!rcBuildRegionsMonotone(this, *_compactHF, params.borderSize, params.minRegionArea, params.mergeRegionArea))
	{
		LOG_CRIT("buildNavigation: Could not build monotone regions.");
		CleanupBuildData();
		return false;
	}

	/*if (!rcBuildLayerRegions(this, *_compactHF, params.borderSize, params.minRegionArea))
	{
		LOG_CRIT("buildNavigation: Could not build layer regions.");
		return 0;
	}*/

	_cset = rcAllocContourSet();
	if (!_cset)
	{
		LOG_CRIT("buildNavigation: Out of memory 'cset'.");
		CleanupBuildData();
		return false;
	}
	if (!rcBuildContours(this, *_compactHF, params.maxSimplificationError, params.maxEdgeLen, *_cset))
	{
		LOG_CRIT("buildNavigation: Could not create contours.");
		CleanupBuildData();
		return false;
	}

	// Build polygon navmesh from the contours.
	_polyMesh = rcAllocPolyMesh();
	if (!_polyMesh)
	{
		LOG_CRIT("buildNavigation: Out of memory 'pmesh'.");
		CleanupBuildData();
		return false;
	}
	if (!rcBuildPolyMesh(this, *_cset, params.maxVertsPerPoly, *_polyMesh))
	{
		LOG_CRIT("buildNavigation: Could not triangulate contours.");
		CleanupBuildData();
		return false;
	}

	//
	// Step 7. Create detail mesh which allows to access approximate height on each polygon.
	//

	_detailMesh = rcAllocPolyMeshDetail();
	if (!_detailMesh)
	{
		LOG_CRIT("buildNavigation: Out of memory 'pmdtl'.");
		CleanupBuildData();
		return false;
	}

	if (!rcBuildPolyMeshDetail(this, *_polyMesh, *_compactHF, params.detailSampleDist, params.detailSampleMaxError, *_detailMesh))
	{
		LOG_CRIT("buildNavigation: Could not build detail mesh.");
		CleanupBuildData();
		return false;
	}

	rcFreeCompactHeightfield(_compactHF);
	_compactHF = 0;
	rcFreeContourSet(_cset);
	_cset = 0;

	const bool created = CreateRoutingData(scene, chunk, params, navMesh, replaceIndex);
	CleanupBuildData();
	return created;
}

bool RecastInterface::CreateRoutingData(HexEngine::Scene* scene, HexEngine::Chunk* chunk, const NavMeshCreationParams& params, HexEngine::NavMeshId* navMesh, int32_t replaceIndex)
{
	if (_polyMesh == nullptr || _detailMesh == nullptr || navMesh == nullptr)
	{
		LOG_CRIT("Could not create routing data because the generated polygon mesh is invalid.");
		return false;
	}

	for (int i = 0; i < _polyMesh->npolys; ++i)
	{
		if (_polyMesh->areas[i] == RC_WALKABLE_AREA)
			_polyMesh->areas[i] = POLYAREA_GROUND;

		if (_polyMesh->areas[i] == POLYAREA_GROUND ||
			_polyMesh->areas[i] == POLYAREA_GRASS ||
			_polyMesh->areas[i] == POLYAREA_ROAD)
		{
			_polyMesh->flags[i] = POLYFLAGS_WALK;
		}
		else if (_polyMesh->areas[i] == POLYAREA_WATER)
		{
			_polyMesh->flags[i] = POLYFLAGS_SWIM;
		}
		else if (_polyMesh->areas[i] == POLYAREA_DOOR)
		{
			_polyMesh->flags[i] = POLYFLAGS_WALK | POLYFLAGS_DOOR;
		}
	}

	_filter.setIncludeFlags(POLYFLAGS_ALL ^ POLYFLAGS_DISABLED);

	// Lower the baked navmesh by the configured height bias to cancel the Cell-Height
	// (ch) voxel quantization that otherwise floats the mesh ~ch above the floor and
	// leaves agents hovering. Applied to the detail mesh - what getPolyHeight (agent
	// placement) and the debug overlay both read - so it's a zero-runtime-cost fix.
	// The detail mesh is rebuilt every bake, so this never compounds across rebakes.
	if (params.heightBias != 0.0f && _detailMesh != nullptr && _detailMesh->verts != nullptr)
	{
		for (int i = 0; i < _detailMesh->nverts; ++i)
			_detailMesh->verts[i * 3 + 1] -= params.heightBias;
	}

	// Off-mesh connections from NavMeshLinkComponent. Each link binds its two
	// endpoints to nearby polys, letting agents path across a gap with no walkable
	// polygon between them (e.g. stitching two navmesh islands over excluded grass).
	// Areas/flags match the walkable ground polys so the query filter includes them.
	// These buffers must outlive dtCreateNavMeshData below.
	std::vector<float>          offMeshVerts;   // [count*6] : start xyz, end xyz
	std::vector<float>          offMeshRad;     // [count]
	std::vector<unsigned char>  offMeshDir;     // [count] : 0 = one-way, DT_OFFMESH_CON_BIDIR = both
	std::vector<unsigned char>  offMeshAreas;   // [count]
	std::vector<unsigned short> offMeshFlags;   // [count]
	std::vector<unsigned int>   offMeshUserID;  // [count]
	int32_t                     offMeshCount = 0;

	if (scene != nullptr)
	{
		// Scene-level links; for chunk bakes Detour only binds endpoints that land on
		// this tile (a link spanning two chunks won't connect - use a scene navmesh).
		std::vector<HexEngine::NavMeshLink> links;
		scene->GatherNavMeshLinks(links);

		for (const auto& l : links)
		{
			offMeshVerts.push_back(l.start.x); offMeshVerts.push_back(l.start.y); offMeshVerts.push_back(l.start.z);
			offMeshVerts.push_back(l.end.x);   offMeshVerts.push_back(l.end.y);   offMeshVerts.push_back(l.end.z);
			offMeshRad.push_back(l.radius);
			offMeshDir.push_back(l.bidirectional ? (unsigned char)DT_OFFMESH_CON_BIDIR : (unsigned char)0);
			offMeshAreas.push_back((unsigned char)POLYAREA_GROUND);
			offMeshFlags.push_back((unsigned short)POLYFLAGS_WALK);
			offMeshUserID.push_back((unsigned int)(1000 + offMeshCount));
			++offMeshCount;
		}

		if (offMeshCount > 0)
			LOG_INFO("Navmesh: adding %d off-mesh link(s).", offMeshCount);
	}

	dtNavMeshCreateParams dtparams;
	memset(&dtparams, 0, sizeof(dtparams));
	dtparams.verts = _polyMesh->verts;
	dtparams.vertCount = _polyMesh->nverts;
	dtparams.polys = _polyMesh->polys;
	dtparams.polyAreas = _polyMesh->areas;
	dtparams.polyFlags = _polyMesh->flags;
	dtparams.polyCount = _polyMesh->npolys;
	dtparams.nvp = _polyMesh->nvp;
	dtparams.detailMeshes = _detailMesh->meshes;
	dtparams.detailVerts = _detailMesh->verts;
	dtparams.detailVertsCount = _detailMesh->nverts;
	dtparams.detailTris = _detailMesh->tris;
	dtparams.detailTriCount = _detailMesh->ntris;
	dtparams.offMeshConVerts  = offMeshCount > 0 ? offMeshVerts.data()  : nullptr;
	dtparams.offMeshConRad    = offMeshCount > 0 ? offMeshRad.data()    : nullptr;
	dtparams.offMeshConDir    = offMeshCount > 0 ? offMeshDir.data()    : nullptr;
	dtparams.offMeshConAreas  = offMeshCount > 0 ? offMeshAreas.data()  : nullptr;
	dtparams.offMeshConFlags  = offMeshCount > 0 ? offMeshFlags.data()  : nullptr;
	dtparams.offMeshConUserID = offMeshCount > 0 ? offMeshUserID.data() : nullptr;
	dtparams.offMeshConCount  = offMeshCount;
	dtparams.walkableHeight = _creationParams.walkableHeight;
	dtparams.walkableRadius = _creationParams.walkableRadius;
	dtparams.walkableClimb = _creationParams.walkableClimb;
	rcVcopy(dtparams.bmin, _polyMesh->bmin);
	rcVcopy(dtparams.bmax, _polyMesh->bmax);

	// Sink the whole navmesh by the height bias. Poly-mesh verts are stored quantized
	// and dequantized at runtime as header.bmin.y + v.y*ch, so lowering bmin/bmax .y
	// drops every poly vert by heightBias. This is what fixes FLAT polys (sidewalks,
	// floors) - they carry no interior detail verts, so their height comes from these
	// poly-mesh boundary verts, not the detail verts lowered above. Together the two
	// edits drop the entire surface uniformly onto the floor.
	dtparams.bmin[1] -= params.heightBias;
	dtparams.bmax[1] -= params.heightBias;
	dtparams.cs = _creationParams.cs;
	dtparams.ch = _creationParams.ch;
	dtparams.buildBvTree = true;

	//unsigned char* navData = 0;
	int navDataSize = 0;

	NavMeshes stored;
	stored.scene = scene;
	stored.chunk = chunk;
	stored.params = params;


	if (!dtCreateNavMeshData(&dtparams, &stored.navMeshData, &navDataSize))
	{
		LOG_CRIT("Could not build Detour navmesh.");
		return false;
	}
	stored.navMeshDataSize = navDataSize;

	stored.navMesh = dtAllocNavMesh();
	if (!stored.navMesh)
	{
		dtFree(stored.navMeshData);
		LOG_CRIT("Could not create Detour navmesh");
		return false;
	}

	dtStatus status;

	status = stored.navMesh->init(stored.navMeshData, navDataSize, DT_TILE_FREE_DATA);
	if (dtStatusFailed(status))
	{
		FreeNavMesh(stored);
		LOG_CRIT("Could not init Detour navmesh");
		return false;
	}

	stored.navMeshQuery = dtAllocNavMeshQuery();

	status = stored.navMeshQuery->init(stored.navMesh, 2048);
	if (dtStatusFailed(status))
	{
		FreeNavMesh(stored);
		LOG_CRIT("Could not init Detour navmesh query");
		return false;
	}

	if (replaceIndex >= 0 && replaceIndex < (int32_t)_navMeshes.size())
	{
		FreeNavMesh(_navMeshes[(size_t)replaceIndex]);
		_navMeshes[(size_t)replaceIndex] = stored;
		*navMesh = (HexEngine::NavMeshId)replaceIndex;
		return true;
	}

	_navMeshes.push_back(stored);

	*navMesh = _navMeshes.size() - 1;

	return true;
}

bool RecastInterface::GetNavMeshBytes(HexEngine::NavMeshId id, std::vector<uint8_t>& outData)
{
	if (id >= _navMeshes.size())
		return false;
	const NavMeshes& m = _navMeshes[(size_t)id];
	if (m.navMeshData == nullptr || m.navMeshDataSize <= 0)
		return false;
	outData.assign(m.navMeshData, m.navMeshData + m.navMeshDataSize);
	return true;
}

bool RecastInterface::LoadNavMeshFromBytes(HexEngine::Scene* scene, const std::vector<uint8_t>& data, HexEngine::NavMeshId* outNavMesh)
{
	if (data.empty() || outNavMesh == nullptr)
		return false;

	NavMeshes stored;
	stored.scene = scene;
	stored.navMeshDataSize = (int32_t)data.size();
	// dtNavMesh (DT_TILE_FREE_DATA) takes ownership of the buffer, so allocate via dtAlloc.
	stored.navMeshData = (uint8_t*)dtAlloc((int)data.size(), DT_ALLOC_PERM);
	if (stored.navMeshData == nullptr)
		return false;
	memcpy(stored.navMeshData, data.data(), data.size());

	stored.navMesh = dtAllocNavMesh();
	if (!stored.navMesh)
	{
		dtFree(stored.navMeshData);
		return false;
	}
	if (dtStatusFailed(stored.navMesh->init(stored.navMeshData, (int)data.size(), DT_TILE_FREE_DATA)))
	{
		FreeNavMesh(stored);
		LOG_CRIT("Could not init Detour navmesh from bytes.");
		return false;
	}

	stored.navMeshQuery = dtAllocNavMeshQuery();
	if (!stored.navMeshQuery || dtStatusFailed(stored.navMeshQuery->init(stored.navMesh, 2048)))
	{
		FreeNavMesh(stored);
		LOG_CRIT("Could not init Detour navmesh query from bytes.");
		return false;
	}

	_navMeshes.push_back(stored);
	*outNavMesh = (HexEngine::NavMeshId)(_navMeshes.size() - 1);
	return true;
}

void RecastInterface::DebugRender()
{
	// Draw only the latest navmesh per (scene, chunk) pair to avoid visual overlap
	// when meshes are rebuilt/recreated over time.
	std::vector<std::pair<HexEngine::Scene*, HexEngine::Chunk*>> drawnPairs;
	drawnPairs.reserve(_navMeshes.size());

	for (int32_t i = (int32_t)_navMeshes.size() - 1; i >= 0; --i)
	{
		const auto& nav = _navMeshes[(size_t)i];
		if (nav.navMesh == nullptr)
			continue;

		const std::pair<HexEngine::Scene*, HexEngine::Chunk*> key{ nav.scene, nav.chunk };
		if (std::find(drawnPairs.begin(), drawnPairs.end(), key) != drawnPairs.end())
			continue;

		drawnPairs.push_back(key);
		// COLOR_TILES tints polys per tile; OFFMESHCONS draws off-mesh links
		// (NavMeshLinkComponent) as an arc + endpoint circles - without that flag the
		// links bake but are never rendered. Endpoint circles draw red when that end
		// failed to bind to a poly, which doubles as a connection diagnostic.
		duDebugDrawNavMesh(&_debugRenderer, *nav.navMesh, DU_DRAWNAVMESH_COLOR_TILES | DU_DRAWNAVMESH_OFFMESHCONS);

		//duDebugDrawCompactHeightfieldSolid(&_debugRenderer, *_compactHF);
	}
}

float frand()
{
	return HexEngine::GetRandomFloat(0.0f, 1.0f);
}

inline bool inRange(const float* v1, const float* v2, const float r, const float h)
{
	const float dx = v2[0] - v1[0];
	const float dy = v2[1] - v1[1];
	const float dz = v2[2] - v1[2];
	return (dx * dx + dz * dz) < r * r && fabsf(dy) < h;
}


static bool GetSteerTarget(dtNavMeshQuery* navQuery, const float* startPos, const float* endPos,
	const float minTargetDist,
	const dtPolyRef* path, const int pathSize,
	float* steerPos, unsigned char& steerPosFlag, dtPolyRef& steerPosRef,
	float* outPoints = 0, int* outPointCount = 0)
{
	// Find steer target.
	static const int MAX_STEER_POINTS = 3;
	float steerPath[MAX_STEER_POINTS * 3];
	unsigned char steerPathFlags[MAX_STEER_POINTS];
	dtPolyRef steerPathPolys[MAX_STEER_POINTS];
	int nsteerPath = 0;
	navQuery->findStraightPath(startPos, endPos, path, pathSize,
		steerPath, steerPathFlags, steerPathPolys, &nsteerPath, MAX_STEER_POINTS);
	if (!nsteerPath)
		return false;

	if (outPoints && outPointCount)
	{
		*outPointCount = nsteerPath;
		for (int i = 0; i < nsteerPath; ++i)
			dtVcopy(&outPoints[i * 3], &steerPath[i * 3]);
	}


	// Find vertex far enough to steer to.
	int ns = 0;
	while (ns < nsteerPath)
	{
		// Stop at Off-Mesh link or when point is further than slop away.
		if ((steerPathFlags[ns] & DT_STRAIGHTPATH_OFFMESH_CONNECTION) ||
			!inRange(&steerPath[ns * 3], startPos, minTargetDist, 1000.0f))
			break;
		ns++;
	}
	// Failed to find good point to steer to.
	if (ns >= nsteerPath)
		return false;

	dtVcopy(steerPos, &steerPath[ns * 3]);
	steerPos[1] = startPos[1];
	steerPosFlag = steerPathFlags[ns];
	steerPosRef = steerPathPolys[ns];

	return true;
}

static int fixupShortcuts(dtPolyRef* path, int npath, dtNavMeshQuery* navQuery)
{
	if (npath < 3)
		return npath;

	// Get connected polygons
	static const int maxNeis = 16;
	dtPolyRef neis[maxNeis];
	int nneis = 0;

	const dtMeshTile* tile = 0;
	const dtPoly* poly = 0;
	if (dtStatusFailed(navQuery->getAttachedNavMesh()->getTileAndPolyByRef(path[0], &tile, &poly)))
		return npath;

	for (unsigned int k = poly->firstLink; k != DT_NULL_LINK; k = tile->links[k].next)
	{
		const dtLink* link = &tile->links[k];
		if (link->ref != 0)
		{
			if (nneis < maxNeis)
				neis[nneis++] = link->ref;
		}
	}

	// If any of the neighbour polygons is within the next few polygons
	// in the path, short cut to that polygon directly.
	static const int maxLookAhead = 6;
	int cut = 0;
	for (int i = dtMin(maxLookAhead, npath) - 1; i > 1 && cut == 0; i--) {
		for (int j = 0; j < nneis; j++)
		{
			if (path[i] == neis[j]) {
				cut = i;
				break;
			}
		}
	}
	if (cut > 1)
	{
		int offset = cut - 1;
		npath -= offset;
		for (int i = 1; i < npath; i++)
			path[i] = path[i + offset];
	}

	return npath;
}

void RecastInterface::FindPath(const PathParams& params, PathResult& result)
{
	if (params.meshId >= _navMeshes.size())
	{
		LOG_WARN("FindPath called with invalid navmesh id: %u", params.meshId);
		result.path.clear();
		return;
	}

	auto navMesh = _navMeshes.at(params.meshId);

	math::Vector3 nearestSp = math::Vector3::Zero;
	math::Vector3 nearestEp = math::Vector3::Zero;
	dtPolyRef startRef = 0;
	dtPolyRef endRef = 0;

	dtStatus status;

	math::Vector3 searchDistStart = params.searchDistance;
	if (searchDistStart.x <= 0.0f || searchDistStart.y <= 0.0f || searchDistStart.z <= 0.0f)
	{
		searchDistStart = math::Vector3(50.0f, 15.0f, 50.0f);
	}
	
	if (status = navMesh.navMeshQuery->findNearestPoly(
		&params.from.x,
		&searchDistStart.x,
		&_filter,
		&startRef,
		&nearestSp.x); dtStatusFailed(status))
	{
		LOG_CRIT("findNearestPoly failed");
		return;
	}

	

	math::Vector3 searchDistEnd = params.searchDistance;
	if (searchDistEnd.x <= 0.0f || searchDistEnd.y <= 0.0f || searchDistEnd.z <= 0.0f)
	{
		searchDistEnd = math::Vector3(50.0f, 15.0f, 50.0f);
	}

	if (status = navMesh.navMeshQuery->findNearestPoly(
		&params.to.x,
		&searchDistEnd.x,
		&_filter,
		&endRef,
		&nearestEp.x); dtStatusFailed(status))
	{
		LOG_CRIT("findNearestPoly failed");
		return;
	}

#if 0
	dtPolyRef circleRef[64];;
	float costs[64];
	memset(costs, 0.0f, sizeof(costs));
	int count;
	navMesh.navMeshQuery->findPolysAroundCircle(endRef, &params.to.x, 200.0f, &_filter, circleRef, nullptr, costs, &count, 64);

	float bestCost = 0;
	for (int i = 0; i < count; ++i)
	{
		if (costs[i] < bestCost)
		{
			endRef = circleRef[i];
			bestCost = costs[i];
		}
	}
#endif

	/*if (status = _navMeshQuery->findRandomPoint(&_filter, frand, &endRef, &nearestEp.x); dtStatusFailed(status))
	{
		LOG_CRIT("findRandomPoint failed");
		return;
	}*/

	int32_t pathCount = 0;

	std::vector<dtPolyRef> pathRefs(HexEngine::MaxPaths);

	if (status = navMesh.navMeshQuery->findPath(
		startRef, endRef,
		&params.from.x, &params.to.x,
		&_filter,
		pathRefs.data(), &pathCount,
		HexEngine::MaxPaths); dtStatusFailed(status))
	{
		LOG_CRIT("findPath failed");
		return;
	}

	if (pathCount <= 0)
	{
		LOG_WARN("No path could be found between the selected points.");
		return;
	}

	//result.pathRefs.insert(result.pathRefs.end(), pathRefs.begin(), pathRefs.begin() + pathCount);

	math::Vector3 start, end, currentPos;

	navMesh.navMeshQuery->closestPointOnPoly(startRef, &params.from.x, &start.x, nullptr);
	navMesh.navMeshQuery->closestPointOnPoly(endRef, &params.to.x, &end.x, nullptr);

	currentPos = start;

	result.path.clear();

	// push the starting point onto the path
	result.path.push_back(start);

	// now traverse the path
	dtPolyRef visited[16];
	int nvisited = 0;

	math::Vector3 resultingPos;

	const float stepSize = std::max(0.01f, params.stepSize);
	static const float SLOP = 0.01f;
	static const int MAX_SMOOTH = 2048;

	while (pathCount && result.path.size() < MAX_SMOOTH)
	{
		// Find location to steer towards.
		float steerPos[3];
		unsigned char steerPosFlag;
		dtPolyRef steerPosRef;

		if (!GetSteerTarget(navMesh.navMeshQuery, &currentPos.x, &end.x, SLOP,
			pathRefs.data(), pathCount, steerPos, steerPosFlag, steerPosRef))
			break;

		bool endOfPath = (steerPosFlag & DT_STRAIGHTPATH_END) ? true : false;
		bool offMeshConnection = (steerPosFlag & DT_STRAIGHTPATH_OFFMESH_CONNECTION) ? true : false;

		// Find movement delta.
		float delta[3], len;
		dtVsub(delta, steerPos, &currentPos.x);
		len = dtMathSqrtf(dtVdot(delta, delta));
		// If the steer target is end of path or off-mesh link, do not move past the location.
		if ((endOfPath || offMeshConnection) && len < stepSize)
			len = 1;
		else
			len = stepSize / len;
		float moveTgt[3];
		dtVmad(moveTgt, &currentPos.x, delta, len);

		// Move
		float result2[3];
		dtPolyRef visited[16];
		int nvisited = 0;
		navMesh.navMeshQuery->moveAlongSurface((dtPolyRef)pathRefs[0], &currentPos.x, moveTgt, &_filter,
			result2, visited, &nvisited, 16);

		pathCount = dtMergeCorridorStartMoved(pathRefs.data(), pathCount, HexEngine::MaxPaths, visited, nvisited);
		pathCount = fixupShortcuts(pathRefs.data(), pathCount, navMesh.navMeshQuery);

		float h = 0;
		if(dtStatusSucceed(navMesh.navMeshQuery->getPolyHeight((dtPolyRef)pathRefs[0], result2, &h)))
			result2[1] = h;

		dtVcopy(&currentPos.x, result2);

		// Handle end of path and off-mesh links when close enough.
		if (endOfPath && inRange(&currentPos.x, steerPos, SLOP, 1.0f))
		{
			// Reached end of path.
			dtVcopy(&currentPos.x, &end.x);
			if (result.path.size() < MAX_SMOOTH)
			{
				result.path.push_back(currentPos);

				//dtVcopy(&m_smoothPath[m_nsmoothPath * 3], iterPos);
				//m_nsmoothPath++;
			}
			break;
		}
		else if (offMeshConnection && inRange(&currentPos.x, steerPos, SLOP, 1.0f))
		{
			// Reached an off-mesh connection (e.g. a NavMeshLinkComponent stitching
			// pavement to a house). Advance the corridor up to and over the link,
			// emit the entry point, then move currentPos to the far endpoint so the
			// smoothed path continues on the other side instead of stalling here.
			float startPos[3], endPos[3];

			dtPolyRef prevRef = 0, polyRef = pathRefs[0];
			int npos = 0;
			while (npos < pathCount && polyRef != steerPosRef)
			{
				prevRef = polyRef;
				polyRef = pathRefs[npos];
				npos++;
			}
			for (int i = npos; i < pathCount; ++i)
				pathRefs[i - npos] = pathRefs[i];
			pathCount -= npos;

			if (dtStatusSucceed(navMesh.navMesh->getOffMeshConnectionPolyEndPoints(prevRef, polyRef, startPos, endPos)))
			{
				if (result.path.size() < MAX_SMOOTH)
					result.path.push_back(math::Vector3(startPos[0], startPos[1], startPos[2]));

				// Hop to the far side of the link, then snap to the ground height of
				// the next corridor poly so smoothing resumes on the house side.
				dtVcopy(&currentPos.x, endPos);
				float eh = 0.0f;
				if (pathCount > 0 && dtStatusSucceed(navMesh.navMeshQuery->getPolyHeight(pathRefs[0], &currentPos.x, &eh)))
					currentPos.y = eh;
			}
		}

		// Store results.
		if (result.path.size() < MAX_SMOOTH)
		{
			result.path.push_back(currentPos);
			//dtVcopy(&m_smoothPath[m_nsmoothPath * 3], iterPos);
			//m_nsmoothPath++;
		}
	}

	/*dtStatus status;
	if (status = _navMeshQuery->moveAlongSurface(
		result.pathRefs[result.iteratorIndex],
		&result.currentPos.x,
		&result.end.x,
		&_filter,
		&resultingPos.x,
		visited,
		&nvisited,
		16); dtStatusFailed(status))
	{
		LOG_CRIT("moveAlongSurface failed");
		return MoveResult::Failed;
	}

	result.targetPos = resultingPos;

	float h = 0;
	_navMeshQuery->getPolyHeight(result.pathRefs[result.iteratorIndex], &result.currentPos.x, &h);

	result.targetPos.y = h;

	if ((result.targetPos - result.currentPos).Length() <= 100.0f)
	{
		result.iteratorIndex++;

		if (result.iteratorIndex >= result.pathRefs.size())
		{
			_navMeshQuery->finalizeSlicedFindPath((dtPolyRef*)result.pathRefs.data(), &result.numPaths, MaxPaths);

			return MoveResult::ReachedDestination;
		}
	}*/
}

HexEngine::INavMeshProvider::MoveResult RecastInterface::Move(PathResult& result)
{
	if (result.path.empty())
	{
		return MoveResult::Failed;
	}

	return MoveResult::Moving;
}

void RecastInterface::doLog(const rcLogCategory category, const char* msg, const int len)
{
	switch (category)
	{
	case rcLogCategory::RC_LOG_ERROR:
		LOG_CRIT(msg);
		break;

	case rcLogCategory::RC_LOG_PROGRESS:
		LOG_INFO(msg);
		break;

	case rcLogCategory::RC_LOG_WARNING:
		LOG_WARN(msg);
		break;
	}
}
