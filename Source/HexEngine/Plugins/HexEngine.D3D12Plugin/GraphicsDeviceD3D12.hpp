
#pragma once

#include <HexEngine.Core/Graphics/IGraphicsDevice.hpp>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <unordered_map>

#include "Texture2DD3D12.hpp"

/**
 * @brief D3D12 IGraphicsDevice implementation.
 *
 * Phase B2 (current): real device, command queue, swap chain, frame fencing,
 * backbuffer wrapping, BeginFrame/EndFrame with backbuffer clear + present.
 * The rest of the IGraphicsDevice surface is silent no-ops returning
 * nullptr/defaults so the engine boots without crashing inside vtable
 * dispatch. Without B3+ no scene geometry actually renders.
 *
 * Phase B3+ (future): textures, buffers, descriptor heaps, PSOs.
 *
 * Important: this header MUST NOT include <d3d11.h>. The D3D11 compat shims
 * on IGraphicsDevice are gated on `__d3d11_h__` and would leak D3D11 into the
 * D3D12 plugin if we ever transitively pulled in that header.
 */
class GraphicsDeviceD3D12 : public HexEngine::IGraphicsDevice
{
public:
	GraphicsDeviceD3D12() = default;
	virtual ~GraphicsDeviceD3D12() override { Destroy(); }

	virtual HexEngine::GraphicsBackend GetBackend() const override { return HexEngine::GraphicsBackend::D3D12; }

	// -- Lifecycle (real B2 implementations) --
	virtual bool Create() override;
	virtual void Destroy() override;
	virtual bool AttachToWindow(HexEngine::Window* window) override;
	virtual void Resize(HexEngine::Window* window, uint32_t width, uint32_t height) override;

	// -- Resources --
	virtual HexEngine::ITexture2D* GetBackBuffer(HexEngine::Window* window = nullptr) override;
	virtual HexEngine::ITexture2D* CreateTexture(HexEngine::ITexture2D*) override { return nullptr; }
	virtual HexEngine::ITexture2D* CreateTexture2D(const HexEngine::TextureDesc&, const HexEngine::SubresourceData* = nullptr) override { return nullptr; }
	virtual HexEngine::ITexture3D* CreateTexture3D(const HexEngine::TextureDesc&, const HexEngine::SubresourceData* = nullptr) override { return nullptr; }
	virtual HexEngine::IVertexBuffer* CreateVertexBuffer(const HexEngine::BufferDesc&, const void* = nullptr) override { return nullptr; }
	virtual HexEngine::IIndexBuffer*  CreateIndexBuffer(const HexEngine::BufferDesc&, const void* = nullptr)  override { return nullptr; }

	virtual HexEngine::IShaderStage* CreateVertexShader(std::vector<uint8_t>&)   override { return nullptr; }
	virtual HexEngine::IShaderStage* CreatePixelShader(std::vector<uint8_t>&)    override { return nullptr; }
	virtual HexEngine::IShaderStage* CreateGeometryShader(std::vector<uint8_t>&) override { return nullptr; }
	virtual HexEngine::IShaderStage* CreateComputeShader(std::vector<uint8_t>&)  override { return nullptr; }
	virtual HexEngine::IShaderStage* CreateComputeShaderFromSource(const std::string&, const std::string& = "MainCS") override { return nullptr; }
	virtual HexEngine::IInputLayout* CreateInputLayout(const HexEngine::InputElement*, uint32_t, const std::vector<uint8_t>&) override { return nullptr; }

	virtual HexEngine::IConstantBuffer* CreateConstantBuffer(uint32_t) override { return nullptr; }
	virtual HexEngine::IStructuredBuffer* CreateStructuredBuffer(
		uint32_t, uint32_t, HexEngine::StructuredBufferFlags,
		HexEngine::ResourceUsage = HexEngine::ResourceUsage::Default,
		HexEngine::CpuAccess = HexEngine::CpuAccess::None,
		const void* = nullptr) override { return nullptr; }
	virtual HexEngine::IConstantBuffer* GetEngineConstantBuffer(HexEngine::EngineConstantBuffer) override { return nullptr; }

