
#include "PsoCache.hpp"
#include "InputLayoutD3D12.hpp"
#include <HexEngine.Core/HexEngine.hpp>

namespace
{
	// State translators - lifted out so the Resolve path stays readable.

	D3D12_RENDER_TARGET_BLEND_DESC RtBlend(HexEngine::BlendState s)
	{
		D3D12_RENDER_TARGET_BLEND_DESC b = {};
		b.BlendEnable           = (s != HexEngine::BlendState::Opaque && s != HexEngine::BlendState::Invalid);
		b.LogicOpEnable         = FALSE;
		b.LogicOp               = D3D12_LOGIC_OP_NOOP;
		b.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		switch (s)
		{
		case HexEngine::BlendState::Additive:
			b.SrcBlend = D3D12_BLEND_ONE;       b.DestBlend = D3D12_BLEND_ONE;       b.BlendOp = D3D12_BLEND_OP_ADD;
			b.SrcBlendAlpha = D3D12_BLEND_ONE;  b.DestBlendAlpha = D3D12_BLEND_ONE;  b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			break;
		case HexEngine::BlendState::Subtractive:
			b.SrcBlend = D3D12_BLEND_ONE;       b.DestBlend = D3D12_BLEND_ONE;       b.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
			b.SrcBlendAlpha = D3D12_BLEND_ONE;  b.DestBlendAlpha = D3D12_BLEND_ONE;  b.BlendOpAlpha = D3D12_BLEND_OP_REV_SUBTRACT;
			break;
		case HexEngine::BlendState::Transparency:
			b.SrcBlend = D3D12_BLEND_SRC_ALPHA; b.DestBlend = D3D12_BLEND_INV_SRC_ALPHA; b.BlendOp = D3D12_BLEND_OP_ADD;
			b.SrcBlendAlpha = D3D12_BLEND_ONE;  b.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA; b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			break;
		case HexEngine::BlendState::TransparencyPreserveAlpha:
			b.SrcBlend = D3D12_BLEND_SRC_ALPHA; b.DestBlend = D3D12_BLEND_INV_SRC_ALPHA; b.BlendOp = D3D12_BLEND_OP_ADD;
			b.SrcBlendAlpha = D3D12_BLEND_ZERO; b.DestBlendAlpha = D3D12_BLEND_ONE; b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			break;
		case HexEngine::BlendState::Multiplicative:
			b.SrcBlend = D3D12_BLEND_ZERO;      b.DestBlend = D3D12_BLEND_SRC_COLOR; b.BlendOp = D3D12_BLEND_OP_ADD;
			b.SrcBlendAlpha = D3D12_BLEND_ZERO; b.DestBlendAlpha = D3D12_BLEND_ONE;  b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			break;
		case HexEngine::BlendState::Opaque:
		case HexEngine::BlendState::Invalid:
		default:
			b.SrcBlend = D3D12_BLEND_ONE;       b.DestBlend = D3D12_BLEND_ZERO;      b.BlendOp = D3D12_BLEND_OP_ADD;
			b.SrcBlendAlpha = D3D12_BLEND_ONE;  b.DestBlendAlpha = D3D12_BLEND_ZERO; b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			break;
		}
		return b;
	}

	D3D12_DEPTH_STENCIL_DESC DepthStencil(HexEngine::DepthBufferState s)
	{
		D3D12_DEPTH_STENCIL_DESC d = {};
		d.StencilEnable    = FALSE;
		d.StencilReadMask  = D3D12_DEFAULT_STENCIL_READ_MASK;
		d.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
		switch (s)
		{
		case HexEngine::DepthBufferState::DepthNone:
			d.DepthEnable    = FALSE;
			d.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
			d.DepthFunc      = D3D12_COMPARISON_FUNC_ALWAYS;
			break;
		case HexEngine::DepthBufferState::DepthRead:
			d.DepthEnable    = TRUE;
			d.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
			d.DepthFunc      = D3D12_COMPARISON_FUNC_LESS_EQUAL;
			break;
		case HexEngine::DepthBufferState::DepthReverseZ:
			d.DepthEnable    = TRUE;
			d.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
			d.DepthFunc      = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
			break;
		case HexEngine::DepthBufferState::DepthReadReverseZ:
			d.DepthEnable    = TRUE;
			d.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
			d.DepthFunc      = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
			break;
		case HexEngine::DepthBufferState::DepthDefault:
		default:
			d.DepthEnable    = TRUE;
			d.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
			d.DepthFunc      = D3D12_COMPARISON_FUNC_LESS_EQUAL;
			break;
		}
		return d;
	}

