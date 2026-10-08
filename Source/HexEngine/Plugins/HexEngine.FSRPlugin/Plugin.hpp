#pragma once

#include <HexEngine.Core\HexEngine.hpp>
#include "FSR2Upscaler.hpp"

/// <summary>
/// AMD FidelityFX Super Resolution 2.2 plugin. Exposes IUpscalerProvider.
/// </summary>
class FSRPlugin : public HexEngine::IPlugin
{
public:
	FSRPlugin();

	virtual void Destroy() override;

	virtual void GetVersionData(VersionData* data) override;

	virtual HexEngine::IPluginInterface* CreateInterface(const std::string& interfaceName) override;

	virtual void GetDependencies(std::vector<std::string>& dependencies) const override {};

private:
	FSR2Upscaler* _upscaler = nullptr;
};

inline FSRPlugin* g_pFSRPlugin = nullptr;
