
#pragma once

#include <HexEngine.Core/Graphics/IGraphicsDevice.hpp>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <unordered_map>
#include <vector>
#include <deque>

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
	virtual void SetRenderTargets(const std::vector<HexEngine::ITexture2D*>&, HexEngine::ITexture2D* = nullptr) override {}
	virtual void GetRenderTargets(std::vector<HexEngine::ITexture2D*>&, HexEngine::ITexture2D** = nullptr) override {}
	virtual void SetRenderTargets(uint32_t, const std::vector<HexEngine::ITexture2D*>&, HexEngine::ITexture2D*) override {}

	// -- Draw / dispatch (B4 real impls) --
	virtual void DrawIndexed(uint32_t numIndices) override;
	virtual void DrawIndexedInstanced(uint32_t numIndices, uint32_t instanceCount) override;
	virtual void DrawIndexedInstancedIndirect(void* argsBuffer, uint32_t alignedByteOffset = 0) override;
	virtual void Draw(uint32_t vertexCount, int32_t startVertexLocation = 0) override;
	virtual void DrawInstancedIndirect(HexEngine::IStructuredBuffer* argsBuffer, uint32_t alignedByteOffset = 0) override;
	virtual void Dispatch(uint32_t gx, uint32_t gy, uint32_t gz) override;
	virtual void DispatchIndirect(HexEngine::IStructuredBuffer* argsBuffer, uint32_t alignedByteOffset = 0) override;
	virtual void CopyStructureCount(HexEngine::IStructuredBuffer*, HexEngine::IStructuredBuffer*, uint32_t = 0) override {}

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

	virtual uint32_t GetBoundResourceIndex() override { return _boundResourceIndex; }
	virtual void SetBoundResourceIndex(uint32_t value) override { _boundResourceIndex = value; }

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
	 * @brief Stages `byteSize` bytes from `src` into `dst` via a temporary
	 *        upload buffer and a CopyBufferRegion call on the open list.
	 *
	 * Used by StructuredBufferD3D12::SetData when the buffer lives in the
	 * default heap. The upload buffer is kept alive in `_pendingUploads`
	 * until the matching frame fence completes.
	 */
	bool UploadBufferData(StructuredBufferD3D12* dst, const void* src, uint32_t byteSize, uint32_t dstByteOffset);

	/** @brief Stages texture pixel data via a temporary upload buffer. */
	bool UploadTextureData(Texture2DD3D12* dst, const void* src, uint32_t byteSize);
	bool UploadTextureData(Texture3DD3D12* dst, const void* src, uint32_t byteSize);

	// Descriptor heap access for resource classes.
	DescriptorHeapAllocator& RtvHeap()   { return _rtvHeap; }
	DescriptorHeapAllocator& DsvHeap()   { return _dsvHeap; }
	DescriptorHeapAllocator& CbvSrvUavHeap() { return _cbvSrvUavHeap; }

private:
	bool CreateDeviceAndQueue();
	void WaitForGpu();
	void FlushPendingUploads();

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

	// Upload buffers awaiting fence completion - we keep them alive until the
	// GPU's done copying out of them. Each entry pairs the resource with the
	// fence value it has to outlive.
	struct PendingUpload
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		uint64_t                                fenceValue = 0;
	};
	std::deque<PendingUpload>                        _pendingUploads;

	math::Color                                      _clearColour {0.32f, 0.36f, 0.43f, 1.0f};
	HexEngine::Viewport                              _viewport;
	HexEngine::Viewport                              _backBufferViewport;
	HexEngine::ScissorRect                           _scissor;
	HexEngine::BlendState                            _blendState   = HexEngine::BlendState::Opaque;
	HexEngine::CullingMode                           _cullingMode  = HexEngine::CullingMode::BackFace;
	HexEngine::DepthBufferState                      _depthState   = HexEngine::DepthBufferState::DepthDefault;
	uint32_t                                         _boundResourceIndex = 0;

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
};
