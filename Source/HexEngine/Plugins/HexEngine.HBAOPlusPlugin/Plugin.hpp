
#pragma once

#include <HexEngine.Core\HexEngine.hpp>
#include "HBAOPlus.hpp"

class HBAOPlusPlugin : public HexEngine::IPlugin
{
public:
	HBAOPlusPlugin();

	//virtual bool Create() override;

	virtual void Destroy() override;

	virtual void GetVersionData(VersionData* data) override;

	virtual HexEngine::IPluginInterface* CreateInterface(const std::string& interfaceName) override;

	virtual void GetDependencies(std::vector<std::string>& dependencies) const override;

private:
	HBAOPlus* _hbao = nullptr;
};

inline HBAOPlusPlugin* g_pHBAOPlusPlugin = nullptr;