	// -- State setters (silent no-ops in B2; B4 wires them to command list) --
	virtual void SetConstantBufferVS(uint32_t, HexEngine::IConstantBuffer*) override {}
	virtual void SetConstantBufferPS(uint32_t, HexEngine::IConstantBuffer*) override {}
	virtual void SetConstantBufferGS(uint32_t, HexEngine::IConstantBuffer*) override {}
	virtual void SetConstantBufferCS(uint32_t, HexEngine::IConstantBuffer*) override {}
	virtual void SetIndexBuffer(HexEngine::IIndexBuffer*) override {}
	virtual void SetVertexBuffer(uint32_t, HexEngine::IVertexBuffer*) override {}
	virtual void SetTopology(HexEngine::PrimitiveTopology) override {}
	virtual void SetVertexShader(HexEngine::IShaderStage*) override {}
	virtual void SetPixelShader(HexEngine::IShaderStage*) override {}
	virtual void SetGeometryShader(HexEngine::IShaderStage*) override {}
	virtual void SetComputeShader(HexEngine::IShaderStage*) override {}
	virtual void SetInputLayout(HexEngine::IInputLayout*) override {}
	virtual void SetTexture2D(uint32_t, HexEngine::ITexture2D*) override {}
	virtual void SetTexture2D(HexEngine::ITexture2D*) override {}
	virtual void SetTexture3D(HexEngine::ITexture3D*) override {}
	virtual void SetGeometryTexture3D(uint32_t, HexEngine::ITexture3D*) override {}
	virtual void SetVertexStructuredBuffer(uint32_t, HexEngine::IStructuredBuffer*) override {}
	virtual void SetGeometryStructuredBuffer(uint32_t, HexEngine::IStructuredBuffer*) override {}
	virtual void SetComputeTexture3D(uint32_t, HexEngine::ITexture3D*) override {}
	virtual void SetComputeRwTexture3D(uint32_t, HexEngine::ITexture3D*) override {}
	virtual void SetComputeStructuredBuffer(uint32_t, HexEngine::IStructuredBuffer*) override {}
	virtual void SetComputeRwStructuredBuffer(uint32_t, HexEngine::IStructuredBuffer*, uint32_t = 0xFFFFFFFFu) override {}
	virtual void ClearGeometryTexture3D(uint32_t) override {}
	virtual void ClearVertexStructuredBuffer(uint32_t) override {}
	virtual void ClearComputeTexture3D(uint32_t) override {}
	virtual void ClearComputeRwTexture3D(uint32_t) override {}
	virtual void ClearGeometryStructuredBuffer(uint32_t) override {}
	virtual void ClearComputeStructuredBuffer(uint32_t) override {}
	virtual void ClearComputeRwStructuredBuffer(uint32_t) override {}
	virtual void SetTexture2DArray(uint32_t, const std::vector<HexEngine::ITexture2D*>&) override {}
	virtual void SetTexture2DArray(const std::vector<HexEngine::ITexture2D*>&) override {}

	virtual void SetRenderTarget(HexEngine::ITexture2D* renderTarget, HexEngine::ITexture2D* depthStencil = nullptr) override;
	virtual void SetRenderTargets(const std::vector<HexEngine::ITexture2D*>&, HexEngine::ITexture2D* = nullptr) override {}
	virtual void GetRenderTargets(std::vector<HexEngine::ITexture2D*>&, HexEngine::ITexture2D** = nullptr) override {}
	virtual void SetRenderTargets(uint32_t, const std::vector<HexEngine::ITexture2D*>&, HexEngine::ITexture2D*) override {}

	// -- Draw / dispatch (no-ops in B2; B4 implements them) --
	virtual void DrawIndexed(uint32_t) override {}
	virtual void DrawIndexedInstanced(uint32_t, uint32_t) override {}
	virtual void DrawIndexedInstancedIndirect(void*, uint32_t = 0) override {}
	virtual void Draw(uint32_t, int32_t = 0) override {}
	virtual void DrawInstancedIndirect(HexEngine::IStructuredBuffer*, uint32_t = 0) override {}
	virtual void Dispatch(uint32_t, uint32_t, uint32_t) override {}
	virtual void DispatchIndirect(HexEngine::IStructuredBuffer*, uint32_t = 0) override {}
	virtual void CopyStructureCount(HexEngine::IStructuredBuffer*, HexEngine::IStructuredBuffer*, uint32_t = 0) override {}

