
#pragma once

#include <cstdint>
#include <string>

// Backend-neutral graphics types.
//
// These types describe resource/state intent in HexEngine terms, independent of
// the rendering backend (D3D11 / D3D12 / future Vulkan). Backends translate
// these into their native enums (e.g. TextureFormat -> DXGI_FORMAT in the D3D11
// plugin's FormatsD3D11.hpp).
//
// NOTHING in this header may include backend-specific headers (no <d3d11.h>,
// no <d3d12.h>, no <vulkan.h>). That's the whole point: this file is the
// surface that the engine and editor are allowed to depend on.

namespace HexEngine
{
	/** @brief Identifies which graphics backend an IGraphicsDevice is implementing. */
	enum class GraphicsBackend : uint32_t
	{
		Unknown = 0,
		D3D11,
		D3D12,
		// Future: Vulkan, Metal...
	};

	/**
	 * @brief Resolves whether a renderer plugin with `selfBackend` should activate
	 *        given the current `r_renderer` cvar value.
	 *
	 * Plugin contract: each renderer plugin's CreateInterface() calls this with
	 * its own GraphicsBackend tag. If it returns false, the plugin returns nullptr
	 * from CreateInterface and the engine falls back to whichever other renderer
	 * plugin claims IGraphicsDevice. The check guarantees that at most one
	 * renderer plugin per run hands its IGraphicsDevice to the engine.
	 *
	 * Cvar values (from Game3DEnvironment.cpp):
	 *   0 = auto -> currently prefers D3D11 (until D3D12 implementation is complete)
	 *   1 = D3D11
	 *   2 = D3D12
	 */
	inline bool ShouldActivateBackend(GraphicsBackend selfBackend, int32_t rendererCvar)
	{
		switch (rendererCvar)
		{
		case 1: return selfBackend == GraphicsBackend::D3D11;
		case 2: return selfBackend == GraphicsBackend::D3D12;
		case 0: // auto
		default:
			// Phase A bias: D3D11 wins auto-mode because the D3D12 plugin is a
			// non-functional stub. When D3D12 reaches feature parity, flip the
			// auto-mode preference here and document the cutover.
			return selfBackend == GraphicsBackend::D3D11;
		}
	}

	/**
	 * @brief Pixel / vertex / typed-buffer format.
	 *
	 * Mirrors the closed set of DXGI_FORMAT values the engine actually uses.
	 * If a new format is required, add it here AND add the translation entry
	 * in every backend's format-translation table (e.g.
	 * HexEngine.D3D11Plugin/FormatsD3D11.hpp).
	 */
	enum class TextureFormat : uint32_t
	{
		Unknown = 0,

		// 8-bit
		R8_UNORM,
		R8_SNORM,
		R8_UINT,
		R8_SINT,
		R8G8_UNORM,
		R8G8_SNORM,
		R8G8_UINT,
		R8G8_SINT,
		R8G8B8A8_UNORM,
		R8G8B8A8_UNORM_SRGB,
		R8G8B8A8_SNORM,
		R8G8B8A8_UINT,
		R8G8B8A8_SINT,
		B8G8R8A8_UNORM,
		B8G8R8A8_UNORM_SRGB,
		B8G8R8X8_UNORM,
		B8G8R8X8_UNORM_SRGB,

		// 16-bit
		R16_TYPELESS,
		R16_UNORM,
		R16_SNORM,
		R16_UINT,
		R16_SINT,
		R16_FLOAT,
		R16G16_UNORM,
		R16G16_SNORM,
		R16G16_UINT,
		R16G16_SINT,
		R16G16_FLOAT,
		R16G16B16A16_UNORM,
		R16G16B16A16_SNORM,
		R16G16B16A16_UINT,
		R16G16B16A16_SINT,
		R16G16B16A16_FLOAT,

		// 32-bit
		R32_TYPELESS,
		R32_UINT,
		R32_SINT,
		R32_FLOAT,
		R32G32_UINT,
		R32G32_SINT,
		R32G32_FLOAT,
		R32G32B32_UINT,
		R32G32B32_SINT,
		R32G32B32_FLOAT,
		R32G32B32A32_UINT,
		R32G32B32A32_SINT,
		R32G32B32A32_FLOAT,

