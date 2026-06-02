

#pragma once

#include "../Required.hpp"
#include "Formats.hpp"
#include "ITexture2D.hpp"
#include "ITexture3D.hpp"
#include "IVertexBuffer.hpp"
#include "IIndexBuffer.hpp"
#include "IShader.hpp"
#include "IShaderStage.hpp"
#include "IInputLayout.hpp"
#include "IConstantBuffer.hpp"
#include "IStructuredBuffer.hpp"
#include "../FileSystem/ResourceSystem.hpp"
#include "Material.hpp"
#include "../Plugin/IPlugin.hpp"
#include "RenderStructs.hpp"

namespace HexEngine
{
	class Mesh;
	class Window;
	class Camera;

	// ------------------------------------------------------------------------
	// D3D11 -> neutral translation helpers used by the inline compatibility
	// shims on IGraphicsDevice (defined further down inside the class). The
	// entire namespace is gated on `__d3d11_h__` so that translation units that
	// don't pull in <d3d11.h> (e.g. the future HexEngine.D3D12Plugin) never
	// instantiate these functions and never see the D3D11 enum names.
	// ------------------------------------------------------------------------
#if defined(__d3d11_h__)
	namespace detail
	{
		inline TextureFormat ShimToTextureFormat(DXGI_FORMAT f)
		{
			switch (f)
			{
			case DXGI_FORMAT_UNKNOWN:                   return TextureFormat::Unknown;
			case DXGI_FORMAT_R8_UNORM:                  return TextureFormat::R8_UNORM;
			case DXGI_FORMAT_R8_SNORM:                  return TextureFormat::R8_SNORM;
			case DXGI_FORMAT_R8_UINT:                   return TextureFormat::R8_UINT;
			case DXGI_FORMAT_R8_SINT:                   return TextureFormat::R8_SINT;
			case DXGI_FORMAT_R8G8_UNORM:                return TextureFormat::R8G8_UNORM;
			case DXGI_FORMAT_R8G8_SNORM:                return TextureFormat::R8G8_SNORM;
			case DXGI_FORMAT_R8G8_UINT:                 return TextureFormat::R8G8_UINT;
			case DXGI_FORMAT_R8G8_SINT:                 return TextureFormat::R8G8_SINT;
			case DXGI_FORMAT_R8G8B8A8_UNORM:            return TextureFormat::R8G8B8A8_UNORM;
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:       return TextureFormat::R8G8B8A8_UNORM_SRGB;
			case DXGI_FORMAT_R8G8B8A8_SNORM:            return TextureFormat::R8G8B8A8_SNORM;
			case DXGI_FORMAT_R8G8B8A8_UINT:             return TextureFormat::R8G8B8A8_UINT;
			case DXGI_FORMAT_R8G8B8A8_SINT:             return TextureFormat::R8G8B8A8_SINT;
			case DXGI_FORMAT_B8G8R8A8_UNORM:            return TextureFormat::B8G8R8A8_UNORM;
			case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:       return TextureFormat::B8G8R8A8_UNORM_SRGB;
			case DXGI_FORMAT_B8G8R8X8_UNORM:            return TextureFormat::B8G8R8X8_UNORM;
			case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:       return TextureFormat::B8G8R8X8_UNORM_SRGB;
			case DXGI_FORMAT_R16_TYPELESS:              return TextureFormat::R16_TYPELESS;
			case DXGI_FORMAT_R16_UNORM:                 return TextureFormat::R16_UNORM;
			case DXGI_FORMAT_R16_SNORM:                 return TextureFormat::R16_SNORM;
			case DXGI_FORMAT_R16_UINT:                  return TextureFormat::R16_UINT;
			case DXGI_FORMAT_R16_SINT:                  return TextureFormat::R16_SINT;
			case DXGI_FORMAT_R16_FLOAT:                 return TextureFormat::R16_FLOAT;
			case DXGI_FORMAT_R16G16_UNORM:              return TextureFormat::R16G16_UNORM;
			case DXGI_FORMAT_R16G16_SNORM:              return TextureFormat::R16G16_SNORM;
			case DXGI_FORMAT_R16G16_UINT:               return TextureFormat::R16G16_UINT;
			case DXGI_FORMAT_R16G16_SINT:               return TextureFormat::R16G16_SINT;
			case DXGI_FORMAT_R16G16_FLOAT:              return TextureFormat::R16G16_FLOAT;
			case DXGI_FORMAT_R16G16B16A16_UNORM:        return TextureFormat::R16G16B16A16_UNORM;
			case DXGI_FORMAT_R16G16B16A16_SNORM:        return TextureFormat::R16G16B16A16_SNORM;
			case DXGI_FORMAT_R16G16B16A16_UINT:         return TextureFormat::R16G16B16A16_UINT;
			case DXGI_FORMAT_R16G16B16A16_SINT:         return TextureFormat::R16G16B16A16_SINT;
			case DXGI_FORMAT_R16G16B16A16_FLOAT:        return TextureFormat::R16G16B16A16_FLOAT;
			case DXGI_FORMAT_R32_TYPELESS:              return TextureFormat::R32_TYPELESS;
			case DXGI_FORMAT_R32_UINT:                  return TextureFormat::R32_UINT;
			case DXGI_FORMAT_R32_SINT:                  return TextureFormat::R32_SINT;
			case DXGI_FORMAT_R32_FLOAT:                 return TextureFormat::R32_FLOAT;
			case DXGI_FORMAT_R32G32_UINT:               return TextureFormat::R32G32_UINT;
			case DXGI_FORMAT_R32G32_SINT:               return TextureFormat::R32G32_SINT;
			case DXGI_FORMAT_R32G32_FLOAT:              return TextureFormat::R32G32_FLOAT;
			case DXGI_FORMAT_R32G32B32_UINT:            return TextureFormat::R32G32B32_UINT;
			case DXGI_FORMAT_R32G32B32_SINT:            return TextureFormat::R32G32B32_SINT;
			case DXGI_FORMAT_R32G32B32_FLOAT:           return TextureFormat::R32G32B32_FLOAT;
			case DXGI_FORMAT_R32G32B32A32_UINT:         return TextureFormat::R32G32B32A32_UINT;
			case DXGI_FORMAT_R32G32B32A32_SINT:         return TextureFormat::R32G32B32A32_SINT;
			case DXGI_FORMAT_R32G32B32A32_FLOAT:        return TextureFormat::R32G32B32A32_FLOAT;
			case DXGI_FORMAT_R10G10B10A2_UNORM:         return TextureFormat::R10G10B10A2_UNORM;
			case DXGI_FORMAT_R10G10B10A2_UINT:          return TextureFormat::R10G10B10A2_UINT;
			case DXGI_FORMAT_R11G11B10_FLOAT:           return TextureFormat::R11G11B10_FLOAT;
			case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:        return TextureFormat::R9G9B9E5_SHAREDEXP;
			case DXGI_FORMAT_D16_UNORM:                 return TextureFormat::D16_UNORM;
			case DXGI_FORMAT_D24_UNORM_S8_UINT:         return TextureFormat::D24_UNORM_S8_UINT;
			case DXGI_FORMAT_D32_FLOAT:                 return TextureFormat::D32_FLOAT;
			case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:      return TextureFormat::D32_FLOAT_S8X24_UINT;
			case DXGI_FORMAT_R24G8_TYPELESS:            return TextureFormat::R24G8_TYPELESS;
			case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:     return TextureFormat::R24_UNORM_X8_TYPELESS;
			case DXGI_FORMAT_R32G8X24_TYPELESS:         return TextureFormat::R32G8X24_TYPELESS;
			case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:  return TextureFormat::R32_FLOAT_X8X24_TYPELESS;
			default:                                    return TextureFormat::Unknown;
			}
		}

