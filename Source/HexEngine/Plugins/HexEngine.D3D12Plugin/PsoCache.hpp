
#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <unordered_map>
#include <cstdint>
#include <HexEngine.Core/Graphics/RenderStructs.hpp>

class ShaderStageD3D12;
class InputLayoutD3D12;

/**
 * @brief Identity of a graphics PSO for hash-based lookup.
 *
 * Includes everything D3D12 needs at PSO bake time: the shader bytecode
 * identities (we hash the pointer, since shader stage objects outlive PSOs),
 * the input layout pointer, blend / depth / raster state enum values, and
 * the RTV/DSV formats.
 *
 * Compute PSOs use a separate cache (CsPsoKey) keyed only on the compute
 * shader pointer.
 */
struct GfxPsoKey
{
	const void*                vsBytecode = nullptr;
	const void*                psBytecode = nullptr;
	const void*                gsBytecode = nullptr;
	const InputLayoutD3D12*    inputLayout = nullptr;
	HexEngine::BlendState       blendState   = HexEngine::BlendState::Opaque;
	HexEngine::DepthBufferState depthState   = HexEngine::DepthBufferState::DepthDefault;
	HexEngine::CullingMode      cullingMode  = HexEngine::CullingMode::BackFace;
	D3D12_PRIMITIVE_TOPOLOGY_TYPE topology    = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	uint32_t                    rtCount      = 0;
	DXGI_FORMAT                 rtFormats[8] = {};
	DXGI_FORMAT                 dsFormat     = DXGI_FORMAT_UNKNOWN;
	uint32_t                    sampleCount  = 1;

	bool operator==(const GfxPsoKey& o) const
	{
		if (vsBytecode != o.vsBytecode || psBytecode != o.psBytecode || gsBytecode != o.gsBytecode) return false;
		if (inputLayout != o.inputLayout) return false;
		if (blendState != o.blendState || depthState != o.depthState || cullingMode != o.cullingMode) return false;
		if (topology != o.topology) return false;
		if (rtCount != o.rtCount || dsFormat != o.dsFormat || sampleCount != o.sampleCount) return false;
		for (uint32_t i = 0; i < rtCount; ++i)
			if (rtFormats[i] != o.rtFormats[i]) return false;
		return true;
	}
};

struct GfxPsoKeyHash
{
	size_t operator()(const GfxPsoKey& k) const noexcept
	{
		size_t h = (size_t)k.vsBytecode;
		auto mix = [&](size_t v) { h ^= v + 0x9E3779B97F4A7C15ull + (h << 12) + (h >> 4); };
		mix((size_t)k.psBytecode);
		mix((size_t)k.gsBytecode);
		mix((size_t)k.inputLayout);
		mix((size_t)k.blendState);
		mix((size_t)k.depthState);
		mix((size_t)k.cullingMode);
		mix((size_t)k.topology);
		mix((size_t)k.rtCount);
		mix((size_t)k.dsFormat);
		mix((size_t)k.sampleCount);
		for (uint32_t i = 0; i < k.rtCount; ++i)
			mix((size_t)k.rtFormats[i]);
		return h;
	}
};

struct CsPsoKey
{
	const void* csBytecode = nullptr;
	bool operator==(const CsPsoKey& o) const { return csBytecode == o.csBytecode; }
};

struct CsPsoKeyHash
{
	size_t operator()(const CsPsoKey& k) const noexcept { return (size_t)k.csBytecode; }
};

/**
 * @brief Lazy graphics + compute PSO cache.
 *
 * GraphicsDeviceD3D12 calls Resolve() during a pre-draw / pre-dispatch flush
 * with the current pending state. The first time a (key) tuple appears, the
 * cache bakes a D3D12_PIPELINE_STATE; every subsequent occurrence is a
 * hash-map lookup.
 */
class PsoCache
{
public:
	void Create(ID3D12Device* device, ID3D12RootSignature* rootSig);
	void Destroy();

	ID3D12PipelineState* ResolveGraphics(const GfxPsoKey& key,
		const void* vsBytes, size_t vsSize,
		const void* psBytes, size_t psSize,
		const void* gsBytes, size_t gsSize,
		const D3D12_INPUT_ELEMENT_DESC* inputElements, uint32_t inputElementCount);

	ID3D12PipelineState* ResolveCompute(const CsPsoKey& key, const void* csBytes, size_t csSize);

private:
	ID3D12Device*        _device  = nullptr;
	ID3D12RootSignature* _rootSig = nullptr;
	std::unordered_map<GfxPsoKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>, GfxPsoKeyHash> _gfxCache;
	std::unordered_map<CsPsoKey,  Microsoft::WRL::ComPtr<ID3D12PipelineState>, CsPsoKeyHash>  _csCache;
};
