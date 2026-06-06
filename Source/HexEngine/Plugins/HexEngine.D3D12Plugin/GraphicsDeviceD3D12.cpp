
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
	// The thread that calls Create() is the "main" thread for D3D12
	// purposes - cmd list recording is locked to this thread. Other threads
	// that try to call UploadTextureData / UploadBufferData get their work
	// routed to the cross-thread queue and processed at BeginFrame.
	_mainThreadId = std::this_thread::get_id();

	if (!CreateDeviceAndQueue())
		return false;

	for (uint32_t i = 0; i < kFrameCount; ++i)
	{
		HRESULT hr = _device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_frames[i].alloc));
		if (FAILED(hr)) { LOG_CRIT("D3D12: CreateCommandAllocator[%u] failed (0x%X)", i, hr); return false; }
	}

	// Dedicated allocator for between-frame uploads (EnsureCmdListRecording).
	{
		HRESULT hr = _device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_uploadAlloc));
		if (FAILED(hr)) { LOG_CRIT("D3D12: CreateCommandAllocator(upload) failed (0x%X)", hr); return false; }
	}

	HRESULT hr = _device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _uploadAlloc.Get(), nullptr, IID_PPV_ARGS(&_cmdList));
	if (FAILED(hr)) { LOG_CRIT("D3D12: CreateCommandList failed (0x%X)", hr); return false; }
	// Leave the list OPEN. Init-time uploads (texture initialData, vertex /
	// index buffer initial contents) happen between Create() and the first
	// BeginFrame and need somewhere to record their CopyTextureRegion /
	// CopyBufferRegion calls. BeginFrame's first invocation drains whatever's
	// on the list before resetting allocator + list for the actual frame.
	_cmdListIsRecording = true;

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

	// Pre-create null CBV/SRV/UAV descriptors used to populate descriptor
	// table slots that the engine didn't bind but the bound shader's root
	// signature reserves a slot for. D3D12 does NOT zero-initialise shader-
	// visible heap entries between frames - leaving stale descriptor bits
	// from prior draws is undefined behaviour and has been observed to TDR
	// even on trivial shaders like UIBasic (texture-sample + colour mul).
	{
		uint32_t idx = 0;
		D3D12_CONSTANT_BUFFER_VIEW_DESC nullCbv = {};
		nullCbv.BufferLocation = 0;
		nullCbv.SizeInBytes    = 0;
		_nullCbv = _cbvSrvUavHeap.Allocate(idx);
		_device->CreateConstantBufferView(&nullCbv, _nullCbv);

		D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv = {};
		nullSrv.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
		nullSrv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
		nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		nullSrv.Texture2D.MipLevels     = 1;
		_nullSrv = _cbvSrvUavHeap.Allocate(idx);
		_device->CreateShaderResourceView(nullptr, &nullSrv, _nullSrv);

		D3D12_UNORDERED_ACCESS_VIEW_DESC nullUav = {};
		nullUav.Format        = DXGI_FORMAT_R8G8B8A8_UNORM;
		nullUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		_nullUav = _cbvSrvUavHeap.Allocate(idx);
		_device->CreateUnorderedAccessView(nullptr, nullptr, &nullUav, _nullUav);
	}

	// Engine-managed constant buffers. Sizes match the structs declared in
	// HexEngine.Core/Graphics/RenderStructs.hpp; these are the per-frame /
	// per-object / per-shadow-caster / per-animation buffers the renderer
	// expects to be present on every backend (mirrors D3D11 plugin Create
	// at line 231-234). Without these allocated, Mesh::UpdateConstantBuffer
	// logs "Invalid per-object constant buffer" every draw and meshes
	// never get their world matrix written.
	_engineConstantBuffers[(uint32_t)HexEngine::EngineConstantBuffer::PerFrameBuffer]         = CreateConstantBuffer(sizeof(HexEngine::PerFrameConstantBuffer));
	_engineConstantBuffers[(uint32_t)HexEngine::EngineConstantBuffer::PerObjectBuffer]        = CreateConstantBuffer(sizeof(HexEngine::PerObjectBuffer));
	_engineConstantBuffers[(uint32_t)HexEngine::EngineConstantBuffer::PerShadowCasterBuffer]  = CreateConstantBuffer(sizeof(HexEngine::PerShadowCasterBuffer));
	_engineConstantBuffers[(uint32_t)HexEngine::EngineConstantBuffer::PerAnimationBuffer]     = CreateConstantBuffer(sizeof(HexEngine::PerAnimationBuffer));

	LOG_INFO("HexEngine.D3D12Plugin: device + queue + %u-frame ring + descriptor heaps + texture loader + B4 PSO/RootSig + engine CBs ready", kFrameCount);
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

		// DRED (Device Removed Extended Data) - on a TDR / DEVICE_HUNG, lets
		// us GetAutoBreadcrumbsOutput / GetPageFaultAllocationOutput to see
		// exactly which command, draw, or resource caused the GPU to crash.
		// Has to be enabled BEFORE device creation. PageFault detection +
		// breadcrumb capture both forced on.
		ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred))))
		{
			dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			LOG_INFO("D3D12: DRED breadcrumbs + page-fault detection enabled");
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

#ifdef _DEBUG
	// Capture debug-layer messages into the engine log. Without this hook,
	// D3D12 validation errors only appear in the IDE's Output window via
	// OutputDebugString, which makes headless debugging painful and means
	// the LogFile we read after a session is misleadingly clean.
	if (SUCCEEDED(_device.As(&_infoQueue)))
	{
		_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
		_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR,      TRUE);
		_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING,    FALSE);

		// Suppress break-into-debugger for the device-removal error so our
		// next BeginFrame can run CheckAndReportDeviceRemoval and dump DRED
		// breadcrumbs. Without this the debug layer halts the process at
		// the removal point and we never get to query DRED.
		_infoQueue->SetBreakOnID(D3D12_MESSAGE_ID_DEVICE_REMOVAL_PROCESS_AT_FAULT, FALSE);
		_infoQueue->SetBreakOnID(D3D12_MESSAGE_ID_DEVICE_REMOVAL_PROCESS_POSSIBLY_AT_FAULT, FALSE);
		_infoQueue->SetBreakOnID(D3D12_MESSAGE_ID_DEVICE_REMOVAL_PROCESS_NOT_AT_FAULT, FALSE);

		// Filter out noisy categories we don't care about during bring-up.
		D3D12_MESSAGE_ID denyIds[] = {
			D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
			D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
		};
		D3D12_INFO_QUEUE_FILTER filter = {};
		filter.DenyList.NumIDs  = (UINT)std::size(denyIds);
		filter.DenyList.pIDList = denyIds;
		_infoQueue->AddStorageFilterEntries(&filter);
		LOG_INFO("D3D12: info queue installed (debug-layer messages will be logged)");
	}

	// ID3D12InfoQueue1 (Win10 19H1+) lets us register a callback that fires
	// SYNCHRONOUSLY the instant the runtime emits a message - we don't have
	// to wait until the next EndFrame's PumpDebugMessages drain. Critical
	// for catching device-removal events while DRED state is still fresh
	// AND while the engine is mid-frame (i.e. our normal poll path hasn't
	// reached us yet).
	{
		Microsoft::WRL::ComPtr<ID3D12InfoQueue1> iq1;
		if (SUCCEEDED(_device.As(&iq1)))
		{
			DWORD cookie = 0;
			iq1->RegisterMessageCallback(
				[](D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY sev, D3D12_MESSAGE_ID id, LPCSTR desc, void* ctx)
				{
					if (sev <= D3D12_MESSAGE_SEVERITY_ERROR)
						LOG_WARN("D3D12 InfoQueue1 SYNC [#%u sev=%d]: %s", (unsigned)id, (int)sev, desc ? desc : "<null>");
					// Device-removal IDs - dump DRED immediately while runtime
					// state is fresh.
					if (id == D3D12_MESSAGE_ID_DEVICE_REMOVAL_PROCESS_AT_FAULT ||
					    id == D3D12_MESSAGE_ID_DEVICE_REMOVAL_PROCESS_POSSIBLY_AT_FAULT ||
					    id == D3D12_MESSAGE_ID_DEVICE_REMOVAL_PROCESS_NOT_AT_FAULT)
					{
						// Dump DRED unconditionally - GetDeviceRemovedReason
						// may still return S_OK at this exact moment, but the
						// breadcrumbs / page fault data is already populated
						// inside the runtime.
						auto* self = static_cast<GraphicsDeviceD3D12*>(ctx);
						if (self) self->DumpDredNow("InfoQueue1 device-removal callback");
					}
				},
				D3D12_MESSAGE_CALLBACK_FLAG_NONE,
				this,
				&cookie);
			LOG_INFO("D3D12: ID3D12InfoQueue1 message callback registered (cookie=%lu)", cookie);
		}
		else
		{
			LOG_INFO("D3D12: ID3D12InfoQueue1 unavailable - DRED capture relies on poll path only");
		}
	}
