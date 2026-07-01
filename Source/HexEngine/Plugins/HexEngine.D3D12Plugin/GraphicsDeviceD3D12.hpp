
#pragma once

#include <HexEngine.Core/Graphics/IGraphicsDevice.hpp>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <unordered_map>
#include <vector>
#include <deque>
#include <mutex>
#include <thread>

#include "DescriptorHeapAllocator.hpp"
#include "RootSignatureD3D12.hpp"
#include "PsoCache.hpp"
#include "ShaderVisibleHeap.hpp"
#include "Texture2DD3D12.hpp"
#include "Texture3DD3D12.hpp"
#include "VertexBufferD3D12.hpp"
#include "IndexBufferD3D12.hpp"
#include "ConstantBufferD3D12.hpp"
#include "StructuredBufferD3D12.hpp"
#include "InputLayoutD3D12.hpp"
#include "ShaderStageD3D12.hpp"

/**
 * @brief D3D12 IGraphicsDevice implementation.
 *
 * Phase B3 (current): real device + swap chain + descriptor heaps + every
 * resource creation method (textures, all buffer types, input layouts).
 * The state setters / draw calls remain no-ops - that's B4. So r_renderer = 2
 * now boots through engine init without crashing, presents a clear-coloured
 * backbuffer, but no scene geometry renders.
 *
 * Important: this header MUST NOT include <d3d11.h>. The D3D11 compat shims
 * on IGraphicsDevice are gated on `__d3d11_h__` and would leak D3D11 into the
 * D3D12 plugin if anything transitively pulled in that header.
 */
class GraphicsDeviceD3D12 : public HexEngine::IGraphicsDevice
{
public:
	GraphicsDeviceD3D12() = default;
	virtual ~GraphicsDeviceD3D12() override { Destroy(); }

	virtual HexEngine::GraphicsBackend GetBackend() const override { return HexEngine::GraphicsBackend::D3D12; }

	// -- Lifecycle --
	virtual bool Create() override;
	virtual void Destroy() override;
	virtual bool AttachToWindow(HexEngine::Window* window) override;
	virtual void Resize(HexEngine::Window* window, uint32_t width, uint32_t height) override;

	// -- Resource creation (B3 real impls) --
	virtual HexEngine::ITexture2D* GetBackBuffer(HexEngine::Window* window = nullptr) override;
	virtual HexEngine::ITexture2D* CreateTexture(HexEngine::ITexture2D* clone) override;
	virtual HexEngine::ITexture2D* CreateTexture2D(const HexEngine::TextureDesc& desc, const HexEngine::SubresourceData* initialData = nullptr) override;
	virtual HexEngine::ITexture3D* CreateTexture3D(const HexEngine::TextureDesc& desc, const HexEngine::SubresourceData* initialData = nullptr) override;
	virtual HexEngine::IVertexBuffer* CreateVertexBuffer(const HexEngine::BufferDesc& desc, const void* initialData = nullptr) override;
	virtual HexEngine::IIndexBuffer*  CreateIndexBuffer(const HexEngine::BufferDesc& desc, const void* initialData = nullptr) override;

	// Shader / input layout creation. Shader stages currently just hold the
	// bytecode blob; B4 wires them into PSOs.
	virtual HexEngine::IShaderStage* CreateVertexShader(std::vector<uint8_t>&)   override;
	virtual HexEngine::IShaderStage* CreatePixelShader(std::vector<uint8_t>&)    override;
	virtual HexEngine::IShaderStage* CreateGeometryShader(std::vector<uint8_t>&) override;
	virtual HexEngine::IShaderStage* CreateComputeShader(std::vector<uint8_t>&)  override;
	virtual HexEngine::IShaderStage* CreateComputeShaderFromSource(const std::string&, const std::string& = "MainCS") override { return nullptr; }
	virtual HexEngine::IInputLayout* CreateInputLayout(const HexEngine::InputElement*, uint32_t, const std::vector<uint8_t>&) override;

