
#pragma once

#include <HexEngine.Core\HexEngine.hpp>
#include "GraphicsDeviceD3D11.hpp"

class D3D11Plugin : public IPlugin
{
public:
	D3D11Plugin();

	//virtual bool Create() override;

	virtual void Destroy() override;

	virtual void GetVersionData(VersionData* data) override;

	virtual IPluginInterface* CreateInterface(const std::string& interfaceName) override;

	virtual void GetDependencies(std::vector<std::string>& dependencies) const override {}

private:
	GraphicsDeviceD3D11* _device = nullptr;
};

inline D3D11Plugin* g_pD3D11Plugin = nullptr;