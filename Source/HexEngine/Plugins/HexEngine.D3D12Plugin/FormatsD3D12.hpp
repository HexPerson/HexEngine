
#pragma once

// Translation helpers between HexEngine's backend-neutral Formats.hpp types
// and the D3D12 / DXGI enums. INTERNAL to the D3D12 plugin - no Core or
// Editor header is allowed to include this (or <d3d12.h>).

#include <d3d12.h>
#include <dxgi1_6.h>
#include <HexEngine.Core/Graphics/Formats.hpp>
#include <HexEngine.Core/Graphics/RenderStructs.hpp>

namespace HexEngine
{
	// Inverse of ToDXGI12 - used by the clone-an-existing-texture path which
	// queries the source resource's D3D12_RESOURCE_DESC and needs to round-trip
	// the format back through the neutral TextureFormat enum so the standard
	// CreateTexture2D path can be reused. Unknown / unmapped formats fall
	// through to TextureFormat::Unknown - caller should treat that as a clone
	// failure.
	inline TextureFormat FromDXGI12(DXGI_FORMAT f)
	{
		switch (f)
		{
		case DXGI_FORMAT_UNKNOWN:                    return TextureFormat::Unknown;
		case DXGI_FORMAT_R8_UNORM:                   return TextureFormat::R8_UNORM;
		case DXGI_FORMAT_R8_SNORM:                   return TextureFormat::R8_SNORM;
		case DXGI_FORMAT_R8_UINT:                    return TextureFormat::R8_UINT;
		case DXGI_FORMAT_R8_SINT:                    return TextureFormat::R8_SINT;
		case DXGI_FORMAT_R8G8_UNORM:                 return TextureFormat::R8G8_UNORM;
		case DXGI_FORMAT_R8G8_SNORM:                 return TextureFormat::R8G8_SNORM;
		case DXGI_FORMAT_R8G8_UINT:                  return TextureFormat::R8G8_UINT;
		case DXGI_FORMAT_R8G8_SINT:                  return TextureFormat::R8G8_SINT;
		case DXGI_FORMAT_R8G8B8A8_UNORM:             return TextureFormat::R8G8B8A8_UNORM;
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:        return TextureFormat::R8G8B8A8_UNORM_SRGB;
		case DXGI_FORMAT_R8G8B8A8_SNORM:             return TextureFormat::R8G8B8A8_SNORM;
		case DXGI_FORMAT_R8G8B8A8_UINT:              return TextureFormat::R8G8B8A8_UINT;
		case DXGI_FORMAT_R8G8B8A8_SINT:              return TextureFormat::R8G8B8A8_SINT;
		case DXGI_FORMAT_B8G8R8A8_UNORM:             return TextureFormat::B8G8R8A8_UNORM;
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:        return TextureFormat::B8G8R8A8_UNORM_SRGB;
		case DXGI_FORMAT_B8G8R8X8_UNORM:             return TextureFormat::B8G8R8X8_UNORM;
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:        return TextureFormat::B8G8R8X8_UNORM_SRGB;
		case DXGI_FORMAT_R16_TYPELESS:               return TextureFormat::R16_TYPELESS;
		case DXGI_FORMAT_R16_UNORM:                  return TextureFormat::R16_UNORM;
		case DXGI_FORMAT_R16_SNORM:                  return TextureFormat::R16_SNORM;
		case DXGI_FORMAT_R16_UINT:                   return TextureFormat::R16_UINT;
		case DXGI_FORMAT_R16_SINT:                   return TextureFormat::R16_SINT;
		case DXGI_FORMAT_R16_FLOAT:                  return TextureFormat::R16_FLOAT;
		case DXGI_FORMAT_R16G16_UNORM:               return TextureFormat::R16G16_UNORM;
		case DXGI_FORMAT_R16G16_SNORM:               return TextureFormat::R16G16_SNORM;
		case DXGI_FORMAT_R16G16_UINT:                return TextureFormat::R16G16_UINT;
		case DXGI_FORMAT_R16G16_SINT:                return TextureFormat::R16G16_SINT;
		case DXGI_FORMAT_R16G16_FLOAT:               return TextureFormat::R16G16_FLOAT;
		case DXGI_FORMAT_R16G16B16A16_UNORM:         return TextureFormat::R16G16B16A16_UNORM;
		case DXGI_FORMAT_R16G16B16A16_SNORM:         return TextureFormat::R16G16B16A16_SNORM;
		case DXGI_FORMAT_R16G16B16A16_UINT:          return TextureFormat::R16G16B16A16_UINT;
		case DXGI_FORMAT_R16G16B16A16_SINT:          return TextureFormat::R16G16B16A16_SINT;
		case DXGI_FORMAT_R16G16B16A16_FLOAT:         return TextureFormat::R16G16B16A16_FLOAT;
		case DXGI_FORMAT_R32_TYPELESS:               return TextureFormat::R32_TYPELESS;
		case DXGI_FORMAT_R32_UINT:                   return TextureFormat::R32_UINT;
		case DXGI_FORMAT_R32_SINT:                   return TextureFormat::R32_SINT;
		case DXGI_FORMAT_R32_FLOAT:                  return TextureFormat::R32_FLOAT;
		case DXGI_FORMAT_R32G32_UINT:                return TextureFormat::R32G32_UINT;
		case DXGI_FORMAT_R32G32_SINT:                return TextureFormat::R32G32_SINT;
		case DXGI_FORMAT_R32G32_FLOAT:               return TextureFormat::R32G32_FLOAT;
		case DXGI_FORMAT_R32G32B32_UINT:             return TextureFormat::R32G32B32_UINT;
		case DXGI_FORMAT_R32G32B32_SINT:             return TextureFormat::R32G32B32_SINT;
		case DXGI_FORMAT_R32G32B32_FLOAT:            return TextureFormat::R32G32B32_FLOAT;
		case DXGI_FORMAT_R32G32B32A32_UINT:          return TextureFormat::R32G32B32A32_UINT;
		case DXGI_FORMAT_R32G32B32A32_SINT:          return TextureFormat::R32G32B32A32_SINT;
		case DXGI_FORMAT_R32G32B32A32_FLOAT:         return TextureFormat::R32G32B32A32_FLOAT;
		case DXGI_FORMAT_R10G10B10A2_UNORM:          return TextureFormat::R10G10B10A2_UNORM;
		case DXGI_FORMAT_R10G10B10A2_UINT:           return TextureFormat::R10G10B10A2_UINT;
		case DXGI_FORMAT_R11G11B10_FLOAT:            return TextureFormat::R11G11B10_FLOAT;
		case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:         return TextureFormat::R9G9B9E5_SHAREDEXP;
		case DXGI_FORMAT_D16_UNORM:                  return TextureFormat::D16_UNORM;
		case DXGI_FORMAT_D24_UNORM_S8_UINT:          return TextureFormat::D24_UNORM_S8_UINT;
		case DXGI_FORMAT_D32_FLOAT:                  return TextureFormat::D32_FLOAT;
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:       return TextureFormat::D32_FLOAT_S8X24_UINT;
		case DXGI_FORMAT_R24G8_TYPELESS:             return TextureFormat::R24G8_TYPELESS;
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:      return TextureFormat::R24_UNORM_X8_TYPELESS;
		case DXGI_FORMAT_R32G8X24_TYPELESS:          return TextureFormat::R32G8X24_TYPELESS;
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:   return TextureFormat::R32_FLOAT_X8X24_TYPELESS;
		default:                                     return TextureFormat::Unknown;
		}
	}

