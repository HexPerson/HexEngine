

#pragma once

#include "../Required.hpp"
#include "ITexture2D.hpp"
#include "ITexture3D.hpp"
#include "IVertexBuffer.hpp"
#include "IIndexBuffer.hpp"
#include "IShader.hpp"
#include "IShaderStage.hpp"
#include "IInputLayout.hpp"
#include "IConstantBuffer.hpp"
#include "../FileSystem/ResourceSystem.hpp"
#include "Material.hpp"
#include "../Plugin/IPlugin.hpp"
#include "RenderStructs.hpp"

namespace HexEngine
{
	class Mesh;
	class Window;
	class Camera;

	/**
	 * @brief Graphics backend interface (D3D11 plugin implementation).
	 *
	 * Exposes device creation, resource allocation, binding, draw submission,
	 * and frame presentation services used by the renderer.
	 */
	class IGraphicsDevice : public IPluginInterface
	{
	public:
		DECLARE_PLUGIN_INTERFACE(IGraphicsDevice, 001);

		virtual ~IGraphicsDevice() {}

		virtual void Lock() {};
		virtual void Unlock() {};

		/** @brief Creates the backend device/context and static GPU state. */
		virtual bool Create() = 0;

		/** @brief Destroys backend device/context resources. */
		virtual void Destroy() {};

		/** @brief Attaches backend swapchain/backbuffer resources to a window. */
		virtual bool AttachToWindow(Window* window) = 0;

		/** @brief Resizes swapchain/backbuffer resources for a window. */
		virtual void Resize(Window* window, uint32_t width, uint32_t height) = 0;

		/** @brief Returns the backbuffer texture for the selected window. */
		virtual ITexture2D* GetBackBuffer(Window* window = nullptr) = 0;

		/** @brief Creates a texture clone with matching descriptor/content. */
		virtual ITexture2D* CreateTexture(ITexture2D* clone) = 0;

		/** @brief Creates a 2D texture resource with explicit descriptor options. */
		virtual ITexture2D* CreateTexture2D(
			int32_t width,
			int32_t height,
			DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM,
			int32_t arraySize = 1,
			uint32_t bindFlags = D3D11_BIND_SHADER_RESOURCE,
			int32_t mipLevels = 0,
			int32_t sampleCount = 1,
			int32_t sampleQuality = 0,
			D3D11_SUBRESOURCE_DATA* initialData = nullptr,
			D3D11_CPU_ACCESS_FLAG access = (D3D11_CPU_ACCESS_FLAG)0,
			D3D11_RTV_DIMENSION rtvDimension = D3D11_RTV_DIMENSION_UNKNOWN,
			D3D11_UAV_DIMENSION uavDimension = D3D11_UAV_DIMENSION_UNKNOWN,
			D3D11_SRV_DIMENSION srvDimension = D3D11_SRV_DIMENSION_UNKNOWN,
			D3D11_DSV_DIMENSION dsvDimension = D3D11_DSV_DIMENSION_UNKNOWN,
			D3D11_USAGE usage = D3D11_USAGE_DEFAULT,
			uint32_t miscFlags = 0) = 0;

		/*virtual ITexture2D* CreateTexture2D(
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
			D3D11_DSV_DIMENSION dsvDimension = D3D11_DSV_DIMENSION_UNKNOWN) = 0;*/

		/** @brief Creates a 3D texture resource. */
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
			D3D11_DSV_DIMENSION dsvDimension = D3D11_DSV_DIMENSION_UNKNOWN) = 0;

		/** @brief Creates a vertex buffer resource. */
		virtual IVertexBuffer* CreateVertexBuffer(int32_t byteWidth, uint32_t byteStride, D3D11_USAGE usage, uint32_t cpuAccessFlags) = 0;

		/** @brief Creates and uploads a vertex buffer resource. */
		virtual IVertexBuffer* CreateVertexBuffer(int32_t byteWidth, uint32_t byteStride, D3D11_USAGE usage, uint32_t cpuAccessFlags, void* vertices) = 0;

		/** @brief Creates an index buffer resource. */
		virtual IIndexBuffer* CreateIndexBuffer(int32_t byteWidth, uint32_t byteStride, D3D11_USAGE usage, uint32_t cpuAccessFlags) = 0;

		/** @brief Creates and uploads an index buffer resource. */
		virtual IIndexBuffer* CreateIndexBuffer(int32_t byteWidth, uint32_t byteStride, D3D11_USAGE usage, uint32_t cpuAccessFlags, void* indices) = 0;

		/** @brief Creates a compiled vertex shader stage object. */
		virtual IShaderStage* CreateVertexShader(std::vector<uint8_t>& shaderCode) = 0;

		/** @brief Creates a compiled pixel shader stage object. */
		virtual IShaderStage* CreatePixelShader(std::vector<uint8_t>& shaderCode) = 0;

		/** @brief Creates an input layout from descriptor + vertex shader bytecode. */
		virtual IInputLayout* CreateInputLayout(D3D11_INPUT_ELEMENT_DESC* desc, uint32_t numElements, const std::vector<uint8_t>& vertexShaderBinary) = 0;

