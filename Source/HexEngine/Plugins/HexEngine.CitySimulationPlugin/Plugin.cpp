#include "Plugin.hpp"
#include "CitySimulationEditorToolPlugin.hpp"

CREATE_PLUGIN(g_pCitySimulationPlugin, CitySimulationPlugin);

CitySimulationPlugin::CitySimulationPlugin()
{
	_interface = new CitySimulationInterface();
	_interface->Create();
	_editorToolPlugin = new CitySimulationEditorToolPlugin(_interface);
}

void CitySimulationPlugin::Destroy()
{
	SAFE_DELETE(_editorToolPlugin);

	if (_interface != nullptr)
	{
		_interface->Destroy();
		SAFE_DELETE(_interface);
	}
}

void CitySimulationPlugin::GetVersionData(VersionData* data)
{
	data->author = "HexEngine";
	data->description = "Integrated city simulation and road authoring plugin.";
	data->majorVersion = 1;
	data->minorVersion = 0;
	data->name = "HexEngine.CitySimulationPlugin";
	data->enabled = true;
}

HexEngine::IPluginInterface* CitySimulationPlugin::CreateInterface(const std::string& interfaceName)
{
	if (interfaceName == CitySimulationInterface::InterfaceName)
		return _interface;

	return nullptr;
}

void CitySimulationPlugin::GetDependencies(std::vector<std::string>& dependencies) const
{
	(void)dependencies;
}

HexEngine::IEditorToolPlugin* CitySimulationPlugin::GetEditorToolPlugin()
{
	return _editorToolPlugin;
}
