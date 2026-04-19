#pragma once

#include <HexEngine.Core/HexEngine.hpp>

class CitySimulationInterface;

class CitySimulationEditorToolPlugin final : public HexEngine::IEditorToolPlugin
{
public:
	explicit CitySimulationEditorToolPlugin(CitySimulationInterface* citySimulationInterface);
	virtual ~CitySimulationEditorToolPlugin();

	virtual void OnCreateUI(HexEngine::MenuBar* menuBar) override;
	virtual void OnAssetExplorerCreateNew(HexEngine::ContextMenu* menu, HexEngine::ContextRoot* rootMenu, const fs::path& baseDir, HexEngine::FileSystem* fileSystem, std::function<void()> onAssetsCreated) override;
	virtual void OnMessage(HexEngine::Message* message, HexEngine::MessageListener* sender) override;

private:
	void CreateRoadPrefab(const fs::path& baseDir, HexEngine::FileSystem* fileSystem, const std::function<void()>& onAssetsCreated);
	void CreateVehiclePrefab(const fs::path& baseDir, HexEngine::FileSystem* fileSystem, const std::function<void()>& onAssetsCreated);

private:
	CitySimulationInterface* _citySimulationInterface = nullptr;
	bool _uiCreated = false;
};
