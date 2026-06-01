
#include "GraphicsDeviceD3D12.hpp"
#include <HexEngine.Core/HexEngine.hpp>
#include <HexEngine.Core/Graphics/Window.hpp>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool GraphicsDeviceD3D12::Create()
{
	if (!CreateDeviceAndQueue())
		return false;

	// Per-frame allocators - one per swap-chain frame so we can record the
	// next frame's commands while the previous one is still in flight.
	for (uint32_t i = 0; i < kFrameCount; ++i)
	{
		HRESULT hr = _device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(&_frames[i].alloc));
		if (FAILED(hr))
		{
			LOG_CRIT("D3D12: CreateCommandAllocator[%u] failed (0x%X)", i, hr);
			return false;
		}
	}

	// One reusable command list - we Reset it onto whichever allocator
	// matches the current frame index each BeginFrame.
	HRESULT hr = _device->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		_frames[0].alloc.Get(),
		nullptr,
		IID_PPV_ARGS(&_cmdList));
	if (FAILED(hr))
	{
		LOG_CRIT("D3D12: CreateCommandList failed (0x%X)", hr);
		return false;
	}
	// Lists come out of CreateCommandList in the recording state - close it
	// immediately so BeginFrame can Reset cleanly.
	_cmdList->Close();

	hr = _device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence));
	if (FAILED(hr))
	{
		LOG_CRIT("D3D12: CreateFence failed (0x%X)", hr);
		return false;
	}

	_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (_fenceEvent == nullptr)
	{
		LOG_CRIT("D3D12: CreateEventW failed (gle=%lu)", GetLastError());
		return false;
	}

	LOG_INFO("HexEngine.D3D12Plugin: device + direct queue + %u-frame command ring created", kFrameCount);
	return true;
}

bool GraphicsDeviceD3D12::CreateDeviceAndQueue()
{
	UINT dxgiFlags = 0;
#ifdef _DEBUG
	// Pull in the D3D12 debug layer. Failure here isn't fatal (might not be
	// installed on a clean machine), but the validation output is worth a lot
	// during bring-up - chase down any DEBUG_LAYER unavailable warnings before
	// shipping.
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
	if (FAILED(hr))
	{
		LOG_CRIT("D3D12: CreateDXGIFactory2 failed (0x%X)", hr);
		return false;
	}

	// Prefer the highest-perf hardware adapter that supports FL 11_0+ D3D12.
	for (UINT i = 0; ; ++i)
	{
		ComPtr<IDXGIAdapter1> adapter;
		if (_dxgiFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) == DXGI_ERROR_NOT_FOUND)
			break;

		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);
		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			continue;

		if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&_device))))
		{
			_dxgiAdapter = adapter;
			char name[128];
			WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
			LOG_INFO("D3D12: using adapter '%s' (%llu MB dedicated VRAM)", name, (uint64_t)desc.DedicatedVideoMemory / (1024 * 1024));
			break;
		}
	}

	if (!_device)
	{
		LOG_CRIT("D3D12: no D3D12-capable hardware adapter found");
		return false;
	}

	D3D12_COMMAND_QUEUE_DESC qDesc = {};
	qDesc.Type     = D3D12_COMMAND_LIST_TYPE_DIRECT;
	qDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	qDesc.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
	qDesc.NodeMask = 0;
	hr = _device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&_directQueue));
	if (FAILED(hr))
	{
		LOG_CRIT("D3D12: CreateCommandQueue(DIRECT) failed (0x%X)", hr);
		return false;
	}

	return true;
}

void GraphicsDeviceD3D12::Destroy()
{
	if (_device && _directQueue && _fence && _fenceEvent != nullptr)
		WaitForGpu();

	_windowCtx.clear();
	_cmdList.Reset();
	for (auto& f : _frames)
	{
		f.alloc.Reset();
		f.fenceValue = 0;
	}
	_fence.Reset();
	if (_fenceEvent != nullptr)
	{
		CloseHandle(_fenceEvent);
		_fenceEvent = nullptr;
	}
	_directQueue.Reset();
	_device.Reset();
	_dxgiAdapter.Reset();
	_dxgiFactory.Reset();
}

void GraphicsDeviceD3D12::WaitForGpu()
{
	// Drop a fence value into the queue then block until the GPU reaches it.
	// Conservative - used at Destroy and Resize where we need every prior frame
	// to have completed before we can release backbuffer resources.
	const uint64_t signalValue = _nextFenceValue++;
	if (FAILED(_directQueue->Signal(_fence.Get(), signalValue)))
		return;

	if (_fence->GetCompletedValue() < signalValue)
	{
		if (SUCCEEDED(_fence->SetEventOnCompletion(signalValue, _fenceEvent)))
			WaitForSingleObject(_fenceEvent, INFINITE);
	}
}

