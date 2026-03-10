
#include "Plugin.hpp"

CREATE_PLUGIN(g_pAssimpPlugin, AssimpPlugin);

AssimpPlugin::AssimpPlugin()
{
	_importer = new AssimpModelImporter;
}

//bool AssimpPlugin::Create()
//{
//	return true;
//}

void AssimpPlugin::Destroy()
{
	SAFE_DELETE(_importer);
}

void AssimpPlugin::GetVersionData(VersionData* data)
{
	data->author = "HexPerson";
	data->description = "Loads models, materials, and animations via the Assimp library";
	data->majorVersion = 1;
	data->minorVersion = 0;
	data->name = "HexEngine.AssimpPlugin";
}

HexEngine::IPluginInterface* AssimpPlugin::CreateInterface(const std::string& interfaceName)
{
	if (interfaceName == HexEngine::IModelImporter::InterfaceName)
		return _importer;

	return nullptr;
}
