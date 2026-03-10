

#include "DebugUtils/Include/RecastDebugDraw.h"
#include "DebugUtils/Include/DetourDebugDraw.h"
#include "DetourCrowd/Include/DetourPathCorridor.h"
#include "Detour/Include/DetourCommon.h"

#include "RecastInterface.hpp"

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


bool RecastInterface::Create()
{
	return true;
}

void RecastInterface::Destroy()
{

}

bool RecastInterface::CreateNavMeshForScene(HexEngine::Scene* scene, const NavMeshCreationParams& params, HexEngine::NavMeshId* navMesh)
{
	return CreateNavMeshInternal(scene, nullptr, params, navMesh);
}

bool RecastInterface::CreateNavMeshForChunk(HexEngine::Scene* scene, HexEngine::Chunk* chunk, const NavMeshCreationParams& params, HexEngine::NavMeshId* navMesh)
{
	return CreateNavMeshInternal(scene, chunk, params, navMesh);
}

bool RecastInterface::RebuildMesh(HexEngine::NavMeshId id)
{
	auto navMesh = _navMeshes.at(id);

	dtFreeNavMesh(navMesh.navMesh);
	navMesh.navMesh = nullptr;

	return true;
}

bool RecastInterface::CreateNavMeshInternal(HexEngine::Scene* scene, HexEngine::Chunk* chunk, const NavMeshCreationParams& params, HexEngine::NavMeshId* navMesh)
{
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
		return false;
	}

	
	uint32_t numFaces;
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

	_triAreas = new uint8_t[numFaces];

	// Find triangles which are walkable based on their slope and rasterize them.
	// If your input data is multiple meshes, you can transform them here, calculate
	// the are type for each of the meshes and rasterize them.
	memset(_triAreas, 0, numFaces * sizeof(unsigned char));
	rcMarkWalkableTriangles(this, params.walkableSlopeAngle, &vertices.data()->x, vertices.size(), (int*)indices.data(), numFaces, _triAreas);

	if (!rcRasterizeTriangles(this, &vertices.data()->x, vertices.size(), (int*)indices.data(), _triAreas, numFaces, *_heightField, params.walkableClimb))
	{
		LOG_CRIT("buildNavigation: Could not rasterize triangles.");
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
		return false;
	}
	if (!rcBuildCompactHeightfield(this, params.walkableHeight, params.walkableClimb, *_heightField, *_compactHF))
	{
		LOG_CRIT("buildNavigation: Could not build compact data.");
		return false;
	}

	rcFreeHeightField(_heightField);
	_heightField = 0;

	// Erode the walkable area by agent radius.
	if (!rcErodeWalkableArea(this, params.walkableRadius, *_compactHF))
	{
		LOG_CRIT("buildNavigation: Could not erode.");
		return false;
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
		return 0;
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
		return false;
	}
	if (!rcBuildContours(this, *_compactHF, params.maxSimplificationError, params.maxEdgeLen, *_cset))
	{
		LOG_CRIT("buildNavigation: Could not create contours.");
		return false;
	}

	// Build polygon navmesh from the contours.
	_polyMesh = rcAllocPolyMesh();
	if (!_polyMesh)
	{
		LOG_CRIT("buildNavigation: Out of memory 'pmesh'.");
		return false;
	}
	if (!rcBuildPolyMesh(this, *_cset, params.maxVertsPerPoly, *_polyMesh))
	{
		LOG_CRIT("buildNavigation: Could not triangulate contours.");
		return false;
	}

	//
	// Step 7. Create detail mesh which allows to access approximate height on each polygon.
	//

	_detailMesh = rcAllocPolyMeshDetail();
	if (!_detailMesh)
	{
		LOG_CRIT("buildNavigation: Out of memory 'pmdtl'.");
		return false;
	}

	if (!rcBuildPolyMeshDetail(this, *_polyMesh, *_compactHF, params.detailSampleDist, params.detailSampleMaxError, *_detailMesh))
	{
		LOG_CRIT("buildNavigation: Could not build detail mesh.");
		return false;
	}

	rcFreeCompactHeightfield(_compactHF);
	_compactHF = 0;
	rcFreeContourSet(_cset);
	_cset = 0;

	return CreateRoutingData(navMesh);
}

bool RecastInterface::CreateRoutingData(HexEngine::NavMeshId* navMesh)
{
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
	dtparams.offMeshConVerts = nullptr;
	dtparams.offMeshConRad = nullptr;
	dtparams.offMeshConDir = nullptr;
	dtparams.offMeshConAreas = nullptr;
	dtparams.offMeshConFlags = nullptr;
	dtparams.offMeshConUserID = nullptr;
	dtparams.offMeshConCount = 0;
	dtparams.walkableHeight = _creationParams.walkableHeight;
	dtparams.walkableRadius = _creationParams.walkableRadius;
	dtparams.walkableClimb = _creationParams.walkableClimb;
	rcVcopy(dtparams.bmin, _polyMesh->bmin);
	rcVcopy(dtparams.bmax, _polyMesh->bmax);
	dtparams.cs = _creationParams.cs;
	dtparams.ch = _creationParams.ch;
	dtparams.buildBvTree = true;

	//unsigned char* navData = 0;
	int navDataSize = 0;

	NavMeshes stored;


	if (!dtCreateNavMeshData(&dtparams, &stored.navMeshData, &navDataSize))
	{
		LOG_CRIT("Could not build Detour navmesh.");
		return false;
	}

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
		dtFree(stored.navMeshData);
		LOG_CRIT("Could not init Detour navmesh");
		return false;
	}

	stored.navMeshQuery = dtAllocNavMeshQuery();

	status = stored.navMeshQuery->init(stored.navMesh, 2048);
	if (dtStatusFailed(status))
	{
		LOG_CRIT("Could not init Detour navmesh query");
		return false;
	}

	_navMeshes.push_back(stored);

	*navMesh = _navMeshes.size() - 1;

	return true;
}

