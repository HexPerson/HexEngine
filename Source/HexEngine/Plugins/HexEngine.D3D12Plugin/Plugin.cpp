
#include "Plugin.hpp"

CREATE_PLUGIN(g_pD3D12Plugin, D3D12Plugin);

// Defined in HexEngine.Core/Environment/Game3DEnvironment.cpp.
namespace HexEngine { extern HEX_API HVar r_renderer; }

D3D12Plugin::D3D12Plugin()
{
	_device = new GraphicsDeviceD3D12;
}

void D3D12Plugin::Destroy()
{
	SAFE_DELETE(_device);
}

void D3D12Plugin::GetVersionData(VersionData* data)
{
	data->author       = "HexPerson";
	data->description  = "Direct3D 12 renderer backend (Phase A skeleton; not yet implemented)";
	data->majorVersion = 0;
	data->minorVersion = 1;
	data->name         = "HexEngine.D3D12Plugin";
}

HexEngine::IPluginInterface* D3D12Plugin::CreateInterface(const std::string& interfaceName)
{
	if (interfaceName == HexEngine::IGraphicsDevice::InterfaceName)
	{
		// Self-rejection: only activate when r_renderer specifically asks for
		// D3D12. With r_renderer = 0 (auto) Phase A bias is toward D3D11 since
		// D3D12 is a stub; once D3D12 is feature-complete, flip the auto rule
		// in HexEngine::ShouldActivateBackend.
		if (!HexEngine::ShouldActivateBackend(HexEngine::GraphicsBackend::D3D12, HexEngine::r_renderer._val.i32))
		{
			LOG_INFO("HexEngine.D3D12Plugin: r_renderer=%d selected a different backend; standing down.", HexEngine::r_renderer._val.i32);
			return nullptr;
		}

		LOG_WARN("HexEngine.D3D12Plugin: serving the D3D12 IGraphicsDevice but the backend is a non-functional Phase A stub - expect immediate failure on Create().");
		return _device;
	}
	return nullptr;
}