		// Packed
		R10G10B10A2_UNORM,
		R10G10B10A2_UINT,
		R11G11B10_FLOAT,
		R9G9B9E5_SHAREDEXP,

		// Depth / stencil
		D16_UNORM,
		D24_UNORM_S8_UINT,
		D32_FLOAT,
		D32_FLOAT_S8X24_UINT,

		// Typeless aliases needed for view re-interpretation of depth buffers
		R24G8_TYPELESS,
		R24_UNORM_X8_TYPELESS,
		R32G8X24_TYPELESS,
		R32_FLOAT_X8X24_TYPELESS,

		Count
	};

	/**
	 * @brief Texture / buffer bind capability flags (combinable).
	 *
	 * Tells the backend which views the resource will need (SRV for shader read,
	 * RTV for render target, UAV for unordered access, etc.). At least one
	 * shader-stage-binding flag is usually required; D3D11 in particular won't
	 * create a resource with BindFlags::None.
	 */
	enum class BindFlags : uint32_t
	{
		None            = 0,
		VertexBuffer    = 1 << 0,
		IndexBuffer     = 1 << 1,
		ConstantBuffer  = 1 << 2,
		ShaderResource  = 1 << 3,
		StreamOutput    = 1 << 4,
		RenderTarget    = 1 << 5,
		DepthStencil    = 1 << 6,
		UnorderedAccess = 1 << 7,
	};

	inline constexpr BindFlags operator|(BindFlags a, BindFlags b)
	{
		return static_cast<BindFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
	}
	inline constexpr BindFlags operator&(BindFlags a, BindFlags b)
	{
		return static_cast<BindFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
	}
	inline constexpr BindFlags& operator|=(BindFlags& a, BindFlags b) { a = a | b; return a; }
	inline constexpr BindFlags& operator&=(BindFlags& a, BindFlags b) { a = a & b; return a; }
	inline constexpr bool HasAny(BindFlags v, BindFlags mask)
	{
		return (static_cast<uint32_t>(v) & static_cast<uint32_t>(mask)) != 0u;
	}

	/**
	 * @brief Resource update frequency / heap-residence intent.
	 *
	 * Maps 1:1 to D3D11_USAGE. On D3D12 these collapse to upload/default/readback
	 * heap selection.
	 */
	enum class ResourceUsage : uint32_t
	{
		Default = 0,   ///< GPU-read/write, no CPU access
		Immutable,     ///< Created once with initial data, GPU-read-only
		Dynamic,       ///< CPU-write-frequent (Map/Discard or upload-ring)
		Staging,       ///< CPU/GPU copy intermediate, not bindable
	};

	/** @brief CPU access requirement on a resource (combinable). */
	enum class CpuAccess : uint32_t
	{
		None  = 0,
		Read  = 1 << 0,
		Write = 1 << 1,
	};

	inline constexpr CpuAccess operator|(CpuAccess a, CpuAccess b)
	{
		return static_cast<CpuAccess>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
	}
	inline constexpr CpuAccess& operator|=(CpuAccess& a, CpuAccess b) { a = a | b; return a; }

	/**
	 * @brief Resource dimensionality for view binding.
	 *
	 * Single enum replaces the four D3D11_RTV/UAV/SRV/DSV_DIMENSION enums - in
	 * practice they were always pinned to the same logical dimension at every
	 * call site.
	 */
	enum class ResourceDimension : uint32_t
	{
		Unknown = 0,
		Buffer,
		Texture1D,
		Texture2D,
		Texture2DMS,
		Texture3D,
		TextureCube,
		Texture2DArray,
		TextureCubeArray,
	};

	/**
	 * @brief Misc resource hints (combinable).
	 *
	 * Generally one-per-resource; only Structured and IndirectArgs are commonly
	 * combined (e.g. an indirect-draw args buffer that's also structured).
	 */
	enum class ResourceMiscFlags : uint32_t
	{
		None                = 0,
		GenerateMips        = 1 << 0,
		Shared              = 1 << 1,
		SharedKeyedMutex    = 1 << 2,
		SharedNTHandle      = 1 << 3,
		BufferStructured    = 1 << 4,
		DrawIndirectArgs    = 1 << 5,
		BufferAllowRawViews = 1 << 6,
	};