#endif

	return true;
}

void GraphicsDeviceD3D12::Destroy()
{
	if (_device && _directQueue && _fence && _fenceEvent) WaitForGpu();

	if (_textureLoader) { delete _textureLoader; _textureLoader = nullptr; }
	for (auto*& cb : _engineConstantBuffers)
	{
		if (cb) { delete cb; cb = nullptr; }
	}
	_shaderVisibleHeap.Destroy();
	_psoCache.Destroy();
	_rootSig.Destroy();
	_pendingUploads.clear();
	_windowCtx.clear();
	_cbvSrvUavHeap.Destroy();
	_dsvHeap.Destroy();
	_rtvHeap.Destroy();
	_cmdList.Reset();
	_uploadAlloc.Reset();
	_uploadFenceValue = 0;
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
	// Same retirement rule for deferred-release resources: once the fence
	// they were stamped with has been signalled, the GPU has finished with
	// them and the ComPtr can safely drop.
	while (!_pendingDeletions.empty() && _pendingDeletions.front().fenceValue <= completed)
		_pendingDeletions.pop_front();
}

void GraphicsDeviceD3D12::DeferredRelease(Microsoft::WRL::ComPtr<ID3D12Resource> resource)
{
	if (!resource)
		return;
	// Stamp with the fence value EndFrame will signal for the current frame.
	// Any draws / barriers already recorded reference the resource at GPU
	// virtual addresses that stay valid until that signal completes; entries
	// drain in FlushPendingUploads at the next BeginFrame after the wait.
	_pendingDeletions.push_back({ std::move(resource), _nextFenceValue });
}

bool GraphicsDeviceD3D12::CheckAndReportDeviceRemoval()
{
	if (!_device) return false;
	const HRESULT removed = _device->GetDeviceRemovedReason();

	// One-shot trace so we can confirm this function actually ran. Helps
	// distinguish "function never fired" from "function fired but DRED
	// query failed silently".
	static bool firstCheckTraced = false;
	if (!firstCheckTraced)
	{
		firstCheckTraced = true;
		LOG_INFO("D3D12 DRED: CheckAndReportDeviceRemoval first call - GetDeviceRemovedReason returned HR=0x%X (S_OK if device is healthy).", (uint32_t)removed);
	}

	if (SUCCEEDED(removed)) return false;

	DumpDredNow("CheckAndReportDeviceRemoval");
	return true;
}

