
#include "GraphicsDeviceD3D12.hpp"
#include "FormatsD3D12.hpp"
#include "ShaderStageD3D12.hpp"
#include "TextureImporterD3D12.hpp"
#include <HexEngine.Core/HexEngine.hpp>
#include <HexEngine.Core/Graphics/Window.hpp>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// Helpers (file-local)
// ---------------------------------------------------------------------------

namespace
{
	D3D12_HEAP_PROPERTIES MakeHeapProps(D3D12_HEAP_TYPE type)
	{
		D3D12_HEAP_PROPERTIES p = {};
		p.Type                 = type;
		p.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		p.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		p.CreationNodeMask     = 0;
		p.VisibleNodeMask      = 0;
		return p;
	}

	D3D12_RESOURCE_DESC MakeBufferDesc(uint64_t bytes, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
	{
		D3D12_RESOURCE_DESC d = {};
		d.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
		d.Alignment          = 0;
		d.Width              = bytes;
		d.Height             = 1;
		d.DepthOrArraySize   = 1;
		d.MipLevels          = 1;
		d.Format             = DXGI_FORMAT_UNKNOWN;
		d.SampleDesc.Count   = 1;
		d.SampleDesc.Quality = 0;
		d.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		d.Flags              = flags;
		return d;
	}

	D3D12_RESOURCE_DESC MakeTex2DDesc(const HexEngine::TextureDesc& src)
	{
		D3D12_RESOURCE_DESC d = {};
		d.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		d.Width              = (UINT64)src.width;
		d.Height             = (UINT)src.height;
		d.DepthOrArraySize   = (UINT16)src.arraySize;
		d.MipLevels          = (UINT16)(src.mipLevels <= 0 ? 1 : src.mipLevels);
		d.Format             = HexEngine::ToDXGI12(src.format);
		d.SampleDesc.Count   = (UINT)(src.sampleCount <= 0 ? 1 : src.sampleCount);
		d.SampleDesc.Quality = (UINT)src.sampleQuality;
		d.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		d.Flags              = HexEngine::ToD3D12ResourceFlags(src.bindFlags);
		return d;
	}

	D3D12_RESOURCE_DESC MakeTex3DDesc(const HexEngine::TextureDesc& src)
	{
		D3D12_RESOURCE_DESC d = {};
		d.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
		d.Width              = (UINT64)src.width;
		d.Height             = (UINT)src.height;
		d.DepthOrArraySize   = (UINT16)src.depth;
		d.MipLevels          = (UINT16)(src.mipLevels <= 0 ? 1 : src.mipLevels);
		d.Format             = HexEngine::ToDXGI12(src.format);
		d.SampleDesc.Count   = 1;
		d.SampleDesc.Quality = 0;
		d.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		d.Flags              = HexEngine::ToD3D12ResourceFlags(src.bindFlags);
		return d;
	}

	// D3D12 constant buffer size must be a 256-byte multiple.
	constexpr uint32_t kCbAlignment = 256;
	uint32_t AlignCbSize(uint32_t bytes)
	{
		return (bytes + kCbAlignment - 1) & ~(kCbAlignment - 1);
	}
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool GraphicsDeviceD3D12::Create()
{
	if (!CreateDeviceAndQueue())
		return false;

	for (uint32_t i = 0; i < kFrameCount; ++i)
	{
		HRESULT hr = _device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_frames[i].alloc));
		if (FAILED(hr)) { LOG_CRIT("D3D12: CreateCommandAllocator[%u] failed (0x%X)", i, hr); return false; }
	}

	HRESULT hr = _device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _frames[0].alloc.Get(), nullptr, IID_PPV_ARGS(&_cmdList));
	if (FAILED(hr)) { LOG_CRIT("D3D12: CreateCommandList failed (0x%X)", hr); return false; }
	_cmdList->Close();

	hr = _device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence));
	if (FAILED(hr)) { LOG_CRIT("D3D12: CreateFence failed (0x%X)", hr); return false; }

	_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (_fenceEvent == nullptr) { LOG_CRIT("D3D12: CreateEventW failed (gle=%lu)", GetLastError()); return false; }

	// Descriptor heaps. Sizes are generous defaults sufficient for HexEngine's
	// current scene complexity; bump if you hit "out of slots" warnings.
	if (!_rtvHeap.Create(_device.Get(),       D3D12_DESCRIPTOR_HEAP_TYPE_RTV,         256, /*shaderVisible=*/false))   return false;
	if (!_dsvHeap.Create(_device.Get(),       D3D12_DESCRIPTOR_HEAP_TYPE_DSV,         64,  /*shaderVisible=*/false))   return false;
	if (!_cbvSrvUavHeap.Create(_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8192,/*shaderVisible=*/false))   return false;

	// Texture file loader (png/jpg/dds/...). Registers itself with the
	// ResourceSystem in its ctor. Must come AFTER descriptor heaps so its
	// CreateTexture2D path has somewhere to allocate SRVs from.
	_textureLoader = new TextureImporterD3D12();

	// B4: universal root sig + PSO cache + shader-visible heap for draws.
	if (!_rootSig.Create(_device.Get())) { LOG_CRIT("D3D12: root signature creation failed"); return false; }
	_psoCache.Create(_device.Get(), _rootSig.Get());
	if (!_shaderVisibleHeap.Create(_device.Get())) { LOG_CRIT("D3D12: shader-visible heap creation failed"); return false; }

	LOG_INFO("HexEngine.D3D12Plugin: device + queue + %u-frame ring + descriptor heaps + texture loader + B4 PSO/RootSig ready", kFrameCount);
	return true;
}

