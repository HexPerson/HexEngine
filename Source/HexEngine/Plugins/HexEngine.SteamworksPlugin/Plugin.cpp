
#include "Plugin.hpp"

// Networking backend selection cvar (defined in HexEngine.Core). 0 = auto/GNS,
// 1 = GNS, 2 = Steam P2P. The Steam networking transport only activates when the
// user explicitly selects backend 2.
namespace HexEngine { extern HEX_API HVar net_backend; }

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
	_netInterface = new SteamNetworkingSystem;
}

/// <summary>
/// Called just before the plugin is unloaded
/// </summary>
void SteamworksPlugin::Destroy()
{
	SAFE_DELETE(_interface);
	SAFE_DELETE(_netInterface);
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

	// Steam P2P networking transport - only when the user picked net_backend == 2.
	// (net_backend 0/1 resolve to the GameNetworkingSockets direct-IP plugin.)
	if (interfaceName == HexEngine::INetworkSystem::InterfaceName)
	{
		if (HexEngine::net_backend._val.i32 == 2)
			return _netInterface;
		LOG_INFO("HexEngine.SteamworksPlugin: net_backend=%d (not Steam); networking transport standing down.", HexEngine::net_backend._val.i32);
		return nullptr;
	}

	return nullptr;
}

void SteamworksPlugin::GetDependencies(std::vector<std::string>& dependencies) const
{

}
