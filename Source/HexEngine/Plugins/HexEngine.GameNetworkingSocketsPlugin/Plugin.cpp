
#include "Plugin.hpp"

// Networking backend selection cvar (defined in HexEngine.Core). 0 = auto/GNS,
// 1 = GNS, 2 = Steam P2P. This plugin provides the GNS direct-IP backend, so it
// stands down when the user explicitly picked the Steam backend.
namespace HexEngine { extern HEX_API HVar net_backend; }

CREATE_PLUGIN(g_pGnsPlugin, GameNetworkingSocketsPlugin);

GameNetworkingSocketsPlugin::GameNetworkingSocketsPlugin()
{
	// Allocate the provider at construction (IPlugin has no virtual Create()).
	// The engine initialises the GNS library by calling _interface->Create()
	// through TryCreateInterface at boot.
	_interface = new GameNetworkingSystem;
}

void GameNetworkingSocketsPlugin::Destroy()
{
	SAFE_DELETE(_interface);
}

void GameNetworkingSocketsPlugin::GetVersionData(VersionData* data)
{
	data->author = "HexPerson";
	data->description = "Game networking transport (Valve GameNetworkingSockets) + replication";
	data->majorVersion = 1;
	data->minorVersion = 0;
	data->name = "GameNetworkingSockets Plugin";
}

HexEngine::IPluginInterface* GameNetworkingSocketsPlugin::CreateInterface(const std::string& interfaceName)
{
	if (interfaceName == HexEngine::INetworkSystem::InterfaceName)
	{
		// Stand down when the user picked the Steam P2P backend (net_backend == 2).
		// net_backend 0 (auto) and 1 (gns) both resolve to this direct-IP backend.
		if (HexEngine::net_backend._val.i32 == 2)
		{
			LOG_INFO("HexEngine.GameNetworkingSocketsPlugin: net_backend=2 (Steam) selected; standing down.");
			return nullptr;
		}
		return _interface;
	}

	return nullptr;
}

void GameNetworkingSocketsPlugin::GetDependencies(std::vector<std::string>& dependencies) const
{
}
