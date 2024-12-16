
#include "Plugin.hpp"

CREATE_PLUGIN(g_pD3D11Plugin, D3D11Plugin);

D3D11Plugin::D3D11Plugin()
{
	_device = new GraphicsDeviceD3D11;
}

//bool D3D11Plugin::Create()
//{
//	return _device->Create();;
//}

void D3D11Plugin::Destroy()
{
	SAFE_DELETE(_device);
}

void D3D11Plugin::GetVersionData(VersionData* data)
{
	data->author = "HexPerson";
	data->description = "Provides nVidia's PhysX as a physics implementation";
	data->majorVersion = 1;
	data->minorVersion = 0;
	data->name = "HexEngine.D3D11Plugin";
}

IPluginInterface* D3D11Plugin::CreateInterface(const std::string& interfaceName)
{
	if (interfaceName == IGraphicsDevice::InterfaceName)
		return _device;

	return nullptr;
}