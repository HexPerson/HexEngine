

#pragma once

#include <HexEngine.Core/Graphics/IGraphicsDevice.hpp>
#include "Texture2D.hpp"
#include "Texture3D.hpp"
#include "VertexBuffer.hpp"
#include "IndexBuffer.hpp"
#include "Shader.hpp"
#include "InputLayout.hpp"
#include "ConstantBuffer.hpp"
#include "StructuredBuffer.hpp"
#include "TextureImporter.hpp"

#include <CommonStates.h>
#include <PostProcess.h>

#define DEFERRED_SHADING 1

struct DeviceData
{
	IDXGISwapChain* swapchain = nullptr;
	Texture2D* backbuffer = nullptr;
	DXGI_SWAP_CHAIN_DESC swapchainDesc;
	DXGI_FORMAT backbufferFormat = DXGI_FORMAT_UNKNOWN;
	bool hdrOutputActive = false;
};
class GraphicsDeviceD3D11 : public HexEngine::IGraphicsDevice
{
public:
	virtual ~GraphicsDeviceD3D11()
	{
		Destroy();
	}
	friend class GBuffer;
	friend class VolumetricLighting;
	friend class Bloom;
	friend class Texture2D;

	//GraphicsSystemD3D11();

	virtual HexEngine::GraphicsBackend GetBackend() const override { return HexEngine::GraphicsBackend::D3D11; }

	// Bring the base-class D3D11 compatibility overloads back into scope on
	// the derived class - the neutral-typed virtual overrides below would
	// otherwise hide them through the usual C++ overload-hiding rule.
	using HexEngine::IGraphicsDevice::CreateTexture2D;
	using HexEngine::IGraphicsDevice::CreateTexture3D;
	using HexEngine::IGraphicsDevice::CreateVertexBuffer;
	using HexEngine::IGraphicsDevice::CreateIndexBuffer;
	using HexEngine::IGraphicsDevice::CreateStructuredBuffer;
	using HexEngine::IGraphicsDevice::CreateInputLayout;
	using HexEngine::IGraphicsDevice::SetTopology;
	using HexEngine::IGraphicsDevice::SetViewport;
	using HexEngine::IGraphicsDevice::SetViewports;
	using HexEngine::IGraphicsDevice::SetScissorRect;
	using HexEngine::IGraphicsDevice::SetScissorRects;
	using HexEngine::IGraphicsDevice::SetPixelShaderResource;
	using HexEngine::IGraphicsDevice::SetPixelShaderResources;

	virtual void Lock() override;
	virtual void Unlock() override;

	virtual float GetDisplayPeakNits() const override { return _displayPeakNits; }

	virtual bool Create() override;

	virtual void Destroy() override;

	virtual bool AttachToWindow(HexEngine::Window* window) override;

	virtual void Resize(HexEngine::Window* window, uint32_t width, uint32_t height) override;

	virtual Texture2D* GetBackBuffer(HexEngine::Window* window = nullptr) override;

	virtual Texture2D* CreateTexture(HexEngine::ITexture2D* clone) override;

	virtual Texture2D*    CreateTexture2D(const HexEngine::TextureDesc& desc, const HexEngine::SubresourceData* initialData = nullptr) override;

	virtual Texture3D*    CreateTexture3D(const HexEngine::TextureDesc& desc, const HexEngine::SubresourceData* initialData = nullptr) override;

	virtual VertexBuffer* CreateVertexBuffer(const HexEngine::BufferDesc& desc, const void* initialData = nullptr) override;

	virtual IndexBuffer*  CreateIndexBuffer(const HexEngine::BufferDesc& desc, const void* initialData = nullptr) override;

	virtual ShaderStageImpl<ID3D11VertexShader>* CreateVertexShader(std::vector<uint8_t>& shaderCode) override;

	virtual ShaderStageImpl<ID3D11PixelShader>* CreatePixelShader(std::vector<uint8_t>& shaderCode) override;

	virtual ShaderStageImpl<ID3D11GeometryShader>* CreateGeometryShader(std::vector<uint8_t>& shaderCode) override;

	virtual ShaderStageImpl<ID3D11ComputeShader>* CreateComputeShader(std::vector<uint8_t>& shaderCode) override;
	virtual ShaderStageImpl<ID3D11ComputeShader>* CreateComputeShaderFromSource(const std::string& shaderSource, const std::string& entryPoint = "MainCS") override;

	virtual InputLayout*     CreateInputLayout(const HexEngine::InputElement* elements, uint32_t numElements, const std::vector<uint8_t>& vertexShaderBinary) override;

	virtual ConstantBuffer*  CreateConstantBuffer(uint32_t size) override;
	virtual StructuredBuffer* CreateStructuredBuffer(
		uint32_t elementStride,
		uint32_t elementCount,
		HexEngine::StructuredBufferFlags flags,
		HexEngine::ResourceUsage usage = HexEngine::ResourceUsage::Default,
		HexEngine::CpuAccess cpuAccess = HexEngine::CpuAccess::None,
		const void* initialData = nullptr) override;