		inline BindFlags ShimToBindFlags(uint32_t f)
		{
			BindFlags out = BindFlags::None;
			if (f & D3D11_BIND_VERTEX_BUFFER)    out |= BindFlags::VertexBuffer;
			if (f & D3D11_BIND_INDEX_BUFFER)     out |= BindFlags::IndexBuffer;
			if (f & D3D11_BIND_CONSTANT_BUFFER)  out |= BindFlags::ConstantBuffer;
			if (f & D3D11_BIND_SHADER_RESOURCE)  out |= BindFlags::ShaderResource;
			if (f & D3D11_BIND_STREAM_OUTPUT)    out |= BindFlags::StreamOutput;
			if (f & D3D11_BIND_RENDER_TARGET)    out |= BindFlags::RenderTarget;
			if (f & D3D11_BIND_DEPTH_STENCIL)    out |= BindFlags::DepthStencil;
			if (f & D3D11_BIND_UNORDERED_ACCESS) out |= BindFlags::UnorderedAccess;
			return out;
		}

		inline ResourceUsage ShimToResourceUsage(D3D11_USAGE u)
		{
			switch (u)
			{
			case D3D11_USAGE_DEFAULT:   return ResourceUsage::Default;
			case D3D11_USAGE_IMMUTABLE: return ResourceUsage::Immutable;
			case D3D11_USAGE_DYNAMIC:   return ResourceUsage::Dynamic;
			case D3D11_USAGE_STAGING:   return ResourceUsage::Staging;
			default:                    return ResourceUsage::Default;
			}
		}

		inline CpuAccess ShimToCpuAccess(uint32_t f)
		{
			CpuAccess out = CpuAccess::None;
			if (f & D3D11_CPU_ACCESS_READ)  out |= CpuAccess::Read;
			if (f & D3D11_CPU_ACCESS_WRITE) out |= CpuAccess::Write;
			return out;
		}

