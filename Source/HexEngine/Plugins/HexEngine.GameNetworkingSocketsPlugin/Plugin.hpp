
#pragma once

#include <HexEngine.Core\HexEngine.hpp>
#include "GameNetworkingSystem.hpp"

/// <summary>
/// Plugin wrapper exposing the GameNetworkingSockets-backed INetworkSystem to
/// the engine. Mirrors the Steamworks plugin: the provider is built in the ctor
/// and released in Destroy(); the engine calls provider->Create() later via the
/// environment's TryCreateInterface lookup.
/// </summary>
class GameNetworkingSocketsPlugin : public HexEngine::IPlugin
{
public:
	GameNetworkingSocketsPlugin();

	virtual void Destroy() override;
	virtual void GetVersionData(VersionData* data) override;
	virtual HexEngine::IPluginInterface* CreateInterface(const std::string& interfaceName) override;
	virtual void GetDependencies(std::vector<std::string>& dependencies) const override;

private:
	GameNetworkingSystem* _interface = nullptr;
};

inline GameNetworkingSocketsPlugin* g_pGnsPlugin = nullptr;
