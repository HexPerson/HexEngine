#include "NRDInterface.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <d3dcompiler.h>
#include <fstream>
#include <sstream>

#pragma comment(lib, "d3dcompiler.lib")

namespace
{
	HexEngine::HVar r_nrdDenoiser("r_nrdDenoiser", "NRD denoiser mode: 0 = RELAX_SPECULAR, 1 = REBLUR_SPECULAR", 0, 0, 1);
	HexEngine::HVar r_nrdRelaxPreset("r_nrdRelaxPreset", "RELAX preset: 0 = custom, 1 = stable, 2 = balanced, 3 = responsive", 2, 0, 3);
	HexEngine::HVar r_nrdRelaxDiffMaxFrames("r_nrdRelaxDiffMaxFrames", "RELAX diffuse maximum accumulated frames", 4, 0, 63);
	HexEngine::HVar r_nrdRelaxSpecMaxFrames("r_nrdRelaxSpecMaxFrames", "RELAX specular maximum accumulated frames", 6, 0, 63);
	HexEngine::HVar r_nrdRelaxDiffFastFrames("r_nrdRelaxDiffFastFrames", "RELAX diffuse fast-history accumulated frames", 1, 0, 63);
	HexEngine::HVar r_nrdRelaxSpecFastFrames("r_nrdRelaxSpecFastFrames", "RELAX specular fast-history accumulated frames", 2, 0, 63);
	HexEngine::HVar r_nrdRelaxHistoryFixFrames("r_nrdRelaxHistoryFixFrames", "RELAX history-fix frame count", 2, 0, 3);
	HexEngine::HVar r_nrdRelaxDiffPrepassBlur("r_nrdRelaxDiffPrepassBlur", "RELAX diffuse prepass blur radius", 10.0f, 0.0f, 70.0f);
	HexEngine::HVar r_nrdRelaxSpecPrepassBlur("r_nrdRelaxSpecPrepassBlur", "RELAX specular prepass blur radius", 24.0f, 0.0f, 70.0f);
	HexEngine::HVar r_nrdRelaxHistoryClampSigma("r_nrdRelaxHistoryClampSigma", "RELAX history clamp sigma scale", 1.0f, 0.1f, 4.0f);
	HexEngine::HVar r_nrdRelaxLobeAngleFraction("r_nrdRelaxLobeAngleFraction", "RELAX lobe angle fraction", 0.35f, 0.01f, 1.0f);
	HexEngine::HVar r_nrdRelaxRoughnessFraction("r_nrdRelaxRoughnessFraction", "RELAX roughness fraction", 0.12f, 0.01f, 1.0f);
	HexEngine::HVar r_nrdRelaxDiffPhiLuminance("r_nrdRelaxDiffPhiLuminance", "RELAX diffuse luminance edge stopping", 2.0f, 0.1f, 8.0f);
	HexEngine::HVar r_nrdRelaxSpecPhiLuminance("r_nrdRelaxSpecPhiLuminance", "RELAX specular luminance edge stopping", 1.0f, 0.1f, 8.0f);
	HexEngine::HVar r_nrdRelaxSpecVarianceBoost("r_nrdRelaxSpecVarianceBoost", "RELAX specular variance boost", 0.0f, 0.0f, 8.0f);
	HexEngine::HVar r_nrdRelaxHitDistanceReconstruction("r_nrdRelaxHitDistanceReconstruction", "RELAX hit distance reconstruction mode: 0 = off, 1 = 3x3, 2 = 5x5", 0, 0, 2);
	HexEngine::HVar r_nrdRelaxAntiFirefly("r_nrdRelaxAntiFirefly", "Enable RELAX anti-firefly suppression", true, false, true);
	HexEngine::HVar r_nrdRelaxRoughnessEdgeStopping("r_nrdRelaxRoughnessEdgeStopping", "Enable RELAX roughness edge stopping", true, false, true);
	HexEngine::HVar r_nrdRelaxAntilag("r_nrdRelaxAntilag", "Enable RELAX anti-lag", false, false, true);
	HexEngine::HVar r_nrdRelaxAntilagAcceleration("r_nrdRelaxAntilagAcceleration", "RELAX anti-lag acceleration amount", 0.25f, 0.0f, 1.0f);
	HexEngine::HVar r_nrdRelaxAntilagSpatialSigma("r_nrdRelaxAntilagSpatialSigma", "RELAX anti-lag spatial sigma scale", 3.0f, 0.1f, 8.0f);
	HexEngine::HVar r_nrdRelaxAntilagTemporalSigma("r_nrdRelaxAntilagTemporalSigma", "RELAX anti-lag temporal sigma scale", 0.25f, 0.01f, 4.0f);
	HexEngine::HVar r_nrdRelaxAntilagReset("r_nrdRelaxAntilagReset", "RELAX anti-lag reset amount", 0.6f, 0.0f, 1.0f);
	constexpr nrd::Identifier gDenoiserIdentifier = 0;

	class ShaderInclude : public ID3DInclude
	{
	public:
		ShaderInclude()
		{
			const fs::path current = fs::current_path();
			_roots.push_back(current / "ThirdParty/nrd/Shaders/Include");
			_roots.push_back(current / "ThirdParty/nrd/Shaders/Resources");
			_roots.push_back(current / "../ThirdParty/nrd/Shaders/Include");
			_roots.push_back(current / "../ThirdParty/nrd/Shaders/Resources");
			_roots.push_back(current / "../../ThirdParty/nrd/Shaders/Include");
			_roots.push_back(current / "../../ThirdParty/nrd/Shaders/Resources");
			_roots.push_back(current / "../../../ThirdParty/nrd/Shaders/Include");
			_roots.push_back(current / "../../../ThirdParty/nrd/Shaders/Resources");
		}

		HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE, LPCSTR pFileName, LPCVOID, LPCVOID* ppData, UINT* pBytes) override
		{
			for (const auto& root : _roots)
			{
				const fs::path candidate = root / pFileName;
				if (!fs::exists(candidate))
					continue;

				std::ifstream file(candidate, std::ios::binary | std::ios::ate);
				if (!file.is_open())
					continue;

				auto size = static_cast<size_t>(file.tellg());
				file.seekg(0, std::ios::beg);

				char* data = new char[size];
				file.read(data, size);
				*ppData = data;
				*pBytes = static_cast<UINT>(size);
				return S_OK;
			}

			return E_FAIL;
		}

		HRESULT STDMETHODCALLTYPE Close(LPCVOID pData) override
		{
			delete[] reinterpret_cast<const char*>(pData);
			return S_OK;
		}