		inline ResourceDimension ShimDimensionFromAny(
			D3D11_RTV_DIMENSION rtv,
			D3D11_UAV_DIMENSION uav,
			D3D11_SRV_DIMENSION srv,
			D3D11_DSV_DIMENSION dsv)
		{
			if (srv == D3D11_SRV_DIMENSION_TEXTURE2D)      return ResourceDimension::Texture2D;
			if (srv == D3D11_SRV_DIMENSION_TEXTURE2DMS)    return ResourceDimension::Texture2DMS;
			if (srv == D3D11_SRV_DIMENSION_TEXTURE3D)      return ResourceDimension::Texture3D;
			if (srv == D3D11_SRV_DIMENSION_TEXTURE2DARRAY) return ResourceDimension::Texture2DArray;
			if (srv == D3D11_SRV_DIMENSION_TEXTURECUBE)    return ResourceDimension::TextureCube;
			if (srv == D3D11_SRV_DIMENSION_BUFFER)         return ResourceDimension::Buffer;
			if (rtv == D3D11_RTV_DIMENSION_TEXTURE2D)      return ResourceDimension::Texture2D;
			if (rtv == D3D11_RTV_DIMENSION_TEXTURE2DMS)    return ResourceDimension::Texture2DMS;
			if (rtv == D3D11_RTV_DIMENSION_TEXTURE3D)      return ResourceDimension::Texture3D;
			if (uav == D3D11_UAV_DIMENSION_TEXTURE2D)      return ResourceDimension::Texture2D;
			if (uav == D3D11_UAV_DIMENSION_TEXTURE3D)      return ResourceDimension::Texture3D;
			if (uav == D3D11_UAV_DIMENSION_BUFFER)         return ResourceDimension::Buffer;
			if (dsv == D3D11_DSV_DIMENSION_TEXTURE2D)      return ResourceDimension::Texture2D;
			if (dsv == D3D11_DSV_DIMENSION_TEXTURE2DMS)    return ResourceDimension::Texture2DMS;
			return ResourceDimension::Texture2D;
		}

		inline ResourceMiscFlags ShimToMiscFlags(uint32_t f)
		{
			ResourceMiscFlags out = ResourceMiscFlags::None;
			if (f & D3D11_RESOURCE_MISC_GENERATE_MIPS)            out |= ResourceMiscFlags::GenerateMips;
			if (f & D3D11_RESOURCE_MISC_SHARED)                   out |= ResourceMiscFlags::Shared;
			if (f & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX)        out |= ResourceMiscFlags::SharedKeyedMutex;
			if (f & D3D11_RESOURCE_MISC_SHARED_NTHANDLE)          out |= ResourceMiscFlags::SharedNTHandle;
			if (f & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED)        out |= ResourceMiscFlags::BufferStructured;
			if (f & D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS)        out |= ResourceMiscFlags::DrawIndirectArgs;
			if (f & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS)   out |= ResourceMiscFlags::BufferAllowRawViews;
			return out;
		}

		inline PrimitiveTopology ShimToTopology(D3D_PRIMITIVE_TOPOLOGY t)
		{
			switch (t)
			{
			case D3D_PRIMITIVE_TOPOLOGY_POINTLIST:     return PrimitiveTopology::PointList;
			case D3D_PRIMITIVE_TOPOLOGY_LINELIST:      return PrimitiveTopology::LineList;
			case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:     return PrimitiveTopology::LineStrip;
			case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:  return PrimitiveTopology::TriangleList;
			case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP: return PrimitiveTopology::TriangleStrip;
			default:                                    return PrimitiveTopology::Undefined;
			}
		}

		inline Viewport ShimToViewport(const D3D11_VIEWPORT& v)
		{
			return Viewport(v.TopLeftX, v.TopLeftY, v.Width, v.Height, v.MinDepth, v.MaxDepth);
		}