bool GraphicsDeviceD3D12::CreateDeviceAndQueue()
{
	UINT dxgiFlags = 0;
#ifdef _DEBUG
	{
		ComPtr<ID3D12Debug> debug;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
		{
			debug->EnableDebugLayer();
			dxgiFlags |= DXGI_CREATE_FACTORY_DEBUG;
			LOG_INFO("D3D12: debug layer enabled");
		}
	}
#endif

	HRESULT hr = CreateDXGIFactory2(dxgiFlags, IID_PPV_ARGS(&_dxgiFactory));
	if (FAILED(hr)) { LOG_CRIT("D3D12: CreateDXGIFactory2 failed (0x%X)", hr); return false; }

	for (UINT i = 0; ; ++i)
	{
		ComPtr<IDXGIAdapter1> adapter;
		if (_dxgiFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) == DXGI_ERROR_NOT_FOUND)
			break;

		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);
		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

		if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&_device))))
		{
			_dxgiAdapter = adapter;
			char name[128];
			WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
			LOG_INFO("D3D12: using adapter '%s' (%llu MB VRAM)", name, (uint64_t)desc.DedicatedVideoMemory / (1024 * 1024));
			break;
		}
	}

	if (!_device) { LOG_CRIT("D3D12: no D3D12-capable hardware adapter found"); return false; }

	D3D12_COMMAND_QUEUE_DESC qDesc = {};
	qDesc.Type     = D3D12_COMMAND_LIST_TYPE_DIRECT;
	qDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	qDesc.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
	hr = _device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&_directQueue));
	if (FAILED(hr)) { LOG_CRIT("D3D12: CreateCommandQueue(DIRECT) failed (0x%X)", hr); return false; }
	return true;
}

void GraphicsDeviceD3D12::Destroy()
{
	if (_device && _directQueue && _fence && _fenceEvent) WaitForGpu();

	if (_textureLoader) { delete _textureLoader; _textureLoader = nullptr; }
	_shaderVisibleHeap.Destroy();
	_psoCache.Destroy();
	_rootSig.Destroy();
	_pendingUploads.clear();
	_windowCtx.clear();
	_cbvSrvUavHeap.Destroy();
	_dsvHeap.Destroy();
	_rtvHeap.Destroy();
	_cmdList.Reset();
	for (auto& f : _frames) { f.alloc.Reset(); f.fenceValue = 0; }
	_fence.Reset();
	if (_fenceEvent) { CloseHandle(_fenceEvent); _fenceEvent = nullptr; }
	_directQueue.Reset();
	_device.Reset();
	_dxgiAdapter.Reset();
	_dxgiFactory.Reset();
}

void GraphicsDeviceD3D12::WaitForGpu()
{
	const uint64_t signalValue = _nextFenceValue++;
	if (FAILED(_directQueue->Signal(_fence.Get(), signalValue))) return;
	if (_fence->GetCompletedValue() < signalValue)
	{
		if (SUCCEEDED(_fence->SetEventOnCompletion(signalValue, _fenceEvent)))
			WaitForSingleObject(_fenceEvent, INFINITE);
	}
}

void GraphicsDeviceD3D12::FlushPendingUploads()
{
	const uint64_t completed = _fence->GetCompletedValue();
	while (!_pendingUploads.empty() && _pendingUploads.front().fenceValue <= completed)
		_pendingUploads.pop_front();
}

// ---------------------------------------------------------------------------
// Swap chain attach + resize
// ---------------------------------------------------------------------------

bool GraphicsDeviceD3D12::AttachToWindow(HexEngine::Window* window)
{
	if (window == nullptr) return false;

	WindowContext& ctx = _windowCtx[window];

	RECT rc{};
	GetClientRect(window->GetHandle(), &rc);
	ctx.width  = std::max<LONG>(1, rc.right  - rc.left);
	ctx.height = std::max<LONG>(1, rc.bottom - rc.top);

	DXGI_SWAP_CHAIN_DESC1 desc = {};
	desc.Width            = ctx.width;
	desc.Height           = ctx.height;
	desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.BufferCount      = kFrameCount;
	desc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	desc.AlphaMode        = DXGI_ALPHA_MODE_IGNORE;
	desc.Scaling          = DXGI_SCALING_STRETCH;

	ComPtr<IDXGISwapChain1> sc1;
	HRESULT hr = _dxgiFactory->CreateSwapChainForHwnd(_directQueue.Get(), window->GetHandle(), &desc, nullptr, nullptr, &sc1);
	if (FAILED(hr)) { LOG_CRIT("D3D12: CreateSwapChainForHwnd failed (0x%X)", hr); return false; }
	if (FAILED(sc1.As(&ctx.swapChain))) return false;

	_dxgiFactory->MakeWindowAssociation(window->GetHandle(), DXGI_MWA_NO_ALT_ENTER);

	// Wrap each backbuffer with a Texture2DD3D12 + grab an RTV slot from the heap.
	for (uint32_t i = 0; i < kFrameCount; ++i)
	{
		Texture2DD3D12& bb = ctx.backbuffers[i];
		hr = ctx.swapChain->GetBuffer(i, IID_PPV_ARGS(&bb._resource));
		if (FAILED(hr)) { LOG_CRIT("D3D12: SwapChain::GetBuffer[%u] failed (0x%X)", i, hr); return false; }

		bb._rtv = _rtvHeap.Allocate(bb._rtvIndex);
		_device->CreateRenderTargetView(bb._resource.Get(), nullptr, bb._rtv);
		bb._format         = desc.Format;
		bb._width          = (int32_t)ctx.width;
		bb._height         = (int32_t)ctx.height;
		bb._currentState   = D3D12_RESOURCE_STATE_PRESENT;
		bb._ownsResource   = false;

		char dbgName[64];
		_snprintf_s(dbgName, sizeof(dbgName), "D3D12Backbuffer[%u]", i);
		bb.SetDebugName(dbgName);
	}

	ctx.currentFrameIndex = ctx.swapChain->GetCurrentBackBufferIndex();
	_activeWindow = &ctx;
	_backBufferViewport = HexEngine::Viewport(0.0f, 0.0f, (float)ctx.width, (float)ctx.height);

	LOG_INFO("D3D12: swap chain attached (%ux%u, %u backbuffers)", ctx.width, ctx.height, kFrameCount);
	return true;
}

