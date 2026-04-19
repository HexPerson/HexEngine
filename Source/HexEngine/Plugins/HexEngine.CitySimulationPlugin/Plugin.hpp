#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include "CitySimulationInterface.hpp"

class CitySimulationEditorToolPlugin;

class CitySimulationPlugin final : public HexEngine::IPlugin
{
public:
	CitySimulationPlugin();
	virtual ~CitySimulationPlugin() = default;

	virtual void Destroy() override;
	virtual void GetVersionData(VersionData* data) override;
	virtual HexEngine::IPluginInterface* CreateInterface(const std::string& interfaceName) override;
	virtual void GetDependencies(std::vector<std::string>& dependencies) const override;
	virtual HexEngine::IEditorToolPlugin* GetEditorToolPlugin() override;

private:
	CitySimulationInterface* _interface = nullptr;
	CitySimulationEditorToolPlugin* _editorToolPlugin = nullptr;
};

inline CitySimulationPlugin* g_pCitySimulationPlugin = nullptr;