void GraphicsDeviceD3D12::DumpDredNow(const char* triggerSource)
{
	if (!_device) return;
	static bool reported = false;
	if (reported) return;
	reported = true;

	// Open a dedicated DRED dump file with line-buffered + explicit fflush
	// semantics. The engine's LogFile buffers writes and the engine often
	// crashes / TDRs immediately after the device-removal callback returns,
	// before that buffer flushes - which is why earlier DRED runs left no
	// trace in LogFile_HexEngineStudio.txt. A raw FILE* with fflush after
	// each line survives the crash.
	FILE* fp = nullptr;
	fopen_s(&fp, "DRED_dump.txt", "w");
	auto writeLine = [&](const char* fmt, ...)
	{
		char buf[2048];
		va_list ap; va_start(ap, fmt);
		vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);
		if (fp) { fputs(buf, fp); fputc('\n', fp); fflush(fp); }
		OutputDebugStringA(buf); OutputDebugStringA("\n");
		LOG_WARN("%s", buf);
	};

	const HRESULT removed = _device->GetDeviceRemovedReason();
	writeLine("D3D12 DRED: dump triggered by %s (GetDeviceRemovedReason HR=0x%X). Querying DRED breadcrumbs + page fault info...",
		triggerSource ? triggerSource : "?", (uint32_t)removed);

	using Microsoft::WRL::ComPtr;
	ComPtr<ID3D12DeviceRemovedExtendedData> dred;
	HRESULT qiHr = _device->QueryInterface(IID_PPV_ARGS(&dred));
	if (FAILED(qiHr))
	{
		LOG_WARN("D3D12 DRED: QueryInterface(ID3D12DeviceRemovedExtendedData) failed (0x%X) - DRED unavailable on this Windows / driver, no breadcrumbs.", (uint32_t)qiHr);
		return;
	}

	D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT bcOut = {};
	HRESULT bcHr = dred->GetAutoBreadcrumbsOutput(&bcOut);
	if (FAILED(bcHr))
	{
		LOG_WARN("D3D12 DRED: GetAutoBreadcrumbsOutput failed (0x%X)", (uint32_t)bcHr);
	}
	else if (bcOut.pHeadAutoBreadcrumbNode == nullptr)
	{
		LOG_WARN("D3D12 DRED: No breadcrumb nodes - either no work was submitted before removal, or breadcrumb enablement didn't take effect.");
	}
	else
	{
		const D3D12_AUTO_BREADCRUMB_NODE* node = bcOut.pHeadAutoBreadcrumbNode;
		uint32_t nodeIdx = 0;
		while (node != nullptr && nodeIdx < 32)
		{
			const UINT lastOp = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0;
			const UINT total  = node->BreadcrumbCount;
			LOG_WARN("D3D12 DRED breadcrumb #%u: queue='%S' cmdlist='%S' last_completed=%u/%u",
				nodeIdx,
				node->pCommandQueueDebugNameW ? node->pCommandQueueDebugNameW : L"<unnamed>",
				node->pCommandListDebugNameW  ? node->pCommandListDebugNameW  : L"<unnamed>",
				lastOp, total);
			const UINT firstShow = lastOp > 4 ? lastOp - 4 : 0;
			const UINT lastShow  = std::min<UINT>(lastOp + 4, total);
			for (UINT i = firstShow; i < lastShow; ++i)
			{
				LOG_WARN("D3D12 DRED   op[%u]=%u (D3D12_AUTO_BREADCRUMB_OP)%s", i, (uint32_t)node->pCommandHistory[i], i == lastOp ? "  <-- LAST COMPLETED" : "");
			}
			node = node->pNext;
			++nodeIdx;
		}
	}

	D3D12_DRED_PAGE_FAULT_OUTPUT pfOut = {};
	HRESULT pfHr = dred->GetPageFaultAllocationOutput(&pfOut);
	if (FAILED(pfHr))
	{
		LOG_WARN("D3D12 DRED: GetPageFaultAllocationOutput failed (0x%X)", (uint32_t)pfHr);
	}
	else if (pfOut.PageFaultVA == 0)
	{
		LOG_WARN("D3D12 DRED: No page fault recorded (hang was probably a shader timeout, not a memory access fault)");
	}
	else
	{
		LOG_WARN("D3D12 DRED page fault: VA = 0x%llX", (unsigned long long)pfOut.PageFaultVA);
		auto dumpAllocList = [](const char* tag, const D3D12_DRED_ALLOCATION_NODE* head)
		{
			uint32_t i = 0;
			for (auto* n = head; n != nullptr && i < 16; n = n->pNext, ++i)
			{
				LOG_WARN("D3D12 DRED   %s[%u]: type=%u name='%S'",
					tag, i, (uint32_t)n->AllocationType, n->ObjectNameW ? n->ObjectNameW : L"<unnamed>");
			}
		};
		dumpAllocList("existing", pfOut.pHeadExistingAllocationNode);
		dumpAllocList("recently freed", pfOut.pHeadRecentFreedAllocationNode);
	}

	// Dump the engine-side draw trace ring. DRED breadcrumbs only record
	// opcodes; this gives us the actual VS/PS bytecode pointers and index
	// counts of the last several Draw* submissions, which correlates with
	// the breadcrumb's last_completed index to identify the failing draw.
	writeLine("D3D12 DRED: --- CPU-side draw trace (last %u draws of %llu total) ---",
		(unsigned)std::min<uint64_t>(kDrawTraceCapacity, _drawTraceCount),
		(unsigned long long)_drawTraceCount);
	const uint64_t first = _drawTraceCount > kDrawTraceCapacity ? _drawTraceCount - kDrawTraceCapacity : 0;
	for (uint64_t i = first; i < _drawTraceCount; ++i)
	{
		const auto& s = _drawTrace[i % kDrawTraceCapacity];
		writeLine("D3D12 DRED draw[%llu]: vsBytecode=%p psBytecode=%p indices=%u inst=%u rts=%u ds=%u",
			(unsigned long long)s.drawIndex, s.vsBytecode, s.psBytecode,
			s.indexCount, s.instanceCount, s.rtCount, s.dsBound);
	}
}