	virtual void GetBackBufferDimensions(uint32_t& width, uint32_t& height) override;
	virtual HexEngine::IResourceLoader* GetTextureLoader() override { return nullptr; }

	virtual void SetDepthBufferState(HexEngine::DepthBufferState state) override { _depthState = state; }
	virtual HexEngine::DepthBufferState GetDepthBufferState() const override { return _depthState; }

	virtual void SetClearColour(const math::Color& colour) override { _clearColour = colour; }
	virtual void SetCullingMode(HexEngine::CullingMode mode) override { _cullingMode = mode; }
	virtual HexEngine::CullingMode GetCullingMode() const override { return _cullingMode; }

	virtual void* GetNativeDevice() override { return _device.Get(); }
	virtual void* GetNativeDeviceContext() override { return _directQueue.Get(); }

	virtual bool GetSupportedDisplayModes(std::vector<HexEngine::ScreenDisplayMode>&) override { return false; }

	virtual void SetPixelShaderResource(uint32_t, HexEngine::ITexture2D*) override {}
	virtual void SetPixelShaderResource(HexEngine::ITexture2D*) override {}
	virtual void SetPixelShaderResources(uint32_t, const std::vector<HexEngine::ITexture2D*>&) override {}
	virtual void SetPixelShaderResources(const std::vector<HexEngine::ITexture2D*>&) override {}
	virtual void UnbindAllPixelShaderResources() override {}

	virtual uint32_t GetBoundResourceIndex() override { return _boundResourceIndex; }
	virtual void SetBoundResourceIndex(uint32_t value) override { _boundResourceIndex = value; }

	virtual void BeginFrame(HexEngine::Window* window, HexEngine::ITexture2D* depthBuffer = nullptr) override;
	virtual void EndFrame(HexEngine::Window* window) override;

	virtual void SetViewports(const std::vector<HexEngine::Viewport>&) override {}
	virtual void SetViewport(const HexEngine::Viewport& viewport) override { _viewport = viewport; }
	virtual HexEngine::Viewport GetBackBufferViewport() const override { return _backBufferViewport; }

	virtual void SetBlendState(HexEngine::BlendState state) override { _blendState = state; }
	virtual HexEngine::BlendState GetBlendState() const override { return _blendState; }
	virtual int32_t GetCurrentMSAALevel() const override { return 1; }

	virtual void SetScissorRect(const HexEngine::ScissorRect& rect) override { _scissor = rect; }
	virtual void SetScissorRects(const std::vector<HexEngine::ScissorRect>&) override {}
	virtual void ClearScissorRect() override {}

	virtual void ResetState() override {}

	// Public so Texture2DD3D12::ClearRenderTargetView can reach the active
	// command list + insert the state transition before clearing.
	ID3D12GraphicsCommandList* GetActiveCommandList() const { return _cmdList.Get(); }
	void TransitionResource(Texture2DD3D12* tex, D3D12_RESOURCE_STATES newState);

private:
	bool CreateDeviceAndQueue();
	void WaitForGpu();

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

	// One swap-chain + RTV heap per window. Per-frame backbuffers live in
	// _backbuffers[]; the swap chain reports which one is current via
	// GetCurrentBackBufferIndex().
	struct WindowContext
	{
		Microsoft::WRL::ComPtr<IDXGISwapChain3>      swapChain;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
		Texture2DD3D12                                backbuffers[kFrameCount];
		UINT                                          rtvDescriptorSize = 0;
		uint32_t                                      width  = 0;
		uint32_t                                      height = 0;
		uint32_t                                      currentFrameIndex = 0;
	};
	std::unordered_map<HexEngine::Window*, WindowContext> _windowCtx;

	WindowContext*                                   _activeWindow = nullptr;

	math::Color                                      _clearColour {0.32f, 0.36f, 0.43f, 1.0f}; // matches D3D11 clear
	HexEngine::Viewport                              _viewport;
	HexEngine::Viewport                              _backBufferViewport;
	HexEngine::ScissorRect                           _scissor;
	HexEngine::BlendState                            _blendState   = HexEngine::BlendState::Opaque;
	HexEngine::CullingMode                           _cullingMode  = HexEngine::CullingMode::BackFace;
	HexEngine::DepthBufferState                      _depthState   = HexEngine::DepthBufferState::DepthDefault;
	uint32_t                                         _boundResourceIndex = 0;
};