	virtual HexEngine::IConstantBuffer*   CreateConstantBuffer(uint32_t size) override;
	virtual HexEngine::IStructuredBuffer* CreateStructuredBuffer(
		uint32_t elementStride, uint32_t elementCount,
		HexEngine::StructuredBufferFlags flags,
		HexEngine::ResourceUsage usage = HexEngine::ResourceUsage::Default,
		HexEngine::CpuAccess cpuAccess = HexEngine::CpuAccess::None,
		const void* initialData = nullptr) override;
	virtual HexEngine::IConstantBuffer*   GetEngineConstantBuffer(HexEngine::EngineConstantBuffer buffer) override;

	// -- State setters (B4 routes them through the pending-state struct) --
	virtual void SetConstantBufferVS(uint32_t slot, HexEngine::IConstantBuffer* buf) override;
	virtual void SetConstantBufferPS(uint32_t slot, HexEngine::IConstantBuffer* buf) override;
	virtual void SetConstantBufferGS(uint32_t slot, HexEngine::IConstantBuffer* buf) override;
	virtual void SetConstantBufferCS(uint32_t slot, HexEngine::IConstantBuffer* buf) override;
	virtual void SetIndexBuffer(HexEngine::IIndexBuffer* buf) override;
	virtual void SetVertexBuffer(uint32_t slot, HexEngine::IVertexBuffer* buf) override;
	virtual void SetTopology(HexEngine::PrimitiveTopology t) override;
	virtual void SetVertexShader(HexEngine::IShaderStage* s) override;
	virtual void SetPixelShader(HexEngine::IShaderStage* s) override;
	virtual void SetGeometryShader(HexEngine::IShaderStage* s) override;
	virtual void SetComputeShader(HexEngine::IShaderStage* s) override;
	virtual void SetInputLayout(HexEngine::IInputLayout* l) override;
	virtual void SetTexture2D(uint32_t slot, HexEngine::ITexture2D* tex) override;
	virtual void SetTexture2D(HexEngine::ITexture2D* tex) override;
	virtual void SetTexture3D(HexEngine::ITexture3D* tex) override;
	virtual void SetGeometryTexture3D(uint32_t slot, HexEngine::ITexture3D* tex) override;
	virtual void SetVertexStructuredBuffer(uint32_t slot, HexEngine::IStructuredBuffer* buf) override;
	virtual void SetGeometryStructuredBuffer(uint32_t slot, HexEngine::IStructuredBuffer* buf) override;
	virtual void SetComputeTexture3D(uint32_t slot, HexEngine::ITexture3D* tex) override;
	virtual void SetComputeRwTexture3D(uint32_t slot, HexEngine::ITexture3D* tex) override;
	virtual void SetComputeStructuredBuffer(uint32_t slot, HexEngine::IStructuredBuffer* buf) override;
	virtual void SetComputeRwStructuredBuffer(uint32_t slot, HexEngine::IStructuredBuffer* buf, uint32_t = 0xFFFFFFFFu) override;
	virtual void ClearGeometryTexture3D(uint32_t slot) override;
	virtual void ClearVertexStructuredBuffer(uint32_t slot) override;
	virtual void ClearComputeTexture3D(uint32_t slot) override;
	virtual void ClearComputeRwTexture3D(uint32_t slot) override;
	virtual void ClearGeometryStructuredBuffer(uint32_t slot) override;
	virtual void ClearComputeStructuredBuffer(uint32_t slot) override;
	virtual void ClearComputeRwStructuredBuffer(uint32_t slot) override;
	virtual void SetTexture2DArray(uint32_t slot, const std::vector<HexEngine::ITexture2D*>& textures) override;
	virtual void SetTexture2DArray(const std::vector<HexEngine::ITexture2D*>& textures) override;

