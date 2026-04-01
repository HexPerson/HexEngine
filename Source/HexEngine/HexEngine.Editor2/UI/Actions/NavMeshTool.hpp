#pragma once

#include <HexEngine.Core\HexEngine.hpp>

namespace HexEditor
{
	class NavMeshTool : public HexEngine::Dialog
	{
	public:
		NavMeshTool(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size);
		~NavMeshTool();

		static NavMeshTool* CreateEditorDialog(Element* parent);

	private:
		bool BuildSceneNavMesh();
		bool RebuildSceneNavMesh();
		bool BuildChunkNavMeshes();
		bool RebuildChunkNavMeshes();

		static HexEngine::INavMeshProvider::NavMeshCreationParams BuildDefaultParams();

	private:
		HexEngine::ComponentWidget* _settings = nullptr;
		HexEngine::INavMeshProvider::NavMeshCreationParams _params = {};
		HexEngine::NavMeshId _sceneNavMeshId = 0;
		bool _hasSceneNavMesh = false;
		std::vector<HexEngine::NavMeshId> _chunkNavMeshIds;
	};
}
