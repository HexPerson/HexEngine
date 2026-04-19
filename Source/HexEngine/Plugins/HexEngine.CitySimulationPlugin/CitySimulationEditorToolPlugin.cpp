#include "CitySimulationEditorToolPlugin.hpp"
#include "CitySimulationInterface.hpp"
#include "RoadComponent.hpp"
#include "VehicleComponent.hpp"

#include <HexEngine.Core/FileSystem/SceneSaveFile.hpp>
#include <format>

CitySimulationEditorToolPlugin::CitySimulationEditorToolPlugin(CitySimulationInterface* citySimulationInterface) :
	_citySimulationInterface(citySimulationInterface)
{
}

CitySimulationEditorToolPlugin::~CitySimulationEditorToolPlugin()
{
}

void CitySimulationEditorToolPlugin::OnCreateUI(HexEngine::MenuBar* menuBar)
{
	(void)menuBar;
	if (_uiCreated)
		return;

	_uiCreated = true;
}

void CitySimulationEditorToolPlugin::OnAssetExplorerCreateNew(HexEngine::ContextMenu* menu, HexEngine::ContextRoot* rootMenu, const fs::path& baseDir, HexEngine::FileSystem* fileSystem, std::function<void()> onAssetsCreated)
{
	if (menu == nullptr || rootMenu == nullptr)
		return;

	menu->AddItem(new HexEngine::ContextItem(L"Road Prefab",
		[this, baseDir, fileSystem, onAssetsCreated](const std::wstring&)
		{
			CreateRoadPrefab(baseDir, fileSystem, onAssetsCreated);
		}), rootMenu);

	menu->AddItem(new HexEngine::ContextItem(L"Vehicle Prefab",
		[this, baseDir, fileSystem, onAssetsCreated](const std::wstring&)
		{
			CreateVehiclePrefab(baseDir, fileSystem, onAssetsCreated);
		}), rootMenu);
}

void CitySimulationEditorToolPlugin::OnMessage(HexEngine::Message* message, HexEngine::MessageListener* sender)
{
	(void)sender;

	if (_citySimulationInterface == nullptr || message == nullptr)
		return;

	if (auto* duplicated = message->CastAs<HexEngine::EditorEntityDuplicatedMessage>(); duplicated != nullptr)
	{
		duplicated->handled = _citySimulationInterface->OnEntityDuplicated(duplicated->source, duplicated->duplicate) || duplicated->handled;
	}
}

void CitySimulationEditorToolPlugin::CreateRoadPrefab(const fs::path& baseDir, HexEngine::FileSystem* fileSystem, const std::function<void()>& onAssetsCreated)
{
	if (fileSystem == nullptr)
	{
		LOG_WARN("CitySimulationPlugin: cannot create road prefab without filesystem context.");
		return;
	}

	fs::path targetPath = fileSystem->GetLocalAbsoluteDataPath(baseDir / L"NewRoadPrefab.hprefab");
	int32_t duplicateIndex = 1;
	while (fs::exists(targetPath))
	{
		targetPath = fileSystem->GetLocalAbsoluteDataPath(baseDir / std::format(L"NewRoadPrefab{}.hprefab", duplicateIndex));
		++duplicateIndex;
	}

	auto prefabScene = HexEngine::g_pEnv->_sceneManager->CreateEmptyScene(false);
	if (prefabScene == nullptr)
	{
		LOG_WARN("CitySimulationPlugin: failed to create scene for road prefab.");
		return;
	}

	auto* rootEntity = prefabScene->CreateEntity("RoadSection");
	if (rootEntity == nullptr)
	{
		LOG_WARN("CitySimulationPlugin: failed to create road root entity.");
		return;
	}

	rootEntity->AddComponent<RoadComponent>();

	std::vector<HexEngine::Entity*> entitiesToSave = { rootEntity };
	HexEngine::SceneSaveFile saveFile(targetPath, std::ios::out | std::ios::trunc, prefabScene, HexEngine::SceneFileFlags::IsPrefab);
	if (!saveFile.Save(entitiesToSave))
	{
		LOG_WARN("CitySimulationPlugin: failed to save road prefab '%s'.", targetPath.string().c_str());
		return;
	}

	if (onAssetsCreated)
	{
		onAssetsCreated();
	}
}

void CitySimulationEditorToolPlugin::CreateVehiclePrefab(const fs::path& baseDir, HexEngine::FileSystem* fileSystem, const std::function<void()>& onAssetsCreated)
{
	if (fileSystem == nullptr)
	{
		LOG_WARN("CitySimulationPlugin: cannot create vehicle prefab without filesystem context.");
		return;
	}

	fs::path targetPath = fileSystem->GetLocalAbsoluteDataPath(baseDir / L"NewVehiclePrefab.hprefab");
	int32_t duplicateIndex = 1;
	while (fs::exists(targetPath))
	{
		targetPath = fileSystem->GetLocalAbsoluteDataPath(baseDir / std::format(L"NewVehiclePrefab{}.hprefab", duplicateIndex));
		++duplicateIndex;
	}

	auto prefabScene = HexEngine::g_pEnv->_sceneManager->CreateEmptyScene(false);
	if (prefabScene == nullptr)
	{
		LOG_WARN("CitySimulationPlugin: failed to create scene for vehicle prefab.");
		return;
	}

	auto* rootEntity = prefabScene->CreateEntity("VehicleRoot");
	if (rootEntity == nullptr)
	{
		LOG_WARN("CitySimulationPlugin: failed to create vehicle root entity.");
		return;
	}

	rootEntity->AddComponent<VehicleComponent>();

	std::vector<HexEngine::Entity*> entitiesToSave = { rootEntity };
	HexEngine::SceneSaveFile saveFile(targetPath, std::ios::out | std::ios::trunc, prefabScene, HexEngine::SceneFileFlags::IsPrefab);
	if (!saveFile.Save(entitiesToSave))
	{
		LOG_WARN("CitySimulationPlugin: failed to save vehicle prefab '%s'.", targetPath.string().c_str());
		return;
	}

	if (onAssetsCreated)
	{
		onAssetsCreated();
	}
}
