
#include "Plugin.hpp"

CREATE_PLUGIN(g_pPhysXPlugin, PhysXPlugin);

PhysXPlugin::PhysXPlugin()
{
	_physx = new PhysicsSystemPhysX;
}

//bool PhysXPlugin::Create()
//{
//	return _physx->Create();;
//}

void PhysXPlugin::Destroy()
{
	SAFE_DELETE(_physx);
}

void PhysXPlugin::GetVersionData(VersionData* data)
{
	data->author = "HexPerson";
	data->description = "Provides nVidia's PhysX as a physics implementation";
	data->majorVersion = 1;
	data->minorVersion = 0;
	data->name = "HexEngine.PhysXPlugin";
}

HexEngine::IPluginInterface* PhysXPlugin::CreateInterface(const std::string& interfaceName)
{
	if (interfaceName == HexEngine::IPhysicsSystem::InterfaceName)
		return _physx;

	return nullptr;
}