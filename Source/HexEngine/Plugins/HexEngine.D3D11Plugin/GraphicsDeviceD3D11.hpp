

#pragma once

#include <HexEngine.Core/Graphics/IGraphicsDevice.hpp>
#include "Texture2D.hpp"
#include "VertexBuffer.hpp"
#include "IndexBuffer.hpp"
#include "Shader.hpp"
#include "InputLayout.hpp"
#include "ConstantBuffer.hpp"
#include "TextureImporter.hpp"

#include <CommonStates.h>
#include <PostProcess.h>

#define DEFERRED_SHADING 1

namespace HexEngine
{
	struct DeviceData
	{
		IDXGISwapChain* swapchain = nullptr;
		Texture2D* backbuffer = nullptr;
		DXGI_SWAP_CHAIN_DESC swapchainDesc;
	};
	class GraphicsDeviceD3D11 : public IGraphicsDevice
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

		virtual void Lock() override;
		virtual void Unlock() override;

		virtual bool Create() override;

		virtual void Destroy() override;

		virtual bool AttachToWindow(Window* window) override;

		virtual void Resize(Window* window, uint32_t width, uint32_t height) override;

		virtual Texture2D* GetBackBuffer(Window* window = nullptr) override;

		virtual Texture2D* CreateTexture(ITexture2D* clone) override;

		virtual Texture2D* CreateTexture2D(
			int32_t width,
			int32_t height,
			DXGI_FORMAT format,
			int32_t arraySize,
			uint32_t bindFlags,
			int32_t mipLevels,
			int32_t sampleCount,
			int32_t sampleQuality,
			D3D11_SUBRESOURCE_DATA* initialData = nullptr,
			D3D11_CPU_ACCESS_FLAG access = (D3D11_CPU_ACCESS_FLAG)0,
			D3D11_RTV_DIMENSION rtvDimension = D3D11_RTV_DIMENSION_UNKNOWN,
			D3D11_UAV_DIMENSION uavDimension = D3D11_UAV_DIMENSION_UNKNOWN,
			D3D11_SRV_DIMENSION srvDimension = D3D11_SRV_DIMENSION_UNKNOWN,
			D3D11_DSV_DIMENSION dsvDimension = D3D11_DSV_DIMENSION_UNKNOWN,
			D3D11_USAGE usage = D3D11_USAGE_DEFAULT,
			uint32_t miscFlags = 0) override;

		/*virtual Texture2D* CreateTexture2D(
			int32_t width,
			int32_t height,
			DXGI_FORMAT format,
			int32_t arraySize,
			uint32_t bindFlags,
			int32_t mipLevels,
			int32_t sampleCount,
			int32_t sampleQuality,
			D3D11_SUBRESOURCE_DATA* initialData,
			D3D11_RTV_DIMENSION rtvDimension = D3D11_RTV_DIMENSION_UNKNOWN,
			D3D11_UAV_DIMENSION uavDimension = D3D11_UAV_DIMENSION_UNKNOWN,
			D3D11_SRV_DIMENSION srvDimension = D3D11_SRV_DIMENSION_UNKNOWN,
			D3D11_DSV_DIMENSION dsvDimension = D3D11_DSV_DIMENSION_UNKNOWN) override;*/

		virtual ITexture3D* CreateTexture3D(
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
			D3D11_RTV_DIMENSION rtvDimension = D3D11_RTV_DIMENSION_UNKNOWN,
			D3D11_UAV_DIMENSION uavDimension = D3D11_UAV_DIMENSION_UNKNOWN,
			D3D11_SRV_DIMENSION srvDimension = D3D11_SRV_DIMENSION_UNKNOWN,
			D3D11_DSV_DIMENSION dsvDimension = D3D11_DSV_DIMENSION_UNKNOWN) override;

		virtual VertexBuffer* CreateVertexBuffer(int32_t byteWidth, uint32_t byteStride, D3D11_USAGE usage, uint32_t cpuAccessFlags) override;

		virtual VertexBuffer* CreateVertexBuffer(int32_t byteWidth, uint32_t byteStride, D3D11_USAGE usage, uint32_t cpuAccessFlags, void* vertices) override;