	virtual ConstantBuffer* GetEngineConstantBuffer(HexEngine::EngineConstantBuffer buffer) override;

	virtual void SetIndexBuffer(HexEngine::IIndexBuffer* buffer) override;

	virtual void SetVertexBuffer(uint32_t slot, HexEngine::IVertexBuffer* buffer) override;

	virtual void SetTopology(HexEngine::PrimitiveTopology topology) override;

	virtual void SetVertexShader(HexEngine::IShaderStage* shader) override;

	virtual void SetPixelShader(HexEngine::IShaderStage* shader) override;
	virtual void SetGeometryShader(HexEngine::IShaderStage* shader) override;
	virtual void SetComputeShader(HexEngine::IShaderStage* shader) override;

	virtual void SetInputLayout(HexEngine::IInputLayout* layout) override;

	virtual void SetConstantBufferVS(uint32_t slot, HexEngine::IConstantBuffer* buffer) override;

	virtual void SetConstantBufferPS(uint32_t slot, HexEngine::IConstantBuffer* buffer) override;
	virtual void SetConstantBufferGS(uint32_t slot, HexEngine::IConstantBuffer* buffer) override;
	virtual void SetConstantBufferCS(uint32_t slot, HexEngine::IConstantBuffer* buffer) override;

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

	virtual void DrawIndexed(uint32_t numIndices) override;

	virtual void DrawIndexedInstanced(uint32_t numIndices, uint32_t instanceCount) override;
	virtual void DrawIndexedInstancedIndirect(void* argsBuffer, uint32_t alignedByteOffset = 0) override;

	virtual void Draw(uint32_t vertexCount, int32_t startVertexLocation = 0) override;
	virtual void DrawInstancedIndirect(HexEngine::IStructuredBuffer* argsBuffer, uint32_t alignedByteOffset = 0) override;
	virtual void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;
	virtual void DispatchIndirect(HexEngine::IStructuredBuffer* argsBuffer, uint32_t alignedByteOffset = 0) override;
	virtual void CopyStructureCount(HexEngine::IStructuredBuffer* sourceBuffer, HexEngine::IStructuredBuffer* destinationBuffer, uint32_t destinationByteOffset = 0) override;

	virtual void GetBackBufferDimensions(uint32_t& width, uint32_t& height) override;

	virtual TextureImporter* GetTextureLoader() override;

	virtual void SetDepthBufferState(HexEngine::DepthBufferState state) override;

	virtual HexEngine::DepthBufferState GetDepthBufferState() const override;

	virtual void SetClearColour(const math::Color& colour) override;

	virtual void SetCullingMode(HexEngine::CullingMode mode) override;

	virtual HexEngine::CullingMode GetCullingMode() const override;

	virtual /*ID3D11Device*/void* GetNativeDevice() override;
	virtual /*ID3D11DeviceContext*/void* GetNativeDeviceContext() override;

	virtual bool GetSupportedDisplayModes(std::vector<HexEngine::ScreenDisplayMode>& modes) override;

	virtual void SetPixelShaderResource(uint32_t slot, HexEngine::ITexture2D* texture) override;
	virtual void SetPixelShaderResource(HexEngine::ITexture2D* texture) override;
	virtual void SetPixelShaderResources(uint32_t slot, const std::vector<HexEngine::ITexture2D*>& textures) override;
	virtual void SetPixelShaderResources(const std::vector<HexEngine::ITexture2D*>& textures) override;

	// D3D11-internal overloads kept for use by other D3D11 plugin classes that
	// pre-built ID3D11ShaderResourceView objects (e.g. TextureImporter, Shader).
	// Not part of the public IGraphicsDevice interface.
	void SetPixelShaderResourceRaw(uint32_t slot, ID3D11ShaderResourceView* resource);
	void SetPixelShaderResourceRaw(ID3D11ShaderResourceView* resource);
	void SetPixelShaderResourcesRaw(uint32_t slot, const std::vector<ID3D11ShaderResourceView*>& resources);
	void SetPixelShaderResourcesRaw(const std::vector<ID3D11ShaderResourceView*>& resources);

	virtual void SetRenderTargets(uint32_t numTargets, const std::vector<HexEngine::ITexture2D*>& targets, HexEngine::ITexture2D* depthStencil) override;

	virtual void UnbindAllPixelShaderResources() override;

	//virtual void BindGBuffer() override;

	//virtual void BindShadowMaps() override;

	virtual uint32_t GetBoundResourceIndex() override;

	virtual void SetBoundResourceIndex(uint32_t value) override;

	virtual void BeginFrame(HexEngine::Window* window, HexEngine::ITexture2D* depthBuffer) override;

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

private:
	bool CreateFactory();

	bool CreateInternal();

	void DestroyInternal();

