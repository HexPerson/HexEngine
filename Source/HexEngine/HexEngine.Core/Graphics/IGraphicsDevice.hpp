

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
	

	struct Atmosphere
	{
		float zenithExponent;
		float anisotropicIntensity;
		float density;
		float fogDensity;

		float volumetricScattering;
		float volumetricStrength;
		int volumetricSteps;
		float volumetricStepIncrement;

		math::Vector4 ambientLight;
	};

	struct BloomParams
	{
		float luminosityThreshold;
		float viewportScale;
		float pad_1;
		float pad_2;
	};

	struct ShadowSettings
	{
		float shadowFilterMaxSize;
		float penumbraFilterMaxSize;
		float biasMultiplier;
		float samples;
		float cascadeBlendRange;
		int	  passIndex;
		int	  cascadeOverride;
		float shadowMapSize;
		int   lightIndex;
		int	  pad0;
		int	  pad1;
		int	  pad2;
	};

	struct OceanSettings
	{
		OceanSettings() :
			shallowColour(HEX_RGBA_TO_FLOAT4(63.0f, 155.0f, 205.0f, 255.0f)),
			deepColour(HEX_RGBA_TO_FLOAT4(20.0f, 51.0f, 75.0f, 255.0f)),
			fogColour(HEX_RGBA_TO_FLOAT4(195.0f, 241.0f, 242.0f, 255.0f)),
			fresnelPow(3.2f),
			shoreFadeStrength(12.0f),
			fadeFactor(15.0f),
			reflectionStrength(0.4f)
		{}

		math::Vector4 shallowColour;
		math::Vector4 deepColour;
		math::Vector4 fogColour;
		float fresnelPow;
		float shoreFadeStrength;
		float fadeFactor;
		float reflectionStrength;
	};

	struct ColourGradeSettings
	{
		float contrast;
		float exposure;		
		float hueShift;
		float saturation;

		math::Vector3 colourFilter;
		float colour_pad;		
	};

	struct PerFrameConstantBuffer
	{
		// Current frame
		math::Matrix _viewMatrix;
		math::Matrix _projectionMatrix;
		math::Matrix _viewProjectionMatrix;
		math::Matrix _viewMatrixInverse;
		math::Matrix _projectionMatrixInverse;
		math::Matrix _viewProjectionMatrixInverse;

		// Previous frame
		math::Matrix _viewMatrixPrev;
		math::Matrix _projectionMatrixPrev;
		math::Matrix _viewProjectionMatrixPrev;
		math::Matrix _viewMatrixInversePrev;
		math::Matrix _projectionMatrixInversePrev;
		math::Matrix _viewProjectionMatrixInversePrev;

		math::Vector4 _eyePos;
		math::Vector4 _eyeDir;
		math::Vector4 _lightPosition;
		math::Vector4 _lightDirection;
		math::Vector4 _frustumSplits;
		float _globalLight[4];
		int _screenWidth;
		int _screenHeight;
		float _time;
		float _gamma;

		Atmosphere _atmosphere;		
		BloomParams _bloom;		
		OceanSettings _oceanConfig;
		ColourGradeSettings _colourGrading;

		math::Vector2 _jitterOffsets;
		uint32_t _frame;
		float _pad_4;
	};

	struct PerShadowCasterBuffer
	{
		math::Matrix _lightViewMatrix[6];
		math::Matrix _lightProjectionMatrix[6];
		math::Matrix _lightViewProjectionMatrix[6];

		math::Vector3 _shadowCasterLightDir;
		float _lightRadius;

		float _spotLightConeSize;
		float pad0;
		float pad1;
		float pad2;

		ShadowSettings _shadowConfig;
	};

	struct PerObjectBuffer
	{
		math::Matrix _worldMatrix;
		uint32_t _flags;
		int entityId;
		int pad[2];

		MaterialProperties _material;
	};

	struct PerAnimationBuffer
	{
		math::Matrix _boneTransforms[70];
	};

	enum class EngineConstantBuffer
	{
		PerFrameBuffer,
		PerObjectBuffer,
		PerShadowCasterBuffer,
		PerAnimationBuffer,
		NumEngineConstantBuffers
	};

	struct ScreenDisplayMode
	{
		struct RefreshRate
		{
			int32_t numerator;
			int32_t denominator;
		};

		int32_t width;
		int32_t height;
		RefreshRate refresh;
	};

	class Mesh;
	class Window;
	class Camera;

	class IGraphicsDevice : public IPluginInterface
	{
	public:
		DECLARE_PLUGIN_INTERFACE(IGraphicsDevice, 001);

		virtual ~IGraphicsDevice() {}

		virtual void Lock() {};
		virtual void Unlock() {};

		virtual bool Create() = 0;

		virtual void Destroy() {};

		virtual bool AttachToWindow(Window* window) = 0;

		virtual bool AttachToWindow(HWND handle, uint32_t width, uint32_t height, bool fullscreen) = 0;

		virtual void Resize(uint32_t width, uint32_t height) = 0;

		virtual ITexture2D* GetBackBuffer() = 0;

		virtual ITexture2D* CreateTexture(ITexture2D* clone) = 0;

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

		virtual IVertexBuffer* CreateVertexBuffer(int32_t byteWidth, uint32_t byteStride, D3D11_USAGE usage, uint32_t cpuAccessFlags) = 0;

		virtual IVertexBuffer* CreateVertexBuffer(int32_t byteWidth, uint32_t byteStride, D3D11_USAGE usage, uint32_t cpuAccessFlags, void* vertices) = 0;

		virtual IIndexBuffer* CreateIndexBuffer(int32_t byteWidth, uint32_t byteStride, D3D11_USAGE usage, uint32_t cpuAccessFlags) = 0;

		virtual IIndexBuffer* CreateIndexBuffer(int32_t byteWidth, uint32_t byteStride, D3D11_USAGE usage, uint32_t cpuAccessFlags, void* indices) = 0;

		virtual IShaderStage* CreateVertexShader(std::vector<uint8_t>& shaderCode) = 0;

		virtual IShaderStage* CreatePixelShader(std::vector<uint8_t>& shaderCode) = 0;

		virtual IInputLayout* CreateInputLayout(D3D11_INPUT_ELEMENT_DESC* desc, uint32_t numElements, const std::vector<uint8_t>& vertexShaderBinary) = 0;

		virtual IConstantBuffer* CreateConstantBuffer(uint32_t size) = 0;

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

		virtual void DrawIndexed(uint32_t numIndices) = 0;

		virtual void DrawIndexedInstanced(uint32_t numIndices, uint32_t instanceCount) = 0;

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

		virtual void BeginFrame(ITexture2D* depthBuffer=nullptr) = 0;

		virtual void EndFrame() = 0;

		virtual void SetViewports(const std::vector<D3D11_VIEWPORT>& viewports) = 0;

		virtual void SetViewport(const D3D11_VIEWPORT& viewport) = 0;

		virtual const D3D11_VIEWPORT& GetBackBufferViewport() const = 0;

		virtual void SetBlendState(BlendState state) = 0;

		virtual BlendState GetBlendState() const = 0;

		virtual int32_t GetCurrentMSAALevel() const = 0;

		virtual void SetScissorRect(const RECT& rect) = 0;

		virtual void SetScissorRects(const std::vector<RECT>& rects) = 0;

		virtual void ClearScissorRect() = 0;

		virtual void ResetState() = 0;

		virtual uint32_t GetDesiredBackBufferFormat() const {
			return DXGI_FORMAT_R16G16B16A16_FLOAT;
		}

	};
}
