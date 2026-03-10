
#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include "Recast/Include/Recast.h"
#include "Detour/Include/DetourNavMesh.h"
#include "Detour/Include/DetourNavMeshBuilder.h"
#include "Detour/Include/DetourNavMeshQuery.h"
#include "DebugRender.hpp"

class RecastInterface : public HexEngine::INavMeshProvider, public rcContext
{
public:
	struct NavMeshes
	{
		dtNavMesh* navMesh;
		uint8_t* navMeshData;
		dtNavMeshQuery* navMeshQuery;
		HexEngine::Chunk* chunk = nullptr;
	};
	RecastInterface() {}
	virtual ~RecastInterface() {}

	virtual bool Create() override;

	virtual void Destroy() override;

	virtual bool CreateNavMeshForScene(HexEngine::Scene* scene, const NavMeshCreationParams& params, HexEngine::NavMeshId* navMesh) override;

	virtual bool CreateNavMeshForChunk(HexEngine::Scene* scene, HexEngine::Chunk* chunk, const NavMeshCreationParams& params, HexEngine::NavMeshId* navMesh) override;

	virtual bool RebuildMesh(HexEngine::NavMeshId id) override;

	virtual void DebugRender() override;

	virtual void FindPath(const PathParams& params, PathResult& result) override;

	virtual MoveResult Move(PathResult& result) override;

	virtual void doLog(const rcLogCategory category, const char* msg, const int len) override;

private:
	bool CreateNavMeshInternal(HexEngine::Scene* scene, HexEngine::Chunk* chunk, const NavMeshCreationParams& params, HexEngine::NavMeshId* navMesh);

private:
	bool CreateRoutingData(HexEngine::NavMeshId* navMesh);

private:
	rcHeightfield* _heightField = nullptr;
	rcCompactHeightfield* _compactHF = nullptr;
	rcContourSet* _cset = nullptr;
	rcPolyMesh* _polyMesh = nullptr;
	rcPolyMeshDetail* _detailMesh = nullptr;
	uint8_t* _triAreas = nullptr;
	RCDebugRenderer _debugRenderer;
	NavMeshCreationParams _creationParams;

	//dtNavMesh* _navMesh = nullptr;
	//dtNavMeshQuery* _navMeshQuery = nullptr;
	dtQueryFilter _filter;

	std::vector<NavMeshes> _navMeshes;
};