		/** @brief Creates a constant buffer. */
		virtual IConstantBuffer* CreateConstantBuffer(uint32_t size) = 0;

		/** @brief Returns one of the engine-owned global constant buffers. */
		virtual IConstantBuffer* GetEngineConstantBuffer(EngineConstantBuffer buffer) = 0;

		virtual void SetConstantBufferVS(uint32_t slot, IConstantBuffer* buffer) = 0;

		virtual void SetConstantBufferPS(uint32_t slot, IConstantBuffer* buffer) = 0;

		virtual void SetIndexBuffer(IIndexBuffer* buffer) = 0;

		virtual void SetVertexBuffer(uint32_t slot, IVertexBuffer* buffer) = 0;

		virtual void SetTopology(D3D_PRIMITIVE_TOPOLOGY topology) = 0;

		virtual void SetVertexShader(IShaderStage* shader) = 0;

		virtual void SetPixelShader(IShaderStage* shader) = 0;		

		virtual void SetInputLayout(IInputLayout* layout) = 0;

		virtual void SetTexture2D(uint32_t slot, ITexture2D* texture) = 0;

		virtual void SetTexture2D(ITexture2D* texture) = 0;

		virtual void SetTexture3D(ITexture3D* texture) = 0;

		virtual void SetTexture2DArray(uint32_t slot, const std::vector<ITexture2D*>& textures) = 0;

		virtual void SetTexture2DArray(const std::vector<ITexture2D*>& textures) = 0;

		virtual void SetRenderTarget(ITexture2D* renderTarget, ITexture2D* depthStencil = nullptr) = 0;

		virtual void SetRenderTargets(const std::vector<ITexture2D*>& renderTargets, ITexture2D* depthStencil = nullptr) = 0;

		virtual void GetRenderTargets(std::vector<ITexture2D*>& renderTargets, ITexture2D** depthStencil = nullptr) = 0;

		/** @brief Issues indexed draw call. */
		virtual void DrawIndexed(uint32_t numIndices) = 0;

		/** @brief Issues indexed instanced draw call. */
		virtual void DrawIndexedInstanced(uint32_t numIndices, uint32_t instanceCount) = 0;

		/** @brief Issues non-indexed draw call. */
		virtual void Draw(uint32_t vertexCount, int32_t startVertexLocation = 0) = 0;

		virtual void GetBackBufferDimensions(uint32_t& width, uint32_t& height) = 0;

		virtual IResourceLoader* GetTextureLoader() = 0;

		virtual void SetDepthBufferState(DepthBufferState state) = 0;

		virtual DepthBufferState GetDepthBufferState() const = 0;

		virtual void SetClearColour(const math::Color& colour) = 0;

		virtual void SetCullingMode(CullingMode mode) = 0;

		virtual CullingMode GetCullingMode() const = 0;

		virtual void* GetNativeDevice() = 0;

		virtual void* GetNativeDeviceContext() = 0;

		/** @brief Returns all display modes supported by the active output. */
		virtual bool GetSupportedDisplayModes(std::vector<ScreenDisplayMode>& modes) = 0;

		virtual void SetPixelShaderResource(uint32_t slot, ID3D11ShaderResourceView* resource) = 0;

		virtual void SetPixelShaderResource(ID3D11ShaderResourceView* resource) = 0;

		virtual void SetPixelShaderResources(uint32_t slot, const std::vector<ID3D11ShaderResourceView*>& resources) = 0;

		virtual void SetPixelShaderResources(const std::vector<ID3D11ShaderResourceView*>& resources) = 0;

		virtual void SetRenderTargets(uint32_t numTargets, const std::vector<ITexture2D*>& targets, ITexture2D* depthStencil) = 0;

		virtual void UnbindAllPixelShaderResources() = 0;

		virtual uint32_t GetBoundResourceIndex() = 0;

		//virtual void BindGBuffer() = 0;

		//virtual void BindShadowMaps() = 0;

		/** @brief Begins rendering for a frame/window. */
		virtual void BeginFrame(Window* window, ITexture2D* depthBuffer=nullptr) = 0;

		/** @brief Ends rendering and presents the frame/window. */
		virtual void EndFrame(Window* window) = 0;

		virtual void SetViewports(const std::vector<D3D11_VIEWPORT>& viewports) = 0;

		virtual void SetViewport(const D3D11_VIEWPORT& viewport) = 0;

		virtual const D3D11_VIEWPORT& GetBackBufferViewport() const = 0;

		virtual void SetBlendState(BlendState state) = 0;

		virtual BlendState GetBlendState() const = 0;

		virtual int32_t GetCurrentMSAALevel() const = 0;

		virtual void SetScissorRect(const RECT& rect) = 0;

		virtual void SetScissorRects(const std::vector<RECT>& rects) = 0;

		virtual void ClearScissorRect() = 0;

		/** @brief Resets cached graphics state to backend defaults. */
		virtual void ResetState() = 0;

		/** @brief Returns preferred swapchain/backbuffer format for this backend. */
		virtual uint32_t GetDesiredBackBufferFormat() const {
			return DXGI_FORMAT_R16G16B16A16_FLOAT;
		}

	};
}