// ---------------------------------------------------------------------------
// Swap chain attach + resize
// ---------------------------------------------------------------------------

bool GraphicsDeviceD3D12::AttachToWindow(HexEngine::Window* window)
{
	if (window == nullptr)
		return false;

	WindowContext& ctx = _windowCtx[window];

	RECT rc{};
	GetClientRect(window->GetHandle(), &rc);
	ctx.width  = std::max<LONG>(1, rc.right  - rc.left);
	ctx.height = std::max<LONG>(1, rc.bottom - rc.top);

	DXGI_SWAP_CHAIN_DESC1 desc = {};
	desc.Width            = ctx.width;
	desc.Height           = ctx.height;
	desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM; // Phase B2 SDR-only; B5 picks up HDR scRGB once tonemap lands
	desc.SampleDesc.Count = 1;
	desc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.BufferCount      = kFrameCount;
	desc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	desc.AlphaMode        = DXGI_ALPHA_MODE_IGNORE;
	desc.Scaling          = DXGI_SCALING_STRETCH;
	desc.Flags            = 0;

	ComPtr<IDXGISwapChain1> sc1;
	HRESULT hr = _dxgiFactory->CreateSwapChainForHwnd(
		_directQueue.Get(),
		window->GetHandle(),
		&desc,
		nullptr,
		nullptr,
		&sc1);
	if (FAILED(hr))
	{
		LOG_CRIT("D3D12: CreateSwapChainForHwnd failed (0x%X)", hr);
		return false;
	}
	hr = sc1.As(&ctx.swapChain);
	if (FAILED(hr))
		return false;

	// Disable DXGI's alt-enter takeover; HexEngine controls fullscreen state.
	_dxgiFactory->MakeWindowAssociation(window->GetHandle(), DXGI_MWA_NO_ALT_ENTER);

	// RTV heap with one slot per backbuffer.
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = kFrameCount;
	rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	hr = _device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&ctx.rtvHeap));
	if (FAILED(hr))
	{
		LOG_CRIT("D3D12: CreateDescriptorHeap(RTV) failed (0x%X)", hr);
		return false;
	}
	ctx.rtvDescriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = ctx.rtvHeap->GetCPUDescriptorHandleForHeapStart();
	for (uint32_t i = 0; i < kFrameCount; ++i)
	{
		Texture2DD3D12& bb = ctx.backbuffers[i];
		hr = ctx.swapChain->GetBuffer(i, IID_PPV_ARGS(&bb._resource));
		if (FAILED(hr))
		{
			LOG_CRIT("D3D12: SwapChain::GetBuffer[%u] failed (0x%X)", i, hr);
			return false;
		}
		_device->CreateRenderTargetView(bb._resource.Get(), nullptr, rtvHandle);
		bb._rtv          = rtvHandle;
		bb._format       = desc.Format;
		bb._width        = (int32_t)ctx.width;
		bb._height       = (int32_t)ctx.height;
		bb._currentState = D3D12_RESOURCE_STATE_PRESENT;
		bb._ownsResource = false; // swap chain owns it

		char dbgName[64];
		_snprintf_s(dbgName, sizeof(dbgName), "D3D12Backbuffer[%u]", i);
		bb.SetDebugName(dbgName);

		rtvHandle.ptr += ctx.rtvDescriptorSize;
	}

	ctx.currentFrameIndex = ctx.swapChain->GetCurrentBackBufferIndex();
	_activeWindow = &ctx;

	_backBufferViewport = HexEngine::Viewport(0.0f, 0.0f, (float)ctx.width, (float)ctx.height);

	LOG_INFO("D3D12: swap chain attached to window (%ux%u, %u backbuffers)", ctx.width, ctx.height, kFrameCount);
	return true;
}

