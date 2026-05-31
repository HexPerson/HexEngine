
#include "GraphicsDeviceD3D12.hpp"
#include <HexEngine.Core/HexEngine.hpp>

// Phase A skeleton. Every method is a stub - Create() returns false so the
// engine fails fast and the rest of the surface is never exercised. The macro
// keeps the noise low while making it obvious which methods still need a real
// D3D12 implementation when Phase B begins.
#define D3D12_STUB(name) LOG_CRIT("GraphicsDeviceD3D12::" name " not implemented (Phase A skeleton)")

bool GraphicsDeviceD3D12::Create()
{
	LOG_WARN("HexEngine.D3D12Plugin: Create() called on the Phase A skeleton - D3D12 backend is not yet implemented. Set r_renderer = 1 (D3D11) or 0 (auto) to use the working backend.");
	return false;
}

void GraphicsDeviceD3D12::Destroy() {}

bool GraphicsDeviceD3D12::AttachToWindow(HexEngine::Window*)                     { D3D12_STUB("AttachToWindow"); return false; }
void GraphicsDeviceD3D12::Resize(HexEngine::Window*, uint32_t, uint32_t)         { D3D12_STUB("Resize"); }
HexEngine::ITexture2D* GraphicsDeviceD3D12::GetBackBuffer(HexEngine::Window*)    { D3D12_STUB("GetBackBuffer"); return nullptr; }
HexEngine::ITexture2D* GraphicsDeviceD3D12::CreateTexture(HexEngine::ITexture2D*) { D3D12_STUB("CreateTexture"); return nullptr; }
HexEngine::ITexture2D* GraphicsDeviceD3D12::CreateTexture2D(const HexEngine::TextureDesc&, const HexEngine::SubresourceData*) { D3D12_STUB("CreateTexture2D"); return nullptr; }
HexEngine::ITexture3D* GraphicsDeviceD3D12::CreateTexture3D(const HexEngine::TextureDesc&, const HexEngine::SubresourceData*) { D3D12_STUB("CreateTexture3D"); return nullptr; }
HexEngine::IVertexBuffer* GraphicsDeviceD3D12::CreateVertexBuffer(const HexEngine::BufferDesc&, const void*) { D3D12_STUB("CreateVertexBuffer"); return nullptr; }
HexEngine::IIndexBuffer*  GraphicsDeviceD3D12::CreateIndexBuffer(const HexEngine::BufferDesc&, const void*)  { D3D12_STUB("CreateIndexBuffer"); return nullptr; }

HexEngine::IShaderStage* GraphicsDeviceD3D12::CreateVertexShader(std::vector<uint8_t>&)   { D3D12_STUB("CreateVertexShader"); return nullptr; }
HexEngine::IShaderStage* GraphicsDeviceD3D12::CreatePixelShader(std::vector<uint8_t>&)    { D3D12_STUB("CreatePixelShader"); return nullptr; }
HexEngine::IShaderStage* GraphicsDeviceD3D12::CreateGeometryShader(std::vector<uint8_t>&) { D3D12_STUB("CreateGeometryShader"); return nullptr; }
HexEngine::IShaderStage* GraphicsDeviceD3D12::CreateComputeShader(std::vector<uint8_t>&)  { D3D12_STUB("CreateComputeShader"); return nullptr; }
HexEngine::IShaderStage* GraphicsDeviceD3D12::CreateComputeShaderFromSource(const std::string&, const std::string&) { D3D12_STUB("CreateComputeShaderFromSource"); return nullptr; }
HexEngine::IInputLayout* GraphicsDeviceD3D12::CreateInputLayout(const HexEngine::InputElement*, uint32_t, const std::vector<uint8_t>&) { D3D12_STUB("CreateInputLayout"); return nullptr; }

HexEngine::IConstantBuffer* GraphicsDeviceD3D12::CreateConstantBuffer(uint32_t) { D3D12_STUB("CreateConstantBuffer"); return nullptr; }
HexEngine::IStructuredBuffer* GraphicsDeviceD3D12::CreateStructuredBuffer(uint32_t, uint32_t, HexEngine::StructuredBufferFlags, HexEngine::ResourceUsage, HexEngine::CpuAccess, const void*) { D3D12_STUB("CreateStructuredBuffer"); return nullptr; }
HexEngine::IConstantBuffer* GraphicsDeviceD3D12::GetEngineConstantBuffer(HexEngine::EngineConstantBuffer) { D3D12_STUB("GetEngineConstantBuffer"); return nullptr; }

