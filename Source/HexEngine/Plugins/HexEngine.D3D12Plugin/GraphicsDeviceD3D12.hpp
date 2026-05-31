
#pragma once

#include <HexEngine.Core/Graphics/IGraphicsDevice.hpp>

/**
 * @brief D3D12 IGraphicsDevice implementation.
 *
 * Phase A skeleton: every virtual method is a stub. Create() returns false so
 * the engine fails fast under r_renderer = 2; the rest of the methods assert
 * or return null/defaults if anyone manages to get a usable device pointer
 * (they shouldn't - Game3DEnvironment::CreateGraphicsSystem aborts on
 * Create() == false).
 *
 * Phase B fills these in method-by-method. NOTE that this translation unit
 * deliberately does NOT include <d3d11.h>; if it ever does, it'll see the
 * D3D11 compatibility shims on IGraphicsDevice and the abstraction will have
 * leaked. Keep d3d12.h here; never d3d11.h.
 */
class GraphicsDeviceD3D12 : public HexEngine::IGraphicsDevice
{
public:
	GraphicsDeviceD3D12() = default;
	virtual ~GraphicsDeviceD3D12() override = default;

	virtual HexEngine::GraphicsBackend GetBackend() const override { return HexEngine::GraphicsBackend::D3D12; }

	// -- Lifecycle --
	virtual bool Create() override;
	virtual void Destroy() override;
	virtual bool AttachToWindow(HexEngine::Window* window) override;
	virtual void Resize(HexEngine::Window* window, uint32_t width, uint32_t height) override;

	// -- Resources --
	virtual HexEngine::ITexture2D* GetBackBuffer(HexEngine::Window* window = nullptr) override;
	virtual HexEngine::ITexture2D* CreateTexture(HexEngine::ITexture2D* clone) override;
	virtual HexEngine::ITexture2D* CreateTexture2D(const HexEngine::TextureDesc& desc, const HexEngine::SubresourceData* initialData = nullptr) override;
	virtual HexEngine::ITexture3D* CreateTexture3D(const HexEngine::TextureDesc& desc, const HexEngine::SubresourceData* initialData = nullptr) override;
	virtual HexEngine::IVertexBuffer* CreateVertexBuffer(const HexEngine::BufferDesc& desc, const void* initialData = nullptr) override;
	virtual HexEngine::IIndexBuffer*  CreateIndexBuffer(const HexEngine::BufferDesc& desc, const void* initialData = nullptr) override;

	virtual HexEngine::IShaderStage* CreateVertexShader(std::vector<uint8_t>& shaderCode) override;
	virtual HexEngine::IShaderStage* CreatePixelShader(std::vector<uint8_t>& shaderCode) override;
	virtual HexEngine::IShaderStage* CreateGeometryShader(std::vector<uint8_t>& shaderCode) override;
	virtual HexEngine::IShaderStage* CreateComputeShader(std::vector<uint8_t>& shaderCode) override;
	virtual HexEngine::IShaderStage* CreateComputeShaderFromSource(const std::string& shaderSource, const std::string& entryPoint = "MainCS") override;
	virtual HexEngine::IInputLayout* CreateInputLayout(const HexEngine::InputElement* elements, uint32_t numElements, const std::vector<uint8_t>& vertexShaderBinary) override;

	virtual HexEngine::IConstantBuffer* CreateConstantBuffer(uint32_t size) override;
	virtual HexEngine::IStructuredBuffer* CreateStructuredBuffer(
		uint32_t elementStride,
		uint32_t elementCount,
		HexEngine::StructuredBufferFlags flags,
		HexEngine::ResourceUsage usage = HexEngine::ResourceUsage::Default,
		HexEngine::CpuAccess cpuAccess = HexEngine::CpuAccess::None,
		const void* initialData = nullptr) override;
	virtual HexEngine::IConstantBuffer* GetEngineConstantBuffer(HexEngine::EngineConstantBuffer buffer) override;

	// -- State setters --
	virtual void SetConstantBufferVS(uint32_t slot, HexEngine::IConstantBuffer* buffer) override;
	virtual void SetConstantBufferPS(uint32_t slot, HexEngine::IConstantBuffer* buffer) override;
	virtual void SetConstantBufferGS(uint32_t slot, HexEngine::IConstantBuffer* buffer) override;
	virtual void SetConstantBufferCS(uint32_t slot, HexEngine::IConstantBuffer* buffer) override;

	virtual void SetIndexBuffer(HexEngine::IIndexBuffer* buffer) override;
	virtual void SetVertexBuffer(uint32_t slot, HexEngine::IVertexBuffer* buffer) override;
	virtual void SetTopology(HexEngine::PrimitiveTopology topology) override;

	virtual void SetVertexShader(HexEngine::IShaderStage* shader) override;
	virtual void SetPixelShader(HexEngine::IShaderStage* shader) override;
	virtual void SetGeometryShader(HexEngine::IShaderStage* shader) override;
	virtual void SetComputeShader(HexEngine::IShaderStage* shader) override;

