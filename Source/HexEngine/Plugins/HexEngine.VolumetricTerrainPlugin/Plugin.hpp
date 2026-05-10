#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include "VolumetricTerrainInterface.hpp"
#include "VolumetricTerrainEditorTool.hpp"

class VolumetricTerrainPlugin final : public HexEngine::IPlugin
{
public:
	VolumetricTerrainPlugin();
	virtual ~VolumetricTerrainPlugin() = default;

	virtual void Destroy() override;
	virtual void GetVersionData(VersionData* data) override;
	virtual HexEngine::IPluginInterface* CreateInterface(const std::string& interfaceName) override;
	virtual void GetDependencies(std::vector<std::string>& dependencies) const override;
	virtual HexEngine::IEditorToolPlugin* GetEditorToolPlugin() override;

private:
	HexEngine::VolumetricTerrain::VolumetricTerrainInterface* _interface = nullptr;
	HexEngine::VolumetricTerrain::VolumetricTerrainEditorTool* _editorTool = nullptr;
};

inline VolumetricTerrainPlugin* g_pVolumetricTerrainPlugin = nullptr;