	inline constexpr ResourceMiscFlags operator|(ResourceMiscFlags a, ResourceMiscFlags b)
	{
		return static_cast<ResourceMiscFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
	}
	inline constexpr ResourceMiscFlags& operator|=(ResourceMiscFlags& a, ResourceMiscFlags b) { a = a | b; return a; }
	inline constexpr bool HasAny(ResourceMiscFlags v, ResourceMiscFlags mask)
	{
		return (static_cast<uint32_t>(v) & static_cast<uint32_t>(mask)) != 0u;
	}

	/** @brief Primitive topology for the input assembler. */
	enum class PrimitiveTopology : uint32_t
	{
		Undefined = 0,
		PointList,
		LineList,
		LineStrip,
		TriangleList,
		TriangleStrip,
	};

	/** @brief Render viewport in pixels (origin at top-left). */
	struct Viewport
	{
		float x        = 0.0f;
		float y        = 0.0f;
		float width    = 0.0f;
		float height   = 0.0f;
		float minDepth = 0.0f;
		float maxDepth = 1.0f;

		Viewport() = default;
		Viewport(float x_, float y_, float w_, float h_, float minD = 0.0f, float maxD = 1.0f)
			: x(x_), y(y_), width(w_), height(h_), minDepth(minD), maxDepth(maxD) {}
	};

	/** @brief Scissor rectangle (pixel coordinates, top-left inclusive / bottom-right exclusive). */
	struct ScissorRect
	{
		int32_t left   = 0;
		int32_t top    = 0;
		int32_t right  = 0;
		int32_t bottom = 0;

		ScissorRect() = default;
		ScissorRect(int32_t l, int32_t t, int32_t r, int32_t b)
			: left(l), top(t), right(r), bottom(b) {}
	};

	/** @brief One vertex-input layout slot. */
	struct InputElement
	{
		std::string   semanticName;        ///< e.g. "POSITION", "TEXCOORD"
		uint32_t      semanticIndex        = 0;
		TextureFormat format               = TextureFormat::Unknown;
		uint32_t      inputSlot            = 0;
		uint32_t      alignedByteOffset    = 0; ///< 0xFFFFFFFF for D3D11_APPEND_ALIGNED_ELEMENT semantics
		bool          perInstance          = false;
		uint32_t      instanceDataStepRate = 0;
	};

	/** @brief CPU-side data for one subresource (used at resource creation time). */
	struct SubresourceData
	{
		const void* data            = nullptr;
		uint32_t    rowPitchBytes   = 0; ///< texture row pitch; ignored for buffers
		uint32_t    slicePitchBytes = 0; ///< 3D slice pitch (or buffer total size)
	};

	/**
	 * @brief Texture creation descriptor.
	 *
	 * Replaces the 14-argument CreateTexture2D / CreateTexture3D signatures.
	 * Sensible defaults are provided so most callers can build one with a few
	 * explicit fields.
	 */
	struct TextureDesc
	{
		int32_t           width        = 0;
		int32_t           height       = 0;
		int32_t           depth        = 1;   ///< 3D textures only; ignored for 2D
		int32_t           arraySize    = 1;
		int32_t           mipLevels    = 1;   ///< 0 = full mip chain
		int32_t           sampleCount  = 1;
		int32_t           sampleQuality = 0;
		TextureFormat     format       = TextureFormat::R8G8B8A8_UNORM;
		BindFlags         bindFlags    = BindFlags::ShaderResource;
		ResourceUsage     usage        = ResourceUsage::Default;
		CpuAccess         cpuAccess    = CpuAccess::None;
		ResourceDimension dimension    = ResourceDimension::Texture2D;
		ResourceMiscFlags miscFlags    = ResourceMiscFlags::None;
	};

	/** @brief Buffer creation descriptor (vertex / index / structured / constant). */
	struct BufferDesc
	{
		uint32_t          byteWidth   = 0;
		uint32_t          byteStride  = 0; ///< per-element stride; 0 for raw buffers
		ResourceUsage     usage       = ResourceUsage::Default;
		BindFlags         bindFlags   = BindFlags::None;
		CpuAccess         cpuAccess   = CpuAccess::None;
		ResourceMiscFlags miscFlags   = ResourceMiscFlags::None;
	};
}