	virtual void SetRenderTarget(HexEngine::ITexture2D* renderTarget, HexEngine::ITexture2D* depthStencil = nullptr) override;
	virtual void SetRenderTargets(const std::vector<HexEngine::ITexture2D*>& rts, HexEngine::ITexture2D* depthStencil = nullptr) override;
	virtual void GetRenderTargets(std::vector<HexEngine::ITexture2D*>& outRts, HexEngine::ITexture2D** outDepthStencil = nullptr) override;
	virtual void SetRenderTargets(uint32_t numViews, const std::vector<HexEngine::ITexture2D*>& rts, HexEngine::ITexture2D* depthStencil) override;

	// -- Draw / dispatch (B4 real impls) --
	virtual void DrawIndexed(uint32_t numIndices) override;
	virtual void DrawIndexedInstanced(uint32_t numIndices, uint32_t instanceCount) override;
	virtual void DrawIndexedInstancedIndirect(void* argsBuffer, uint32_t alignedByteOffset = 0) override;
	virtual void Draw(uint32_t vertexCount, int32_t startVertexLocation = 0) override;
	virtual void DrawInstancedIndirect(HexEngine::IStructuredBuffer* argsBuffer, uint32_t alignedByteOffset = 0) override;
	virtual void Dispatch(uint32_t gx, uint32_t gy, uint32_t gz) override;
	virtual void DispatchIndirect(HexEngine::IStructuredBuffer* argsBuffer, uint32_t alignedByteOffset = 0) override;
	virtual void CopyStructureCount(HexEngine::IStructuredBuffer* sourceBuffer, HexEngine::IStructuredBuffer* destinationBuffer, uint32_t destinationByteOffset = 0) override;

	virtual void GetBackBufferDimensions(uint32_t& width, uint32_t& height) override;
	// Defined in the .cpp so the forward-declared TextureImporterD3D12 type
	// can be implicitly converted to IResourceLoader* there (the header
	// doesn't include the importer's full definition).
	virtual HexEngine::IResourceLoader* GetTextureLoader() override;

	virtual void SetDepthBufferState(HexEngine::DepthBufferState state) override { _depthState = state; }
	virtual HexEngine::DepthBufferState GetDepthBufferState() const override { return _depthState; }

	virtual void SetClearColour(const math::Color& colour) override { _clearColour = colour; }
	virtual void SetCullingMode(HexEngine::CullingMode mode) override { _cullingMode = mode; }
	virtual HexEngine::CullingMode GetCullingMode() const override { return _cullingMode; }

	virtual void* GetNativeDevice() override { return _device.Get(); }
	virtual void* GetNativeDeviceContext() override { return _directQueue.Get(); }

	virtual bool GetSupportedDisplayModes(std::vector<HexEngine::ScreenDisplayMode>&) override { return false; }

	virtual void SetPixelShaderResource(uint32_t slot, HexEngine::ITexture2D* tex) override;
	virtual void SetPixelShaderResource(HexEngine::ITexture2D* tex) override;
	virtual void SetPixelShaderResources(uint32_t slot, const std::vector<HexEngine::ITexture2D*>& textures) override;
	virtual void SetPixelShaderResources(const std::vector<HexEngine::ITexture2D*>& textures) override;
	virtual void UnbindAllPixelShaderResources() override;

	// These steer the auto-bind cursor used by the parameterless SetPixelShaderResource(tex)
	// / SetTexture2D(tex) overloads. Engine code calls SetBoundResourceIndex(N) to
	// reposition the cursor mid-frame before binding the next batch. The D3D11
	// plugin uses this same convention via _currentlyBoundSRVIndex; we share the
	// physical storage with _autoBindCursor here so the two stay coherent.
	virtual uint32_t GetBoundResourceIndex() override { return _autoBindCursor; }
	virtual void SetBoundResourceIndex(uint32_t value) override { _autoBindCursor = value; }

	virtual void BeginFrame(HexEngine::Window* window, HexEngine::ITexture2D* depthBuffer = nullptr) override;
	virtual void EndFrame(HexEngine::Window* window) override;

	virtual void SetViewports(const std::vector<HexEngine::Viewport>& viewports) override;
	virtual void SetViewport(const HexEngine::Viewport& viewport) override;
	virtual HexEngine::Viewport GetBackBufferViewport() const override { return _backBufferViewport; }