	private:
		std::vector<fs::path> _roots;
	};

	void* Allocate(void*, size_t size, size_t)
	{
		return malloc(size);
	}

	void* Reallocate(void*, void* memory, size_t size, size_t)
	{
		return realloc(memory, size);
	}

	void Free(void*, void* memory)
	{
		free(memory);
	}

	DXGI_FORMAT GetSrvFormat(DXGI_FORMAT format)
	{
		switch (format)
		{
		case DXGI_FORMAT_R32_TYPELESS:
			return DXGI_FORMAT_R32_FLOAT;
		case DXGI_FORMAT_R16_TYPELESS:
			return DXGI_FORMAT_R16_FLOAT;
		case DXGI_FORMAT_R24G8_TYPELESS:
			return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		case DXGI_FORMAT_R32G8X24_TYPELESS:
			return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
		default:
			return format;
		}
	}

	HRESULT CompileShader(const std::string& source, const char* entryPoint, const char* target, ID3DBlob** bytecode)
	{
		ShaderInclude includes;
		ID3DBlob* errors = nullptr;
		const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
		const HRESULT hr = D3DCompile(
			source.data(),
			source.size(),
			nullptr,
			nullptr,
			&includes,
			entryPoint,
			target,
			flags,
			0,
			bytecode,
			&errors);

		if (FAILED(hr) && errors)
		{
			LOG_CRIT("NRD shader compile failed: %s", reinterpret_cast<const char*>(errors->GetBufferPointer()));
		}

		SAFE_RELEASE(errors);
		return hr;
	}
}

nrd::Denoiser NRDInterface::GetSelectedDenoiser()
{
	if (r_nrdDenoiser._val.i32 == 1)
	{
		static bool loggedReblurFallback = false;
		if (!loggedReblurFallback)
		{
			LOG_WARN("REBLUR_SPECULAR is not supported by the current D3D11 NRD backend because REBLUR uses Gather on R16_UINT history textures; falling back to RELAX_DIFFUSE_SPECULAR");
			loggedReblurFallback = true;
		}

		return nrd::Denoiser::RELAX_DIFFUSE_SPECULAR;
	}

	return nrd::Denoiser::RELAX_DIFFUSE_SPECULAR;
}

bool NRDInterface::IsUsingReblur() const
{
    return _activeDenoiser == nrd::Denoiser::REBLUR_SPECULAR;
}

void NRDInterface::TextureBinding::Reset()
{
	SAFE_RELEASE(rtv);
	SAFE_RELEASE(uav);
	SAFE_RELEASE(srv);
	if (ownsTexture)
	{
		SAFE_RELEASE(texture);
	}
	else
	{
		texture = nullptr;
	}

	format = DXGI_FORMAT_UNKNOWN;
	width = 0;
	height = 0;
	ownsTexture = false;
}

bool NRDInterface::Create()
{
	_device = nullptr;
	_context = nullptr;
	_created = true;
	return true;
}

void NRDInterface::Destroy()
{
	DestroyNrdResources();

	SAFE_RELEASE(_preprocessPS);
	SAFE_RELEASE(_resolvePS);
	SAFE_RELEASE(_fullscreenVS);
	SAFE_RELEASE(_pointClampSampler);
	SAFE_RELEASE(_linearClampSampler);

	_previousJitter = math::Vector2::Zero;
	_created = false;
	_device = nullptr;
	_context = nullptr;
}

bool NRDInterface::EnsureDevice()
{
	if (!HexEngine::g_pEnv || !HexEngine::g_pEnv->_graphicsDevice)
		return false;

	_device = reinterpret_cast<ID3D11Device*>(HexEngine::g_pEnv->_graphicsDevice->GetNativeDevice());
	_context = reinterpret_cast<ID3D11DeviceContext*>(HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext());
	return _device != nullptr && _context != nullptr;
}

void NRDInterface::DestroyNrdResources()
{
	for (auto* pipeline : _pipelines)
	{
		SAFE_RELEASE(pipeline);
	}
	_pipelines.clear();

	for (auto& texture : _permanentPool)
		texture.Reset();
	_permanentPool.clear();

	for (auto& texture : _transientPool)
		texture.Reset();
	_transientPool.clear();

	_diffuseSignalInput.Reset();
	_diffuseHitDistanceInput.Reset();
	_specularSignalInput.Reset();
	_specularHitDistanceInput.Reset();
	_normalAndDepthInput.Reset();
	_materialInput.Reset();
	_motionVectorsInput.Reset();
	_normalRoughness.Reset();
	_viewZ.Reset();
	_diffuseRadianceHitDistance.Reset();
	_specularRadianceHitDistance.Reset();
	_denoisedDiffuseRadianceHitDistance.Reset();
	_denoisedSpecularRadianceHitDistance.Reset();
	_resolvedSignal.Reset();

	SAFE_RELEASE(_constantBuffer);

	if (_instance)
	{
		nrd::DestroyInstance(*_instance);
		_instance = nullptr;
	}

	_instanceDesc = nullptr;
	_resetHistory = true;
}

void NRDInterface::CreateBuffers(int32_t width, int32_t height, HexEngine::ITexture2D* diffuseSignalInput, HexEngine::ITexture2D* diffuseHitDistance, HexEngine::ITexture2D* specularSignalInput, HexEngine::ITexture2D* specularHitDistance, HexEngine::ITexture2D* normalAndDepth, HexEngine::ITexture2D* material, HexEngine::ITexture2D* motionVectors)
{
	if (!_loggedCreateBuffers)
	{
		LOG_DEBUG(
			"NRD CreateBuffers: %dx%d diffSignal=%d diffHit=%d specSignal=%d specHit=%d normalDepth=%d material=%d motion=%d",
			width,
			height,
			diffuseSignalInput ? diffuseSignalInput->GetFormat() : -1,
			diffuseHitDistance ? diffuseHitDistance->GetFormat() : -1,
			specularSignalInput ? specularSignalInput->GetFormat() : -1,
			specularHitDistance ? specularHitDistance->GetFormat() : -1,
			normalAndDepth ? normalAndDepth->GetFormat() : -1,
			material ? material->GetFormat() : -1,
			motionVectors ? motionVectors->GetFormat() : -1);
		_loggedCreateBuffers = true;
	}

	RecreateResources(width, height, diffuseSignalInput, diffuseHitDistance, specularSignalInput, specularHitDistance, normalAndDepth, material, motionVectors);
}

void NRDInterface::BuildFrameData(HexEngine::DenoiserFrameData& fd, HexEngine::ITexture2D* diffuseSignalInput, HexEngine::ITexture2D* diffuseHitDistance, HexEngine::ITexture2D* specularSignalInput, HexEngine::ITexture2D* specularHitDistance, HexEngine::ITexture2D* normalAndDepth, HexEngine::ITexture2D* material, HexEngine::ITexture2D* motionVectors)
{
	fd.diffuseSignalInput = diffuseSignalInput;
	fd.diffuseHitDistance = diffuseHitDistance;
	fd.specularSignalInput = specularSignalInput;
	fd.specularHitDistance = specularHitDistance;
	fd.normalAndDepth = normalAndDepth;
	fd.material = material;
	fd.motionVectors = motionVectors;
}

