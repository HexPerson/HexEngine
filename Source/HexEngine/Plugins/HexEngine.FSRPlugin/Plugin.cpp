#include "Plugin.hpp"

CREATE_PLUGIN(g_pFSRPlugin, FSRPlugin);

FSRPlugin::FSRPlugin()
{
	_upscaler = new FSR2Upscaler;
}

void FSRPlugin::Destroy()
{
	SAFE_DELETE(_upscaler);
}

void FSRPlugin::GetVersionData(VersionData* data)
{
	data->author = "HexPerson";
	data->description = "AMD FidelityFX Super Resolution 2.2 temporal upscaler (D3D11)";
	data->majorVersion = 1;
	data->minorVersion = 0;
	data->name = "FSR Plugin";
}

HexEngine::IPluginInterface* FSRPlugin::CreateInterface(const std::string& interfaceName)
{
	if (interfaceName == HexEngine::IUpscalerProvider::InterfaceName)
		return _upscaler;

	return nullptr;
}
