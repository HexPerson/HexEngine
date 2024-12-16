
#pragma once

#include <HexEngine.Core\HexEngine.hpp>
#include "PhysicsSystemPhysX.hpp"

class PhysXPlugin : public IPlugin
{
public:
	PhysXPlugin();

	//virtual bool Create() override;

	virtual void Destroy() override;

	virtual void GetVersionData(VersionData* data) override;

	virtual IPluginInterface* CreateInterface(const std::string& interfaceName) override;

	virtual void GetDependencies(std::vector<std::string>& dependencies) const override {}

private:
	PhysicsSystemPhysX* _physx = nullptr;
};

inline PhysXPlugin* g_pPhysXPlugin = nullptr;