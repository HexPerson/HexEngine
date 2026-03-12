#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include "OIDN.hpp"

class OIDNPlugin : public HexEngine::IPlugin
{
public:
	OIDNPlugin();

	virtual void Destroy() override;

	virtual void GetVersionData(HexEngine::IPlugin::VersionData* data) override;

	virtual HexEngine::IPluginInterface* CreateInterface(const std::string& interfaceName) override;

	virtual void GetDependencies(std::vector<std::string>& dependencies) const override;

private:
	OIDN* _interface = nullptr;
};

inline OIDNPlugin* g_pOIDNPlugin = nullptr;