		virtual IndexBuffer* CreateIndexBuffer(int32_t byteWidth, uint32_t byteStride, D3D11_USAGE usage, uint32_t cpuAccessFlags) override;

		virtual IndexBuffer* CreateIndexBuffer(int32_t byteWidth, uint32_t byteStride, D3D11_USAGE usage, uint32_t cpuAccessFlags, void* indices) override;

		virtual ShaderStageImpl<ID3D11VertexShader>* CreateVertexShader(std::vector<uint8_t>& shaderCode) override;

		virtual ShaderStageImpl<ID3D11PixelShader>* CreatePixelShader(std::vector<uint8_t>& shaderCode) override;

		virtual InputLayout* CreateInputLayout(D3D11_INPUT_ELEMENT_DESC* desc, uint32_t numElements, const std::vector<uint8_t>& vertexShaderBinary) override;

		virtual ConstantBuffer* CreateConstantBuffer(uint32_t size);

		virtual ConstantBuffer* GetEngineConstantBuffer(EngineConstantBuffer buffer) override;

		virtual void SetIndexBuffer(IIndexBuffer* buffer) override;

		virtual void SetVertexBuffer(uint32_t slot, IVertexBuffer* buffer) override;

		virtual void SetTopology(D3D_PRIMITIVE_TOPOLOGY topology) override;

		virtual void SetVertexShader(IShaderStage* shader) override;

		virtual void SetPixelShader(IShaderStage* shader) override;

		virtual void SetInputLayout(IInputLayout* layout) override;

		virtual void SetConstantBufferVS(uint32_t slot, IConstantBuffer* buffer) override;

		virtual void SetConstantBufferPS(uint32_t slot, IConstantBuffer* buffer) override;

		virtual void SetTexture2D(uint32_t slot, ITexture2D* texture) override;

		virtual void SetTexture2D(ITexture2D* texture) override;

		virtual void SetTexture3D(ITexture3D* texture) override;

		virtual void SetTexture2DArray(uint32_t slot, const std::vector<ITexture2D*>& textures) override;

		virtual void SetTexture2DArray(const std::vector<ITexture2D*>& textures) override;

		virtual void SetRenderTarget(ITexture2D* renderTarget, ITexture2D* depthStencil = nullptr) override;

		virtual void SetRenderTargets(const std::vector<ITexture2D*>& renderTargets, ITexture2D* depthStencil = nullptr) override;

		virtual void GetRenderTargets(std::vector<ITexture2D*>& renderTargets, ITexture2D** depthStencil = nullptr) override;

		virtual void DrawIndexed(uint32_t numIndices) override;

		virtual void DrawIndexedInstanced(uint32_t numIndices, uint32_t instanceCount) override;

		virtual void Draw(uint32_t vertexCount, int32_t startVertexLocation = 0) override;

		virtual void GetBackBufferDimensions(uint32_t& width, uint32_t& height) override;

		virtual TextureImporter* GetTextureLoader() override;

		virtual void SetDepthBufferState(DepthBufferState state) override;

		virtual DepthBufferState GetDepthBufferState() const override;

		virtual void SetClearColour(const math::Color& colour) override;

		virtual void SetCullingMode(CullingMode mode) override;

		virtual CullingMode GetCullingMode() const override;

		virtual /*ID3D11Device*/void* GetNativeDevice() override;
		virtual /*ID3D11DeviceContext*/void* GetNativeDeviceContext() override;

		virtual bool GetSupportedDisplayModes(std::vector<ScreenDisplayMode>& modes) override;

		virtual void SetPixelShaderResource(uint32_t slot, ID3D11ShaderResourceView* resource) override;

		virtual void SetPixelShaderResource(ID3D11ShaderResourceView* resource) override;

		virtual void SetPixelShaderResources(uint32_t slot, const std::vector<ID3D11ShaderResourceView*>& resources) override;

		virtual void SetPixelShaderResources(const std::vector<ID3D11ShaderResourceView*>& resources) override;

		virtual void SetRenderTargets(uint32_t numTargets, const std::vector<ITexture2D*>& targets, ITexture2D* depthStencil) override;

		virtual void UnbindAllPixelShaderResources() override;