void GraphicsDeviceD3D12::DrainCrossThreadUploads()
{
	// Pull entries out under the mutex (short critical section), then process
	// them outside the lock - the actual upload work calls into D3D12 from
	// the main thread, so we don't want to hold the queue lock during it
	// (other threads might want to enqueue more while we work).
	std::deque<PendingCrossThreadUpload> work;
	{
		std::lock_guard<std::mutex> lock(_crossThreadUploadsMutex);
		work.swap(_crossThreadUploads);
	}
	if (work.empty()) return;

	EnsureCmdListRecording();
	for (auto& up : work)
	{
		switch (up.kind)
		{
		case PendingCrossThreadUpload::Kind::Tex2D:
			UploadTextureData(reinterpret_cast<Texture2DD3D12*>(up.dst), up.data.data(), up.byteSize, up.srcRowPitch);
			break;
		case PendingCrossThreadUpload::Kind::Tex3D:
			UploadTextureData(reinterpret_cast<Texture3DD3D12*>(up.dst), up.data.data(), up.byteSize, up.srcRowPitch, up.srcSlicePitch);
			break;
		case PendingCrossThreadUpload::Kind::Buffer:
			UploadBufferData(reinterpret_cast<StructuredBufferD3D12*>(up.dst), up.data.data(), up.byteSize, up.dstByteOffset);
			break;
		}
	}
}

void GraphicsDeviceD3D12::EnsureCmdListRecording()
{
	if (_cmdListIsRecording)
		return;
	if (!_cmdList || !_fence || !_uploadAlloc)
		return;

	// Asset loads can fire between EndFrame and the next BeginFrame
	// (FolderExplorer hovering, hot-reload, init-time importers, etc.). The
	// cmd list is closed in that window. Reset it onto our dedicated upload
	// allocator (NOT one of the per-frame ring slots) so when BeginFrame
	// later drains+resets a per-frame allocator, the cmd list's lingering
	// association is with _uploadAlloc - a different physical allocator -
	// and the validation layer doesn't trip COMMAND_ALLOCATOR_CANNOT_RESET.
	//
	// We wait for _uploadFenceValue before resetting _uploadAlloc so the
	// previous between-frame submit (and its uploads) is GPU-complete before
	// we reuse its memory.
	if (_uploadFenceValue != 0 && _fence->GetCompletedValue() < _uploadFenceValue)
	{
		if (SUCCEEDED(_fence->SetEventOnCompletion(_uploadFenceValue, _fenceEvent)))
			WaitForSingleObject(_fenceEvent, INFINITE);
	}

	_uploadAlloc->Reset();
	_cmdList->Reset(_uploadAlloc.Get(), nullptr);
	_cmdListIsRecording = true;
}

