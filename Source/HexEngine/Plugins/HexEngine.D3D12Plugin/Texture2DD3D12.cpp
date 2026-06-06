
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

		// Defer the underlying ID3D12Resource's release until in-flight
		// command lists are done. Without this, a TAA / GBuffer / cloned
		// texture destroyed mid-frame (engine resize, DLSS toggle) leaves
		// dangling RTV / SRV bindings in the still-recording cmd list,
		// which Close() flags as OBJECT_DELETED_WHILE_STILL_IN_USE and the
		// GPU eventually hangs. Backbuffers skip this - the swap chain
		// owns them and our wrapper just drops its non-owning ref.
		if (_ownsResource && _resource)
		{
			device->DeferredRelease(std::move(_resource));
		}
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

	// D3D11 copies are state-neutral; the engine routinely leaves the source
	// RT bound as the OM target across a copy and expects to keep drawing
	// into it (e.g. TAA::Resolve does SetRenderTarget(rt) -> Draw -> rt.CopyTo
	// -> next-frame draws still bound to rt). D3D12 requires us to transition
	// the resource back to its bound role so the next draw doesn't fail
	// INVALID_SUBRESOURCE_STATE.
	device->RestoreBoundRoleIfNeeded(this);
	device->RestoreBoundRoleIfNeeded(dst);
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
