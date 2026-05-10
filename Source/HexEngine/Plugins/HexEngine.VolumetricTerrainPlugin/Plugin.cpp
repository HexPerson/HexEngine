#include "Plugin.hpp"

CREATE_PLUGIN(g_pVolumetricTerrainPlugin, VolumetricTerrainPlugin);

VolumetricTerrainPlugin::VolumetricTerrainPlugin()
{
	_interface = new HexEngine::VolumetricTerrain::VolumetricTerrainInterface();
	_interface->Create();
	_editorTool = new HexEngine::VolumetricTerrain::VolumetricTerrainEditorTool(_interface);
}

void VolumetricTerrainPlugin::Destroy()
{
	SAFE_DELETE(_editorTool);

	if (_interface != nullptr)
	{
		_interface->Destroy();
		SAFE_DELETE(_interface);
	}
}

void VolumetricTerrainPlugin::GetVersionData(VersionData* data)
{
	data->author = "HexEngine";
	data->description = "Chunked volumetric SDF terrain plugin with realtime sculpting and marching meshes.";
	data->majorVersion = 1;
	data->minorVersion = 0;
	data->name = "HexEngine.VolumetricTerrainPlugin";
	data->enabled = true;
}

HexEngine::IPluginInterface* VolumetricTerrainPlugin::CreateInterface(const std::string& interfaceName)
{
	if (interfaceName == HexEngine::VolumetricTerrain::VolumetricTerrainInterface::InterfaceName)
		return _interface;

	return nullptr;
}

void VolumetricTerrainPlugin::GetDependencies(std::vector<std::string>& dependencies) const
{
	(void)dependencies;
}

HexEngine::IEditorToolPlugin* VolumetricTerrainPlugin::GetEditorToolPlugin()
{
	return _editorTool;
}