void GraphicsDeviceD3D12::Resize(HexEngine::Window* window, uint32_t width, uint32_t height)
{
	auto it = _windowCtx.find(window);
	if (it == _windowCtx.end() || width == 0 || height == 0) return;

	WindowContext& ctx = it->second;
	if (ctx.width == width && ctx.height == height) return;

	WaitForGpu();

	// Release backbuffer COM refs + free RTV slots before ResizeBuffers.
	for (uint32_t i = 0; i < kFrameCount; ++i)
	{
		Texture2DD3D12& bb = ctx.backbuffers[i];
		bb._resource.Reset();
		if (bb._rtvIndex != UINT32_MAX) { _rtvHeap.Free(bb._rtvIndex); bb._rtvIndex = UINT32_MAX; }
	}

	HRESULT hr = ctx.swapChain->ResizeBuffers(kFrameCount, width, height, DXGI_FORMAT_UNKNOWN, 0);
	if (FAILED(hr)) { LOG_CRIT("D3D12: SwapChain::ResizeBuffers failed (0x%X)", hr); return; }

	ctx.width  = width;
	ctx.height = height;

	for (uint32_t i = 0; i < kFrameCount; ++i)
	{
		Texture2DD3D12& bb = ctx.backbuffers[i];
		ctx.swapChain->GetBuffer(i, IID_PPV_ARGS(&bb._resource));
		bb._rtv = _rtvHeap.Allocate(bb._rtvIndex);
		_device->CreateRenderTargetView(bb._resource.Get(), nullptr, bb._rtv);
		bb._width        = (int32_t)width;
		bb._height       = (int32_t)height;
		bb._currentState = D3D12_RESOURCE_STATE_PRESENT;
	}

	ctx.currentFrameIndex = ctx.swapChain->GetCurrentBackBufferIndex();
	_backBufferViewport = HexEngine::Viewport(0.0f, 0.0f, (float)width, (float)height);
}

// ---------------------------------------------------------------------------
// Frame submission
// ---------------------------------------------------------------------------

void GraphicsDeviceD3D12::BeginFrame(HexEngine::Window* window, HexEngine::ITexture2D* /*depthBuffer*/)
{
	auto it = _windowCtx.find(window);
	if (it == _windowCtx.end()) return;
	WindowContext& ctx = it->second;
	_activeWindow = &ctx;

	const uint32_t frameIdx = ctx.currentFrameIndex;
	FrameContext& frame = _frames[frameIdx];

	if (_fence->GetCompletedValue() < frame.fenceValue)
	{
		if (SUCCEEDED(_fence->SetEventOnCompletion(frame.fenceValue, _fenceEvent)))
			WaitForSingleObject(_fenceEvent, INFINITE);
	}

	FlushPendingUploads();

	frame.alloc->Reset();
	_cmdList->Reset(frame.alloc.Get(), nullptr);

	// Reset shader-visible heap bump pointer + pending-state bookkeeping for
	// the new frame.
	_shaderVisibleHeap.BeginFrame();
	ResetPendingForBeginFrame();

	Texture2DD3D12& bb = ctx.backbuffers[frameIdx];
	TransitionResource(&bb, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const float clear[4] = { _clearColour.R(), _clearColour.G(), _clearColour.B(), _clearColour.A() };
	_cmdList->ClearRenderTargetView(bb._rtv, clear, 0, nullptr);
	_cmdList->OMSetRenderTargets(1, &bb._rtv, FALSE, nullptr);

	D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)ctx.width, (float)ctx.height, 0.0f, 1.0f };
	_cmdList->RSSetViewports(1, &vp);
	D3D12_RECT sr = { 0, 0, (LONG)ctx.width, (LONG)ctx.height };
	_cmdList->RSSetScissorRects(1, &sr);
}

void GraphicsDeviceD3D12::EndFrame(HexEngine::Window* window)
{
	auto it = _windowCtx.find(window);
	if (it == _windowCtx.end()) return;
	WindowContext& ctx = it->second;

	const uint32_t frameIdx = ctx.currentFrameIndex;
	Texture2DD3D12& bb = ctx.backbuffers[frameIdx];

	TransitionResource(&bb, D3D12_RESOURCE_STATE_PRESENT);
	_cmdList->Close();

	ID3D12CommandList* lists[] = { _cmdList.Get() };
	_directQueue->ExecuteCommandLists(1, lists);

	const uint64_t signalValue = _nextFenceValue++;
	_directQueue->Signal(_fence.Get(), signalValue);
	_frames[frameIdx].fenceValue = signalValue;

	// Tag the upload buffers issued this frame with the frame's fence value
	// so FlushPendingUploads can retire them once the GPU's done.
	for (auto it2 = _pendingUploads.rbegin(); it2 != _pendingUploads.rend(); ++it2)
	{
		if (it2->fenceValue != 0) break;
		it2->fenceValue = signalValue;
	}

	ctx.swapChain->Present(0, 0);
	ctx.currentFrameIndex = ctx.swapChain->GetCurrentBackBufferIndex();
}

// ---------------------------------------------------------------------------
// Misc accessors
// ---------------------------------------------------------------------------

HexEngine::ITexture2D* GraphicsDeviceD3D12::GetBackBuffer(HexEngine::Window* window)
{
	WindowContext* ctx = _activeWindow;
	if (window != nullptr)
	{
		auto it = _windowCtx.find(window);
		if (it != _windowCtx.end()) ctx = &it->second;
	}
	if (ctx == nullptr) return nullptr;
	return &ctx->backbuffers[ctx->currentFrameIndex];
}

HexEngine::IResourceLoader* GraphicsDeviceD3D12::GetTextureLoader()
{
	return _textureLoader;
}