	virtual void SetBlendState(HexEngine::BlendState state) override { _blendState = state; }
	virtual HexEngine::BlendState GetBlendState() const override { return _blendState; }
	virtual int32_t GetCurrentMSAALevel() const override { return 1; }

	virtual void SetScissorRect(const HexEngine::ScissorRect& rect) override;
	virtual void SetScissorRects(const std::vector<HexEngine::ScissorRect>& rects) override;
	virtual void ClearScissorRect() override;

	virtual void ResetState() override {}

	/** @brief Returns the recording command list for B2's clear path. */
	ID3D12GraphicsCommandList* GetActiveCommandList() const { return _cmdList.Get(); }

	/** @brief Inserts a single-subresource TRANSITION barrier and updates the resource's tracked state. */
	void TransitionResource(Texture2DD3D12* tex, D3D12_RESOURCE_STATES newState);
	void TransitionResource(Texture3DD3D12* tex, D3D12_RESOURCE_STATES newState);
	void TransitionResource(StructuredBufferD3D12* buf, D3D12_RESOURCE_STATES newState);

	/**
	 * @brief If `tex` is in the currently-bound _pending.rtvs / dsv set,
	 *        transitions it back to the appropriate (RENDER_TARGET / DEPTH_WRITE)
	 *        state so the existing OMSetRenderTargets binding stays valid.
	 *
	 * D3D11 copies don't change resource state, so engine code freely interleaves
	 * SetRenderTarget(X) -> Draw -> X.CopyTo(Y) -> Draw with X still bound as
	 * the active RT. D3D12 CopyResource requires X to be in COPY_SOURCE, which
	 * leaves a stale OM binding pointing at a resource in the wrong state. The
	 * next draw fails INVALID_SUBRESOURCE_STATE. Call this after any operation
	 * that moves a texture out of its bound role to restore it.
	 */
	void RestoreBoundRoleIfNeeded(Texture2DD3D12* tex);

	/**
	 * @brief Queues a resource for release after the current frame's fence
	 *        completes.
	 *
	 * D3D12 - unlike D3D11 - doesn't track resource lifetime against the
	 * command list. If a Texture2D / Texture3D / Buffer wrapper is destroyed
	 * while a still-recording or still-in-flight command list references the
	 * underlying ID3D12Resource (via a barrier, RTV/SRV binding, or VBV /
	 * IBV / CBV), Close() and ExecuteCommandLists report
	 * OBJECT_DELETED_WHILE_STILL_IN_USE, the GPU reads freed memory, and
	 * DXGI_ERROR_DEVICE_HUNG follows.
	 *
	 * Wrapper Destroy() implementations call this with the underlying
	 * resource instead of dropping the ComPtr directly. The resource is held
	 * alive until at least GetPendingFenceValue() has signalled - which
	 * covers everything currently recorded plus the in-flight frames.
	 * Drained in BeginFrame after the per-slot fence wait.
	 */
	void DeferredRelease(Microsoft::WRL::ComPtr<ID3D12Resource> resource);

	/**
	 * @brief Stages `byteSize` bytes from `src` into `dst` via a temporary
	 *        upload buffer and a CopyBufferRegion call on the open list.
	 *
	 * Used by StructuredBufferD3D12::SetData when the buffer lives in the
	 * default heap. The upload buffer is kept alive in `_pendingUploads`
	 * until the matching frame fence completes.
	 */
	bool UploadBufferData(StructuredBufferD3D12* dst, const void* src, uint32_t byteSize, uint32_t dstByteOffset);

	/** @brief Stages texture pixel data via a temporary upload buffer.
	 *
	 *  @param srcRowPitch Source row stride. 0 means "tightly packed at the
	 *  format-natural rowBytes". WIC/DirectXTex decoded images can have a
	 *  larger pitch than the format-natural row size (alignment), and copying
	 *  with the wrong pitch garbles every row after the first - which is how
	 *  font-atlas text and icons silently render as noise / disappear. */
	bool UploadTextureData(Texture2DD3D12* dst, const void* src, uint32_t byteSize, uint32_t srcRowPitch = 0);
	bool UploadTextureData(Texture3DD3D12* dst, const void* src, uint32_t byteSize, uint32_t srcRowPitch = 0, uint32_t srcSlicePitch = 0);