void GraphicsDeviceD3D12::PumpDebugMessages()
{
#ifdef _DEBUG
	if (!_infoQueue) return;
	const UINT64 count = _infoQueue->GetNumStoredMessages();
	for (UINT64 i = 0; i < count; ++i)
	{
		SIZE_T sz = 0;
		_infoQueue->GetMessage(i, nullptr, &sz);
		if (sz == 0) continue;
		std::vector<uint8_t> buf(sz);
		auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
		if (FAILED(_infoQueue->GetMessage(i, msg, &sz))) continue;

		const char* sev = "INFO";
		switch (msg->Severity)
		{
		case D3D12_MESSAGE_SEVERITY_CORRUPTION: sev = "CORRUPTION"; break;
		case D3D12_MESSAGE_SEVERITY_ERROR:      sev = "ERROR";      break;
		case D3D12_MESSAGE_SEVERITY_WARNING:    sev = "WARNING";    break;
		case D3D12_MESSAGE_SEVERITY_INFO:       sev = "INFO";       break;
		case D3D12_MESSAGE_SEVERITY_MESSAGE:    sev = "MSG";        break;
		}
		// Only mirror at WARN+ to avoid drowning the log in INFO-level chatter.
		if (msg->Severity <= D3D12_MESSAGE_SEVERITY_WARNING)
			LOG_WARN("D3D12 %s [#%u]: %s", sev, (unsigned)msg->ID, msg->pDescription);
	}
	_infoQueue->ClearStoredMessages();

	// If any of those messages was a device-removal, check now while DRED's
	// state is still fresh - waiting for the next BeginFrame may be too late
	// if the engine is about to crash on a use-after-removed access.
	CheckAndReportDeviceRemoval();
#endif
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
	// If the device hung in a prior frame, dump DRED diagnostics once and
	// then bail without trying to record - any further GPU ops would fail
	// with the same DEVICE_HUNG / DEVICE_REMOVED.
	if (CheckAndReportDeviceRemoval())
		return;

	auto it = _windowCtx.find(window);
	if (it == _windowCtx.end()) return;
	WindowContext& ctx = it->second;
	_activeWindow = &ctx;

	const uint32_t frameIdx = ctx.currentFrameIndex;
	FrameContext& frame = _frames[frameIdx];

	// If the cmd list is currently in recording state (init-time uploads
	// from before the first BeginFrame, or stray cross-frame recordings),
	// drain it before resetting the allocator. Otherwise the recorded work
	// is silently dropped (best case) or Reset asserts (worst case).
	if (_cmdListIsRecording)
	{
		HRESULT closeHr = _cmdList->Close();
		if (FAILED(closeHr))
		{
			static bool warned = false;
			if (!warned) { LOG_WARN("D3D12 BeginFrame drain: _cmdList->Close failed (0x%X) - subsequent Reset will fail.", closeHr); warned = true; }
			// Close failure often indicates the device is removed mid-frame.
			// Dump DRED while we can; the next BeginFrame's early-bail may
			// not run if the engine crashes on the FAILED Reset.
			CheckAndReportDeviceRemoval();
		}
		ID3D12CommandList* lists[] = { _cmdList.Get() };
		_directQueue->ExecuteCommandLists(1, lists);
		const uint64_t drainValue = _nextFenceValue++;
		_directQueue->Signal(_fence.Get(), drainValue);
		if (_fence->GetCompletedValue() < drainValue)
		{
			if (SUCCEEDED(_fence->SetEventOnCompletion(drainValue, _fenceEvent)))
				WaitForSingleObject(_fenceEvent, INFINITE);
		}
		// The drain just submitted the cmd list's recordings - those were
		// recording onto _uploadAlloc (init-time uploads in Create(), or
		// EnsureCmdListRecording-driven between-frame uploads). Stamp
		// _uploadFenceValue so the next EnsureCmdListRecording knows when
		// it's safe to Reset _uploadAlloc. The per-frame allocator
		// (frame.alloc) was NOT used by this submit, so it doesn't need
		// stamping here - it stays at whatever fence its last actual frame
		// use stamped it with.
		_uploadFenceValue = drainValue;
		_cmdListIsRecording = false;

		// Stamp pending uploads that were enqueued before this drain with the
		// fence we just signalled, so FlushPendingUploads can retire them
		// once the GPU has finished.
		for (auto it2 = _pendingUploads.rbegin(); it2 != _pendingUploads.rend(); ++it2)
		{
			if (it2->fenceValue != 0) break;
			it2->fenceValue = drainValue;
		}
	}

	if (_fence->GetCompletedValue() < frame.fenceValue)
	{
		if (SUCCEEDED(_fence->SetEventOnCompletion(frame.fenceValue, _fenceEvent)))
			WaitForSingleObject(_fenceEvent, INFINITE);
	}

	FlushPendingUploads();

	frame.alloc->Reset();
	_cmdList->Reset(frame.alloc.Get(), nullptr);
	_cmdListIsRecording = true;
	_currentRingSlot = frameIdx;

	// Process any cross-thread uploads enqueued by async resource loaders
	// since the last frame. Doing this AFTER the cmd list Reset means the
	// uploads go onto the per-frame allocator (not _uploadAlloc), so they
	// run as part of the frame's normal submit.
	DrainCrossThreadUploads();

	// Reset shader-visible heap bump pointer + pending-state bookkeeping for
	// the new frame.
	_shaderVisibleHeap.BeginFrame(frameIdx);
	ResetPendingForBeginFrame();

	Texture2DD3D12& bb = ctx.backbuffers[frameIdx];
	TransitionResource(&bb, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const float clear[4] = { _clearColour.R(), _clearColour.G(), _clearColour.B(), _clearColour.A() };
	_cmdList->ClearRenderTargetView(bb._rtv, clear, 0, nullptr);
	_cmdList->OMSetRenderTargets(1, &bb._rtv, FALSE, nullptr);
	// Mirror the backbuffer binding into _pending so engine code that does
	// SetRenderTarget -> ... -> GetRenderTargets / SetRenderTargets(prev)
	// (Canvas::BeginDraw / EndDraw is the main caller) gets the backbuffer
	// back when it restores. Without this, GetRenderTargets returned empty
	// and the restore went to "no RT bound", silently dropping all draws.
	_pending.rtCount = 1;
	_pending.rtvs[0] = &bb;
	_pending.dsv     = nullptr;

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
	HRESULT closeHr = _cmdList->Close();
	if (FAILED(closeHr))
	{
		static bool warned = false;
		if (!warned) { LOG_WARN("D3D12 EndFrame: _cmdList->Close failed (0x%X) - keeping _cmdListIsRecording=true so the next BeginFrame can drain.", closeHr); warned = true; }
		// Close failure on Present-side strongly suggests device removed
		// during this frame's render. Capture DRED immediately.
		CheckAndReportDeviceRemoval();
	}
	else
	{
		_cmdListIsRecording = false;
	}

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

	// Drain debug-layer messages every frame so the log captures the actual
	// failure mode if a draw is silently rejected by validation.
	PumpDebugMessages();
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

void GraphicsDeviceD3D12::SetRenderTargets(const std::vector<HexEngine::ITexture2D*>& rts, HexEngine::ITexture2D* depthStencil)
{
	// Engine code (Canvas::BeginDraw / EndDraw, GBuffer setup, post-process
	// chains) uses this to bind MRT or to restore a previously-saved RT set.
	// Empty stub here used to silently no-op the restore in Canvas::EndDraw,
	// causing every subsequent draw to land in the canvas's offscreen texture
	// instead of the backbuffer - which is what made every Canvas.Redraw'd
	// element disappear.
	if (_cmdList == nullptr) return;

	// Compact non-null RTs into the front of _pending.rtvs. D3D12's
	// OMSetRenderTargets binds N contiguous handles starting at slot 0;
	// there's no sparse-slot binding. The PSO key also depends on
	// _pending.rtvs[0..rtCount-1] being non-null in order, otherwise it
	// builds an invalid PSO with DXGI_FORMAT_UNKNOWN in the middle.
	const uint32_t count = (uint32_t)std::min<size_t>(rts.size(), 8);
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[8] = {};
	uint32_t boundCount = 0;
	for (uint32_t i = 0; i < count; ++i)
	{
		auto* rt = static_cast<Texture2DD3D12*>(rts[i]);
		if (rt == nullptr) continue;
		TransitionResource(rt, D3D12_RESOURCE_STATE_RENDER_TARGET);
		_pending.rtvs[boundCount]   = rt;
		rtvHandles[boundCount]      = rt->_rtv;
		++boundCount;
	}
	for (uint32_t i = boundCount; i < 8; ++i) _pending.rtvs[i] = nullptr;
	_pending.rtCount = boundCount;
	_pending.dirty   = true;

	auto* ds = static_cast<Texture2DD3D12*>(depthStencil);
	_pending.dsv = ds;
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};
	D3D12_CPU_DESCRIPTOR_HANDLE* dsvPtr = nullptr;
	if (ds != nullptr)
	{
		TransitionResource(ds, D3D12_RESOURCE_STATE_DEPTH_WRITE);
		dsvHandle = ds->_dsv;
		dsvPtr    = &dsvHandle;
	}
	_cmdList->OMSetRenderTargets(boundCount, boundCount > 0 ? rtvHandles : nullptr, FALSE, dsvPtr);
}

void GraphicsDeviceD3D12::SetRenderTargets(uint32_t numViews, const std::vector<HexEngine::ITexture2D*>& rts, HexEngine::ITexture2D* depthStencil)
{
	// The numViews-explicit overload is identical except for capping count to
	// numViews regardless of the vector's size. Callers (post-process passes
	// with optional unused slots) rely on this to clamp.
	std::vector<HexEngine::ITexture2D*> trimmed;
	trimmed.reserve(numViews);
	for (uint32_t i = 0; i < numViews && i < rts.size(); ++i)
		trimmed.push_back(rts[i]);
	SetRenderTargets(trimmed, depthStencil);
}

void GraphicsDeviceD3D12::GetRenderTargets(std::vector<HexEngine::ITexture2D*>& outRts, HexEngine::ITexture2D** outDepthStencil)
{
	outRts.clear();
	for (uint32_t i = 0; i < _pending.rtCount; ++i)
		outRts.push_back(_pending.rtvs[i]);
	if (outDepthStencil != nullptr) *outDepthStencil = _pending.dsv;
}

// ---------------------------------------------------------------------------
// Resource state transitions
// ---------------------------------------------------------------------------

void GraphicsDeviceD3D12::RestoreBoundRoleIfNeeded(Texture2DD3D12* tex)
{
	if (tex == nullptr || _cmdList == nullptr) return;

	bool boundAsRT = false;
	for (uint32_t i = 0; i < _pending.rtCount; ++i)
	{
		if (_pending.rtvs[i] == tex) { boundAsRT = true; break; }
	}
	const bool boundAsDSV = (_pending.dsv == tex);

	if (boundAsRT)
		TransitionResource(tex, D3D12_RESOURCE_STATE_RENDER_TARGET);
	if (boundAsDSV)
		TransitionResource(tex, D3D12_RESOURCE_STATE_DEPTH_WRITE);
}

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

	auto* src = static_cast<Texture2DD3D12*>(clone);

	// Mirror the source's bind-flag capabilities. The previous hardcoded
	// "ShaderResource only" path failed under D3D12 the moment the engine
	// cloned a render target (TAA::Create clones the post-process buffer)
	// because the resource was created without ALLOW_RENDER_TARGET and the
	// first SetRenderTarget transition raised the debug layer's
	// RESOURCE_BARRIER_MISMATCHING_MISC_FLAGS error. D3D11's CreateTexture
	// clone path uses GetDesc on the source texture; we do the equivalent
	// by inspecting the source's view indices (set at creation time iff
	// the matching bind-flag was requested).
	HexEngine::BindFlags bindFlags = HexEngine::BindFlags::None;
	if (src->_srvIndex != UINT32_MAX) bindFlags |= HexEngine::BindFlags::ShaderResource;
	if (src->_rtvIndex != UINT32_MAX) bindFlags |= HexEngine::BindFlags::RenderTarget;
	if (src->_dsvIndex != UINT32_MAX) bindFlags |= HexEngine::BindFlags::DepthStencil;
	if (src->_uavIndex != UINT32_MAX) bindFlags |= HexEngine::BindFlags::UnorderedAccess;
	// If the source had no views at all (shouldn't happen for legit textures
	// but defensive), still create as SR so the clone is at least sampleable.
	if (bindFlags == HexEngine::BindFlags::None)
		bindFlags = HexEngine::BindFlags::ShaderResource;

	// Pull format / dims directly off the underlying resource desc rather than
	// the wrapper's cached fields - the resource is the source of truth and
	// MipLevels = 0 / arraySize > 1 textures need to round-trip exactly.
	D3D12_RESOURCE_DESC srcDesc = src->_resource->GetDesc();

	HexEngine::TextureDesc d;
	d.width      = (int32_t)srcDesc.Width;
	d.height     = (int32_t)srcDesc.Height;
	d.format     = HexEngine::FromDXGI12(srcDesc.Format);
	d.bindFlags  = bindFlags;
	d.arraySize  = (int32_t)srcDesc.DepthOrArraySize;
	d.mipLevels  = (int32_t)srcDesc.MipLevels;
	d.sampleCount = (int32_t)srcDesc.SampleDesc.Count;
	d.dimension  = (srcDesc.DepthOrArraySize > 1) ? HexEngine::ResourceDimension::Texture2DArray
	                                              : HexEngine::ResourceDimension::Texture2D;
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

	if (initialData != nullptr && initialData->data != nullptr)
	{
		// SysMemSlicePitch is meaningless for 2D textures in D3D11 - many
		// callers (FreeType font-atlas creation, anything ported from D3D11
		// idioms) leave it at 0. Don't let a zero slice-pitch skip the
		// upload; reconstruct it from row pitch * height. Without this
		// guard the texture is created but never populated, so sampling
		// returns transparent black - which is why font atlases looked
		// "right structurally but invisible" before this fix.
		uint32_t srcRowPitch   = initialData->rowPitchBytes;
		uint32_t srcSlicePitch = initialData->slicePitchBytes;
		if (srcSlicePitch == 0)
			srcSlicePitch = (srcRowPitch > 0 ? srcRowPitch : (uint32_t)(d.width * 4)) * (uint32_t)d.height;
		UploadTextureData(tex, initialData->data, srcSlicePitch, srcRowPitch);
	}

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

	if (initialData != nullptr && initialData->data != nullptr)
	{
		// As with the 2D path: reconstruct missing slice/row pitches from
		// the texture dims rather than skipping the upload entirely.
		uint32_t srcRowPitch   = initialData->rowPitchBytes;
		uint32_t srcSlicePitch = initialData->slicePitchBytes;
		if (srcRowPitch == 0)   srcRowPitch   = (uint32_t)(d.width * 4);
		if (srcSlicePitch == 0) srcSlicePitch = srcRowPitch * (uint32_t)d.height;
		UploadTextureData(tex, initialData->data, srcSlicePitch * (uint32_t)d.depth, srcRowPitch, srcSlicePitch);
	}

	return tex;
}

