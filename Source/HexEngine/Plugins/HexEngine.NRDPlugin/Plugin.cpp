
#include "Plugin.hpp"

CREATE_PLUGIN(g_pNRDPlugin, NRDPlugin);

NRDPlugin::NRDPlugin()
{
	_interface = new NRDInterface;
}
/// <summary>
/// Called after the plugin is loaded
/// </summary>
/// <returns></returns>
//bool NRDPlugin::Create()
//{
//	return _interface->Create();
//}

/// <summary>
/// Called just before the plugin is unloaded
/// </summary>
void NRDPlugin::Destroy()
{
	SAFE_DELETE(_interface);
}

/// <summary>
/// Called by the engine to retrieve version info about this plugin
/// </summary>
/// <param name="data">A pointer to the VersionInfo instance representing this plugin</param>
void NRDPlugin::GetVersionData(VersionData* data)
{
	data->author = "HexPerson";
	data->description = "A short description of what your plugin does";
	data->majorVersion = 1;
	data->minorVersion = 0;
	data->name = "SamplePlugin";
}

/// <summary>
/// Factory function used by the engine to retrieve implemented interfaces
/// </summary>
/// <param name="interfaceName">The name of the interface being searched for</param>
/// <returns>A pointer to an implemented interface if found, or null if not.</returns>
IPluginInterface* NRDPlugin::CreateInterface(const std::string& interfaceName)
{
	// generally you'd use a InterfaceName from the interface you want to override, e.g. IModelImporter::InterfaceName as this is guaranteed to be correct for the version being implemented.
	// You can use string literals too, but its not recommended
	//
	if (interfaceName == IDenoiserProvider::InterfaceName)
		return _interface;

	return nullptr;
}

void NRDPlugin::GetDependencies(std::vector<std::string>& dependencies) const
{
	// if your plugin depends on any other plugins, add them here. For example:
	// dependencies.push_back("HexEngine.D3D11Plugin");
}