	// Descriptor heap access for resource classes.
	DescriptorHeapAllocator& RtvHeap()   { return _rtvHeap; }
	DescriptorHeapAllocator& DsvHeap()   { return _dsvHeap; }
	DescriptorHeapAllocator& CbvSrvUavHeap() { return _cbvSrvUavHeap; }

	// Index in [0, kFrameCount) for the frame currently being recorded.
	// Dynamic resources (VB / IB / CB) use this to pick which sub-slot of their
	// internal ring to write to so they never trample a region the GPU is
	// still reading from a frame N-1 or N-2 in flight. Updated at the top of
	// BeginFrame after the swap chain advances.
	uint32_t GetCurrentRingSlot() const { return _currentRingSlot; }
	static constexpr uint32_t GetRingCount() { return kFrameCount; }
	ID3D12Device* GetDevice() const { return _device.Get(); }

	// Fence helpers for dynamic-buffer pools. Dynamic VB / IB / CB allocate a
	// fresh sub-resource per Write and stamp it with the fence value that
	// EndFrame will signal for the current frame. A sub-resource is safe to
	// reuse once GetCompletedFenceValue() >= its stamped value.
	uint64_t GetCompletedFenceValue() const { return _fence ? _fence->GetCompletedValue() : 0ULL; }
	uint64_t GetPendingFenceValue()   const { return _nextFenceValue; }

private:
	bool CreateDeviceAndQueue();
	void WaitForGpu();
	void FlushPendingUploads();
	void PumpDebugMessages();

	/**
	 * @brief Ensures _cmdList is in recording state, opening it on the
	 *        current frame's allocator if it was closed.
	 *
	 * Asset loads can happen at any time during the editor / engine update
	 * (FolderExplorer hovering a folder, a hot-reloaded texture coming back
	 * from disk, an init-time importer registering before BeginFrame).
	 * UploadTextureData / UploadBufferData / TransitionResource all need a
	 * recording cmd list. If we're between EndFrame and the next BeginFrame
	 * (cmd list closed), this resets the list onto the current allocator
	 * after a fence wait. BeginFrame then drains whatever was recorded.
	 */
	void EnsureCmdListRecording();

	// Called from BeginFrame on the main thread. Drains queued cross-thread
	// uploads (textures / buffers enqueued by non-main threads via the
	// dispatch in UploadTextureData / UploadBufferData) and records each
	// onto the cmd list, then frees the queue.
	void DrainCrossThreadUploads();

	// If the device has been removed, dump DRED breadcrumbs + page-fault
	// info to LogFile. Returns true if device-removed was detected (caller
	// should then refuse to do further GPU work). Cheap to call every
	// BeginFrame - just queries _device->GetDeviceRemovedReason.
	bool CheckAndReportDeviceRemoval();

	// Unconditionally dump DRED state (breadcrumbs + page-fault output)
	// regardless of what GetDeviceRemovedReason says. Used from the
	// ID3D12InfoQueue1 callback at the moment the runtime emits a
	// device-removal message - by the time GetDeviceRemovedReason
	// returns a failure HRESULT, the engine has often already crashed.
	// One-shot via static guard.
	void DumpDredNow(const char* triggerSource);

	static constexpr uint32_t kFrameCount = 3;

	Microsoft::WRL::ComPtr<IDXGIFactory6>           _dxgiFactory;
	Microsoft::WRL::ComPtr<IDXGIAdapter1>            _dxgiAdapter;
	Microsoft::WRL::ComPtr<ID3D12Device>             _device;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue>       _directQueue;

	struct FrameContext
	{
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> alloc;
		uint64_t fenceValue = 0;
	};
	FrameContext                                     _frames[kFrameCount];
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> _cmdList;

