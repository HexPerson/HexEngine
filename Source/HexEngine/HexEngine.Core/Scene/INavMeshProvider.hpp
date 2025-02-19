
#pragma once

#include "../Plugin/IPlugin.hpp"

namespace HexEngine
{
	class Scene;

	const int32_t MaxPaths = 256;

	class INavMeshProvider : public IPluginInterface
	{
	public:
		DECLARE_PLUGIN_INTERFACE(INavMeshProvider, 001);

		struct NavMeshCreationParams
		{
			float cellSize = 16.0f;
			float cellHeight = 8.0f;
			float walkableSlopeAngle = 45.0f;
			float walkableClimb = 50.0f;
			float walkableHeight = 180.0f;
			float walkableRadius = 8.0f;
			float minRegionArea = 8.0f;
			float mergeRegionArea = 20.0f;
			float maxEdgeLen = 12.0f / cellSize;
			float maxSimplificationError = 1.3f;
			int32_t maxVertsPerPoly = 6;
			float detailSampleDist = cellSize * 6.0f;
			float detailSampleMaxError = cellHeight * 1.0f;
			int borderSize = walkableRadius + 3;
		};

		struct PathParams
		{
			math::Vector3 from;
			math::Vector3 to;
			math::Vector3 searchDistance;	
			float stepSize = 5.0f;
		};

		struct PathResult
		{
			std::vector<math::Vector3> path;
		};

		enum class MoveResult
		{
			Moving,
			ReachedDestination,
			Failed,
		};

		virtual bool CreateNavMeshForScene(Scene* scene, const NavMeshCreationParams& params) = 0;

		virtual bool CreateRoutingData() = 0;

		virtual void FindPath(const PathParams& params, PathResult& result) = 0;

		virtual MoveResult Move(PathResult& result) = 0;

		virtual void DebugRender() = 0;
	};
}