void GraphicsDeviceD3D12::GetBackBufferDimensions(uint32_t& width, uint32_t& height)
{
	if (_activeWindow != nullptr) { width = _activeWindow->width; height = _activeWindow->height; }
	else                          { width = 0; height = 0; }
}

void GraphicsDeviceD3D12::SetRenderTarget(HexEngine::ITexture2D* renderTarget, HexEngine::ITexture2D* depthStencil)
{
	if (_cmdList == nullptr) return;
	auto* rt = static_cast<Texture2DD3D12*>(renderTarget);
	auto* ds = static_cast<Texture2DD3D12*>(depthStencil);

	// Track on _pending so FlushGraphics can build a PSO whose RT/DS formats
	// match what's actually bound. Without this, PSO format-validation rejects
	// the draw.
	_pending.rtCount = (rt != nullptr) ? 1u : 0u;
	_pending.rtvs[0] = rt;
	_pending.dsv     = ds;
	_pending.dirty = true;

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = {};
	if (rt != nullptr)
	{
		TransitionResource(rt, D3D12_RESOURCE_STATE_RENDER_TARGET);
		rtvHandle = rt->_rtv;
	}
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};
	D3D12_CPU_DESCRIPTOR_HANDLE* dsvPtr = nullptr;
	if (ds != nullptr)
	{
		TransitionResource(ds, D3D12_RESOURCE_STATE_DEPTH_WRITE);
		dsvHandle = ds->_dsv;
		dsvPtr    = &dsvHandle;
	}
	_cmdList->OMSetRenderTargets(rt != nullptr ? 1 : 0, rt != nullptr ? &rtvHandle : nullptr, FALSE, dsvPtr);
}

// ---------------------------------------------------------------------------
// Resource state transitions
// ---------------------------------------------------------------------------

void GraphicsDeviceD3D12::TransitionResource(Texture2DD3D12* tex, D3D12_RESOURCE_STATES newState)
{
	if (tex == nullptr || tex->_resource == nullptr || _cmdList == nullptr) return;
	if (tex->_currentState == newState) return;
	D3D12_RESOURCE_BARRIER b = {};
	b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Transition.pResource   = tex->_resource.Get();
	b.Transition.StateBefore = tex->_currentState;
	b.Transition.StateAfter  = newState;
	b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	_cmdList->ResourceBarrier(1, &b);
	tex->_currentState = newState;
}

void GraphicsDeviceD3D12::TransitionResource(Texture3DD3D12* tex, D3D12_RESOURCE_STATES newState)
{
	if (tex == nullptr || tex->_resource == nullptr || _cmdList == nullptr) return;
	if (tex->_currentState == newState) return;
	D3D12_RESOURCE_BARRIER b = {};
	b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Transition.pResource   = tex->_resource.Get();
	b.Transition.StateBefore = tex->_currentState;
	b.Transition.StateAfter  = newState;
	b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	_cmdList->ResourceBarrier(1, &b);
	tex->_currentState = newState;
}

void GraphicsDeviceD3D12::TransitionResource(StructuredBufferD3D12* buf, D3D12_RESOURCE_STATES newState)
{
	if (buf == nullptr || buf->_resource == nullptr || _cmdList == nullptr) return;
	if (buf->_currentState == newState) return;
	D3D12_RESOURCE_BARRIER b = {};
	b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Transition.pResource   = buf->_resource.Get();
	b.Transition.StateBefore = buf->_currentState;
	b.Transition.StateAfter  = newState;
	b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	_cmdList->ResourceBarrier(1, &b);
	buf->_currentState = newState;
}

// ---------------------------------------------------------------------------
// Resource creation
// ---------------------------------------------------------------------------

HexEngine::ITexture2D* GraphicsDeviceD3D12::CreateTexture(HexEngine::ITexture2D* clone)
{
	if (clone == nullptr) return nullptr;
	HexEngine::TextureDesc d;
	d.width      = clone->GetWidth();
	d.height     = clone->GetHeight();
	d.format     = HexEngine::TextureFormat::R8G8B8A8_UNORM; // best-effort; format reflection not exposed by clone path
	d.bindFlags  = HexEngine::BindFlags::ShaderResource;
	d.arraySize  = 1;
	d.mipLevels  = 1;
	d.dimension  = HexEngine::ResourceDimension::Texture2D;
	return CreateTexture2D(d, nullptr);
}