	Microsoft::WRL::ComPtr<ID3D12Fence>              _fence;
	HANDLE                                           _fenceEvent     = nullptr;
	uint64_t                                         _nextFenceValue = 1;

	// Indirect-execution command signatures, created lazily on first use by
	// EnsureIndirectSignatures(). D3D12 (unlike D3D11) requires an
	// ID3D12CommandSignature describing the argument layout before
	// ExecuteIndirect. All three are "simple" signatures (a single draw /
	// dispatch argument, no root-argument changes) so they need no root
	// signature. Byte strides match the corresponding D3D12_*_ARGUMENTS
	// structs, which are layout-identical to the D3D11 indirect-args the
	// engine already fills.
	Microsoft::WRL::ComPtr<ID3D12CommandSignature>   _drawIndexedIndirectSig;
	Microsoft::WRL::ComPtr<ID3D12CommandSignature>   _drawIndirectSig;
	Microsoft::WRL::ComPtr<ID3D12CommandSignature>   _dispatchIndirectSig;
	Microsoft::WRL::ComPtr<ID3D12InfoQueue>          _infoQueue; // Debug-only; polled in EndFrame to mirror validation messages into LogFile.
	// True when _cmdList is in the recording state and safe to write commands
	// into. Initialised to true at Create() so init-time resource uploads
	// (texture initialData, vertex/index buffer fill) can record directly.
	// BeginFrame flips it true after Reset, EndFrame flips it false after
	// Close. The init-time block before the first BeginFrame writes into
	// _frames[0].alloc; first BeginFrame drains it (Close + Execute + fence
	// wait) before resetting that allocator.
	bool                                             _cmdListIsRecording = false;
	// Cross-thread upload queue. The engine loads assets on background
	// threads (TextureImporterD3D12 from FolderExplorer / async scene
	// load, etc.); those threads can't touch the cmd list directly
	// without causing CORRUPTED_MULTITHREADING. They also can't hold a
	// device-level mutex across the call because the engine itself
	// holds scene locks above the call, which would deadlock against
	// the main thread's frame work calling INTO scene code.
	//
	// Solution: non-main threads enqueue uploads here; BeginFrame
	// (on main thread) drains the queue and records each upload onto
	// the cmd list normally. The source data is copied at enqueue time
	// so the caller's lifetime doesn't matter. Destination wrappers
	// stay alive because the engine retains them past the create call.
	struct PendingCrossThreadUpload
	{
		enum class Kind { Tex2D, Tex3D, Buffer } kind = Kind::Tex2D;
		void*                 dst = nullptr;       ///< Texture2DD3D12* / Texture3DD3D12* / StructuredBufferD3D12*
		std::vector<uint8_t>  data;
		uint32_t              byteSize       = 0;
		uint32_t              srcRowPitch    = 0;
		uint32_t              srcSlicePitch  = 0;
		uint32_t              dstByteOffset  = 0;
	};
	std::deque<PendingCrossThreadUpload>             _crossThreadUploads;
	std::mutex                                       _crossThreadUploadsMutex;
	std::thread::id                                  _mainThreadId; ///< set at Create() time
	// 0-based ring index for the currently-recording frame. Mirrors the swap
	// chain's backbuffer index but is also valid before the first BeginFrame
	// (defaults to 0) so init-time buffer fills land in slot 0.
	uint32_t                                         _currentRingSlot = 0;

	// Ring buffer of recent draws for hang diagnostics. DRED breadcrumbs
	// only record opcodes (not which shader / PSO / material), so a GPU
	// hang on a DRAWINDEXEDINSTANCED tells us nothing about WHICH draw.
	// We capture the VS / PS bytecode pointers + index count + draw number
	// CPU-side just before each Draw*, then dump the last N entries when
	// DumpDredNow fires.
	struct DrawTrace
	{
		uint64_t drawIndex     = 0; ///< monotonic counter across all draws
		void*    vsBytecode    = nullptr;
		void*    psBytecode    = nullptr;
		uint32_t indexCount    = 0;
		uint32_t instanceCount = 0;
		uint32_t rtCount       = 0;
		uint32_t dsBound       = 0;
	};
	static constexpr uint32_t kDrawTraceCapacity = 64;
	DrawTrace                                        _drawTrace[kDrawTraceCapacity] = {};
	uint64_t                                         _drawTraceCount = 0; ///< total Draws issued; ring slot = (count-1) % capacity

