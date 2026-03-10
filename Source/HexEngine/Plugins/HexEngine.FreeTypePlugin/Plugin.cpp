
#include "Plugin.hpp"

CREATE_PLUGIN(g_pFreeTypePlugin, FreeTypePlugin);


FreeTypePlugin::FreeTypePlugin()
{
	_interface = new FreeTypeImporter;
}

/// <summary>
/// Called after the plugin is loaded
/// </summary>
/// <returns></returns>
//bool FreeTypePlugin::Create()
//{
//	return _interface->Create();;
//}

/// <summary>
/// Called just before the plugin is unloaded
/// </summary>
void FreeTypePlugin::Destroy()
{
	SAFE_DELETE(_interface);
}

/// <summary>
/// Called by the engine to retrieve version info about this plugin
/// </summary>
/// <param name="data">A pointer to the VersionInfo instance representing this plugin</param>
void FreeTypePlugin::GetVersionData(VersionData* data)
{
	data->author = "HexPerson";
	data->description = "Imports fonts using the freetype library";
	data->majorVersion = 1;
	data->minorVersion = 0;
	data->name = "HexEngine.FreeTypePlugin";
}

/// <summary>
/// Factory function used by the engine to retrieve implemented interfaces
/// </summary>
/// <param name="interfaceName">The name of the interface being searched for</param>
/// <returns>A pointer to an implemented interface if found, or null if not.</returns>
HexEngine::IPluginInterface* FreeTypePlugin::CreateInterface(const std::string& interfaceName)
{
	// generally you'd use a InterfaceName from the interface you want to override, e.g. IModelImporter::InterfaceName as this is guaranteed to be correct for the version being implemented.
	// You can use string literals too, but its not recommended
	//
	if (interfaceName == HexEngine::IFontImporter::InterfaceName)
		return _interface;

	return nullptr;
}
