
#include "Plugin.hpp"

CREATE_PLUGIN(g_pHBAOPlusPlugin, HBAOPlusPlugin);

HBAOPlusPlugin::HBAOPlusPlugin()
{
	_hbao = new HBAOPlus;
}

//bool HBAOPlusPlugin::Create()
//{
//	return _hbao->Create();;
//}

void HBAOPlusPlugin::Destroy()
{
	SAFE_DELETE(_hbao);
}

void HBAOPlusPlugin::GetVersionData(VersionData* data)
{
	data->author = "HexPerson";
	data->description = "Provides nVidia's HBAO+";
	data->majorVersion = 1;
	data->minorVersion = 0;
	data->name = "HexEngine.HBAOPlusPlugin";
}

IPluginInterface* HBAOPlusPlugin::CreateInterface(const std::string& interfaceName)
{
	if (interfaceName == ISSAOProvider::InterfaceName)
		return _hbao;

	return nullptr;
}

void HBAOPlusPlugin::GetDependencies(std::vector<std::string>& dependencies) const
{
	dependencies = {
		"HexEngine.D3D11Plugin"
	};
}