#include "Plugin.hpp"

CREATE_PLUGIN(g_pWeatherPlugin, WeatherPlugin);

WeatherPlugin::WeatherPlugin()
{
	_interface = new HexEngine::Weather::WeatherInterface();
	_interface->Create();
	_editorTool = new HexEngine::Weather::WeatherEditorTool(_interface);
}

void WeatherPlugin::Destroy()
{
	SAFE_DELETE(_editorTool);
	if (_interface != nullptr)
	{
		_interface->Destroy();
		SAFE_DELETE(_interface);
	}
}

void WeatherPlugin::GetVersionData(VersionData* data)
{
	data->author = "HexEngine";
	data->description = "Scene weather orchestration, zones, precipitation, and weather-aware material parameters.";
	data->majorVersion = 1;
	data->minorVersion = 0;
	data->name = "HexEngine.WeatherPlugin";
	data->enabled = true;
}

HexEngine::IPluginInterface* WeatherPlugin::CreateInterface(const std::string& interfaceName)
{
	if (interfaceName == HexEngine::Weather::WeatherInterface::InterfaceName)
		return _interface;

	return nullptr;
}

void WeatherPlugin::GetDependencies(std::vector<std::string>& dependencies) const
{
	(void)dependencies;
}

HexEngine::IEditorToolPlugin* WeatherPlugin::GetEditorToolPlugin()
{
	return _editorTool;
}