bool NRDInterface::RecreateResources(int32_t width, int32_t height, HexEngine::ITexture2D* diffuseSignalInput, HexEngine::ITexture2D* diffuseHitDistance, HexEngine::ITexture2D* specularSignalInput, HexEngine::ITexture2D* specularHitDistance, HexEngine::ITexture2D* normalAndDepth, HexEngine::ITexture2D* material, HexEngine::ITexture2D* motionVectors)
{
	if (!EnsureDevice())
		return false;

	const nrd::Denoiser selectedDenoiser = GetSelectedDenoiser();
	const bool denoiserChanged = _activeDenoiser != selectedDenoiser;
	const bool needsRecreate = !_instance || denoiserChanged || _width != static_cast<uint32_t>(width) || _height != static_cast<uint32_t>(height);
	if (needsRecreate)
	{
		DestroyNrdResources();

		_width = static_cast<uint32_t>(width);
		_height = static_cast<uint32_t>(height);
		_activeDenoiser = selectedDenoiser;

		if (!CreateInstance() || !CreatePipelines() || !CreateConstantBuffer() || !CreateSamplers() || !CompileFullscreenShaders() || !CreatePoolTextures() || !CreateAuxiliaryTextures(specularSignalInput))
		{
			DestroyNrdResources();
			return false;
		}
	}

	if (!CreateExternalBinding(_diffuseSignalInput, diffuseSignalInput) ||
		!CreateExternalBinding(_diffuseHitDistanceInput, diffuseHitDistance) ||
		!CreateExternalBinding(_specularSignalInput, specularSignalInput) ||
		!CreateExternalBinding(_specularHitDistanceInput, specularHitDistance) ||
		!CreateExternalBinding(_normalAndDepthInput, normalAndDepth) ||
		!CreateExternalBinding(_materialInput, material) ||
		!CreateExternalBinding(_motionVectorsInput, motionVectors))
	{
		return false;
	}

	return true;
}

bool NRDInterface::CreateInstance()
{
	nrd::DenoiserDesc denoiserDesc = {};
	denoiserDesc.identifier = gDenoiserIdentifier;
	denoiserDesc.denoiser = _activeDenoiser;

	nrd::InstanceCreationDesc createDesc = {};
	createDesc.denoisers = &denoiserDesc;
	createDesc.denoisersNum = 1;
	createDesc.allocationCallbacks.Allocate = Allocate;
	createDesc.allocationCallbacks.Reallocate = Reallocate;
	createDesc.allocationCallbacks.Free = Free;

	if (nrd::CreateInstance(createDesc, _instance) != nrd::Result::SUCCESS)
	{
		LOG_CRIT("Failed to create NRD instance");
		return false;
	}

	_instanceDesc = &nrd::GetInstanceDesc(*_instance);
	return true;
}

bool NRDInterface::CreatePipelines()
{
	if (!_instanceDesc)
		return false;

	_pipelines.resize(_instanceDesc->pipelinesNum, nullptr);
	for (uint32_t i = 0; i < _instanceDesc->pipelinesNum; ++i)
	{
		const auto& pipelineDesc = _instanceDesc->pipelines[i];
		if (!pipelineDesc.computeShaderDXBC.bytecode || pipelineDesc.computeShaderDXBC.size == 0)
		{
			LOG_CRIT("NRD pipeline %d is missing DXBC bytecode", i);
			return false;
		}

		CHECK_HR(_device->CreateComputeShader(
			pipelineDesc.computeShaderDXBC.bytecode,
			static_cast<SIZE_T>(pipelineDesc.computeShaderDXBC.size),
			nullptr,
			&_pipelines[i]));
	}

	return true;
}

bool NRDInterface::CreateSamplers()
{
	if (_pointClampSampler && _linearClampSampler)
		return true;

	D3D11_SAMPLER_DESC desc = {};
	desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	desc.MaxLOD = D3D11_FLOAT32_MAX;
	desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	CHECK_HR(_device->CreateSamplerState(&desc, &_pointClampSampler));

	desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	CHECK_HR(_device->CreateSamplerState(&desc, &_linearClampSampler));
	return true;
}

bool NRDInterface::CreateConstantBuffer()
{
	if (!_instanceDesc || _instanceDesc->constantBufferMaxDataSize == 0)
		return true;

	D3D11_BUFFER_DESC desc = {};
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.ByteWidth = (_instanceDesc->constantBufferMaxDataSize + 15u) & ~15u;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	CHECK_HR(_device->CreateBuffer(&desc, nullptr, &_constantBuffer));
	return true;
}

bool NRDInterface::CompileFullscreenShaders()
{
	if (_fullscreenVS && _preprocessPS && _resolvePS)
		return true;

	const std::string fullscreenVS = R"(
struct VSOut
{
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
};
VSOut ShaderMain(uint vertexId : SV_VertexID)
{
	VSOut output;
	float2 pos = float2((vertexId == 2) ? 3.0f : -1.0f, (vertexId == 1) ? 3.0f : -1.0f);
	output.position = float4(pos, 0.0f, 1.0f);
	output.uv = float2((pos.x + 1.0f) * 0.5f, 1.0f - ((pos.y + 1.0f) * 0.5f));
	return output;
}
)";

	const std::string preprocessPS = R"(
#define NRD_NORMAL_ENCODING 2
#define NRD_ROUGHNESS_ENCODING 1
#include "NRD.hlsli"
Texture2D<float4> gDiffuseSignal : register(t0);
Texture2D<float4> gDiffuseHitDistance : register(t1);
Texture2D<float4> gSpecularSignal : register(t2);
Texture2D<float4> gSpecularHitDistance : register(t3);
Texture2D<float4> gNormalDepth : register(t4);
Texture2D<float4> gMaterial : register(t5);
SamplerState gLinearClamp : register(s0);
struct VSOut
{
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
};
struct PSOut
{
	float4 normalRoughness : SV_Target0;
	float viewZ : SV_Target1;
	float4 diffuseRadianceHitDist : SV_Target2;
	float4 specularRadianceHitDist : SV_Target3;
};
PSOut ShaderMain(VSOut input)
{
	PSOut output;
	const float4 diffuseSignal = gDiffuseSignal.SampleLevel(gLinearClamp, input.uv, 0.0f);
	const float4 diffuseHitDistance = gDiffuseHitDistance.SampleLevel(gLinearClamp, input.uv, 0.0f);
	const float4 specularSignal = gSpecularSignal.SampleLevel(gLinearClamp, input.uv, 0.0f);
	const float4 specularHitDistance = gSpecularHitDistance.SampleLevel(gLinearClamp, input.uv, 0.0f);
	const float4 normalDepth = gNormalDepth.SampleLevel(gLinearClamp, input.uv, 0.0f);
	const float4 material = gMaterial.SampleLevel(gLinearClamp, input.uv, 0.0f);
	const float3 normal = normalize(normalDepth.xyz);
	const float roughness = saturate(material.y);
	const float viewZ = max(normalDepth.w, 1e-4f);
	output.normalRoughness = NRD_FrontEnd_PackNormalAndRoughness(normal, roughness, 0.0f);
	output.viewZ = viewZ;
	output.diffuseRadianceHitDist = RELAX_FrontEnd_PackRadianceAndHitDist(max(diffuseSignal.rgb, 0.0f), max(diffuseHitDistance.w, 0.0f), true);
	output.specularRadianceHitDist = RELAX_FrontEnd_PackRadianceAndHitDist(max(specularSignal.rgb, 0.0f), max(specularHitDistance.w, 0.0f), true);
	return output;
}
)";

    const std::string resolvePS = R"(