		// Inverse of ShimToTextureFormat. Lets D3D11-aware call sites convert
		// the neutral TextureFormat returned by IGraphicsDevice methods (e.g.
		// GetDesiredBackBufferFormat()) back into a DXGI_FORMAT for places
		// that still need one - typically because the result is being fed
		// straight back into a D3D11-flavoured CreateTexture2D shim. C-style
		// casts (DXGI_FORMAT)tf silently bit-copy the neutral enum value into
		// a totally different DXGI_FORMAT slot; use this helper instead.
		inline DXGI_FORMAT ShimToDxgiFormat(TextureFormat f)
		{
			switch (f)
			{
			case TextureFormat::Unknown:                   return DXGI_FORMAT_UNKNOWN;
			case TextureFormat::R8_UNORM:                  return DXGI_FORMAT_R8_UNORM;
			case TextureFormat::R8_SNORM:                  return DXGI_FORMAT_R8_SNORM;
			case TextureFormat::R8_UINT:                   return DXGI_FORMAT_R8_UINT;
			case TextureFormat::R8_SINT:                   return DXGI_FORMAT_R8_SINT;
			case TextureFormat::R8G8_UNORM:                return DXGI_FORMAT_R8G8_UNORM;
			case TextureFormat::R8G8_SNORM:                return DXGI_FORMAT_R8G8_SNORM;
			case TextureFormat::R8G8_UINT:                 return DXGI_FORMAT_R8G8_UINT;
			case TextureFormat::R8G8_SINT:                 return DXGI_FORMAT_R8G8_SINT;
			case TextureFormat::R8G8B8A8_UNORM:            return DXGI_FORMAT_R8G8B8A8_UNORM;
			case TextureFormat::R8G8B8A8_UNORM_SRGB:       return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
			case TextureFormat::R8G8B8A8_SNORM:            return DXGI_FORMAT_R8G8B8A8_SNORM;
			case TextureFormat::R8G8B8A8_UINT:             return DXGI_FORMAT_R8G8B8A8_UINT;
			case TextureFormat::R8G8B8A8_SINT:             return DXGI_FORMAT_R8G8B8A8_SINT;
			case TextureFormat::B8G8R8A8_UNORM:            return DXGI_FORMAT_B8G8R8A8_UNORM;
			case TextureFormat::B8G8R8A8_UNORM_SRGB:       return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
			case TextureFormat::B8G8R8X8_UNORM:            return DXGI_FORMAT_B8G8R8X8_UNORM;
			case TextureFormat::B8G8R8X8_UNORM_SRGB:       return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
			case TextureFormat::R16_TYPELESS:              return DXGI_FORMAT_R16_TYPELESS;
			case TextureFormat::R16_UNORM:                 return DXGI_FORMAT_R16_UNORM;
			case TextureFormat::R16_SNORM:                 return DXGI_FORMAT_R16_SNORM;
			case TextureFormat::R16_UINT:                  return DXGI_FORMAT_R16_UINT;
			case TextureFormat::R16_SINT:                  return DXGI_FORMAT_R16_SINT;
			case TextureFormat::R16_FLOAT:                 return DXGI_FORMAT_R16_FLOAT;
			case TextureFormat::R16G16_UNORM:              return DXGI_FORMAT_R16G16_UNORM;
			case TextureFormat::R16G16_SNORM:              return DXGI_FORMAT_R16G16_SNORM;
			case TextureFormat::R16G16_UINT:               return DXGI_FORMAT_R16G16_UINT;
			case TextureFormat::R16G16_SINT:               return DXGI_FORMAT_R16G16_SINT;
			case TextureFormat::R16G16_FLOAT:              return DXGI_FORMAT_R16G16_FLOAT;
			case TextureFormat::R16G16B16A16_UNORM:        return DXGI_FORMAT_R16G16B16A16_UNORM;
			case TextureFormat::R16G16B16A16_SNORM:        return DXGI_FORMAT_R16G16B16A16_SNORM;
			case TextureFormat::R16G16B16A16_UINT:         return DXGI_FORMAT_R16G16B16A16_UINT;
			case TextureFormat::R16G16B16A16_SINT:         return DXGI_FORMAT_R16G16B16A16_SINT;
			case TextureFormat::R16G16B16A16_FLOAT:        return DXGI_FORMAT_R16G16B16A16_FLOAT;
			case TextureFormat::R32_TYPELESS:              return DXGI_FORMAT_R32_TYPELESS;
			case TextureFormat::R32_UINT:                  return DXGI_FORMAT_R32_UINT;
			case TextureFormat::R32_SINT:                  return DXGI_FORMAT_R32_SINT;
			case TextureFormat::R32_FLOAT:                 return DXGI_FORMAT_R32_FLOAT;
			case TextureFormat::R32G32_UINT:               return DXGI_FORMAT_R32G32_UINT;
			case TextureFormat::R32G32_SINT:               return DXGI_FORMAT_R32G32_SINT;
			case TextureFormat::R32G32_FLOAT:              return DXGI_FORMAT_R32G32_FLOAT;
			case TextureFormat::R32G32B32_UINT:            return DXGI_FORMAT_R32G32B32_UINT;
			case TextureFormat::R32G32B32_SINT:            return DXGI_FORMAT_R32G32B32_SINT;
			case TextureFormat::R32G32B32_FLOAT:           return DXGI_FORMAT_R32G32B32_FLOAT;
			case TextureFormat::R32G32B32A32_UINT:         return DXGI_FORMAT_R32G32B32A32_UINT;
			case TextureFormat::R32G32B32A32_SINT:         return DXGI_FORMAT_R32G32B32A32_SINT;
			case TextureFormat::R32G32B32A32_FLOAT:        return DXGI_FORMAT_R32G32B32A32_FLOAT;
			case TextureFormat::R10G10B10A2_UNORM:         return DXGI_FORMAT_R10G10B10A2_UNORM;
			case TextureFormat::R10G10B10A2_UINT:          return DXGI_FORMAT_R10G10B10A2_UINT;
			case TextureFormat::R11G11B10_FLOAT:           return DXGI_FORMAT_R11G11B10_FLOAT;
			case TextureFormat::R9G9B9E5_SHAREDEXP:        return DXGI_FORMAT_R9G9B9E5_SHAREDEXP;
			case TextureFormat::D16_UNORM:                 return DXGI_FORMAT_D16_UNORM;
			case TextureFormat::D24_UNORM_S8_UINT:         return DXGI_FORMAT_D24_UNORM_S8_UINT;
			case TextureFormat::D32_FLOAT:                 return DXGI_FORMAT_D32_FLOAT;
			case TextureFormat::D32_FLOAT_S8X24_UINT:      return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
			case TextureFormat::R24G8_TYPELESS:            return DXGI_FORMAT_R24G8_TYPELESS;
			case TextureFormat::R24_UNORM_X8_TYPELESS:     return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
			case TextureFormat::R32G8X24_TYPELESS:         return DXGI_FORMAT_R32G8X24_TYPELESS;
			case TextureFormat::R32_FLOAT_X8X24_TYPELESS:  return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
			default:                                       return DXGI_FORMAT_UNKNOWN;
			}
		}
	}
#endif // __d3d11_h__