	// Dedicated allocator for between-frame uploads. EnsureCmdListRecording
	// (which fires when an asset loads outside a frame boundary - hot reload,
	// FolderExplorer hover, init-time importer) Resets the cmd list onto this
	// allocator instead of the per-frame ring slot's. That way BeginFrame's
	// later Reset of the frame slot's allocator can't conflict with the cmd
	// list's lingering association from the between-frame work, which the
	// validation layer otherwise flags as COMMAND_ALLOCATOR_CANNOT_RESET.
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator>   _uploadAlloc;
	uint64_t                                         _uploadFenceValue = 0;

	struct WindowContext
	{
		Microsoft::WRL::ComPtr<IDXGISwapChain3>      swapChain;
		Texture2DD3D12                                backbuffers[kFrameCount];
		uint32_t                                      width  = 0;
		uint32_t                                      height = 0;
		uint32_t                                      currentFrameIndex = 0;
	};
	std::unordered_map<HexEngine::Window*, WindowContext> _windowCtx;
	WindowContext*                                   _activeWindow = nullptr;

	// CPU-only descriptor heaps. B4 will add a shader-visible heap for binding
	// at draw time; B3 only needs the per-resource slots.
	DescriptorHeapAllocator                          _rtvHeap;
	DescriptorHeapAllocator                          _dsvHeap;
	DescriptorHeapAllocator                          _cbvSrvUavHeap;

	// Pre-created null CBV/SRV/UAV CPU descriptors used to fill unbound table
	// slots at FlushGraphics time. Shaders that read from a slot they
	// declared but the engine didn't bind would otherwise pick up arbitrary
	// stale descriptor bits from prior frames - undefined behaviour that's
	// been observed to TDR even for trivial shaders (UIBasic hang).
	D3D12_CPU_DESCRIPTOR_HANDLE                      _nullCbv = {};
	D3D12_CPU_DESCRIPTOR_HANDLE                      _nullSrv = {};
	D3D12_CPU_DESCRIPTOR_HANDLE                      _nullUav = {};

	// Engine-managed constant buffers. SceneRenderer / Mesh / DiffuseGI etc.
	// retrieve these by enum (PerFrameBuffer, PerObjectBuffer, ...) and Write
	// per-draw data into them. The D3D11 plugin both allocates and implicitly
	// binds the PerObjectBuffer at slot 1 (VS+PS) inside every Draw* call;
	// we mirror that behaviour in GraphicsDeviceD3D12_Draws.cpp's Draw paths.
	HexEngine::IConstantBuffer*                      _engineConstantBuffers[(uint32_t)HexEngine::EngineConstantBuffer::NumEngineConstantBuffers] = {};