HexEngine::ITexture2D* GraphicsDeviceD3D12::CreateTexture2D(const HexEngine::TextureDesc& d, const HexEngine::SubresourceData* initialData)
{
	if (d.width <= 0 || d.height <= 0)
	{
		LOG_CRIT("D3D12: CreateTexture2D called with invalid dimensions (%dx%d)", d.width, d.height);
		return nullptr;
	}

	D3D12_RESOURCE_DESC desc = MakeTex2DDesc(d);
	D3D12_HEAP_PROPERTIES heap = MakeHeapProps(D3D12_HEAP_TYPE_DEFAULT);

	// Build the optimised clear value for RT / DS textures - the runtime
	// fast-paths clears that match this.
	D3D12_CLEAR_VALUE clearValue = {};
	const D3D12_CLEAR_VALUE* clearPtr = nullptr;
	if (HexEngine::HasAny(d.bindFlags, HexEngine::BindFlags::DepthStencil))
	{
		clearValue.Format               = HexEngine::GetDsvFormatD3D12(desc.Format);
		clearValue.DepthStencil.Depth   = 1.0f;
		clearValue.DepthStencil.Stencil = 0;
		clearPtr = &clearValue;
	}
	else if (HexEngine::HasAny(d.bindFlags, HexEngine::BindFlags::RenderTarget))
	{
		clearValue.Format    = desc.Format;
		clearValue.Color[0]  = 0.0f;
		clearValue.Color[1]  = 0.0f;
		clearValue.Color[2]  = 0.0f;
		clearValue.Color[3]  = 0.0f;
		clearPtr = &clearValue;
	}

	const D3D12_RESOURCE_STATES initialState = HexEngine::InitialStateFromBindFlags(d.bindFlags, d.usage);

	auto* tex = new Texture2DD3D12();
	HRESULT hr = _device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, initialState, clearPtr, IID_PPV_ARGS(&tex->_resource));
	if (FAILED(hr))
	{
		LOG_CRIT("D3D12: CreateCommittedResource(Tex2D %dx%d fmt=%u) failed (0x%X)", d.width, d.height, (uint32_t)desc.Format, hr);
		delete tex;
		return nullptr;
	}
	tex->_format       = desc.Format;
	tex->_width        = d.width;
	tex->_height       = d.height;
	tex->_arraySize    = d.arraySize;
	tex->_sampleCount  = (int32_t)desc.SampleDesc.Count;
	tex->_mipLevels    = desc.MipLevels;
	tex->_currentState = initialState;
	tex->_ownsResource = true;

	// SRV
	if (HexEngine::HasAny(d.bindFlags, HexEngine::BindFlags::ShaderResource))
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
		srv.Format                  = HexEngine::GetSrvFormatD3D12(desc.Format);
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.ViewDimension           = HexEngine::SrvDimD3D12(d.dimension, d.arraySize, (int32_t)desc.SampleDesc.Count);
		if (srv.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D)
		{
			srv.Texture2D.MipLevels       = desc.MipLevels;
			srv.Texture2D.MostDetailedMip = 0;
		}
		else if (srv.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2DARRAY)
		{
			srv.Texture2DArray.MipLevels = desc.MipLevels;
			srv.Texture2DArray.ArraySize = d.arraySize;
		}
		else if (srv.ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBE)
		{
			srv.TextureCube.MipLevels = desc.MipLevels;
		}
		tex->_srv = _cbvSrvUavHeap.Allocate(tex->_srvIndex);
		_device->CreateShaderResourceView(tex->_resource.Get(), &srv, tex->_srv);
	}

	// RTV
	if (HexEngine::HasAny(d.bindFlags, HexEngine::BindFlags::RenderTarget))
	{
		D3D12_RENDER_TARGET_VIEW_DESC rtv = {};
		rtv.Format        = desc.Format;
		rtv.ViewDimension = (desc.SampleDesc.Count > 1) ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D;
		tex->_rtv = _rtvHeap.Allocate(tex->_rtvIndex);
		_device->CreateRenderTargetView(tex->_resource.Get(), &rtv, tex->_rtv);
	}

	// DSV
	if (HexEngine::HasAny(d.bindFlags, HexEngine::BindFlags::DepthStencil))
	{
		D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
		dsv.Format        = HexEngine::GetDsvFormatD3D12(desc.Format);
		dsv.ViewDimension = (desc.SampleDesc.Count > 1) ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
		tex->_dsv = _dsvHeap.Allocate(tex->_dsvIndex);
		_device->CreateDepthStencilView(tex->_resource.Get(), &dsv, tex->_dsv);
	}

	// UAV
	if (HexEngine::HasAny(d.bindFlags, HexEngine::BindFlags::UnorderedAccess))
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
		uav.Format        = desc.Format;
		uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		tex->_uav = _cbvSrvUavHeap.Allocate(tex->_uavIndex);
		_device->CreateUnorderedAccessView(tex->_resource.Get(), nullptr, &uav, tex->_uav);
	}

	if (initialData != nullptr && initialData->data != nullptr && initialData->slicePitchBytes > 0)
		UploadTextureData(tex, initialData->data, initialData->slicePitchBytes);

	return tex;
}

HexEngine::ITexture3D* GraphicsDeviceD3D12::CreateTexture3D(const HexEngine::TextureDesc& d, const HexEngine::SubresourceData* initialData)
{
	if (d.width <= 0 || d.height <= 0 || d.depth <= 0) return nullptr;

	D3D12_RESOURCE_DESC desc = MakeTex3DDesc(d);
	D3D12_HEAP_PROPERTIES heap = MakeHeapProps(D3D12_HEAP_TYPE_DEFAULT);
	const D3D12_RESOURCE_STATES initialState = HexEngine::InitialStateFromBindFlags(d.bindFlags, d.usage);

	auto* tex = new Texture3DD3D12();
	HRESULT hr = _device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, initialState, nullptr, IID_PPV_ARGS(&tex->_resource));
	if (FAILED(hr))
	{
		LOG_CRIT("D3D12: CreateCommittedResource(Tex3D %dx%dx%d) failed (0x%X)", d.width, d.height, d.depth, hr);
		delete tex;
		return nullptr;
	}
	tex->_format       = desc.Format;
	tex->_width        = d.width;
	tex->_height       = d.height;
	tex->_depth        = d.depth;
	tex->_mipLevels    = desc.MipLevels;
	tex->_currentState = initialState;

	if (HexEngine::HasAny(d.bindFlags, HexEngine::BindFlags::ShaderResource))
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
		srv.Format                  = HexEngine::GetSrvFormatD3D12(desc.Format);
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE3D;
		srv.Texture3D.MipLevels     = desc.MipLevels;
		tex->_srv = _cbvSrvUavHeap.Allocate(tex->_srvIndex);
		_device->CreateShaderResourceView(tex->_resource.Get(), &srv, tex->_srv);
	}
	if (HexEngine::HasAny(d.bindFlags, HexEngine::BindFlags::RenderTarget))
	{
		D3D12_RENDER_TARGET_VIEW_DESC rtv = {};
		rtv.Format             = desc.Format;
		rtv.ViewDimension      = D3D12_RTV_DIMENSION_TEXTURE3D;
		rtv.Texture3D.WSize    = d.depth;
		tex->_rtv = _rtvHeap.Allocate(tex->_rtvIndex);
		_device->CreateRenderTargetView(tex->_resource.Get(), &rtv, tex->_rtv);
	}
	if (HexEngine::HasAny(d.bindFlags, HexEngine::BindFlags::UnorderedAccess))
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
		uav.Format            = desc.Format;
		uav.ViewDimension     = D3D12_UAV_DIMENSION_TEXTURE3D;
		uav.Texture3D.WSize   = d.depth;
		tex->_uav = _cbvSrvUavHeap.Allocate(tex->_uavIndex);
		_device->CreateUnorderedAccessView(tex->_resource.Get(), nullptr, &uav, tex->_uav);
	}

	if (initialData != nullptr && initialData->data != nullptr && initialData->slicePitchBytes > 0)
		UploadTextureData(tex, initialData->data, initialData->slicePitchBytes * d.depth);

	return tex;
}