	/**
	 * @brief Backend-neutral graphics device interface.
	 *
	 * Exposes device creation, resource allocation, binding, draw submission,
	 * and frame presentation services. All types in the public virtual surface
	 * are defined in HexEngine.Core/Graphics/Formats.hpp; no backend-specific
	 * headers (d3d11.h / d3d12.h / vulkan.h) may appear in any virtual
	 * signature here. (Inline non-virtual D3D11 compatibility shims at the
	 * bottom of the class are gated behind __d3d11_h__ and translate the
	 * legacy call signatures to the neutral ones for during-migration use.)
	 *
	 * Implementations live in renderer plugins (e.g. HexEngine.D3D11Plugin,
	 * HexEngine.D3D12Plugin). Exactly one renderer plugin is active per run,
	 * selected via the r_renderer cvar.
	 */
	class IGraphicsDevice : public IPluginInterface
	{
	public:
		DECLARE_PLUGIN_INTERFACE(IGraphicsDevice, 001);

		virtual ~IGraphicsDevice() {}

		virtual void Lock() {};
		virtual void Unlock() {};

		/**
		 * @brief Returns which backend this implementation is.
		 *
		 * Used by vendor plugins (NRD, OIDN, HBAOPlus, Streamline) to gate
		 * their activation, and by the engine to pick between renderer plugins
		 * at boot.
		 */
		virtual GraphicsBackend GetBackend() const = 0;

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

		/** @brief Creates a 2D texture resource (or 2D-array / cube, per desc.dimension). */
		virtual ITexture2D* CreateTexture2D(const TextureDesc& desc, const SubresourceData* initialData = nullptr) = 0;

		/** @brief Creates a 3D texture resource. */
		virtual ITexture3D* CreateTexture3D(const TextureDesc& desc, const SubresourceData* initialData = nullptr) = 0;

		/** @brief Creates a vertex buffer (optionally with initial contents). */
		virtual IVertexBuffer* CreateVertexBuffer(const BufferDesc& desc, const void* initialData = nullptr) = 0;

		/** @brief Creates an index buffer (optionally with initial contents). */
		virtual IIndexBuffer* CreateIndexBuffer(const BufferDesc& desc, const void* initialData = nullptr) = 0;

		/** @brief Creates a compiled vertex shader stage object. */
		virtual IShaderStage* CreateVertexShader(std::vector<uint8_t>& shaderCode) = 0;

		/** @brief Creates a compiled pixel shader stage object. */
		virtual IShaderStage* CreatePixelShader(std::vector<uint8_t>& shaderCode) = 0;

		/** @brief Creates a compiled geometry shader stage object. */
		virtual IShaderStage* CreateGeometryShader(std::vector<uint8_t>& shaderCode) = 0;

		/** @brief Creates a compiled compute shader stage object. */
		virtual IShaderStage* CreateComputeShader(std::vector<uint8_t>& shaderCode) = 0;

		/**
		 * @brief Compiles a compute shader from HLSL source at runtime.
		 *
		 * Only supported on backends that ship a runtime HLSL compiler (D3D11
		 * via d3dcompiler). Other backends return nullptr; callers should fall
		 * back to a precompiled blob.
		 */
		virtual IShaderStage* CreateComputeShaderFromSource(const std::string& shaderSource, const std::string& entryPoint = "MainCS") = 0;

		/** @brief Creates an input layout from descriptor + vertex shader bytecode. */
		virtual IInputLayout* CreateInputLayout(const InputElement* elements, uint32_t numElements, const std::vector<uint8_t>& vertexShaderBinary) = 0;

		/** @brief Creates a constant buffer. */
		virtual IConstantBuffer* CreateConstantBuffer(uint32_t size) = 0;

		/** @brief Creates a structured / RW-structured buffer. */
		virtual IStructuredBuffer* CreateStructuredBuffer(
			uint32_t elementStride,
			uint32_t elementCount,
			StructuredBufferFlags flags,
			ResourceUsage usage = ResourceUsage::Default,
			CpuAccess cpuAccess = CpuAccess::None,
			const void* initialData = nullptr) = 0;

		/** @brief Returns one of the engine-owned global constant buffers. */
		virtual IConstantBuffer* GetEngineConstantBuffer(EngineConstantBuffer buffer) = 0;

		virtual void SetConstantBufferVS(uint32_t slot, IConstantBuffer* buffer) = 0;
		virtual void SetConstantBufferPS(uint32_t slot, IConstantBuffer* buffer) = 0;
		virtual void SetConstantBufferGS(uint32_t slot, IConstantBuffer* buffer) = 0;
		virtual void SetConstantBufferCS(uint32_t slot, IConstantBuffer* buffer) = 0;

		virtual void SetIndexBuffer(IIndexBuffer* buffer) = 0;
		virtual void SetVertexBuffer(uint32_t slot, IVertexBuffer* buffer) = 0;

		virtual void SetTopology(PrimitiveTopology topology) = 0;

		virtual void SetVertexShader(IShaderStage* shader) = 0;
		virtual void SetPixelShader(IShaderStage* shader) = 0;
		virtual void SetGeometryShader(IShaderStage* shader) = 0;
		virtual void SetComputeShader(IShaderStage* shader) = 0;

		virtual void SetInputLayout(IInputLayout* layout) = 0;

		virtual void SetTexture2D(uint32_t slot, ITexture2D* texture) = 0;
		virtual void SetTexture2D(ITexture2D* texture) = 0;

