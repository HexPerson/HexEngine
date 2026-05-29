
#include "Plugin.hpp"

CREATE_PLUGIN(g_pSteamworksPlugin, SteamworksPlugin);

SteamworksPlugin::SteamworksPlugin()
{
	// Allocate the provider in the ctor (NOT a separate Create() call): IPlugin
	// doesn't have a virtual Create(), so any Create() we declared here would
	// never be invoked by the plugin loader. Mirror the StreamlinePlugin
	// pattern where the inner provider is built at construction time and
	// Destroy() releases it. The engine calls Provider::Create() (SteamAPI_Init)
	// later, via the env's TryCreateInterface lookup.
	_interface = new Steamworks;
}

/// <summary>
/// Called just before the plugin is unloaded
/// </summary>
void SteamworksPlugin::Destroy()
{
	SAFE_DELETE(_interface);
}

/// <summary>
/// Called by the engine to retrieve version info about this plugin
/// </summary>
/// <param name="data">A pointer to the VersionInfo instance representing this plugin</param>
void SteamworksPlugin::GetVersionData(VersionData* data)
{
	data->author = "HexPerson";
	data->description = "Steamworks integration (achievements, stats, rich presence, overlay)";
	data->majorVersion = 1;
	data->minorVersion = 0;
	data->name = "Steamworks Plugin";
}

/// <summary>
/// Factory function used by the engine to retrieve implemented interfaces
/// </summary>
/// <param name="interfaceName">The name of the interface being searched for</param>
/// <returns>A pointer to an implemented interface if found, or null if not.</returns>
HexEngine::IPluginInterface* SteamworksPlugin::CreateInterface(const std::string& interfaceName)
{
	// generally you'd use a InterfaceName from the interface you want to override, e.g. IModelImporter::InterfaceName as this is guaranteed to be correct for the version being implemented.
	// You can use string literals too, but its not recommended
	//
	if (interfaceName == HexEngine::ISteamworksProvider::InterfaceName)
		return _interface;

	return nullptr;
}

void SteamworksPlugin::GetDependencies(std::vector<std::string>& dependencies) const
{

}