// ---------------------------------------------------------------------------
// Buffer creation
// ---------------------------------------------------------------------------

HexEngine::IVertexBuffer* GraphicsDeviceD3D12::CreateVertexBuffer(const HexEngine::BufferDesc& d, const void* initialData)
{
	if (d.byteWidth == 0 || d.byteStride == 0) return nullptr;

	D3D12_RESOURCE_DESC desc = MakeBufferDesc(d.byteWidth);
	D3D12_HEAP_PROPERTIES heap = MakeHeapProps(D3D12_HEAP_TYPE_UPLOAD);
	auto* vb = new VertexBufferD3D12();
	HRESULT hr = _device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vb->_resource));
	if (FAILED(hr)) { LOG_CRIT("D3D12: CreateCommittedResource(VB %u bytes) failed (0x%X)", d.byteWidth, hr); delete vb; return nullptr; }

	vb->_byteSize = d.byteWidth;
	vb->_stride   = d.byteStride;
	vb->_view.BufferLocation = vb->_resource->GetGPUVirtualAddress();
	vb->_view.SizeInBytes    = d.byteWidth;
	vb->_view.StrideInBytes  = d.byteStride;

	D3D12_RANGE noRead = { 0, 0 };
	vb->_resource->Map(0, &noRead, &vb->_mapped);
	if (initialData != nullptr && vb->_mapped != nullptr)
		memcpy(vb->_mapped, initialData, d.byteWidth);
	return vb;
}

HexEngine::IIndexBuffer* GraphicsDeviceD3D12::CreateIndexBuffer(const HexEngine::BufferDesc& d, const void* initialData)
{
	if (d.byteWidth == 0) return nullptr;

	D3D12_RESOURCE_DESC desc = MakeBufferDesc(d.byteWidth);
	D3D12_HEAP_PROPERTIES heap = MakeHeapProps(D3D12_HEAP_TYPE_UPLOAD);
	auto* ib = new IndexBufferD3D12();
	HRESULT hr = _device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&ib->_resource));
	if (FAILED(hr)) { LOG_CRIT("D3D12: CreateCommittedResource(IB %u bytes) failed (0x%X)", d.byteWidth, hr); delete ib; return nullptr; }

	const uint32_t stride = d.byteStride > 0 ? d.byteStride : 4u;
	ib->_byteSize = d.byteWidth;
	ib->_stride   = stride;
	ib->_view.BufferLocation = ib->_resource->GetGPUVirtualAddress();
	ib->_view.SizeInBytes    = d.byteWidth;
	ib->_view.Format         = (stride == 2) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;

	D3D12_RANGE noRead = { 0, 0 };
	ib->_resource->Map(0, &noRead, &ib->_mapped);
	if (initialData != nullptr && ib->_mapped != nullptr)
		memcpy(ib->_mapped, initialData, d.byteWidth);
	return ib;
}

HexEngine::IConstantBuffer* GraphicsDeviceD3D12::CreateConstantBuffer(uint32_t size)
{
	if (size == 0) return nullptr;
	const uint32_t aligned = AlignCbSize(size);

	D3D12_RESOURCE_DESC desc = MakeBufferDesc(aligned);
	D3D12_HEAP_PROPERTIES heap = MakeHeapProps(D3D12_HEAP_TYPE_UPLOAD);
	auto* cb = new ConstantBufferD3D12();
	HRESULT hr = _device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&cb->_resource));
	if (FAILED(hr)) { LOG_CRIT("D3D12: CreateCommittedResource(CB %u bytes) failed (0x%X)", aligned, hr); delete cb; return nullptr; }

	cb->_logicalSize = size;
	cb->_byteSize    = aligned;
	D3D12_RANGE noRead = { 0, 0 };
	cb->_resource->Map(0, &noRead, &cb->_mapped);

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbv = {};
	cbv.BufferLocation = cb->_resource->GetGPUVirtualAddress();
	cbv.SizeInBytes    = aligned;
	cb->_cbv = _cbvSrvUavHeap.Allocate(cb->_cbvIndex);
	_device->CreateConstantBufferView(&cbv, cb->_cbv);
	return cb;
}

