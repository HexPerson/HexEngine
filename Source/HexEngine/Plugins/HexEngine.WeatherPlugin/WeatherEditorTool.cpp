#include "WeatherEditorTool.hpp"

#include "WeatherInterface.hpp"

namespace HexEngine::Weather
{
	WeatherEditorTool::WeatherEditorTool(WeatherInterface* weatherInterface) :
		_weatherInterface(weatherInterface)
	{
	}

	void WeatherEditorTool::OnCreateUI(MenuBar* menuBar)
	{
		if (_uiCreated || menuBar == nullptr)
			return;

		_uiCreated = true;

		auto* root = new MenuBar::RootItem;
		root->name = L"Weather";
		menuBar->AddRootItem(root);

		auto* addController = new MenuBar::Item;
		addController->name = L"Add Weather Controller";
		addController->action = [this](MenuBar::Item*) { CreateWeatherController(); };
		menuBar->AddSubItem(root, addController);

		auto* addZone = new MenuBar::Item;
		addZone->name = L"Add Weather Zone";
		addZone->action = [this](MenuBar::Item*) { CreateWeatherZone(); };
		menuBar->AddSubItem(root, addZone);
	}

	void WeatherEditorTool::OnAssetExplorerCreateNew(ContextMenu* menu, ContextRoot* rootMenu, const fs::path& baseDir, FileSystem* fileSystem, std::function<void()> onAssetsCreated)
	{
		(void)menu;
		(void)rootMenu;
		(void)baseDir;
		(void)fileSystem;
		(void)onAssetsCreated;
	}

	void WeatherEditorTool::OnMessage(Message* message, MessageListener* sender)
	{
		(void)message;
		(void)sender;
	}

	void WeatherEditorTool::CreateWeatherController()
	{
		if (_weatherInterface == nullptr)
			return;

		Scene* scene = g_pEnv->_sceneManager->GetCurrentScene().get();
		if (scene == nullptr)
			return;

		_weatherInterface->CreateWeatherControllerEntity(scene);
	}

	void WeatherEditorTool::CreateWeatherZone()
	{
		if (_weatherInterface == nullptr)
			return;

		Scene* scene = g_pEnv->_sceneManager->GetCurrentScene().get();
		if (scene == nullptr)
			return;

		math::Vector3 position = math::Vector3::Zero;
		if (Camera* camera = scene->GetMainCamera(); camera != nullptr && camera->GetEntity() != nullptr)
			position = camera->GetEntity()->GetPosition();

		_weatherInterface->CreateWeatherZoneEntity(scene, position);
	}
}
