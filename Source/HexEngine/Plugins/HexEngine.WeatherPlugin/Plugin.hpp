#pragma once

#include <HexEngine.Core/HexEngine.hpp>

#include "WeatherEditorTool.hpp"
#include "WeatherInterface.hpp"

class WeatherPlugin final : public HexEngine::IPlugin
{
public:
	WeatherPlugin();
	virtual ~WeatherPlugin() = default;

	virtual void Destroy() override;
	virtual void GetVersionData(VersionData* data) override;
	virtual HexEngine::IPluginInterface* CreateInterface(const std::string& interfaceName) override;
	virtual void GetDependencies(std::vector<std::string>& dependencies) const override;
	virtual HexEngine::IEditorToolPlugin* GetEditorToolPlugin() override;

private:
	HexEngine::Weather::WeatherInterface* _interface = nullptr;
	HexEngine::Weather::WeatherEditorTool* _editorTool = nullptr;
};

inline WeatherPlugin* g_pWeatherPlugin = nullptr;