void GraphicsDeviceD3D12::Resize(HexEngine::Window* window, uint32_t width, uint32_t height)
{
	auto it = _windowCtx.find(window);
	if (it == _windowCtx.end() || width == 0 || height == 0)
		return;

	WindowContext& ctx = it->second;
	if (ctx.width == width && ctx.height == height)
		return;

	// Backbuffers can't be released while in flight - drain the GPU first.
	WaitForGpu();

	for (uint32_t i = 0; i < kFrameCount; ++i)
		ctx.backbuffers[i]._resource.Reset();

	HRESULT hr = ctx.swapChain->ResizeBuffers(kFrameCount, width, height, DXGI_FORMAT_UNKNOWN, 0);
	if (FAILED(hr))
	{
		LOG_CRIT("D3D12: SwapChain::ResizeBuffers failed (0x%X)", hr);
		return;
	}

	ctx.width  = width;
	ctx.height = height;

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = ctx.rtvHeap->GetCPUDescriptorHandleForHeapStart();
	for (uint32_t i = 0; i < kFrameCount; ++i)
	{
		Texture2DD3D12& bb = ctx.backbuffers[i];
		ctx.swapChain->GetBuffer(i, IID_PPV_ARGS(&bb._resource));
		_device->CreateRenderTargetView(bb._resource.Get(), nullptr, rtvHandle);
		bb._rtv          = rtvHandle;
		bb._width        = (int32_t)width;
		bb._height       = (int32_t)height;
		bb._currentState = D3D12_RESOURCE_STATE_PRESENT;
		rtvHandle.ptr += ctx.rtvDescriptorSize;
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
	if (it == _windowCtx.end())
		return;
	WindowContext& ctx = it->second;
	_activeWindow = &ctx;

	const uint32_t frameIdx = ctx.currentFrameIndex;
	FrameContext& frame = _frames[frameIdx];

	// Block until the GPU has finished any prior submission that used this
	// frame's allocator. Without this, Reset() on an in-flight allocator
	// returns E_FAIL.
	if (_fence->GetCompletedValue() < frame.fenceValue)
	{
		if (SUCCEEDED(_fence->SetEventOnCompletion(frame.fenceValue, _fenceEvent)))
			WaitForSingleObject(_fenceEvent, INFINITE);
	}

	frame.alloc->Reset();
	_cmdList->Reset(frame.alloc.Get(), nullptr);

	Texture2DD3D12& bb = ctx.backbuffers[frameIdx];

	// PRESENT -> RENDER_TARGET, clear, leave bound as the RT.
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
	if (it == _windowCtx.end())
		return;
	WindowContext& ctx = it->second;

	const uint32_t frameIdx = ctx.currentFrameIndex;
	Texture2DD3D12& bb = ctx.backbuffers[frameIdx];

	TransitionResource(&bb, D3D12_RESOURCE_STATE_PRESENT);
	_cmdList->Close();

	ID3D12CommandList* lists[] = { _cmdList.Get() };
	_directQueue->ExecuteCommandLists(1, lists);

	// Schedule the fence-signal AFTER this frame's work, then record the value
	// against the allocator we used. Next BeginFrame that lands on this slot
	// will block on it.
	const uint64_t signalValue = _nextFenceValue++;
	_directQueue->Signal(_fence.Get(), signalValue);
	_frames[frameIdx].fenceValue = signalValue;

	ctx.swapChain->Present(0, 0);
	ctx.currentFrameIndex = ctx.swapChain->GetCurrentBackBufferIndex();
}

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------

HexEngine::ITexture2D* GraphicsDeviceD3D12::GetBackBuffer(HexEngine::Window* window)
{
	WindowContext* ctx = _activeWindow;
	if (window != nullptr)
	{
		auto it = _windowCtx.find(window);
		if (it != _windowCtx.end())
			ctx = &it->second;
	}
	if (ctx == nullptr)
		return nullptr;
	return &ctx->backbuffers[ctx->currentFrameIndex];
}

void GraphicsDeviceD3D12::GetBackBufferDimensions(uint32_t& width, uint32_t& height)
{
	if (_activeWindow != nullptr)
	{
		width  = _activeWindow->width;
		height = _activeWindow->height;
	}
	else
	{
		width  = 0;
		height = 0;
	}
}

void GraphicsDeviceD3D12::SetRenderTarget(HexEngine::ITexture2D* renderTarget, HexEngine::ITexture2D* /*depthStencil*/)
{
	if (renderTarget == nullptr || _cmdList == nullptr)
		return;
	auto* tex = static_cast<Texture2DD3D12*>(renderTarget);
	TransitionResource(tex, D3D12_RESOURCE_STATE_RENDER_TARGET);
	_cmdList->OMSetRenderTargets(1, &tex->_rtv, FALSE, nullptr);
}

void GraphicsDeviceD3D12::TransitionResource(Texture2DD3D12* tex, D3D12_RESOURCE_STATES newState)
{
	if (tex == nullptr || tex->_resource == nullptr || _cmdList == nullptr)
		return;
	if (tex->_currentState == newState)
		return;

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource   = tex->_resource.Get();
	barrier.Transition.StateBefore = tex->_currentState;
	barrier.Transition.StateAfter  = newState;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	_cmdList->ResourceBarrier(1, &barrier);
	tex->_currentState = newState;
}