Texture2D<float4> gDiffuseSignal : register(t0);
Texture2D<float4> gSpecularSignal : register(t1);
Texture2D<float4> gDenoisedDiffuse : register(t2);
Texture2D<float4> gDenoisedSpecular : register(t3);
SamplerState gLinearClamp : register(s0);
struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
float3 ResolveChannel(float3 original, float3 denoised)
{
    const float denoisedPeak = max(denoised.r, max(denoised.g, denoised.b));
    const float originalPeak = max(original.r, max(original.g, original.b));
    const bool invalidDenoised = any(isnan(denoised)) || any(isinf(denoised));
    const bool collapsedToBlack = denoisedPeak < 1e-5f && originalPeak > 1e-4f;
    return (invalidDenoised || collapsedToBlack) ? original : denoised;
}
float4 ShaderMain(VSOut input) : SV_Target0
{
    const float3 diffuseSignal = gDiffuseSignal.SampleLevel(gLinearClamp, input.uv, 0.0f).rgb;
    const float3 specularSignal = gSpecularSignal.SampleLevel(gLinearClamp, input.uv, 0.0f).rgb;
    const float3 denoisedDiffuse = gDenoisedDiffuse.SampleLevel(gLinearClamp, input.uv, 0.0f).rgb;
    const float3 denoisedSpecular = gDenoisedSpecular.SampleLevel(gLinearClamp, input.uv, 0.0f).rgb;
    const float3 resolvedDiffuse = ResolveChannel(diffuseSignal, denoisedDiffuse);
    const float3 resolvedSpecular = ResolveChannel(specularSignal, denoisedSpecular);
    return float4(resolvedDiffuse + resolvedSpecular, 1.0f);
}
)";

	ID3DBlob* bytecode = nullptr;
	CHECK_HR(CompileShader(fullscreenVS, "ShaderMain", "vs_5_0", &bytecode));
	CHECK_HR(_device->CreateVertexShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &_fullscreenVS));
	SAFE_RELEASE(bytecode);

	CHECK_HR(CompileShader(preprocessPS, "ShaderMain", "ps_5_0", &bytecode));
	CHECK_HR(_device->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &_preprocessPS));
	SAFE_RELEASE(bytecode);

	CHECK_HR(CompileShader(resolvePS, "ShaderMain", "ps_5_0", &bytecode));
	CHECK_HR(_device->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &_resolvePS));
	SAFE_RELEASE(bytecode);
	return true;
}

bool NRDInterface::CreateTexture(TextureBinding& binding, uint32_t width, uint32_t height, DXGI_FORMAT format, UINT bindFlags)
{
	binding.Reset();

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = bindFlags;

	CHECK_HR(_device->CreateTexture2D(&desc, nullptr, &binding.texture));
	binding.ownsTexture = true;
	binding.width = width;
	binding.height = height;
	binding.format = format;

	if (bindFlags & D3D11_BIND_SHADER_RESOURCE)
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
		CHECK_HR(_device->CreateShaderResourceView(binding.texture, &srvDesc, &binding.srv));
	}

	if (bindFlags & D3D11_BIND_UNORDERED_ACCESS)
	{
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		CHECK_HR(_device->CreateUnorderedAccessView(binding.texture, &uavDesc, &binding.uav));
	}

	if (bindFlags & D3D11_BIND_RENDER_TARGET)
	{
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = format;
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;
		CHECK_HR(_device->CreateRenderTargetView(binding.texture, &rtvDesc, &binding.rtv));
	}

	return true;
}

bool NRDInterface::CreateExternalBinding(TextureBinding& binding, HexEngine::ITexture2D* texture)
{
	binding.Reset();
	if (!texture)
		return false;

	binding.texture = reinterpret_cast<ID3D11Texture2D*>(texture->GetNativePtr());
	binding.width = static_cast<uint32_t>(texture->GetWidth());
	binding.height = static_cast<uint32_t>(texture->GetHeight());
	binding.format = static_cast<DXGI_FORMAT>(texture->GetFormat());
	if (!binding.texture)
		return false;

	D3D11_TEXTURE2D_DESC desc = {};
	binding.texture->GetDesc(&desc);

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = GetSrvFormat(desc.Format);
	srvDesc.ViewDimension = desc.SampleDesc.Count > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
	if (srvDesc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D)
	{
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
	}
	CHECK_HR(_device->CreateShaderResourceView(binding.texture, &srvDesc, &binding.srv));
	return true;
}

