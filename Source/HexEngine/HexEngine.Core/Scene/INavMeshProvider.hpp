
#pragma once

#include "../Plugin/IPlugin.hpp"

namespace HexEngine
{
	class Scene;
	class Chunk;

	const int32_t MaxPaths = 256;
	using NavMeshId = uint32_t;

	class INavMeshProvider : public IPluginInterface
	{
	public:
		DECLARE_PLUGIN_INTERFACE(INavMeshProvider, 001);

		struct NavMeshCreationParams
		{
			/*float cellSize = 16.0f;
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
			int borderSize = (int)walkableRadius + 3;*/

			/// The width of the field along the x-axis. [Limit: >= 0] [Units: vx]
			int width;

			/// The height of the field along the z-axis. [Limit: >= 0] [Units: vx]
			int height;

			/// The width/height size of tile's on the xz-plane. [Limit: >= 0] [Units: vx]
			int tileSize;

			/// The size of the non-navigable border around the heightfield. [Limit: >=0] [Units: vx]
			int borderSize;

			/// The xz-plane cell size to use for fields. [Limit: > 0] [Units: wu] 
			float cs;

			/// The y-axis cell size to use for fields. [Limit: > 0] [Units: wu]
			float ch;

			/// The minimum bounds of the field's AABB. [(x, y, z)] [Units: wu]
			float bmin[3];

			/// The maximum bounds of the field's AABB. [(x, y, z)] [Units: wu]
			float bmax[3];

			/// The maximum slope that is considered walkable. [Limits: 0 <= value < 90] [Units: Degrees] 
			float walkableSlopeAngle;

			/// Minimum floor to 'ceiling' height that will still allow the floor area to 
			/// be considered walkable. [Limit: >= 3] [Units: vx] 
			int walkableHeight;

			/// Maximum ledge height that is considered to still be traversable. [Limit: >=0] [Units: vx] 
			int walkableClimb;

			/// The distance to erode/shrink the walkable area of the heightfield away from 
			/// obstructions.  [Limit: >=0] [Units: vx] 
			int walkableRadius;

			/// The maximum allowed length for contour edges along the border of the mesh. [Limit: >=0] [Units: vx] 
			int maxEdgeLen;

			/// The maximum distance a simplified contour's border edges should deviate 
			/// the original raw contour. [Limit: >=0] [Units: vx]
			float maxSimplificationError;

			/// The minimum number of cells allowed to form isolated island areas. [Limit: >=0] [Units: vx] 
			int minRegionArea;

			/// Any regions with a span count smaller than this value will, if possible, 
			/// be merged with larger regions. [Limit: >=0] [Units: vx] 
			int mergeRegionArea;

			/// The maximum number of vertices allowed for polygons generated during the 
			/// contour to polygon conversion process. [Limit: >= 3] 
			int maxVertsPerPoly;

			/// Sets the sampling distance to use when generating the detail mesh.
			/// (For height detail only.) [Limits: 0 or >= 0.9] [Units: wu] 
			float detailSampleDist;

			/// The maximum distance the detail mesh surface should deviate from heightfield
			/// data. (For height detail only.) [Limit: >=0] [Units: wu] 
			float detailSampleMaxError;
		};		

		struct PathParams
		{
			math::Vector3 from;
			math::Vector3 to;
			math::Vector3 searchDistance;	
			float stepSize = 5.0f;
			NavMeshId meshId;
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

		

		virtual bool CreateNavMeshForScene(Scene* scene, const NavMeshCreationParams& params, NavMeshId* navMesh) = 0;

		virtual bool CreateNavMeshForChunk(Scene* scene, Chunk* chunk, const NavMeshCreationParams& params, NavMeshId* navMesh) = 0;

		virtual bool RebuildMesh(NavMeshId id) = 0;

		virtual void FindPath(const PathParams& params, PathResult& result) = 0;

		virtual MoveResult Move(PathResult& result) = 0;

		virtual void DebugRender() = 0;
	};
}