void GraphicsDeviceD3D12::SetConstantBufferVS(uint32_t, HexEngine::IConstantBuffer*) {}
void GraphicsDeviceD3D12::SetConstantBufferPS(uint32_t, HexEngine::IConstantBuffer*) {}
void GraphicsDeviceD3D12::SetConstantBufferGS(uint32_t, HexEngine::IConstantBuffer*) {}
void GraphicsDeviceD3D12::SetConstantBufferCS(uint32_t, HexEngine::IConstantBuffer*) {}

void GraphicsDeviceD3D12::SetIndexBuffer(HexEngine::IIndexBuffer*) {}
void GraphicsDeviceD3D12::SetVertexBuffer(uint32_t, HexEngine::IVertexBuffer*) {}
void GraphicsDeviceD3D12::SetTopology(HexEngine::PrimitiveTopology) {}

void GraphicsDeviceD3D12::SetVertexShader(HexEngine::IShaderStage*) {}
void GraphicsDeviceD3D12::SetPixelShader(HexEngine::IShaderStage*) {}
void GraphicsDeviceD3D12::SetGeometryShader(HexEngine::IShaderStage*) {}
void GraphicsDeviceD3D12::SetComputeShader(HexEngine::IShaderStage*) {}
void GraphicsDeviceD3D12::SetInputLayout(HexEngine::IInputLayout*) {}

void GraphicsDeviceD3D12::SetTexture2D(uint32_t, HexEngine::ITexture2D*) {}
void GraphicsDeviceD3D12::SetTexture2D(HexEngine::ITexture2D*) {}
void GraphicsDeviceD3D12::SetTexture3D(HexEngine::ITexture3D*) {}
void GraphicsDeviceD3D12::SetGeometryTexture3D(uint32_t, HexEngine::ITexture3D*) {}
void GraphicsDeviceD3D12::SetVertexStructuredBuffer(uint32_t, HexEngine::IStructuredBuffer*) {}
void GraphicsDeviceD3D12::SetGeometryStructuredBuffer(uint32_t, HexEngine::IStructuredBuffer*) {}
void GraphicsDeviceD3D12::SetComputeTexture3D(uint32_t, HexEngine::ITexture3D*) {}
void GraphicsDeviceD3D12::SetComputeRwTexture3D(uint32_t, HexEngine::ITexture3D*) {}
void GraphicsDeviceD3D12::SetComputeStructuredBuffer(uint32_t, HexEngine::IStructuredBuffer*) {}
void GraphicsDeviceD3D12::SetComputeRwStructuredBuffer(uint32_t, HexEngine::IStructuredBuffer*, uint32_t) {}
void GraphicsDeviceD3D12::ClearGeometryTexture3D(uint32_t) {}
void GraphicsDeviceD3D12::ClearVertexStructuredBuffer(uint32_t) {}
void GraphicsDeviceD3D12::ClearComputeTexture3D(uint32_t) {}
void GraphicsDeviceD3D12::ClearComputeRwTexture3D(uint32_t) {}
void GraphicsDeviceD3D12::ClearGeometryStructuredBuffer(uint32_t) {}
void GraphicsDeviceD3D12::ClearComputeStructuredBuffer(uint32_t) {}
void GraphicsDeviceD3D12::ClearComputeRwStructuredBuffer(uint32_t) {}

void GraphicsDeviceD3D12::SetTexture2DArray(uint32_t, const std::vector<HexEngine::ITexture2D*>&) {}
void GraphicsDeviceD3D12::SetTexture2DArray(const std::vector<HexEngine::ITexture2D*>&) {}

void GraphicsDeviceD3D12::SetRenderTarget(HexEngine::ITexture2D*, HexEngine::ITexture2D*) {}
void GraphicsDeviceD3D12::SetRenderTargets(const std::vector<HexEngine::ITexture2D*>&, HexEngine::ITexture2D*) {}
void GraphicsDeviceD3D12::GetRenderTargets(std::vector<HexEngine::ITexture2D*>&, HexEngine::ITexture2D**) {}
void GraphicsDeviceD3D12::SetRenderTargets(uint32_t, const std::vector<HexEngine::ITexture2D*>&, HexEngine::ITexture2D*) {}