DXGI_FORMAT NRDInterface::GetDxgiFormat(nrd::Format format)
{
	switch (format)
	{
	case nrd::Format::R8_UNORM: return DXGI_FORMAT_R8_UNORM;
	case nrd::Format::R8_SNORM: return DXGI_FORMAT_R8_SNORM;
	case nrd::Format::R8_UINT: return DXGI_FORMAT_R8_UINT;
	case nrd::Format::R8_SINT: return DXGI_FORMAT_R8_SINT;

	case nrd::Format::RG8_UNORM: return DXGI_FORMAT_R8G8_UNORM;
	case nrd::Format::RG8_SNORM: return DXGI_FORMAT_R8G8_SNORM;
	case nrd::Format::RG8_UINT: return DXGI_FORMAT_R8G8_UINT;
	case nrd::Format::RG8_SINT: return DXGI_FORMAT_R8G8_SINT;

	case nrd::Format::RGBA8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
	case nrd::Format::RGBA8_SNORM: return DXGI_FORMAT_R8G8B8A8_SNORM;
	case nrd::Format::RGBA8_UINT: return DXGI_FORMAT_R8G8B8A8_UINT;
	case nrd::Format::RGBA8_SINT: return DXGI_FORMAT_R8G8B8A8_SINT;
	case nrd::Format::RGBA8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

	case nrd::Format::R16_UNORM: return DXGI_FORMAT_R16_UNORM;
	case nrd::Format::R16_SNORM: return DXGI_FORMAT_R16_SNORM;
	case nrd::Format::R16_UINT: return DXGI_FORMAT_R16_UINT;
	case nrd::Format::R16_SINT: return DXGI_FORMAT_R16_SINT;
	case nrd::Format::R16_SFLOAT: return DXGI_FORMAT_R16_FLOAT;

	case nrd::Format::RG16_UNORM: return DXGI_FORMAT_R16G16_UNORM;
	case nrd::Format::RG16_SNORM: return DXGI_FORMAT_R16G16_SNORM;
	case nrd::Format::RG16_UINT: return DXGI_FORMAT_R16G16_UINT;
	case nrd::Format::RG16_SINT: return DXGI_FORMAT_R16G16_SINT;
	case nrd::Format::RG16_SFLOAT: return DXGI_FORMAT_R16G16_FLOAT;

	case nrd::Format::RGBA16_UNORM: return DXGI_FORMAT_R16G16B16A16_UNORM;
	case nrd::Format::RGBA16_SNORM: return DXGI_FORMAT_R16G16B16A16_SNORM;
	case nrd::Format::RGBA16_UINT: return DXGI_FORMAT_R16G16B16A16_UINT;
	case nrd::Format::RGBA16_SINT: return DXGI_FORMAT_R16G16B16A16_SINT;
	case nrd::Format::RGBA16_SFLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;

	case nrd::Format::R32_UINT: return DXGI_FORMAT_R32_UINT;
	case nrd::Format::R32_SINT: return DXGI_FORMAT_R32_SINT;
	case nrd::Format::R32_SFLOAT: return DXGI_FORMAT_R32_FLOAT;

	case nrd::Format::RG32_UINT: return DXGI_FORMAT_R32G32_UINT;
	case nrd::Format::RG32_SINT: return DXGI_FORMAT_R32G32_SINT;
	case nrd::Format::RG32_SFLOAT: return DXGI_FORMAT_R32G32_FLOAT;

	case nrd::Format::RGB32_UINT: return DXGI_FORMAT_R32G32B32_UINT;
	case nrd::Format::RGB32_SINT: return DXGI_FORMAT_R32G32B32_SINT;
	case nrd::Format::RGB32_SFLOAT: return DXGI_FORMAT_R32G32B32_FLOAT;

	case nrd::Format::RGBA32_UINT: return DXGI_FORMAT_R32G32B32A32_UINT;
	case nrd::Format::RGBA32_SINT: return DXGI_FORMAT_R32G32B32A32_SINT;
	case nrd::Format::RGBA32_SFLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;

	case nrd::Format::R10_G10_B10_A2_UNORM: return DXGI_FORMAT_R10G10B10A2_UNORM;
	case nrd::Format::R10_G10_B10_A2_UINT: return DXGI_FORMAT_R10G10B10A2_UINT;
	case nrd::Format::R11_G11_B10_UFLOAT: return DXGI_FORMAT_R11G11B10_FLOAT;
	case nrd::Format::R9_G9_B9_E5_UFLOAT: return DXGI_FORMAT_R9G9B9E5_SHAREDEXP;

	default: return DXGI_FORMAT_UNKNOWN;
	}
}

bool NRDInterface::CreatePoolTextures()
{
	if (!_instanceDesc)
		return false;

	_permanentPool.resize(_instanceDesc->permanentPoolSize);
	for (uint32_t i = 0; i < _instanceDesc->permanentPoolSize; ++i)
	{
		const auto& textureDesc = _instanceDesc->permanentPool[i];
		const auto format = GetDxgiFormat(textureDesc.format);
		if (format == DXGI_FORMAT_UNKNOWN)
		{
			LOG_CRIT("Unsupported NRD permanent pool format");
			return false;
		}

		const uint32_t texWidth = std::max<uint32_t>(1, _width / textureDesc.downsampleFactor);
		const uint32_t texHeight = std::max<uint32_t>(1, _height / textureDesc.downsampleFactor);
		if (!CreateTexture(_permanentPool[i], texWidth, texHeight, format, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS))
			return false;
	}

	_transientPool.resize(_instanceDesc->transientPoolSize);
	for (uint32_t i = 0; i < _instanceDesc->transientPoolSize; ++i)
	{
		const auto& textureDesc = _instanceDesc->transientPool[i];
		const auto format = GetDxgiFormat(textureDesc.format);
		if (format == DXGI_FORMAT_UNKNOWN)
		{
			LOG_CRIT("Unsupported NRD transient pool format");
			return false;
		}

		const uint32_t texWidth = std::max<uint32_t>(1, _width / textureDesc.downsampleFactor);
		const uint32_t texHeight = std::max<uint32_t>(1, _height / textureDesc.downsampleFactor);
		if (!CreateTexture(_transientPool[i], texWidth, texHeight, format, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS))
			return false;
	}

	return true;
}

bool NRDInterface::CreateAuxiliaryTextures(HexEngine::ITexture2D* specularSignalInput)
{
	const auto signalFormat = static_cast<DXGI_FORMAT>(specularSignalInput->GetFormat());
	return CreateTexture(_normalRoughness, _width, _height, DXGI_FORMAT_R10G10B10A2_UNORM, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET) &&
		CreateTexture(_viewZ, _width, _height, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET) &&
		CreateTexture(_diffuseRadianceHitDistance, _width, _height, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET) &&
		CreateTexture(_specularRadianceHitDistance, _width, _height, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET) &&
		CreateTexture(_denoisedDiffuseRadianceHitDistance, _width, _height, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS) &&
		CreateTexture(_denoisedSpecularRadianceHitDistance, _width, _height, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS) &&
		CreateTexture(_resolvedSignal, _width, _height, signalFormat, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
}

NRDInterface::TextureBinding* NRDInterface::ResolveResource(const nrd::ResourceDesc& resource)
{
	switch (resource.type)
	{
	case nrd::ResourceType::IN_MV:
		return &_motionVectorsInput;
	case nrd::ResourceType::IN_NORMAL_ROUGHNESS:
		return &_normalRoughness;
	case nrd::ResourceType::IN_VIEWZ:
		return &_viewZ;
	case nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST:
		return &_diffuseRadianceHitDistance;
	case nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST:
		return &_specularRadianceHitDistance;
	case nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST:
		return &_denoisedDiffuseRadianceHitDistance;
	case nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST:
		return &_denoisedSpecularRadianceHitDistance;
	case nrd::ResourceType::TRANSIENT_POOL:
		return resource.indexInPool < _transientPool.size() ? &_transientPool[resource.indexInPool] : nullptr;
	case nrd::ResourceType::PERMANENT_POOL:
		return resource.indexInPool < _permanentPool.size() ? &_permanentPool[resource.indexInPool] : nullptr;
	default:
		return nullptr;
	}
}

bool NRDInterface::RunPreprocess(const HexEngine::DenoiserFrameData&)
{
	ID3D11RenderTargetView* oldRtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
	ID3D11DepthStencilView* oldDsv = nullptr;
	_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRtvs, &oldDsv);

	UINT numViewports = 1;
	D3D11_VIEWPORT oldViewport = {};
	_context->RSGetViewports(&numViewports, &oldViewport);

	ID3D11RenderTargetView* rtvs[] = { _normalRoughness.rtv, _viewZ.rtv, _diffuseRadianceHitDistance.rtv, _specularRadianceHitDistance.rtv };
	_context->OMSetRenderTargets(4, rtvs, nullptr);

	const D3D11_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(_width), static_cast<float>(_height), 0.0f, 1.0f };
	_context->RSSetViewports(1, &viewport);
	_context->IASetInputLayout(nullptr);
	_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	_context->VSSetShader(_fullscreenVS, nullptr, 0);
	_context->PSSetShader(_preprocessPS, nullptr, 0);

	ID3D11ShaderResourceView* srvs[] = { _diffuseSignalInput.srv, _diffuseHitDistanceInput.srv, _specularSignalInput.srv, _specularHitDistanceInput.srv, _normalAndDepthInput.srv, _materialInput.srv };
	_context->PSSetShaderResources(0, 6, srvs);
	ID3D11SamplerState* samplers[] = { _linearClampSampler };
	_context->PSSetSamplers(0, 1, samplers);
	_context->Draw(3, 0);

	ID3D11ShaderResourceView* nullSrvs[] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
	_context->PSSetShaderResources(0, 6, nullSrvs);
	_context->PSSetShader(nullptr, nullptr, 0);
	_context->VSSetShader(nullptr, nullptr, 0);
	_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRtvs, oldDsv);
	_context->RSSetViewports(numViewports, &oldViewport);

	for (auto* rtv : oldRtvs)
		SAFE_RELEASE(rtv);
	SAFE_RELEASE(oldDsv);
	return true;
}

