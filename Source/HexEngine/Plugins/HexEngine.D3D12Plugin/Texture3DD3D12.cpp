
#include "Texture3DD3D12.hpp"
#include "GraphicsDeviceD3D12.hpp"
#include <HexEngine.Core/HexEngine.hpp>

Texture3DD3D12::~Texture3DD3D12() { Destroy(); }

void Texture3DD3D12::Destroy()
{
	if (HexEngine::g_pEnv && HexEngine::g_pEnv->_graphicsDevice)
	{
		auto* device = static_cast<GraphicsDeviceD3D12*>(HexEngine::g_pEnv->_graphicsDevice);
		if (_rtvIndex != UINT32_MAX) { device->RtvHeap().Free(_rtvIndex);       _rtvIndex = UINT32_MAX; }
		if (_srvIndex != UINT32_MAX) { device->CbvSrvUavHeap().Free(_srvIndex); _srvIndex = UINT32_MAX; }
		if (_uavIndex != UINT32_MAX) { device->CbvSrvUavHeap().Free(_uavIndex); _uavIndex = UINT32_MAX; }
		if (_resource)
			device->DeferredRelease(std::move(_resource));
	}
	_resource.Reset();
}

void Texture3DD3D12::SetPixels(uint8_t* data, uint32_t size, int32_t /*slice*/)
{
	if (data == nullptr || size == 0) return;
	auto* device = static_cast<GraphicsDeviceD3D12*>(HexEngine::g_pEnv->_graphicsDevice);
	if (device != nullptr)
		device->UploadTextureData(this, data, size);
}

void Texture3DD3D12::ClearRenderTargetView(const math::Color& colour)
{
	auto* device = static_cast<GraphicsDeviceD3D12*>(HexEngine::g_pEnv->_graphicsDevice);
	if (device == nullptr) return;
	auto* cmd = device->GetActiveCommandList();
	if (cmd == nullptr || _resource == nullptr || _rtvIndex == UINT32_MAX) return;
	device->TransitionResource(this, D3D12_RESOURCE_STATE_RENDER_TARGET);
	const float clear[4] = { colour.R(), colour.G(), colour.B(), colour.A() };
	cmd->ClearRenderTargetView(_rtv, clear, 0, nullptr);
}