void GraphicsDeviceD3D12::DrawIndexed(uint32_t) {}
void GraphicsDeviceD3D12::DrawIndexedInstanced(uint32_t, uint32_t) {}
void GraphicsDeviceD3D12::DrawIndexedInstancedIndirect(void*, uint32_t) {}
void GraphicsDeviceD3D12::Draw(uint32_t, int32_t) {}
void GraphicsDeviceD3D12::DrawInstancedIndirect(HexEngine::IStructuredBuffer*, uint32_t) {}
void GraphicsDeviceD3D12::Dispatch(uint32_t, uint32_t, uint32_t) {}
void GraphicsDeviceD3D12::DispatchIndirect(HexEngine::IStructuredBuffer*, uint32_t) {}
void GraphicsDeviceD3D12::CopyStructureCount(HexEngine::IStructuredBuffer*, HexEngine::IStructuredBuffer*, uint32_t) {}

void GraphicsDeviceD3D12::GetBackBufferDimensions(uint32_t& width, uint32_t& height) { width = 0; height = 0; }
HexEngine::IResourceLoader* GraphicsDeviceD3D12::GetTextureLoader() { return nullptr; }

void GraphicsDeviceD3D12::SetDepthBufferState(HexEngine::DepthBufferState) {}
HexEngine::DepthBufferState GraphicsDeviceD3D12::GetDepthBufferState() const { return HexEngine::DepthBufferState::DepthDefault; }

void GraphicsDeviceD3D12::SetClearColour(const math::Color&) {}
void GraphicsDeviceD3D12::SetCullingMode(HexEngine::CullingMode) {}
HexEngine::CullingMode GraphicsDeviceD3D12::GetCullingMode() const { return HexEngine::CullingMode::BackFace; }

void* GraphicsDeviceD3D12::GetNativeDevice() { return nullptr; }
void* GraphicsDeviceD3D12::GetNativeDeviceContext() { return nullptr; }

bool GraphicsDeviceD3D12::GetSupportedDisplayModes(std::vector<HexEngine::ScreenDisplayMode>&) { return false; }

void GraphicsDeviceD3D12::SetPixelShaderResource(uint32_t, HexEngine::ITexture2D*) {}
void GraphicsDeviceD3D12::SetPixelShaderResource(HexEngine::ITexture2D*) {}
void GraphicsDeviceD3D12::SetPixelShaderResources(uint32_t, const std::vector<HexEngine::ITexture2D*>&) {}
void GraphicsDeviceD3D12::SetPixelShaderResources(const std::vector<HexEngine::ITexture2D*>&) {}
void GraphicsDeviceD3D12::UnbindAllPixelShaderResources() {}

uint32_t GraphicsDeviceD3D12::GetBoundResourceIndex() { return 0; }
void GraphicsDeviceD3D12::SetBoundResourceIndex(uint32_t) {}

void GraphicsDeviceD3D12::BeginFrame(HexEngine::Window*, HexEngine::ITexture2D*) {}
void GraphicsDeviceD3D12::EndFrame(HexEngine::Window*) {}

void GraphicsDeviceD3D12::SetViewports(const std::vector<HexEngine::Viewport>&) {}
void GraphicsDeviceD3D12::SetViewport(const HexEngine::Viewport&) {}
HexEngine::Viewport GraphicsDeviceD3D12::GetBackBufferViewport() const { return HexEngine::Viewport(); }

void GraphicsDeviceD3D12::SetBlendState(HexEngine::BlendState) {}
HexEngine::BlendState GraphicsDeviceD3D12::GetBlendState() const { return HexEngine::BlendState::Opaque; }
int32_t GraphicsDeviceD3D12::GetCurrentMSAALevel() const { return 1; }

void GraphicsDeviceD3D12::SetScissorRect(const HexEngine::ScissorRect&) {}
void GraphicsDeviceD3D12::SetScissorRects(const std::vector<HexEngine::ScissorRect>&) {}
void GraphicsDeviceD3D12::ClearScissorRect() {}

void GraphicsDeviceD3D12::ResetState() {}

#undef D3D12_STUB