// ---------------------------------------------------------------------------
// Buffer creation
// ---------------------------------------------------------------------------

HexEngine::IVertexBuffer* GraphicsDeviceD3D12::CreateVertexBuffer(const HexEngine::BufferDesc& d, const void* initialData)
{
	if (d.byteWidth == 0 || d.byteStride == 0) return nullptr;

	auto* vb = new VertexBufferD3D12();
	vb->_byteSize = d.byteWidth;
	vb->_stride   = d.byteStride;
	vb->_device   = this;
	vb->_view.SizeInBytes   = d.byteWidth;
	vb->_view.StrideInBytes = d.byteStride;

	// Allocate the first upload entry. Static buffers (set once at init,
	// drawn forever) keep only this one entry. Dynamic buffers grow on
	// demand inside SetVertexData when an entry's fence hasn't completed.
	VertexBufferD3D12::UploadEntry entry;
	D3D12_RESOURCE_DESC desc = MakeBufferDesc(d.byteWidth);
	D3D12_HEAP_PROPERTIES heap = MakeHeapProps(D3D12_HEAP_TYPE_UPLOAD);
	HRESULT hr = _device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&entry.resource));
	if (FAILED(hr)) { LOG_CRIT("D3D12: CreateCommittedResource(VB %u bytes) failed (0x%X)", d.byteWidth, hr); delete vb; return nullptr; }
	D3D12_RANGE noRead = { 0, 0 };
	entry.resource->Map(0, &noRead, &entry.mapped);
	if (initialData != nullptr && entry.mapped != nullptr)
		memcpy(entry.mapped, initialData, d.byteWidth);
	vb->_activeResource      = entry.resource.Get();
	vb->_view.BufferLocation = entry.resource->GetGPUVirtualAddress();
	vb->_uploads.push_back(std::move(entry));
	return vb;
}

