
#include "Texture2DD3D12.hpp"
#include "GraphicsDeviceD3D12.hpp"
#include <HexEngine.Core/HexEngine.hpp>

Texture2DD3D12::~Texture2DD3D12()
{
	Destroy();
}

void Texture2DD3D12::Destroy()
{
	if (HexEngine::g_pEnv && HexEngine::g_pEnv->_graphicsDevice)
	{
		auto* device = static_cast<GraphicsDeviceD3D12*>(HexEngine::g_pEnv->_graphicsDevice);
		if (_rtvIndex != UINT32_MAX) { device->RtvHeap().Free(_rtvIndex);       _rtvIndex = UINT32_MAX; }
		if (_dsvIndex != UINT32_MAX) { device->DsvHeap().Free(_dsvIndex);       _dsvIndex = UINT32_MAX; }
		if (_srvIndex != UINT32_MAX) { device->CbvSrvUavHeap().Free(_srvIndex); _srvIndex = UINT32_MAX; }
		if (_uavIndex != UINT32_MAX) { device->CbvSrvUavHeap().Free(_uavIndex); _uavIndex = UINT32_MAX; }
	}
	if (_ownsResource)
		_resource.Reset();
	else
		_resource = nullptr; // swap chain owns it; just drop our COM ref
}

void Texture2DD3D12::SetPixels(uint8_t* data, uint32_t size)
{
	if (data == nullptr || size == 0) return;
	auto* device = static_cast<GraphicsDeviceD3D12*>(HexEngine::g_pEnv->_graphicsDevice);
	if (device != nullptr)
		device->UploadTextureData(this, data, size);
}

void Texture2DD3D12::ClearDepth(uint32_t flags)
{
	auto* device = static_cast<GraphicsDeviceD3D12*>(HexEngine::g_pEnv->_graphicsDevice);
	if (device == nullptr) return;
	auto* cmd = device->GetActiveCommandList();
	if (cmd == nullptr || _dsvIndex == UINT32_MAX) return;
	device->TransitionResource(this, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	const D3D12_CLEAR_FLAGS clearFlags = (D3D12_CLEAR_FLAGS)((flags & 0x1 ? D3D12_CLEAR_FLAG_DEPTH : 0) | (flags & 0x2 ? D3D12_CLEAR_FLAG_STENCIL : D3D12_CLEAR_FLAG_DEPTH));
	cmd->ClearDepthStencilView(_dsv, clearFlags, 1.0f, 0, 0, nullptr);
}

void Texture2DD3D12::CopyTo(ITexture2D* other)
{
	auto* device = static_cast<GraphicsDeviceD3D12*>(HexEngine::g_pEnv->_graphicsDevice);
	if (device == nullptr || other == nullptr) return;
	auto* dst = static_cast<Texture2DD3D12*>(other);
	auto* cmd = device->GetActiveCommandList();
	if (cmd == nullptr || _resource == nullptr || dst->_resource == nullptr) return;
	device->TransitionResource(this, D3D12_RESOURCE_STATE_COPY_SOURCE);
	device->TransitionResource(dst,  D3D12_RESOURCE_STATE_COPY_DEST);
	cmd->CopyResource(dst->_resource.Get(), _resource.Get());
}

void Texture2DD3D12::ClearRenderTargetView(const math::Color& colour)
{
	auto* device = static_cast<GraphicsDeviceD3D12*>(HexEngine::g_pEnv->_graphicsDevice);
	if (device == nullptr) return;
	auto* cmd = device->GetActiveCommandList();
	if (cmd == nullptr || _resource == nullptr || _rtvIndex == UINT32_MAX) return;
	device->TransitionResource(this, D3D12_RESOURCE_STATE_RENDER_TARGET);
	const float clear[4] = { colour.R(), colour.G(), colour.B(), colour.A() };
	cmd->ClearRenderTargetView(_rtv, clear, 0, nullptr);
}