		virtual void SetTexture3D(ITexture3D* texture) = 0;
		virtual void SetGeometryTexture3D(uint32_t slot, ITexture3D* texture) = 0;
		virtual void SetVertexStructuredBuffer(uint32_t slot, IStructuredBuffer* buffer) = 0;
		virtual void SetGeometryStructuredBuffer(uint32_t slot, IStructuredBuffer* buffer) = 0;
		virtual void SetComputeTexture3D(uint32_t slot, ITexture3D* texture) = 0;
		virtual void SetComputeRwTexture3D(uint32_t slot, ITexture3D* texture) = 0;
		virtual void SetComputeStructuredBuffer(uint32_t slot, IStructuredBuffer* buffer) = 0;
		virtual void SetComputeRwStructuredBuffer(uint32_t slot, IStructuredBuffer* buffer, uint32_t initialCount = 0xFFFFFFFFu) = 0;
		virtual void ClearGeometryTexture3D(uint32_t slot) = 0;
		virtual void ClearVertexStructuredBuffer(uint32_t slot) = 0;
		virtual void ClearComputeTexture3D(uint32_t slot) = 0;
		virtual void ClearComputeRwTexture3D(uint32_t slot) = 0;
		virtual void ClearGeometryStructuredBuffer(uint32_t slot) = 0;
		virtual void ClearComputeStructuredBuffer(uint32_t slot) = 0;
		virtual void ClearComputeRwStructuredBuffer(uint32_t slot) = 0;

		virtual void SetTexture2DArray(uint32_t slot, const std::vector<ITexture2D*>& textures) = 0;
		virtual void SetTexture2DArray(const std::vector<ITexture2D*>& textures) = 0;

		virtual void SetRenderTarget(ITexture2D* renderTarget, ITexture2D* depthStencil = nullptr) = 0;
		virtual void SetRenderTargets(const std::vector<ITexture2D*>& renderTargets, ITexture2D* depthStencil = nullptr) = 0;
		virtual void GetRenderTargets(std::vector<ITexture2D*>& renderTargets, ITexture2D** depthStencil = nullptr) = 0;
		virtual void SetRenderTargets(uint32_t numTargets, const std::vector<ITexture2D*>& targets, ITexture2D* depthStencil) = 0;

		/** @brief Issues indexed draw call. */
		virtual void DrawIndexed(uint32_t numIndices) = 0;

		/** @brief Issues indexed instanced draw call. */
		virtual void DrawIndexedInstanced(uint32_t numIndices, uint32_t instanceCount) = 0;

		/** @brief Issues indexed instanced indirect draw call. */
		virtual void DrawIndexedInstancedIndirect(void* argsBuffer, uint32_t alignedByteOffset = 0) = 0;

		/** @brief Issues non-indexed draw call. */
		virtual void Draw(uint32_t vertexCount, int32_t startVertexLocation = 0) = 0;
		virtual void DrawInstancedIndirect(IStructuredBuffer* argsBuffer, uint32_t alignedByteOffset = 0) = 0;
		virtual void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) = 0;
		virtual void DispatchIndirect(IStructuredBuffer* argsBuffer, uint32_t alignedByteOffset = 0) = 0;
		virtual void CopyStructureCount(IStructuredBuffer* sourceBuffer, IStructuredBuffer* destinationBuffer, uint32_t destinationByteOffset = 0) = 0;

		virtual void GetBackBufferDimensions(uint32_t& width, uint32_t& height) = 0;

		/**
		 * @brief Returns the active display's HDR peak luminance in nits.
		 *
		 * Returns 0 when no HDR-capable display is active, when the query
		 * failed, or when the backend doesn't support the query. Callers
		 * should treat 0 as "unknown / use your own fallback" rather than
		 * "no headroom".
		 */
		virtual float GetDisplayPeakNits() const { return 0.0f; }

		virtual IResourceLoader* GetTextureLoader() = 0;

		virtual void SetDepthBufferState(DepthBufferState state) = 0;
		virtual DepthBufferState GetDepthBufferState() const = 0;

		virtual void SetClearColour(const math::Color& colour) = 0;

		virtual void SetCullingMode(CullingMode mode) = 0;
		virtual CullingMode GetCullingMode() const = 0;

		/**
		 * @brief Returns the underlying backend device handle.
		 *
		 * The cast target depends on the backend:
		 *   - D3D11: cast to ID3D11Device*
		 *   - D3D12: cast to ID3D12Device*
		 *
		 * Callers MUST inspect GetBackend() before casting. This entry point
		 * exists for vendor SDKs (NRD, OIDN, HBAOPlus, Streamline) that need
		 * native handles; engine and gameplay code should never need it.
		 */
		virtual void* GetNativeDevice() = 0;

		/**
		 * @brief Returns the immediate device-context / direct-queue handle.
		 *
		 * D3D11: ID3D11DeviceContext*. D3D12: ID3D12CommandQueue* (semantics
		 * differ; vendor plugins typically need backend-specific paths anyway).
		 * Same caller-cast requirements as GetNativeDevice.
		 */
		virtual void* GetNativeDeviceContext() = 0;

