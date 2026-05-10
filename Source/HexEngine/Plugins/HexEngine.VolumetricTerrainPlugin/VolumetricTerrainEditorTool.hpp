#pragma once

#include <HexEngine.Core/HexEngine.hpp>

namespace HexEngine::VolumetricTerrain
{
	class VolumetricTerrainInterface;

	class VolumetricTerrainEditorTool final : public IEditorToolPlugin
	{
	public:
		explicit VolumetricTerrainEditorTool(VolumetricTerrainInterface* terrainInterface);
		virtual ~VolumetricTerrainEditorTool();

		virtual void OnCreateUI(MenuBar* menuBar) override;
		virtual void OnAssetExplorerCreateNew(ContextMenu* menu, ContextRoot* rootMenu, const fs::path& baseDir, FileSystem* fileSystem, std::function<void()> onAssetsCreated) override;
		virtual void OnMessage(Message* message, MessageListener* sender) override;

	private:
		void OpenCreateTerrainDialog();

	private:
		VolumetricTerrainInterface* _terrainInterface = nullptr;
		bool _uiCreated = false;
	};
}
