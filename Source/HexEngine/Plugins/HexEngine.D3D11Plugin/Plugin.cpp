
#include "Plugin.hpp"

CREATE_PLUGIN(g_pD3D11Plugin, D3D11Plugin);

// Defined in HexEngine.Core/Environment/Game3DEnvironment.cpp. 0 = auto, 1 = D3D11, 2 = D3D12.
namespace HexEngine { extern HEX_API HVar r_renderer; }

D3D11Plugin::D3D11Plugin()
{
	_device = new GraphicsDeviceD3D11;
}

void D3D11Plugin::Destroy()
{
	SAFE_DELETE(_device);
}

void D3D11Plugin::GetVersionData(VersionData* data)
{
	data->author = "HexPerson";
	data->description = "Direct3D 11 renderer backend";
	data->majorVersion = 1;
	data->minorVersion = 0;
	data->name = "HexEngine.D3D11Plugin";
}

HexEngine::IPluginInterface* D3D11Plugin::CreateInterface(const std::string& interfaceName)
{
	if (interfaceName == HexEngine::IGraphicsDevice::InterfaceName)
	{
		// Self-rejection: if the user picked a different backend via r_renderer,
		// hand back null so the engine looks for another graphics plugin. This
		// is what allows D3D11 and D3D12 plugins to coexist in Plugins/
		// without racing for the IGraphicsDevice slot.
		if (!HexEngine::ShouldActivateBackend(HexEngine::GraphicsBackend::D3D11, HexEngine::r_renderer._val.i32))
		{
			LOG_INFO("HexEngine.D3D11Plugin: r_renderer=%d selected a different backend; standing down.", HexEngine::r_renderer._val.i32);
			return nullptr;
		}
		return _device;
	}

	return nullptr;
}