		/** @brief Returns all display modes supported by the active output. */
		virtual bool GetSupportedDisplayModes(std::vector<ScreenDisplayMode>& modes) = 0;

		virtual void SetPixelShaderResource(uint32_t slot, ITexture2D* texture) = 0;
		virtual void SetPixelShaderResource(ITexture2D* texture) = 0;
		virtual void SetPixelShaderResources(uint32_t slot, const std::vector<ITexture2D*>& textures) = 0;
		virtual void SetPixelShaderResources(const std::vector<ITexture2D*>& textures) = 0;

		virtual void UnbindAllPixelShaderResources() = 0;

		virtual uint32_t GetBoundResourceIndex() = 0;

		// Force the running "next implicit-slot to bind to" counter without affecting any
		// already-bound SRVs. Useful when a pass binds SRVs at fixed high slots but a later
		// pass relies on the counter being back at a known low value (e.g. Scene::RenderEntities
		// reads it to decide where to place material textures). Tread carefully: callers must
		// ensure the slots between [value, previous-counter) are not relied on elsewhere.
		virtual void SetBoundResourceIndex(uint32_t value) = 0;

		/** @brief Begins rendering for a frame/window. */
		virtual void BeginFrame(Window* window, ITexture2D* depthBuffer = nullptr) = 0;

		/** @brief Ends rendering and presents the frame/window. */
		virtual void EndFrame(Window* window) = 0;

		virtual void SetViewports(const std::vector<Viewport>& viewports) = 0;
		virtual void SetViewport(const Viewport& viewport) = 0;
		virtual Viewport GetBackBufferViewport() const = 0;

		virtual void SetBlendState(BlendState state) = 0;
		virtual BlendState GetBlendState() const = 0;

		virtual int32_t GetCurrentMSAALevel() const = 0;

		virtual void SetScissorRect(const ScissorRect& rect) = 0;
		virtual void SetScissorRects(const std::vector<ScissorRect>& rects) = 0;
		virtual void ClearScissorRect() = 0;

		/** @brief Resets cached graphics state to backend defaults. */
		virtual void ResetState() = 0;

		/** @brief Returns preferred swapchain/backbuffer format for this backend. */
		virtual TextureFormat GetDesiredBackBufferFormat() const
		{
			return TextureFormat::R16G16B16A16_FLOAT;
		}

		// --------------------------------------------------------------------
		// D3D11 compatibility shims.
		//
		// Many existing call sites still pass DXGI_FORMAT / D3D11_BIND_* /
		// D3D11_USAGE etc. to these methods. The neutral virtual methods above
		// are the canonical surface; the inline overloads below translate the
		// old D3D11-flavoured calls into the neutral types so legacy call
		// sites keep compiling unchanged.
		//
		// Block is gated on `__d3d11_h__` so non-D3D11 backends (the future
		// HexEngine.D3D12Plugin in particular) parse this header without ever
		// seeing the shims. They're inline non-virtual members - no vtable
		// cost; the call collapses straight to the neutral virtual dispatch.
		//
		// The block goes away once every callsite is migrated to neutral
		// types directly.
		// --------------------------------------------------------------------
	#if defined(__d3d11_h__)
		inline ITexture2D* CreateTexture2D(
			int32_t width,
			int32_t height,
			DXGI_FORMAT format,
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
			uint32_t miscFlags = 0)
		{
			TextureDesc d;
			d.width = width;
			d.height = height;
			d.depth = 1;
			d.arraySize = arraySize;
			d.mipLevels = mipLevels;
			d.sampleCount = sampleCount;
			d.sampleQuality = sampleQuality;
			d.format = detail::ShimToTextureFormat(format);
			d.bindFlags = detail::ShimToBindFlags(bindFlags);
			d.usage = detail::ShimToResourceUsage(usage);
			d.cpuAccess = detail::ShimToCpuAccess((uint32_t)access);
			d.dimension = detail::ShimDimensionFromAny(rtvDimension, uavDimension, srvDimension, dsvDimension);
			d.miscFlags = detail::ShimToMiscFlags(miscFlags);

			SubresourceData sub;
			SubresourceData* subPtr = nullptr;
			if (initialData != nullptr)
			{
				sub.data = initialData->pSysMem;
				sub.rowPitchBytes = initialData->SysMemPitch;
				sub.slicePitchBytes = initialData->SysMemSlicePitch;
				subPtr = &sub;
			}
			return this->CreateTexture2D(d, subPtr);
		}

		inline ITexture3D* CreateTexture3D(
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
			D3D11_DSV_DIMENSION dsvDimension = D3D11_DSV_DIMENSION_UNKNOWN)
		{
			TextureDesc d;
			d.width = width;
			d.height = height;
			d.depth = depth;
			d.arraySize = arraySize;
			d.mipLevels = mipLevels;
			d.sampleCount = sampleCount;
			d.sampleQuality = sampleQuality;
			d.format = detail::ShimToTextureFormat(format);
			d.bindFlags = detail::ShimToBindFlags(bindFlags);
			d.dimension = ResourceDimension::Texture3D;

			SubresourceData sub;
			SubresourceData* subPtr = nullptr;
			if (initialData != nullptr)
			{
				sub.data = initialData->pSysMem;
				sub.rowPitchBytes = initialData->SysMemPitch;
				sub.slicePitchBytes = initialData->SysMemSlicePitch;
				subPtr = &sub;
			}
			return this->CreateTexture3D(d, subPtr);
		}