bool NRDInterface::RunDenoiser(const HexEngine::DenoiserFrameData& fd)
{
	if (!fd.camera)
		return false;

	nrd::CommonSettings settings = {};
	memcpy(settings.viewToClipMatrix, fd.camera->GetProjectionMatrix().m, sizeof(settings.viewToClipMatrix));
	memcpy(settings.viewToClipMatrixPrev, fd.camera->GetProjectionMatrixPrev().m, sizeof(settings.viewToClipMatrixPrev));
	memcpy(settings.worldToViewMatrix, fd.camera->GetViewMatrix().m, sizeof(settings.worldToViewMatrix));
	memcpy(settings.worldToViewMatrixPrev, fd.camera->GetViewMatrixPrev().m, sizeof(settings.worldToViewMatrixPrev));
	settings.motionVectorScale[0] = 1.0f;
	settings.motionVectorScale[1] = 1.0f;
	settings.motionVectorScale[2] = 0.0f;
	settings.cameraJitter[0] = -fd.jitter.x;
	settings.cameraJitter[1] = -fd.jitter.y;
	settings.cameraJitterPrev[0] = -_previousJitter.x;
	settings.cameraJitterPrev[1] = -_previousJitter.y;
	settings.resourceSize[0] = static_cast<uint16_t>(_width);
	settings.resourceSize[1] = static_cast<uint16_t>(_height);
	settings.resourceSizePrev[0] = static_cast<uint16_t>(_width);
	settings.resourceSizePrev[1] = static_cast<uint16_t>(_height);
	settings.rectSize[0] = static_cast<uint16_t>(_width);
	settings.rectSize[1] = static_cast<uint16_t>(_height);
	settings.rectSizePrev[0] = static_cast<uint16_t>(_width);
	settings.rectSizePrev[1] = static_cast<uint16_t>(_height);
	settings.frameIndex = static_cast<uint32_t>(HexEngine::g_pEnv->_timeManager->_frameCount);
	settings.accumulationMode = _resetHistory ? nrd::AccumulationMode::CLEAR_AND_RESTART : nrd::AccumulationMode::CONTINUE;
	settings.isMotionVectorInWorldSpace = false;
	settings.denoisingRange = 100000.0f;

	if (nrd::SetCommonSettings(*_instance, settings) != nrd::Result::SUCCESS)
	{
		LOG_CRIT("Failed to apply NRD common settings");
		return false;
	}

	if (IsUsingReblur())
	{
		nrd::ReblurSettings reblurSettings = {};
		reblurSettings.maxAccumulatedFrameNum = 10;
		reblurSettings.maxFastAccumulatedFrameNum = 2;
		reblurSettings.maxStabilizedFrameNum = 10;
		reblurSettings.maxStabilizedFrameNumForHitDistance = 10;
		reblurSettings.historyFixFrameNum = 2;
		reblurSettings.specularPrepassBlurRadius = 35.0f;
		reblurSettings.hitDistanceReconstructionMode = nrd::HitDistanceReconstructionMode::OFF;
		reblurSettings.antilagSettings.luminanceSigmaScale = 2.5f;
		reblurSettings.antilagSettings.hitDistanceSigmaScale = 2.0f;
		reblurSettings.antilagSettings.luminanceSensitivity = 2.0f;
		reblurSettings.antilagSettings.hitDistanceSensitivity = 1.5f;

		if (nrd::SetDenoiserSettings(*_instance, gDenoiserIdentifier, &reblurSettings) != nrd::Result::SUCCESS)
		{
			LOG_CRIT("Failed to apply NRD REBLUR settings");
			return false;
		}
	}
	else
	{
		nrd::RelaxSettings relaxSettings = {};
		uint32_t diffMaxFrames = static_cast<uint32_t>(r_nrdRelaxDiffMaxFrames._val.i32);
		uint32_t specMaxFrames = static_cast<uint32_t>(r_nrdRelaxSpecMaxFrames._val.i32);
		uint32_t diffFastFrames = static_cast<uint32_t>(r_nrdRelaxDiffFastFrames._val.i32);
		uint32_t specFastFrames = static_cast<uint32_t>(r_nrdRelaxSpecFastFrames._val.i32);
		uint32_t historyFixFrames = static_cast<uint32_t>(r_nrdRelaxHistoryFixFrames._val.i32);
		float diffPrepassBlur = r_nrdRelaxDiffPrepassBlur._val.f32;
		float specPrepassBlur = r_nrdRelaxSpecPrepassBlur._val.f32;
		float historyClampSigma = r_nrdRelaxHistoryClampSigma._val.f32;
		float lobeAngleFraction = r_nrdRelaxLobeAngleFraction._val.f32;
		float roughnessFraction = r_nrdRelaxRoughnessFraction._val.f32;
		float diffPhiLuminance = r_nrdRelaxDiffPhiLuminance._val.f32;
		float specPhiLuminance = r_nrdRelaxSpecPhiLuminance._val.f32;
		float specVarianceBoost = r_nrdRelaxSpecVarianceBoost._val.f32;
		bool enableAntiFirefly = r_nrdRelaxAntiFirefly._val.b;
		bool enableRoughnessEdgeStopping = r_nrdRelaxRoughnessEdgeStopping._val.b;
		auto hitDistanceMode = static_cast<nrd::HitDistanceReconstructionMode>(r_nrdRelaxHitDistanceReconstruction._val.i32);
		bool enableAntilag = r_nrdRelaxAntilag._val.b;
		float antilagAcceleration = r_nrdRelaxAntilagAcceleration._val.f32;
		float antilagSpatialSigma = r_nrdRelaxAntilagSpatialSigma._val.f32;
		float antilagTemporalSigma = r_nrdRelaxAntilagTemporalSigma._val.f32;
		float antilagReset = r_nrdRelaxAntilagReset._val.f32;

		switch (r_nrdRelaxPreset._val.i32)
		{
		case 1: // stable
			diffMaxFrames = 8;
			specMaxFrames = 10;
			diffFastFrames = 2;
			specFastFrames = 3;
			historyFixFrames = 2;
			diffPrepassBlur = 14.0f;
			specPrepassBlur = 32.0f;
			historyClampSigma = 1.4f;
			lobeAngleFraction = 0.45f;
			roughnessFraction = 0.15f;
			diffPhiLuminance = 2.4f;
			specPhiLuminance = 1.2f;
			specVarianceBoost = 0.0f;
			enableAntiFirefly = true;
			enableRoughnessEdgeStopping = true;
			hitDistanceMode = nrd::HitDistanceReconstructionMode::OFF;
			enableAntilag = true;
			antilagAcceleration = 0.0f;
			antilagSpatialSigma = 4.5f;
			antilagTemporalSigma = 0.5f;
			antilagReset = 0.0f;
			break;
		case 2: // balanced
			diffMaxFrames = 4;
			specMaxFrames = 6;
			diffFastFrames = 1;
			specFastFrames = 2;
			historyFixFrames = 2;
			diffPrepassBlur = 10.0f;
			specPrepassBlur = 24.0f;
			historyClampSigma = 1.0f;
			lobeAngleFraction = 0.35f;
			roughnessFraction = 0.12f;
			diffPhiLuminance = 2.0f;
			specPhiLuminance = 1.0f;
			specVarianceBoost = 0.0f;
			enableAntiFirefly = true;
			enableRoughnessEdgeStopping = true;
			hitDistanceMode = nrd::HitDistanceReconstructionMode::OFF;
			enableAntilag = true;
			antilagAcceleration = 0.0f;
			antilagSpatialSigma = 4.5f;
			antilagTemporalSigma = 0.5f;
			antilagReset = 0.0f;
			break;
		case 3: // responsive
			diffMaxFrames = 3;
			specMaxFrames = 4;
			diffFastFrames = 1;
			specFastFrames = 1;
			historyFixFrames = 1;
			diffPrepassBlur = 6.0f;
			specPrepassBlur = 18.0f;
			historyClampSigma = 0.8f;
			lobeAngleFraction = 0.25f;
			roughnessFraction = 0.10f;
			diffPhiLuminance = 1.6f;
			specPhiLuminance = 0.85f;
			specVarianceBoost = 0.25f;
			enableAntiFirefly = true;
			enableRoughnessEdgeStopping = true;
			hitDistanceMode = nrd::HitDistanceReconstructionMode::OFF;
			enableAntilag = true;
			antilagAcceleration = 0.25f;
			antilagSpatialSigma = 3.0f;
			antilagTemporalSigma = 0.25f;
			antilagReset = 0.6f;
			break;
		default:
			break;
		}

		diffMaxFrames = std::min<uint32_t>(diffMaxFrames, nrd::RELAX_MAX_HISTORY_FRAME_NUM);
		specMaxFrames = std::min<uint32_t>(specMaxFrames, nrd::RELAX_MAX_HISTORY_FRAME_NUM);
		diffFastFrames = diffMaxFrames > 0 ? std::min<uint32_t>(diffFastFrames, diffMaxFrames - 1) : 0;
		specFastFrames = specMaxFrames > 0 ? std::min<uint32_t>(specFastFrames, specMaxFrames - 1) : 0;
		historyFixFrames = std::min<uint32_t>(historyFixFrames, 3u);

		relaxSettings.diffuseMaxAccumulatedFrameNum = diffMaxFrames;
		relaxSettings.specularMaxAccumulatedFrameNum = specMaxFrames;
		relaxSettings.diffuseMaxFastAccumulatedFrameNum = diffFastFrames;
		relaxSettings.specularMaxFastAccumulatedFrameNum = specFastFrames;
		relaxSettings.historyFixFrameNum = historyFixFrames;
		relaxSettings.diffusePrepassBlurRadius = diffPrepassBlur;
		relaxSettings.specularPrepassBlurRadius = specPrepassBlur;
		relaxSettings.diffusePhiLuminance = diffPhiLuminance;
		relaxSettings.specularPhiLuminance = specPhiLuminance;
		relaxSettings.lobeAngleFraction = lobeAngleFraction;
		relaxSettings.roughnessFraction = roughnessFraction;
		relaxSettings.specularVarianceBoost = specVarianceBoost;
		relaxSettings.specularLobeAngleSlack = 0.15f;
		relaxSettings.historyClampingColorBoxSigmaScale = historyClampSigma;
		relaxSettings.enableAntiFirefly = enableAntiFirefly;
		relaxSettings.enableRoughnessEdgeStopping = enableRoughnessEdgeStopping;
		relaxSettings.enableMaterialTestForSpecular = true;
		relaxSettings.hitDistanceReconstructionMode = hitDistanceMode;

		if (enableAntilag)
		{
			relaxSettings.antilagSettings.accelerationAmount = antilagAcceleration;
			relaxSettings.antilagSettings.spatialSigmaScale = antilagSpatialSigma;
			relaxSettings.antilagSettings.temporalSigmaScale = antilagTemporalSigma;
			relaxSettings.antilagSettings.resetAmount = antilagReset;
		}
		else
		{
			relaxSettings.antilagSettings.accelerationAmount = 0.0f;
			relaxSettings.antilagSettings.spatialSigmaScale = 4.5f;
			relaxSettings.antilagSettings.temporalSigmaScale = 0.5f;
			relaxSettings.antilagSettings.resetAmount = 0.0f;
		}

		if (nrd::SetDenoiserSettings(*_instance, gDenoiserIdentifier, &relaxSettings) != nrd::Result::SUCCESS)
		{
			LOG_CRIT("Failed to apply NRD RELAX settings");
			return false;
		}
	}


	const nrd::DispatchDesc* dispatches = nullptr;
	uint32_t dispatchCount = 0;
	if (nrd::GetComputeDispatches(*_instance, &gDenoiserIdentifier, 1, dispatches, dispatchCount) != nrd::Result::SUCCESS)
	{
		LOG_CRIT("Failed to get NRD compute dispatches");
		return false;
	}

	ID3D11SamplerState* samplers[] = { _pointClampSampler, _linearClampSampler };
	if (_instanceDesc->samplersNum > 0)
	{
		_context->CSSetSamplers(_instanceDesc->samplersBaseRegisterIndex, std::min<uint32_t>(_instanceDesc->samplersNum, 2), samplers);
	}

	for (uint32_t i = 0; i < dispatchCount; ++i)
	{
		const auto& dispatchDesc = dispatches[i];
		const auto& pipelineDesc = _instanceDesc->pipelines[dispatchDesc.pipelineIndex];
		_context->CSSetShader(_pipelines[dispatchDesc.pipelineIndex], nullptr, 0);

		if (_constantBuffer && dispatchDesc.constantBufferDataSize > 0)
		{
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			CHECK_HR(_context->Map(_constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
			memcpy(mapped.pData, dispatchDesc.constantBufferData, dispatchDesc.constantBufferDataSize);
			_context->Unmap(_constantBuffer, 0);
			_context->CSSetConstantBuffers(_instanceDesc->constantBufferRegisterIndex, 1, &_constantBuffer);
		}

		uint32_t resourceOffset = 0;
		for (uint32_t rangeIndex = 0; rangeIndex < pipelineDesc.resourceRangesNum; ++rangeIndex)
		{
			const auto& range = pipelineDesc.resourceRanges[rangeIndex];
			if (range.descriptorType == nrd::DescriptorType::TEXTURE)
			{
				std::vector<ID3D11ShaderResourceView*> srvs(range.descriptorsNum, nullptr);
				for (uint32_t j = 0; j < range.descriptorsNum; ++j)
				{
					if (auto* resource = ResolveResource(dispatchDesc.resources[resourceOffset + j]); resource != nullptr)
						srvs[j] = resource->srv;
				}
				_context->CSSetShaderResources(range.baseRegisterIndex, range.descriptorsNum, srvs.data());
			}
			else
			{
				std::vector<ID3D11UnorderedAccessView*> uavs(range.descriptorsNum, nullptr);
				for (uint32_t j = 0; j < range.descriptorsNum; ++j)
				{
					if (auto* resource = ResolveResource(dispatchDesc.resources[resourceOffset + j]); resource != nullptr)
						uavs[j] = resource->uav;
				}
				_context->CSSetUnorderedAccessViews(range.baseRegisterIndex, range.descriptorsNum, uavs.data(), nullptr);
			}

			resourceOffset += range.descriptorsNum;
		}

		_context->Dispatch(dispatchDesc.gridWidth, dispatchDesc.gridHeight, 1);
		UnbindComputeResources(dispatchDesc, pipelineDesc);
	}

	ID3D11SamplerState* nullSamplers[] = { nullptr, nullptr };
	if (_instanceDesc->samplersNum > 0)
		_context->CSSetSamplers(_instanceDesc->samplersBaseRegisterIndex, std::min<uint32_t>(_instanceDesc->samplersNum, 2), nullSamplers);

	_context->CSSetShader(nullptr, nullptr, 0);
	_previousJitter = fd.jitter;
	_resetHistory = false;
	return true;
}

void NRDInterface::UnbindComputeResources(const nrd::DispatchDesc&, const nrd::PipelineDesc& pipelineDesc)
{
	for (uint32_t rangeIndex = 0; rangeIndex < pipelineDesc.resourceRangesNum; ++rangeIndex)
	{
		const auto& range = pipelineDesc.resourceRanges[rangeIndex];
		if (range.descriptorType == nrd::DescriptorType::TEXTURE)
		{
			std::vector<ID3D11ShaderResourceView*> srvs(range.descriptorsNum, nullptr);
			_context->CSSetShaderResources(range.baseRegisterIndex, range.descriptorsNum, srvs.data());
		}
		else
		{
			std::vector<ID3D11UnorderedAccessView*> uavs(range.descriptorsNum, nullptr);
			_context->CSSetUnorderedAccessViews(range.baseRegisterIndex, range.descriptorsNum, uavs.data(), nullptr);
		}
	}
}

bool NRDInterface::RunResolve(const HexEngine::DenoiserFrameData& fd, HexEngine::ITexture2D* output)
{
	if (!fd.diffuseSignalInput || !fd.specularSignalInput || !output)
		return false;

	ID3D11RenderTargetView* oldRtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
	ID3D11DepthStencilView* oldDsv = nullptr;
	_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRtvs, &oldDsv);

	UINT numViewports = 1;
	D3D11_VIEWPORT oldViewport = {};
	_context->RSGetViewports(&numViewports, &oldViewport);

	ID3D11RenderTargetView* rtv = _resolvedSignal.rtv;
	_context->OMSetRenderTargets(1, &rtv, nullptr);

	const D3D11_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(_width), static_cast<float>(_height), 0.0f, 1.0f };
	_context->RSSetViewports(1, &viewport);
	_context->IASetInputLayout(nullptr);
	_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	_context->VSSetShader(_fullscreenVS, nullptr, 0);
	_context->PSSetShader(_resolvePS, nullptr, 0);

	ID3D11ShaderResourceView* srvs[] = { _diffuseSignalInput.srv, _specularSignalInput.srv, _denoisedDiffuseRadianceHitDistance.srv, _denoisedSpecularRadianceHitDistance.srv };
	_context->PSSetShaderResources(0, 4, srvs);
	ID3D11SamplerState* samplers[] = { _linearClampSampler };
	_context->PSSetSamplers(0, 1, samplers);
	_context->Draw(3, 0);

	ID3D11ShaderResourceView* nullSrvs[] = { nullptr, nullptr, nullptr, nullptr };
	_context->PSSetShaderResources(0, 4, nullSrvs);
	_context->PSSetShader(nullptr, nullptr, 0);
	_context->VSSetShader(nullptr, nullptr, 0);
	_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRtvs, oldDsv);
	_context->RSSetViewports(numViewports, &oldViewport);

	for (auto* currentRtv : oldRtvs)
		SAFE_RELEASE(currentRtv);
	SAFE_RELEASE(oldDsv);

	_context->CopyResource(reinterpret_cast<ID3D11Texture2D*>(output->GetNativePtr()), _resolvedSignal.texture);
	return true;
}

void NRDInterface::FilterFrame(const HexEngine::DenoiserFrameData& fd, HexEngine::ITexture2D* output)
{
	if (!fd.diffuseSignalInput || !fd.diffuseHitDistance || !fd.specularSignalInput || !fd.specularHitDistance || !fd.normalAndDepth || !fd.material || !fd.motionVectors || !fd.camera || !output)
		return;

	if (!RecreateResources(
		static_cast<int32_t>(fd.specularSignalInput->GetWidth()),
		static_cast<int32_t>(fd.specularSignalInput->GetHeight()),
		fd.diffuseSignalInput,
		fd.diffuseHitDistance,
		fd.specularSignalInput,
		fd.specularHitDistance,
		fd.normalAndDepth,
		fd.material,
		fd.motionVectors))
	{
		return;
	}

	if (!RunPreprocess(fd))
		return;

	if (!RunDenoiser(fd))
		return;

	RunResolve(fd, output);

	HexEngine::g_pEnv->GetGraphicsDevice().ResetState();
}









