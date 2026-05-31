
#pragma once

// Translation helpers between HexEngine's backend-neutral Formats.hpp types
// and the D3D11 / DXGI enums. This header is INTERNAL to the D3D11 plugin -
// no Core or Editor header is allowed to include it (or even <d3d11.h>).

#include <d3d11.h>
#include <dxgi1_6.h>
#include <HexEngine.Core/Graphics/Formats.hpp>

namespace HexEngine
{
	inline DXGI_FORMAT ToDXGI(TextureFormat f)
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

	inline TextureFormat FromDXGI(DXGI_FORMAT f)
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

	inline uint32_t ToD3D11BindFlags(BindFlags f)
	{
		uint32_t out = 0;
		if (HasAny(f, BindFlags::VertexBuffer))    out |= D3D11_BIND_VERTEX_BUFFER;
		if (HasAny(f, BindFlags::IndexBuffer))     out |= D3D11_BIND_INDEX_BUFFER;
		if (HasAny(f, BindFlags::ConstantBuffer))  out |= D3D11_BIND_CONSTANT_BUFFER;
		if (HasAny(f, BindFlags::ShaderResource))  out |= D3D11_BIND_SHADER_RESOURCE;
		if (HasAny(f, BindFlags::StreamOutput))    out |= D3D11_BIND_STREAM_OUTPUT;
		if (HasAny(f, BindFlags::RenderTarget))    out |= D3D11_BIND_RENDER_TARGET;
		if (HasAny(f, BindFlags::DepthStencil))    out |= D3D11_BIND_DEPTH_STENCIL;
		if (HasAny(f, BindFlags::UnorderedAccess)) out |= D3D11_BIND_UNORDERED_ACCESS;
		return out;
	}

	inline D3D11_USAGE ToD3D11Usage(ResourceUsage u)
	{
		switch (u)
		{
		case ResourceUsage::Default:   return D3D11_USAGE_DEFAULT;
		case ResourceUsage::Immutable: return D3D11_USAGE_IMMUTABLE;
		case ResourceUsage::Dynamic:   return D3D11_USAGE_DYNAMIC;
		case ResourceUsage::Staging:   return D3D11_USAGE_STAGING;
		default:                       return D3D11_USAGE_DEFAULT;
		}
	}

	inline uint32_t ToD3D11CpuAccessFlags(CpuAccess a)
	{
		uint32_t out = 0;
		if ((uint32_t)a & (uint32_t)CpuAccess::Read)  out |= D3D11_CPU_ACCESS_READ;
		if ((uint32_t)a & (uint32_t)CpuAccess::Write) out |= D3D11_CPU_ACCESS_WRITE;
		return out;
	}

	inline uint32_t ToD3D11MiscFlags(ResourceMiscFlags f)
	{
		uint32_t out = 0;
		if (HasAny(f, ResourceMiscFlags::GenerateMips))        out |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
		if (HasAny(f, ResourceMiscFlags::Shared))              out |= D3D11_RESOURCE_MISC_SHARED;
		if (HasAny(f, ResourceMiscFlags::SharedKeyedMutex))    out |= D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
		if (HasAny(f, ResourceMiscFlags::SharedNTHandle))      out |= D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
		if (HasAny(f, ResourceMiscFlags::BufferStructured))    out |= D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		if (HasAny(f, ResourceMiscFlags::DrawIndirectArgs))    out |= D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
		if (HasAny(f, ResourceMiscFlags::BufferAllowRawViews)) out |= D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
		return out;
	}

	inline D3D11_RTV_DIMENSION ToD3D11RtvDim(ResourceDimension d, int32_t sampleCount = 1)
	{
		switch (d)
		{
		case ResourceDimension::Buffer:    return D3D11_RTV_DIMENSION_BUFFER;
		case ResourceDimension::Texture2D: return sampleCount > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
		case ResourceDimension::Texture2DMS: return D3D11_RTV_DIMENSION_TEXTURE2DMS;
		case ResourceDimension::Texture3D: return D3D11_RTV_DIMENSION_TEXTURE3D;
		case ResourceDimension::Texture2DArray: return D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
		default: return D3D11_RTV_DIMENSION_UNKNOWN;
		}
	}

	inline D3D11_UAV_DIMENSION ToD3D11UavDim(ResourceDimension d)
	{
		switch (d)
		{
		case ResourceDimension::Buffer:    return D3D11_UAV_DIMENSION_BUFFER;
		case ResourceDimension::Texture2D: return D3D11_UAV_DIMENSION_TEXTURE2D;
		case ResourceDimension::Texture3D: return D3D11_UAV_DIMENSION_TEXTURE3D;
		case ResourceDimension::Texture2DArray: return D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
		default: return D3D11_UAV_DIMENSION_UNKNOWN;
		}
	}

	inline D3D11_SRV_DIMENSION ToD3D11SrvDim(ResourceDimension d, int32_t arraySize = 1, int32_t sampleCount = 1)
	{
		switch (d)
		{
		case ResourceDimension::Buffer:           return D3D11_SRV_DIMENSION_BUFFER;
		case ResourceDimension::Texture2D:        return sampleCount > 1
			? D3D11_SRV_DIMENSION_TEXTURE2DMS
			: (arraySize > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DARRAY : D3D11_SRV_DIMENSION_TEXTURE2D);
		case ResourceDimension::Texture2DMS:      return D3D11_SRV_DIMENSION_TEXTURE2DMS;
		case ResourceDimension::Texture3D:        return D3D11_SRV_DIMENSION_TEXTURE3D;
		case ResourceDimension::TextureCube:      return D3D11_SRV_DIMENSION_TEXTURECUBE;
		case ResourceDimension::Texture2DArray:   return D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		case ResourceDimension::TextureCubeArray: return D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
		default: return D3D11_SRV_DIMENSION_UNKNOWN;
		}
	}

	inline D3D11_DSV_DIMENSION ToD3D11DsvDim(ResourceDimension d, int32_t sampleCount = 1)
	{
		switch (d)
		{
		case ResourceDimension::Texture2D:   return sampleCount > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
		case ResourceDimension::Texture2DMS: return D3D11_DSV_DIMENSION_TEXTURE2DMS;
		default: return D3D11_DSV_DIMENSION_UNKNOWN;
		}
	}

	inline D3D_PRIMITIVE_TOPOLOGY ToD3D11Topology(PrimitiveTopology t)
	{
		switch (t)
		{
		case PrimitiveTopology::PointList:      return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
		case PrimitiveTopology::LineList:       return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
		case PrimitiveTopology::LineStrip:      return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
		case PrimitiveTopology::TriangleList:   return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		case PrimitiveTopology::TriangleStrip:  return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
		default:                                return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
		}
	}

	inline D3D11_VIEWPORT ToD3D11Viewport(const Viewport& v)
	{
		D3D11_VIEWPORT out{};
		out.TopLeftX = v.x;
		out.TopLeftY = v.y;
		out.Width    = v.width;
		out.Height   = v.height;
		out.MinDepth = v.minDepth;
		out.MaxDepth = v.maxDepth;
		return out;
	}

	inline Viewport FromD3D11Viewport(const D3D11_VIEWPORT& v)
	{
		return Viewport(v.TopLeftX, v.TopLeftY, v.Width, v.Height, v.MinDepth, v.MaxDepth);
	}

	inline D3D11_RECT ToD3D11Rect(const ScissorRect& r)
	{
		D3D11_RECT out{};
		out.left   = r.left;
		out.top    = r.top;
		out.right  = r.right;
		out.bottom = r.bottom;
		return out;
	}
}