		inline IVertexBuffer* CreateVertexBuffer(int32_t byteWidth, uint32_t byteStride, D3D11_USAGE usage, uint32_t cpuAccessFlags)
		{
			BufferDesc d;
			d.byteWidth  = (uint32_t)byteWidth;
			d.byteStride = byteStride;
			d.usage      = detail::ShimToResourceUsage(usage);
			d.cpuAccess  = detail::ShimToCpuAccess(cpuAccessFlags);
			d.bindFlags  = BindFlags::VertexBuffer;
			return this->CreateVertexBuffer(d, nullptr);
		}

		inline IVertexBuffer* CreateVertexBuffer(int32_t byteWidth, uint32_t byteStride, D3D11_USAGE usage, uint32_t cpuAccessFlags, void* vertices)
		{
			BufferDesc d;
			d.byteWidth  = (uint32_t)byteWidth;
			d.byteStride = byteStride;
			d.usage      = detail::ShimToResourceUsage(usage);
			d.cpuAccess  = detail::ShimToCpuAccess(cpuAccessFlags);
			d.bindFlags  = BindFlags::VertexBuffer;
			return this->CreateVertexBuffer(d, vertices);
		}

		inline IIndexBuffer* CreateIndexBuffer(int32_t byteWidth, uint32_t byteStride, D3D11_USAGE usage, uint32_t cpuAccessFlags)
		{
			BufferDesc d;
			d.byteWidth  = (uint32_t)byteWidth;
			d.byteStride = byteStride;
			d.usage      = detail::ShimToResourceUsage(usage);
			d.cpuAccess  = detail::ShimToCpuAccess(cpuAccessFlags);
			d.bindFlags  = BindFlags::IndexBuffer;
			return this->CreateIndexBuffer(d, nullptr);
		}

		inline IIndexBuffer* CreateIndexBuffer(int32_t byteWidth, uint32_t byteStride, D3D11_USAGE usage, uint32_t cpuAccessFlags, void* indices)
		{
			BufferDesc d;
			d.byteWidth  = (uint32_t)byteWidth;
			d.byteStride = byteStride;
			d.usage      = detail::ShimToResourceUsage(usage);
			d.cpuAccess  = detail::ShimToCpuAccess(cpuAccessFlags);
			d.bindFlags  = BindFlags::IndexBuffer;
			return this->CreateIndexBuffer(d, indices);
		}

		inline IStructuredBuffer* CreateStructuredBuffer(
			uint32_t elementStride,
			uint32_t elementCount,
			StructuredBufferFlags flags,
			D3D11_USAGE usage,
			uint32_t cpuAccessFlags = 0,
			const void* initialData = nullptr)
		{
			return this->CreateStructuredBuffer(elementStride, elementCount, flags,
				detail::ShimToResourceUsage(usage),
				detail::ShimToCpuAccess(cpuAccessFlags),
				initialData);
		}

		inline IInputLayout* CreateInputLayout(D3D11_INPUT_ELEMENT_DESC* d3dElems, uint32_t numElements, const std::vector<uint8_t>& vertexShaderBinary)
		{
			std::vector<InputElement> elems(numElements);
			for (uint32_t i = 0; i < numElements; ++i)
			{
				const auto& src = d3dElems[i];
				auto& dst = elems[i];
				dst.semanticName = src.SemanticName != nullptr ? src.SemanticName : std::string();
				dst.semanticIndex = src.SemanticIndex;
				dst.format = detail::ShimToTextureFormat(src.Format);
				dst.inputSlot = src.InputSlot;
				dst.alignedByteOffset = (src.AlignedByteOffset == D3D11_APPEND_ALIGNED_ELEMENT) ? 0xFFFFFFFFu : src.AlignedByteOffset;
				dst.perInstance = (src.InputSlotClass == D3D11_INPUT_PER_INSTANCE_DATA);
				dst.instanceDataStepRate = src.InstanceDataStepRate;
			}
			return this->CreateInputLayout(elems.data(), numElements, vertexShaderBinary);
		}

		inline void SetTopology(D3D_PRIMITIVE_TOPOLOGY t)
		{
			this->SetTopology(detail::ShimToTopology(t));
		}

		inline void SetViewport(const D3D11_VIEWPORT& v)
		{
			this->SetViewport(detail::ShimToViewport(v));
		}

		inline void SetViewports(const std::vector<D3D11_VIEWPORT>& vs)
		{
			std::vector<Viewport> out;
			out.reserve(vs.size());
			for (const auto& v : vs)
				out.push_back(detail::ShimToViewport(v));
			this->SetViewports(out);
		}

		inline void SetScissorRect(const RECT& r)
		{
			this->SetScissorRect(ScissorRect(r.left, r.top, r.right, r.bottom));
		}

		inline void SetScissorRects(const std::vector<RECT>& rs)
		{
			std::vector<ScissorRect> out;
			out.reserve(rs.size());
			for (const auto& r : rs)
				out.push_back(ScissorRect(r.left, r.top, r.right, r.bottom));
			this->SetScissorRects(out);
		}
	#endif // __d3d11_h__
	};
}