	virtual void SetInputLayout(HexEngine::IInputLayout* layout) override;

	virtual void SetTexture2D(uint32_t slot, HexEngine::ITexture2D* texture) override;
	virtual void SetTexture2D(HexEngine::ITexture2D* texture) override;
	virtual void SetTexture3D(HexEngine::ITexture3D* texture) override;
	virtual void SetGeometryTexture3D(uint32_t slot, HexEngine::ITexture3D* texture) override;
	virtual void SetVertexStructuredBuffer(uint32_t slot, HexEngine::IStructuredBuffer* buffer) override;
	virtual void SetGeometryStructuredBuffer(uint32_t slot, HexEngine::IStructuredBuffer* buffer) override;
	virtual void SetComputeTexture3D(uint32_t slot, HexEngine::ITexture3D* texture) override;
	virtual void SetComputeRwTexture3D(uint32_t slot, HexEngine::ITexture3D* texture) override;
	virtual void SetComputeStructuredBuffer(uint32_t slot, HexEngine::IStructuredBuffer* buffer) override;
	virtual void SetComputeRwStructuredBuffer(uint32_t slot, HexEngine::IStructuredBuffer* buffer, uint32_t initialCount = 0xFFFFFFFFu) override;
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
	virtual void SetRenderTargets(const std::vector<HexEngine::ITexture2D*>& renderTargets, HexEngine::ITexture2D* depthStencil = nullptr) override;
	virtual void GetRenderTargets(std::vector<HexEngine::ITexture2D*>& renderTargets, HexEngine::ITexture2D** depthStencil = nullptr) override;
	virtual void SetRenderTargets(uint32_t numTargets, const std::vector<HexEngine::ITexture2D*>& targets, HexEngine::ITexture2D* depthStencil) override;

	// -- Draw / dispatch --
	virtual void DrawIndexed(uint32_t numIndices) override;
	virtual void DrawIndexedInstanced(uint32_t numIndices, uint32_t instanceCount) override;
	virtual void DrawIndexedInstancedIndirect(void* argsBuffer, uint32_t alignedByteOffset = 0) override;
	virtual void Draw(uint32_t vertexCount, int32_t startVertexLocation = 0) override;
	virtual void DrawInstancedIndirect(HexEngine::IStructuredBuffer* argsBuffer, uint32_t alignedByteOffset = 0) override;
	virtual void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;
	virtual void DispatchIndirect(HexEngine::IStructuredBuffer* argsBuffer, uint32_t alignedByteOffset = 0) override;
	virtual void CopyStructureCount(HexEngine::IStructuredBuffer* sourceBuffer, HexEngine::IStructuredBuffer* destinationBuffer, uint32_t destinationByteOffset = 0) override;

	// -- Misc --
	virtual void GetBackBufferDimensions(uint32_t& width, uint32_t& height) override;
	virtual HexEngine::IResourceLoader* GetTextureLoader() override;

	virtual void SetDepthBufferState(HexEngine::DepthBufferState state) override;
	virtual HexEngine::DepthBufferState GetDepthBufferState() const override;

	virtual void SetClearColour(const math::Color& colour) override;
	virtual void SetCullingMode(HexEngine::CullingMode mode) override;
	virtual HexEngine::CullingMode GetCullingMode() const override;

	virtual void* GetNativeDevice() override;
	virtual void* GetNativeDeviceContext() override;

	virtual bool GetSupportedDisplayModes(std::vector<HexEngine::ScreenDisplayMode>& modes) override;

	virtual void SetPixelShaderResource(uint32_t slot, HexEngine::ITexture2D* texture) override;
	virtual void SetPixelShaderResource(HexEngine::ITexture2D* texture) override;
	virtual void SetPixelShaderResources(uint32_t slot, const std::vector<HexEngine::ITexture2D*>& textures) override;
	virtual void SetPixelShaderResources(const std::vector<HexEngine::ITexture2D*>& textures) override;
	virtual void UnbindAllPixelShaderResources() override;

	virtual uint32_t GetBoundResourceIndex() override;
	virtual void SetBoundResourceIndex(uint32_t value) override;

	virtual void BeginFrame(HexEngine::Window* window, HexEngine::ITexture2D* depthBuffer = nullptr) override;
	virtual void EndFrame(HexEngine::Window* window) override;

	virtual void SetViewports(const std::vector<HexEngine::Viewport>& viewports) override;
	virtual void SetViewport(const HexEngine::Viewport& viewport) override;
	virtual HexEngine::Viewport GetBackBufferViewport() const override;

	virtual void SetBlendState(HexEngine::BlendState state) override;
	virtual HexEngine::BlendState GetBlendState() const override;
	virtual int32_t GetCurrentMSAALevel() const override;

	virtual void SetScissorRect(const HexEngine::ScissorRect& rect) override;
	virtual void SetScissorRects(const std::vector<HexEngine::ScissorRect>& rects) override;
	virtual void ClearScissorRect() override;

	virtual void ResetState() override;
};