	inline DXGI_FORMAT ToDXGI12(TextureFormat f)
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

	/**
	 * @brief Picks the SRV-side format for typeless / depth resources.
	 *
	 * When a texture is created as a typeless or depth format, the SRV needs
	 * a specific reinterpretation (e.g. R24G8_TYPELESS texture -> R24_UNORM_X8
	 * SRV view). D3D12 doesn't auto-promote like D3D11's NULL view does, so
	 * we map them explicitly.
	 */
	inline DXGI_FORMAT GetSrvFormatD3D12(DXGI_FORMAT f)
	{
		switch (f)
		{
		case DXGI_FORMAT_R32_TYPELESS:    return DXGI_FORMAT_R32_FLOAT;
		case DXGI_FORMAT_R16_TYPELESS:    return DXGI_FORMAT_R16_FLOAT;
		case DXGI_FORMAT_R24G8_TYPELESS:  return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
		case DXGI_FORMAT_D16_UNORM:       return DXGI_FORMAT_R16_UNORM;
		case DXGI_FORMAT_D24_UNORM_S8_UINT: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		case DXGI_FORMAT_D32_FLOAT:       return DXGI_FORMAT_R32_FLOAT;
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
		default:                          return f;
		}
	}

	/** @brief Picks the DSV-side format for typeless depth resources. */
	inline DXGI_FORMAT GetDsvFormatD3D12(DXGI_FORMAT f)
	{
		switch (f)
		{
		case DXGI_FORMAT_R32_TYPELESS:    return DXGI_FORMAT_D32_FLOAT;
		case DXGI_FORMAT_R16_TYPELESS:    return DXGI_FORMAT_D16_UNORM;
		case DXGI_FORMAT_R24G8_TYPELESS:  return DXGI_FORMAT_D24_UNORM_S8_UINT;
		case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
		default:                          return f;
		}
	}