HexEngine::IStructuredBuffer* GraphicsDeviceD3D12::CreateStructuredBuffer(
	uint32_t elementStride, uint32_t elementCount,
	HexEngine::StructuredBufferFlags flags,
	HexEngine::ResourceUsage usage,
	HexEngine::CpuAccess cpuAccess,
	const void* initialData)
{
	if (elementStride == 0 || elementCount == 0) return nullptr;
	const uint64_t total = (uint64_t)elementStride * elementCount;

	const bool wantsUav = HEX_HASFLAG(flags, HexEngine::StructuredBufferFlags::UnorderedAccess);

	D3D12_RESOURCE_FLAGS rflags = D3D12_RESOURCE_FLAG_NONE;
	if (wantsUav) rflags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	D3D12_RESOURCE_DESC desc = MakeBufferDesc(total, rflags);

	// Upload heap when caller wants CPU writes AND no UAV (UAV requires
	// default heap). Default heap everywhere else; SetData goes through the
	// stage-and-copy path on default-heap buffers.
	const bool useUpload = (HEX_HASFLAG(cpuAccess, HexEngine::CpuAccess::Write)) && !wantsUav;
	D3D12_HEAP_PROPERTIES heap = MakeHeapProps(useUpload ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT);
	const D3D12_RESOURCE_STATES initialState = useUpload ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COMMON;

	auto* sb = new StructuredBufferD3D12();
	HRESULT hr = _device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, initialState, nullptr, IID_PPV_ARGS(&sb->_resource));
	if (FAILED(hr)) { LOG_CRIT("D3D12: CreateCommittedResource(SB %llu bytes) failed (0x%X)", total, hr); delete sb; return nullptr; }

	sb->_elementStride = elementStride;
	sb->_elementCount  = elementCount;
	sb->_flags         = flags;
	sb->_currentState  = initialState;
	sb->_isUploadHeap  = useUpload;

	if (useUpload)
	{
		D3D12_RANGE noRead = { 0, 0 };
		sb->_resource->Map(0, &noRead, &sb->_mapped);
		if (initialData != nullptr && sb->_mapped != nullptr)
			memcpy(sb->_mapped, initialData, total);
	}

	if (HEX_HASFLAG(flags, HexEngine::StructuredBufferFlags::ShaderResource))
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
		srv.Format                  = DXGI_FORMAT_UNKNOWN;
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.ViewDimension           = D3D12_SRV_DIMENSION_BUFFER;
		srv.Buffer.FirstElement     = 0;
		srv.Buffer.NumElements      = elementCount;
		srv.Buffer.StructureByteStride = elementStride;
		sb->_srv = _cbvSrvUavHeap.Allocate(sb->_srvIndex);
		_device->CreateShaderResourceView(sb->_resource.Get(), &srv, sb->_srv);
	}
	if (wantsUav)
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
		uav.Format                  = DXGI_FORMAT_UNKNOWN;
		uav.ViewDimension           = D3D12_UAV_DIMENSION_BUFFER;
		uav.Buffer.FirstElement     = 0;
		uav.Buffer.NumElements      = elementCount;
		uav.Buffer.StructureByteStride = elementStride;
		if (HEX_HASFLAG(flags, HexEngine::StructuredBufferFlags::AppendConsume))
		{
			// Append/consume buffers need a 4-byte counter resource.
			D3D12_RESOURCE_DESC counterDesc = MakeBufferDesc(4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			D3D12_HEAP_PROPERTIES counterHeap = MakeHeapProps(D3D12_HEAP_TYPE_DEFAULT);
			_device->CreateCommittedResource(&counterHeap, D3D12_HEAP_FLAG_NONE, &counterDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&sb->_counterResource));
			uav.Buffer.CounterOffsetInBytes = 0;
		}
		sb->_uav = _cbvSrvUavHeap.Allocate(sb->_uavIndex);
		_device->CreateUnorderedAccessView(sb->_resource.Get(), sb->_counterResource.Get(), &uav, sb->_uav);
	}

	if (!useUpload && initialData != nullptr)
		UploadBufferData(sb, initialData, (uint32_t)total, 0);

	return sb;
}

HexEngine::IConstantBuffer* GraphicsDeviceD3D12::GetEngineConstantBuffer(HexEngine::EngineConstantBuffer)
{
	// B5 wiring; for now the engine creates its own CBs via CreateConstantBuffer.
	return nullptr;
}

// ---------------------------------------------------------------------------
// Shader / input-layout creation (just bytecode container objects for B3;
// PSO assembly lives in B4)
// ---------------------------------------------------------------------------

HexEngine::IShaderStage* GraphicsDeviceD3D12::CreateVertexShader(std::vector<uint8_t>& code)
{
	// One-shot diagnostic: log the first VS that lands and what its bytes look
	// like. DXIL bytecode starts with "DXBC" (yes, ironic - DXIL containers
	// reuse the DXBC container header), then a specific subblob layout for
	// SM 6.x. If the bytes start with anything else, the ShaderSystem v2
	// loader didn't actually pick the DXIL blob.
	static bool warnedFirstVS = false;
	if (!warnedFirstVS && !code.empty())
	{
		warnedFirstVS = true;
		char fourcc[5] = {};
		if (code.size() >= 4) memcpy(fourcc, code.data(), 4);
		LOG_INFO("D3D12 CreateVertexShader: first VS received, %llu bytes, fourcc='%s' (expect 'DXBC' for both DXBC SM5 and DXIL SM6 containers)",
			(uint64_t)code.size(), fourcc);
	}
	auto* s = new ShaderStageD3D12();
	s->_bytecode = code;
	return s;
}
HexEngine::IShaderStage* GraphicsDeviceD3D12::CreatePixelShader(std::vector<uint8_t>& code)    { auto* s = new ShaderStageD3D12(); s->_bytecode = code; return s; }
HexEngine::IShaderStage* GraphicsDeviceD3D12::CreateGeometryShader(std::vector<uint8_t>& code) { auto* s = new ShaderStageD3D12(); s->_bytecode = code; return s; }
HexEngine::IShaderStage* GraphicsDeviceD3D12::CreateComputeShader(std::vector<uint8_t>& code)  { auto* s = new ShaderStageD3D12(); s->_bytecode = code; return s; }

HexEngine::IInputLayout* GraphicsDeviceD3D12::CreateInputLayout(const HexEngine::InputElement* elements, uint32_t numElements, const std::vector<uint8_t>& /*vertexShaderBinary*/)
{
	if (elements == nullptr || numElements == 0) return nullptr;
	auto* layout = new InputLayoutD3D12();
	layout->_semanticNames.reserve(numElements);
	layout->_elements.resize(numElements);
	for (uint32_t i = 0; i < numElements; ++i)
	{
		layout->_semanticNames.push_back(elements[i].semanticName);
		auto& out = layout->_elements[i];
		out.SemanticName         = layout->_semanticNames.back().c_str();
		out.SemanticIndex        = elements[i].semanticIndex;
		out.Format               = HexEngine::ToDXGI12(elements[i].format);
		out.InputSlot            = elements[i].inputSlot;
		out.AlignedByteOffset    = (elements[i].alignedByteOffset == 0xFFFFFFFFu) ? D3D12_APPEND_ALIGNED_ELEMENT : elements[i].alignedByteOffset;
		out.InputSlotClass       = HexEngine::ToD3D12InputClass(elements[i].perInstance);
		out.InstanceDataStepRate = elements[i].instanceDataStepRate;
	}
	return layout;
}