void RecastInterface::DebugRender()
{
	//duDebugDrawPolyMesh(&_debugRenderer, *_polyMesh);
	for(auto nav : _navMeshes)
	{
		duDebugDrawNavMesh(&_debugRenderer, *nav.navMesh, DU_DRAWNAVMESH_COLOR_TILES | DU_DRAWNAVMESH_CLOSEDLIST);

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
	auto navMesh = _navMeshes.at(params.meshId);

	math::Vector3 nearestSp, nearestEp;
	dtPolyRef startRef, endRef;

	dtStatus status;

	math::Vector3 searchDistStart(200.0f);
	
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

	

	if (status = navMesh.navMeshQuery->findNearestPoly(
		&params.to.x,
		&params.searchDistance.x,
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

	std::vector<int32_t> pathRefs(HexEngine::MaxPaths);

	if (status = navMesh.navMeshQuery->findPath(
		startRef, endRef,
		&params.from.x, &params.to.x,
		&_filter,
		(dtPolyRef*)pathRefs.data(), &pathCount,
		HexEngine::MaxPaths); dtStatusFailed(status))
	{
		LOG_CRIT("findPath failed");
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

	static const float STEP_SIZE = params.stepSize;
	static const float SLOP = 0.01f;
	static const int MAX_SMOOTH = 2048;

	while (pathCount && result.path.size() < MAX_SMOOTH)
	{
		// Find location to steer towards.
		float steerPos[3];
		unsigned char steerPosFlag;
		dtPolyRef steerPosRef;

		if (!GetSteerTarget(navMesh.navMeshQuery, &currentPos.x, &end.x, SLOP,
			(dtPolyRef*)pathRefs.data(), pathCount, steerPos, steerPosFlag, steerPosRef))
			break;

		bool endOfPath = (steerPosFlag & DT_STRAIGHTPATH_END) ? true : false;
		bool offMeshConnection = (steerPosFlag & DT_STRAIGHTPATH_OFFMESH_CONNECTION) ? true : false;

		// Find movement delta.
		float delta[3], len;
		dtVsub(delta, steerPos, &currentPos.x);
		len = dtMathSqrtf(dtVdot(delta, delta));
		// If the steer target is end of path or off-mesh link, do not move past the location.
		if ((endOfPath || offMeshConnection) && len < STEP_SIZE)
			len = 1;
		else
			len = STEP_SIZE / len;
		float moveTgt[3];
		dtVmad(moveTgt, &currentPos.x, delta, len);

		// Move
		float result2[3];
		dtPolyRef visited[16];
		int nvisited = 0;
		navMesh.navMeshQuery->moveAlongSurface((dtPolyRef)pathRefs[0], &currentPos.x, moveTgt, &_filter,
			result2, visited, &nvisited, 16);

		pathCount = dtMergeCorridorStartMoved((dtPolyRef*)pathRefs.data(), pathCount, HexEngine::MaxPaths, visited, nvisited);
		pathCount = fixupShortcuts((dtPolyRef*)pathRefs.data(), pathCount, navMesh.navMeshQuery);

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
#if 0
		else if (offMeshConnection && inRange(&result.currentPos.x, steerPos, SLOP, 1.0f))
		{
			// Reached off-mesh connection.
			float startPos[3], endPos[3];

			// Advance the path up to and over the off-mesh connection.
			dtPolyRef prevRef = 0, polyRef = polys[0];
			int npos = 0;
			while (npos < npolys && polyRef != steerPosRef)
			{
				prevRef = polyRef;
				polyRef = polys[npos];
				npos++;
			}
			for (int i = npos; i < npolys; ++i)
				polys[i - npos] = polys[i];
			npolys -= npos;

			// Handle the connection.
			dtStatus status = m_navMesh->getOffMeshConnectionPolyEndPoints(prevRef, polyRef, startPos, endPos);
			if (dtStatusSucceed(status))
			{
				if (m_nsmoothPath < MAX_SMOOTH)
				{
					dtVcopy(&m_smoothPath[m_nsmoothPath * 3], startPos);
					m_nsmoothPath++;
					// Hack to make the dotted path not visible during off-mesh connection.
					if (m_nsmoothPath & 1)
					{
						dtVcopy(&m_smoothPath[m_nsmoothPath * 3], startPos);
						m_nsmoothPath++;
					}
				}
				// Move position at the other side of the off-mesh link.
				dtVcopy(iterPos, endPos);
				float eh = 0.0f;
				m_navQuery->getPolyHeight(polys[0], iterPos, &eh);
				iterPos[1] = eh;
			}
		}
#endif

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