HexEngine::IIndexBuffer* GraphicsDeviceD3D12::CreateIndexBuffer(const HexEngine::BufferDesc& d, const void* initialData)
{
	if (d.byteWidth == 0) return nullptr;
	const uint32_t stride = d.byteStride > 0 ? d.byteStride : 4u;

	auto* ib = new IndexBufferD3D12();
	ib->_byteSize = d.byteWidth;
	ib->_stride   = stride;
	ib->_device   = this;
	ib->_view.SizeInBytes = d.byteWidth;
	ib->_view.Format      = (stride == 2) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;

	IndexBufferD3D12::UploadEntry entry;
	D3D12_RESOURCE_DESC desc = MakeBufferDesc(d.byteWidth);
	D3D12_HEAP_PROPERTIES heap = MakeHeapProps(D3D12_HEAP_TYPE_UPLOAD);
	HRESULT hr = _device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&entry.resource));
	if (FAILED(hr)) { LOG_CRIT("D3D12: CreateCommittedResource(IB %u bytes) failed (0x%X)", d.byteWidth, hr); delete ib; return nullptr; }
	D3D12_RANGE noRead = { 0, 0 };
	entry.resource->Map(0, &noRead, &entry.mapped);
	if (initialData != nullptr && entry.mapped != nullptr)
		memcpy(entry.mapped, initialData, d.byteWidth);
	ib->_activeResource      = entry.resource.Get();
	ib->_view.BufferLocation = entry.resource->GetGPUVirtualAddress();
	ib->_uploads.push_back(std::move(entry));
	return ib;
}

HexEngine::IConstantBuffer* GraphicsDeviceD3D12::CreateConstantBuffer(uint32_t size)
{
	if (size == 0) return nullptr;
	const uint32_t aligned = AlignCbSize(size);

	auto* cb = new ConstantBufferD3D12();
	cb->_logicalSize = size;
	cb->_byteSize    = aligned;
	cb->_device      = this;

	ConstantBufferD3D12::UploadEntry entry;
	D3D12_RESOURCE_DESC desc = MakeBufferDesc(aligned);
	D3D12_HEAP_PROPERTIES heap = MakeHeapProps(D3D12_HEAP_TYPE_UPLOAD);
	HRESULT hr = _device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&entry.resource));
	if (FAILED(hr)) { LOG_CRIT("D3D12: CreateCommittedResource(CB %u bytes) failed (0x%X)", aligned, hr); delete cb; return nullptr; }
	D3D12_RANGE noRead = { 0, 0 };
	entry.resource->Map(0, &noRead, &entry.mapped);
	cb->_activeResource = entry.resource.Get();

	// Allocate a single CBV slot; Write() recreates the view in place to
	// point at whichever upload entry it just wrote.
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbv = {};
	cbv.BufferLocation = entry.resource->GetGPUVirtualAddress();
	cbv.SizeInBytes    = aligned;
	cb->_cbv = _cbvSrvUavHeap.Allocate(cb->_cbvIndex);
	_device->CreateConstantBufferView(&cbv, cb->_cbv);

	cb->_uploads.push_back(std::move(entry));
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