		//virtual void BindGBuffer() override;

		//virtual void BindShadowMaps() override;

		virtual uint32_t GetBoundResourceIndex() override;

		virtual void BeginFrame(Window* window, ITexture2D* depthBuffer) override;

		virtual void EndFrame(Window* window) override;

		virtual void SetViewports(const std::vector<D3D11_VIEWPORT>& viewports) override;

		virtual void SetViewport(const D3D11_VIEWPORT& viewport) override;

		virtual const D3D11_VIEWPORT& GetBackBufferViewport() const override;

		virtual void SetBlendState(BlendState state) override;

		virtual BlendState GetBlendState() const override;

		virtual int32_t GetCurrentMSAALevel() const override;

		virtual void SetScissorRect(const RECT& rect) override;

		virtual void SetScissorRects(const std::vector<RECT>& rects) override;

		virtual void ClearScissorRect() override;

		virtual void ResetState() override;

	private:
		bool CreateFactory();

		bool CreateInternal();

		void DestroyInternal();

	private:
		IDXGIDevice* _dxgiDevice = nullptr;
		IDXGIAdapter* _dxgiAdapter = nullptr;
		IDXGIFactory1* _dxgiFactory = nullptr;
		IDXGIOutput* _dxgiOutput = nullptr;

		std::recursive_mutex _lock;

		std::vector<ScreenDisplayMode> _supportedScreenDisplayModes;

		//DXGI_SWAP_CHAIN_DESC _swapChainDesc;

		D3D_FEATURE_LEVEL _featureLevelSupported = D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_11_0;
		ID3D11Device* _device = nullptr;
		ID3D11DeviceContext* _deviceContext = nullptr;
		//IDXGISwapChain* _swapChain = nullptr;
		//ID3D11RenderTargetView* _renderTargetView = nullptr;
		//Texture2D* _backBuffer = nullptr;
		//ID3D11ShaderResourceView* _renderTargetSRV = nullptr;
		RenderState _prevRenderState;
		std::vector<Mesh*> _meshesToRender;
		ConstantBuffer* _engineConstantBuffers[(uint32_t)EngineConstantBuffer::NumEngineConstantBuffers] = { nullptr };
		D3D11_VIEWPORT _bbufferViewport;
		//ID3D11RasterizerState* _rasterState = nullptr;
		//ID3D11RasterizerState* _rasterStateCullFront = nullptr;
		//ID3D11RasterizerState* _rasterStateCullNone = nullptr;
		ID3D11BlendState* _subtractivetBlendState = nullptr;
		//ID3D11Texture2D* _depthStencilBuffer = nullptr;

		std::unordered_map<Window*, DeviceData> _deviceData;
		

		//ShadowMap* _shadowMap[4];
		//GBuffer _gbuffer;
		uint32_t _bbufferWidth = 0;
		uint32_t _bbufferHeight = 0;
		TextureImporter* _textureLoader = nullptr;
		//ID3D11SamplerState* _texSamplerClamp = nullptr;
		//ID3D11SamplerState* _texSamplerWrap = nullptr;
		ID3D11SamplerState* _texSamplerComparison = nullptr;
		ID3D11SamplerState* _texSamplerMirrored = nullptr;

		dx::CommonStates* _states = nullptr;
		math::Color _clearColour;
		//dx::BasicPostProcess* _postProcess = nullptr;
		//Texture2D* _bloomTextures[2] = { nullptr };
		//Texture2D* _renderTexture = nullptr;
		//Texture2D* _composedTexture = nullptr;
		//Texture2D* _toneMappedTexture = nullptr;
		//dx::DualPostProcess* _cobinePostProcess = nullptr;
		//dx::ToneMapPostProcess* _toneMapProcess = nullptr;

		
		uint32_t _currentlyBoundSRVIndex = 0;
		bool _isInShadowMapGeneration = false;

		std::vector<ITexture2D*> _boundRenderTargets;
		ITexture2D* _boundDepthStencil = nullptr;

		ID3D11ShaderResourceView** _emptyShaderResources = nullptr;
		uint32_t _maxEmptyResources = 0;
	};

	inline GraphicsDeviceD3D11* g_pGraphics = nullptr;
}

