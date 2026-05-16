#pragma once

#include <HexEngine.Core/HexEngine.hpp>

namespace HexEngine::Weather
{
	class WeatherInterface;

	class WeatherEditorTool final : public IEditorToolPlugin
	{
	public:
		explicit WeatherEditorTool(WeatherInterface* weatherInterface);

		virtual void OnCreateUI(MenuBar* menuBar) override;
		virtual void OnAssetExplorerCreateNew(ContextMenu* menu, ContextRoot* rootMenu, const fs::path& baseDir, FileSystem* fileSystem, std::function<void()> onAssetsCreated) override;
		virtual void OnMessage(Message* message, MessageListener* sender) override;

	private:
		void CreateWeatherController();
		void CreateWeatherZone();

	private:
		WeatherInterface* _weatherInterface = nullptr;
		bool _uiCreated = false;
	};
}