	// Upload buffers awaiting fence completion - we keep them alive until the
	// GPU's done copying out of them. Each entry pairs the resource with the
	// fence value it has to outlive.
	struct PendingUpload
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		uint64_t                                fenceValue = 0;
	};
	std::deque<PendingUpload>                        _pendingUploads;

	// Deferred-release queue for resources whose owning wrapper has been
	// Destroy()ed but whose underlying ID3D12Resource may still be referenced
	// by an in-flight (or currently-recording) command list. Drained in
	// BeginFrame after the per-slot fence wait. Same shape as PendingUpload
	// but used for engine-side resources rather than transient upload stages.
	struct PendingDeletion
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		uint64_t                                fenceValue = 0;
	};
	std::deque<PendingDeletion>                      _pendingDeletions;

	math::Color                                      _clearColour {0.32f, 0.36f, 0.43f, 1.0f};
	HexEngine::Viewport                              _viewport;
	HexEngine::Viewport                              _backBufferViewport;
	HexEngine::ScissorRect                           _scissor;
	HexEngine::BlendState                            _blendState   = HexEngine::BlendState::Opaque;
	HexEngine::CullingMode                           _cullingMode  = HexEngine::CullingMode::BackFace;
	HexEngine::DepthBufferState                      _depthState   = HexEngine::DepthBufferState::DepthDefault;

	// PNG/JPG/DDS/etc decoder + IResource factory. Created in Create() once
	// the device is up so it has somewhere to allocate textures from. The
	// ResourceSystem owns the registration lifetime.
	class TextureImporterD3D12*                      _textureLoader = nullptr;

	// ---- B4: pipeline + bind plumbing ----

	RootSignatureD3D12                               _rootSig;
	PsoCache                                         _psoCache;
	ShaderVisibleHeap                                _shaderVisibleHeap;

	// Pending pipeline state - state setters fill these in; the next draw
	// hashes them into a PSO key and resolves the cache. Compute shader
	// stays in here too; Dispatch reads it independently.
	struct PendingState
	{
		ShaderStageD3D12*       vs              = nullptr;
		ShaderStageD3D12*       ps              = nullptr;
		ShaderStageD3D12*       gs              = nullptr;
		ShaderStageD3D12*       cs              = nullptr;
		InputLayoutD3D12*       inputLayout     = nullptr;
		HexEngine::PrimitiveTopology topology   = HexEngine::PrimitiveTopology::TriangleList;
		// Currently-bound RTVs and depth; flush() also infers the PSO's RTV
		// formats from here.
		HexEngine::ITexture2D*  rtvs[8]         = {};
		uint32_t                rtCount         = 0;
		HexEngine::ITexture2D*  dsv             = nullptr;
		// Vertex / index buffers as they were last set.
		HexEngine::IVertexBuffer* vbs[8]        = {};
		HexEngine::IIndexBuffer*  ib            = nullptr;
		bool                    dirty = true;
	};

	// Currently-bound resource handles. We keep CPU descriptor handles here
	// (sourced from each resource's per-type heap slot) and copy them into the
	// shader-visible heap at draw time.
	struct PendingBindings
	{
		D3D12_CPU_DESCRIPTOR_HANDLE cbvs[RootSignatureD3D12::kCbvCount] = {};
		D3D12_CPU_DESCRIPTOR_HANDLE srvs[RootSignatureD3D12::kSrvCount] = {};
		D3D12_CPU_DESCRIPTOR_HANDLE uavs[RootSignatureD3D12::kUavCount] = {};
		// Highest slot index used + 1 - lets the bind shrink the descriptor-
		// table copy to "only what's actually filled in".
		uint32_t cbvHighWater = 0;
		uint32_t srvHighWater = 0;
		uint32_t uavHighWater = 0;
	};

	PendingState     _pending;
	PendingBindings  _bindings;
	// Tracks where SetPixelShaderResource(slot=auto) writes to next - matches
	// the D3D11 plugin's bound-resource-index behaviour so call sites that
	// rely on the running counter stay consistent.
	uint32_t         _autoBindCursor = 0;

	bool   FlushGraphics();   ///< called by Draw* before issuing the draw
	bool   FlushCompute();    ///< called by Dispatch* before issuing the dispatch
	void   ResetPendingForBeginFrame();
	bool   EnsureIndirectSignatures(); ///< lazily creates the 3 ExecuteIndirect command signatures

	// D3D11->D3D12 hazard fix: D3D11 silently auto-unbinds an RT/DS when you
	// bind it as an SRV. D3D12 doesn't - the next draw validates the bound
	// RTV's state and errors out. This helper mirrors the D3D11 plugin's
	// SetPixelShaderResource hazard check: if `tex` is currently bound as an
	// RTV or DSV, drop the OMSetRenderTargets state (the caller is then
	// expected to SetRenderTarget back to a usable target before the next
	// draw, which is exactly what the engine's pipeline ordering does).
	void   UnbindAsRenderTargetIfBound(HexEngine::ITexture2D* tex);
};
