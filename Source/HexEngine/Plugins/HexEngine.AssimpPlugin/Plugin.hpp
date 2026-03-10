
#pragma once

#include <HexEngine.Core\HexEngine.hpp>
#include "AssimpModelSystem.hpp"

class AssimpPlugin : public HexEngine::IPlugin
{
public:
	AssimpPlugin();

	//virtual bool Create() override;

	virtual void Destroy() override;

	virtual void GetVersionData(VersionData* data) override;

	virtual HexEngine::IPluginInterface* CreateInterface(const std::string& interfaceName) override;

	virtual void GetDependencies(std::vector<std::string>& dependencies) const override {}

private:
	AssimpModelImporter* _importer = nullptr;
};

inline AssimpPlugin* g_pAssimpPlugin = nullptr;