	D3D12_CULL_MODE Cull(HexEngine::CullingMode m)
	{
		switch (m)
		{
		case HexEngine::CullingMode::NoCulling: return D3D12_CULL_MODE_NONE;
		case HexEngine::CullingMode::FrontFace: return D3D12_CULL_MODE_FRONT;
		case HexEngine::CullingMode::BackFace:
		default:                                return D3D12_CULL_MODE_BACK;
		}
	}
}

void PsoCache::Create(ID3D12Device* device, ID3D12RootSignature* rootSig)
{
	_device  = device;
	_rootSig = rootSig;
}

void PsoCache::Destroy()
{
	_gfxCache.clear();
	_csCache.clear();
	_device  = nullptr;
	_rootSig = nullptr;
}

ID3D12PipelineState* PsoCache::ResolveGraphics(const GfxPsoKey& key,
	const void* vsBytes, size_t vsSize,
	const void* psBytes, size_t psSize,
	const void* gsBytes, size_t gsSize,
	const D3D12_INPUT_ELEMENT_DESC* inputElements, uint32_t inputElementCount)
{
	auto it = _gfxCache.find(key);
	if (it != _gfxCache.end())
		return it->second.Get();

	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
	desc.pRootSignature                  = _rootSig;
	desc.VS                              = { vsBytes, vsSize };
	desc.PS                              = { psBytes, psSize };
	if (gsBytes && gsSize) desc.GS       = { gsBytes, gsSize };

	desc.BlendState.AlphaToCoverageEnable  = FALSE;
	desc.BlendState.IndependentBlendEnable = FALSE;
	auto rtBlend = RtBlend(key.blendState);
	for (auto& rt : desc.BlendState.RenderTarget) rt = rtBlend;

	desc.SampleMask                      = UINT_MAX;

	D3D12_RASTERIZER_DESC rast = {};
	rast.FillMode              = D3D12_FILL_MODE_SOLID;
	rast.CullMode              = Cull(key.cullingMode);
	rast.FrontCounterClockwise = FALSE;
	rast.DepthClipEnable       = TRUE;
	rast.MultisampleEnable     = (key.sampleCount > 1);
	rast.AntialiasedLineEnable = FALSE;
	rast.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
	desc.RasterizerState       = rast;

	desc.DepthStencilState               = DepthStencil(key.depthState);
	desc.InputLayout.NumElements         = inputElementCount;
	desc.InputLayout.pInputElementDescs  = inputElements;
	desc.IBStripCutValue                 = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
	desc.PrimitiveTopologyType           = key.topology;
	desc.NumRenderTargets                = key.rtCount;
	for (uint32_t i = 0; i < key.rtCount; ++i) desc.RTVFormats[i] = key.rtFormats[i];
	desc.DSVFormat                       = key.dsFormat;
	desc.SampleDesc.Count                = key.sampleCount;
	desc.SampleDesc.Quality              = 0;
	desc.NodeMask                        = 0;
	desc.Flags                           = D3D12_PIPELINE_STATE_FLAG_NONE;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
	HRESULT hr = _device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso));
	if (FAILED(hr))
	{
		LOG_CRIT("PsoCache::ResolveGraphics CreateGraphicsPipelineState failed (0x%X)", hr);
		return nullptr;
	}
	auto* raw = pso.Get();
	_gfxCache.emplace(key, std::move(pso));
	return raw;
}

ID3D12PipelineState* PsoCache::ResolveCompute(const CsPsoKey& key, const void* csBytes, size_t csSize)
{
	auto it = _csCache.find(key);
	if (it != _csCache.end())
		return it->second.Get();

	D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
	desc.pRootSignature = _rootSig;
	desc.CS             = { csBytes, csSize };
	desc.NodeMask       = 0;
	desc.Flags          = D3D12_PIPELINE_STATE_FLAG_NONE;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
	HRESULT hr = _device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pso));
	if (FAILED(hr))
	{
		LOG_CRIT("PsoCache::ResolveCompute CreateComputePipelineState failed (0x%X)", hr);
		return nullptr;
	}
	auto* raw = pso.Get();
	_csCache.emplace(key, std::move(pso));
	return raw;
}