	/** @brief Maps a HexEngine bind-flag set to D3D12 resource flags. */
	inline D3D12_RESOURCE_FLAGS ToD3D12ResourceFlags(BindFlags f)
	{
		D3D12_RESOURCE_FLAGS out = D3D12_RESOURCE_FLAG_NONE;
		if (HasAny(f, BindFlags::RenderTarget))    out |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		if (HasAny(f, BindFlags::DepthStencil))    out |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		if (HasAny(f, BindFlags::UnorderedAccess)) out |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		// DENY_SHADER_RESOURCE is required when ALLOW_DEPTH_STENCIL is set and
		// the resource has no SHADER_RESOURCE bind flag - depth-only textures.
		if (HasAny(f, BindFlags::DepthStencil) && !HasAny(f, BindFlags::ShaderResource))
			out |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
		return out;
	}

	/** @brief Picks the initial D3D12 resource state from bind flags. */
	inline D3D12_RESOURCE_STATES InitialStateFromBindFlags(BindFlags f, ResourceUsage usage)
	{
		// Upload-heap resources MUST start in GENERIC_READ; the runtime enforces it.
		if (usage == ResourceUsage::Dynamic)
			return D3D12_RESOURCE_STATE_GENERIC_READ;
		// Readback-heap resources MUST start in COPY_DEST.
		if (usage == ResourceUsage::Staging)
			return D3D12_RESOURCE_STATE_COPY_DEST;
		// For default-heap, pick the most-likely first use.
		if (HasAny(f, BindFlags::DepthStencil))    return D3D12_RESOURCE_STATE_DEPTH_WRITE;
		if (HasAny(f, BindFlags::RenderTarget))    return D3D12_RESOURCE_STATE_RENDER_TARGET;
		if (HasAny(f, BindFlags::UnorderedAccess)) return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		if (HasAny(f, BindFlags::VertexBuffer) || HasAny(f, BindFlags::ConstantBuffer))
			return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		if (HasAny(f, BindFlags::IndexBuffer))     return D3D12_RESOURCE_STATE_INDEX_BUFFER;
		// Generic read covers shader-resource binding + most copies.
		return D3D12_RESOURCE_STATE_COMMON;
	}

	inline D3D12_HEAP_TYPE ToD3D12HeapType(ResourceUsage u)
	{
		switch (u)
		{
		case ResourceUsage::Immutable: return D3D12_HEAP_TYPE_DEFAULT;
		case ResourceUsage::Default:   return D3D12_HEAP_TYPE_DEFAULT;
		case ResourceUsage::Dynamic:   return D3D12_HEAP_TYPE_UPLOAD;
		case ResourceUsage::Staging:   return D3D12_HEAP_TYPE_READBACK;
		default:                       return D3D12_HEAP_TYPE_DEFAULT;
		}
	}

	inline D3D_PRIMITIVE_TOPOLOGY ToD3D12Topology(PrimitiveTopology t)
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

	inline D3D12_INPUT_CLASSIFICATION ToD3D12InputClass(bool perInstance)
	{
		return perInstance
			? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
			: D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	}

	/** @brief Picks an SRV view dimension from a resource dimension. */
	inline D3D12_SRV_DIMENSION SrvDimD3D12(ResourceDimension d, int32_t arraySize, int32_t sampleCount)
	{
		switch (d)
		{
		case ResourceDimension::Buffer:           return D3D12_SRV_DIMENSION_BUFFER;
		case ResourceDimension::Texture2D:        return sampleCount > 1
			? D3D12_SRV_DIMENSION_TEXTURE2DMS
			: (arraySize > 1 ? D3D12_SRV_DIMENSION_TEXTURE2DARRAY : D3D12_SRV_DIMENSION_TEXTURE2D);
		case ResourceDimension::Texture2DMS:      return D3D12_SRV_DIMENSION_TEXTURE2DMS;
		case ResourceDimension::Texture3D:        return D3D12_SRV_DIMENSION_TEXTURE3D;
		case ResourceDimension::TextureCube:      return D3D12_SRV_DIMENSION_TEXTURECUBE;
		case ResourceDimension::Texture2DArray:   return D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
		case ResourceDimension::TextureCubeArray: return D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
		default:                                  return D3D12_SRV_DIMENSION_UNKNOWN;
		}
	}
}