HexEngine::IConstantBuffer* GraphicsDeviceD3D12::GetEngineConstantBuffer(HexEngine::EngineConstantBuffer which)
{
	const uint32_t idx = (uint32_t)which;
	if (idx >= (uint32_t)HexEngine::EngineConstantBuffer::NumEngineConstantBuffers)
		return nullptr;
	return _engineConstantBuffers[idx];
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

	// If called from a non-main thread (async scene loader, file watcher,
	// editor UI worker), queue the upload for the main thread to process
	// at the next BeginFrame instead of touching the cmd list here. D3D12
	// cmd lists are NOT thread-safe.
	if (std::this_thread::get_id() != _mainThreadId)
	{
		PendingCrossThreadUpload up;
		up.kind          = PendingCrossThreadUpload::Kind::Buffer;
		up.dst           = dst;
		up.byteSize      = byteSize;
		up.dstByteOffset = dstByteOffset;
		up.data.assign(static_cast<const uint8_t*>(src), static_cast<const uint8_t*>(src) + byteSize);
		std::lock_guard<std::mutex> lock(_crossThreadUploadsMutex);
		_crossThreadUploads.push_back(std::move(up));
		return true;
	}

	EnsureCmdListRecording();

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

bool GraphicsDeviceD3D12::UploadTextureData(Texture2DD3D12* dst, const void* src, uint32_t byteSize, uint32_t srcRowPitch)
{
	if (dst == nullptr || src == nullptr || byteSize == 0 || _cmdList == nullptr) return false;

	if (std::this_thread::get_id() != _mainThreadId)
	{
		PendingCrossThreadUpload up;
		up.kind         = PendingCrossThreadUpload::Kind::Tex2D;
		up.dst          = dst;
		up.byteSize     = byteSize;
		up.srcRowPitch  = srcRowPitch;
		up.data.assign(static_cast<const uint8_t*>(src), static_cast<const uint8_t*>(src) + byteSize);
		std::lock_guard<std::mutex> lock(_crossThreadUploadsMutex);
		_crossThreadUploads.push_back(std::move(up));
		return true;
	}

	EnsureCmdListRecording();

	D3D12_RESOURCE_DESC desc = dst->_resource->GetDesc();
	UINT64 uploadSize = 0;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	UINT numRows = 0;
	UINT64 rowBytes = 0;
	_device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &numRows, &rowBytes, &uploadSize);

	// Source row pitch: caller passes the actual source stride (WIC/DirectXTex
	// loaders typically align rows >= rowBytes). 0 means "no extra padding,
	// tightly packed at rowBytes". Mismatching this is invisible until you
	// look closely at the texture - the first row looks right and subsequent
	// rows read from slightly the wrong offset, which is exactly what causes
	// "text disappears" / "icons garbled" with intact panel rendering.
	const uint64_t effectiveSrcRowPitch = (srcRowPitch > 0) ? srcRowPitch : rowBytes;

	D3D12_HEAP_PROPERTIES uploadHeap = MakeHeapProps(D3D12_HEAP_TYPE_UPLOAD);
	D3D12_RESOURCE_DESC   uploadDesc = MakeBufferDesc(uploadSize);

	PendingUpload pending;
	HRESULT hr = _device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&pending.resource));
	if (FAILED(hr)) return false;

	void* mapped = nullptr;
	D3D12_RANGE noRead = { 0, 0 };
	pending.resource->Map(0, &noRead, &mapped);
	// Row-by-row copy. Destination uses the D3D12-required RowPitch (256-byte
	// aligned), source uses whatever pitch the decoder produced.
	auto* d = static_cast<uint8_t*>(mapped) + footprint.Offset;
	auto* s = static_cast<const uint8_t*>(src);
	for (UINT r = 0; r < numRows; ++r)
		memcpy(d + r * footprint.Footprint.RowPitch, s + r * effectiveSrcRowPitch, (size_t)rowBytes);
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

bool GraphicsDeviceD3D12::UploadTextureData(Texture3DD3D12* dst, const void* src, uint32_t byteSize, uint32_t srcRowPitch, uint32_t srcSlicePitch)
{
	if (dst == nullptr || src == nullptr || _cmdList == nullptr) return false;

	if (std::this_thread::get_id() != _mainThreadId)
	{
		PendingCrossThreadUpload up;
		up.kind           = PendingCrossThreadUpload::Kind::Tex3D;
		up.dst            = dst;
		up.byteSize       = byteSize;
		up.srcRowPitch    = srcRowPitch;
		up.srcSlicePitch  = srcSlicePitch;
		// Use byteSize if non-zero, else fall back to srcSlicePitch * depth-ish.
		// For a 3D upload we need the full volume payload; the caller passes
		// byteSize that already accounts for depth in the call sites.
		const uint32_t copyBytes = byteSize > 0 ? byteSize : (srcSlicePitch * 1);
		up.data.assign(static_cast<const uint8_t*>(src), static_cast<const uint8_t*>(src) + copyBytes);
		std::lock_guard<std::mutex> lock(_crossThreadUploadsMutex);
		_crossThreadUploads.push_back(std::move(up));
		return true;
	}

	EnsureCmdListRecording();

	D3D12_RESOURCE_DESC desc = dst->_resource->GetDesc();
	UINT64 uploadSize = 0;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	UINT numRows = 0;
	UINT64 rowBytes = 0;
	_device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &numRows, &rowBytes, &uploadSize);

	const uint64_t effectiveSrcRowPitch   = (srcRowPitch   > 0) ? srcRowPitch   : rowBytes;
	const uint64_t effectiveSrcSlicePitch = (srcSlicePitch > 0) ? srcSlicePitch : (effectiveSrcRowPitch * numRows);

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
	for (UINT z = 0; z < slices; ++z)
	{
		for (UINT r = 0; r < numRows; ++r)
			memcpy(d + z * slicePitchDst + r * footprint.Footprint.RowPitch,
			       s + z * effectiveSrcSlicePitch + r * effectiveSrcRowPitch,
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