// ---------------------------------------------------------------------------
// Upload paths (used by SetData / SetPixels)
// ---------------------------------------------------------------------------

bool GraphicsDeviceD3D12::UploadBufferData(StructuredBufferD3D12* dst, const void* src, uint32_t byteSize, uint32_t dstByteOffset)
{
	if (dst == nullptr || src == nullptr || byteSize == 0 || _cmdList == nullptr) return false;

	D3D12_HEAP_PROPERTIES uploadHeap = MakeHeapProps(D3D12_HEAP_TYPE_UPLOAD);
	D3D12_RESOURCE_DESC   uploadDesc = MakeBufferDesc(byteSize);

	PendingUpload pending;
	HRESULT hr = _device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&pending.resource));
	if (FAILED(hr)) return false;

	void* mapped = nullptr;
	D3D12_RANGE noRead = { 0, 0 };
	pending.resource->Map(0, &noRead, &mapped);
	memcpy(mapped, src, byteSize);
	pending.resource->Unmap(0, nullptr);

	TransitionResource(dst, D3D12_RESOURCE_STATE_COPY_DEST);
	_cmdList->CopyBufferRegion(dst->_resource.Get(), dstByteOffset, pending.resource.Get(), 0, byteSize);
	_pendingUploads.push_back(std::move(pending)); // fence value stamped at EndFrame
	return true;
}

bool GraphicsDeviceD3D12::UploadTextureData(Texture2DD3D12* dst, const void* src, uint32_t byteSize)
{
	if (dst == nullptr || src == nullptr || byteSize == 0 || _cmdList == nullptr) return false;

	D3D12_RESOURCE_DESC desc = dst->_resource->GetDesc();
	UINT64 uploadSize = 0;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	UINT numRows = 0;
	UINT64 rowBytes = 0;
	_device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &numRows, &rowBytes, &uploadSize);

	D3D12_HEAP_PROPERTIES uploadHeap = MakeHeapProps(D3D12_HEAP_TYPE_UPLOAD);
	D3D12_RESOURCE_DESC   uploadDesc = MakeBufferDesc(uploadSize);

	PendingUpload pending;
	HRESULT hr = _device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&pending.resource));
	if (FAILED(hr)) return false;

	void* mapped = nullptr;
	D3D12_RANGE noRead = { 0, 0 };
	pending.resource->Map(0, &noRead, &mapped);
	// Row-by-row copy: source is densely packed (rowBytes per row), destination
	// has the API's row pitch (footprint.Footprint.RowPitch, typically >=rowBytes).
	auto* d = static_cast<uint8_t*>(mapped) + footprint.Offset;
	auto* s = static_cast<const uint8_t*>(src);
	for (UINT r = 0; r < numRows; ++r)
		memcpy(d + r * footprint.Footprint.RowPitch, s + r * rowBytes, (size_t)rowBytes);
	pending.resource->Unmap(0, nullptr);

	TransitionResource(dst, D3D12_RESOURCE_STATE_COPY_DEST);

	D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
	dstLoc.pResource        = dst->_resource.Get();
	dstLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dstLoc.SubresourceIndex = 0;

	D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
	srcLoc.pResource       = pending.resource.Get();
	srcLoc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	srcLoc.PlacedFootprint = footprint;

	_cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
	_pendingUploads.push_back(std::move(pending));
	return true;
}

bool GraphicsDeviceD3D12::UploadTextureData(Texture3DD3D12* dst, const void* src, uint32_t /*byteSize*/)
{
	if (dst == nullptr || src == nullptr || _cmdList == nullptr) return false;

	D3D12_RESOURCE_DESC desc = dst->_resource->GetDesc();
	UINT64 uploadSize = 0;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	UINT numRows = 0;
	UINT64 rowBytes = 0;
	_device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &numRows, &rowBytes, &uploadSize);

	D3D12_HEAP_PROPERTIES uploadHeap = MakeHeapProps(D3D12_HEAP_TYPE_UPLOAD);
	D3D12_RESOURCE_DESC   uploadDesc = MakeBufferDesc(uploadSize);

	PendingUpload pending;
	if (FAILED(_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&pending.resource))))
		return false;

	void* mapped = nullptr;
	D3D12_RANGE noRead = { 0, 0 };
	pending.resource->Map(0, &noRead, &mapped);
	auto* d = static_cast<uint8_t*>(mapped) + footprint.Offset;
	auto* s = static_cast<const uint8_t*>(src);
	// Tex3D: numRows is per-slice; iterate slices.
	const UINT slices = desc.DepthOrArraySize;
	const UINT slicePitchDst = footprint.Footprint.RowPitch * numRows;
	const UINT slicePitchSrc = (UINT)rowBytes * numRows;
	for (UINT z = 0; z < slices; ++z)
	{
		for (UINT r = 0; r < numRows; ++r)
			memcpy(d + z * slicePitchDst + r * footprint.Footprint.RowPitch,
			       s + z * slicePitchSrc + r * rowBytes,
			       (size_t)rowBytes);
	}
	pending.resource->Unmap(0, nullptr);

	TransitionResource(dst, D3D12_RESOURCE_STATE_COPY_DEST);

	D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
	dstLoc.pResource        = dst->_resource.Get();
	dstLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dstLoc.SubresourceIndex = 0;

	D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
	srcLoc.pResource       = pending.resource.Get();
	srcLoc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	srcLoc.PlacedFootprint = footprint;

	_cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
	_pendingUploads.push_back(std::move(pending));
	return true;
}