	// Backwards-compatible internal helpers that still take D3D11-typed args.
	// The public IGraphicsDevice methods take HexEngine::TextureDesc /
	// BufferDesc and translate to D3D11 enums via FormatsD3D11.hpp before
	// dispatching here.
	Texture2D* CreateTexture2D_Internal(
		int32_t width,
		int32_t height,
		DXGI_FORMAT format,
		int32_t arraySize,
		uint32_t bindFlags,
		int32_t mipLevels,
		int32_t sampleCount,
		int32_t sampleQuality,
		D3D11_SUBRESOURCE_DATA* initialData,
		D3D11_CPU_ACCESS_FLAG access,
		D3D11_RTV_DIMENSION rtvDimension,
		D3D11_UAV_DIMENSION uavDimension,
		D3D11_SRV_DIMENSION srvDimension,
		D3D11_DSV_DIMENSION dsvDimension,
		D3D11_USAGE usage,
		uint32_t miscFlags);

	Texture3D* CreateTexture3D_Internal(
		int32_t width,
		int32_t height,
		int32_t depth,
		DXGI_FORMAT format,
		int32_t arraySize,
		uint32_t bindFlags,
		int32_t mipLevels,
		int32_t sampleCount,
		int32_t sampleQuality,
		D3D11_SUBRESOURCE_DATA* initialData,
		D3D11_RTV_DIMENSION rtvDimension,
		D3D11_UAV_DIMENSION uavDimension,
		D3D11_SRV_DIMENSION srvDimension,
		D3D11_DSV_DIMENSION dsvDimension);

	VertexBuffer* CreateVertexBuffer_Internal(int32_t byteWidth, uint32_t byteStride, D3D11_USAGE usage, uint32_t cpuAccessFlags, const void* vertices);
	IndexBuffer*  CreateIndexBuffer_Internal(int32_t byteWidth, uint32_t byteStride, D3D11_USAGE usage, uint32_t cpuAccessFlags, const void* indices);
	InputLayout*  CreateInputLayout_Internal(D3D11_INPUT_ELEMENT_DESC* desc, uint32_t numElements, const std::vector<uint8_t>& vertexShaderBinary);

private:
	IDXGIDevice* _dxgiDevice = nullptr;
	IDXGIAdapter* _dxgiAdapter = nullptr;
	IDXGIFactory1* _dxgiFactory = nullptr;
	IDXGIOutput* _dxgiOutput = nullptr;

	std::recursive_mutex _lock;

	std::vector<HexEngine::ScreenDisplayMode> _supportedScreenDisplayModes;

	//DXGI_SWAP_CHAIN_DESC _swapChainDesc;

	D3D_FEATURE_LEVEL _featureLevelSupported = D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_11_0;
	ID3D11Device* _device = nullptr;
	ID3D11DeviceContext* _deviceContext = nullptr;
	//IDXGISwapChain* _swapChain = nullptr;
	//ID3D11RenderTargetView* _renderTargetView = nullptr;
	//Texture2D* _backBuffer = nullptr;
	//ID3D11ShaderResourceView* _renderTargetSRV = nullptr;
	HexEngine::RenderState _prevRenderState;
	ConstantBuffer* _engineConstantBuffers[(uint32_t)HexEngine::EngineConstantBuffer::NumEngineConstantBuffers] = { nullptr };
	D3D11_VIEWPORT _bbufferViewport;
	//ID3D11RasterizerState* _rasterState = nullptr;
	//ID3D11RasterizerState* _rasterStateCullFront = nullptr;
	//ID3D11RasterizerState* _rasterStateCullNone = nullptr;
	ID3D11BlendState* _subtractivetBlendState = nullptr;
	ID3D11BlendState* _additivePreserveAlphaBlendState = nullptr;
	ID3D11BlendState* _transparencyPreserveAlphaBlendState = nullptr;
	ID3D11BlendState* _multiplicativeBlendState = nullptr;
	//ID3D11Texture2D* _depthStencilBuffer = nullptr;

	std::unordered_map<HexEngine::Window*, DeviceData> _deviceData;


	//ShadowMap* _shadowMap[4];
	//GBuffer _gbuffer;
	uint32_t _bbufferWidth = 0;
	uint32_t _bbufferHeight = 0;
	// Cached at AttachToWindow time from IDXGIOutput6::GetDesc1::MaxLuminance.
	// 0 when the active display isn't HDR-capable or the query failed.
	float _displayPeakNits = 0.0f;
	TextureImporter* _textureLoader = nullptr;
	//ID3D11SamplerState* _texSamplerClamp = nullptr;
	//ID3D11SamplerState* _texSamplerWrap = nullptr;
	ID3D11SamplerState* _texSamplerComparison = nullptr;
	ID3D11SamplerState* _texSamplerMirrored = nullptr;

	dx::CommonStates* _states = nullptr;
	math::Color _clearColour;
	

	uint32_t _currentlyBoundSRVIndex = 0;
	bool _isInShadowMapGeneration = false;

	std::vector<HexEngine::ITexture2D*> _boundRenderTargets;
	HexEngine::ITexture2D* _boundDepthStencil = nullptr;

	ID3D11ShaderResourceView** _emptyShaderResources = nullptr;
	uint32_t _maxEmptyResources = 0;
};

inline GraphicsDeviceD3D11* g_pGraphics = nullptr;

