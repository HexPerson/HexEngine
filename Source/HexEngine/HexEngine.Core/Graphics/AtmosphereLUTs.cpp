#include "AtmosphereLUTs.hpp"
#include "../HexEngine.hpp"
#include "../Input/HVar.hpp"
#include "IGraphicsDevice.hpp"
#include "IConstantBuffer.hpp"
#include "IShader.hpp"
#include "ITexture2D.hpp"
#include "ITexture3D.hpp"
#include <d3d11.h>

namespace HexEngine
{
	// Master switch for the LUT pipeline. When off we skip every dispatch
	// in Update() and downstream samplers see the previous frame's LUTs
	// (or the cleared-black initial contents if Update() never ran). Lets
	// us A/B perf vs. the analytic path without a recompile.
	HVar r_atmosphereLUTs("r_atmosphereLUTs", "Enable Hillaire 2020 atmosphere LUT precomputes (sky/aerial-perspective/scattering)", true, false, true);
	// Per-frame sky-view cbuffer. Layout matches the matching cbuffer in
	// AtmosphereSkyViewLUT.shader at register(b6). Single Vector4 worth of
	// camera state + Vector4 worth of sun state.
	struct SkyViewParamsCB
	{
		float cameraHeightMM;
		float pad0;
		float pad1;
		float pad2;
		math::Vector3 sunDirection;
		float sunIntensity;
	};

	// Post-LUT sky tint cbuffer. Consumed by SkySphere.shader at b6 after
	// it samples the SkyView LUT. The Hillaire LUT can only produce
	// clear-sky/sunset looks; this tint fakes overcast/storm/rain.
	struct SkyRenderParamsCB
	{
		math::Vector4 overcastColor;   // rgb tint, a unused
		float overcastAmount;          // 0 = pure LUT, 1 = pure tint
		float pad0;
		float pad1;
		float pad2;
	};

	AtmosphereLUTs::AtmosphereLUTs() = default;

	AtmosphereLUTs::~AtmosphereLUTs()
	{
		Destroy();
	}

	bool AtmosphereLUTs::Create()
	{
		auto* graphics = g_pEnv != nullptr ? g_pEnv->_graphicsDevice : nullptr;
		if (graphics == nullptr)
		{
			LOG_WARN("AtmosphereLUTs::Create called before graphics device is ready - LUT subsystem disabled");
			return false;
		}

		_atmosphereCBuffer = graphics->CreateConstantBuffer(sizeof(Params));
		if (_atmosphereCBuffer == nullptr)
		{
			LOG_WARN("AtmosphereLUTs::Create failed to allocate the atmosphere cbuffer");
			return false;
		}

		_skyViewCBuffer = graphics->CreateConstantBuffer(sizeof(SkyViewParamsCB));
		if (_skyViewCBuffer == nullptr)
		{
			LOG_WARN("AtmosphereLUTs::Create failed to allocate the sky-view cbuffer");
			return false;
		}

		_skyRenderCBuffer = graphics->CreateConstantBuffer(sizeof(SkyRenderParamsCB));
		if (_skyRenderCBuffer == nullptr)
		{
			LOG_WARN("AtmosphereLUTs::Create failed to allocate the sky-render cbuffer");
			return false;
		}
		// Seed with "no overcast" so the sky reads as clear sky if the
		// SetSkyRenderParams setter is never called.
		const SkyRenderParamsCB defaultSkyRender = { math::Vector4(1.0f, 1.0f, 1.0f, 1.0f), 0.0f, 0.0f, 0.0f, 0.0f };
		_skyRenderCBuffer->Write((void*)&defaultSkyRender, sizeof(defaultSkyRender));

		_paramsDirty = true;
		_needsTransmittanceRebuild = true;

		// Textures, UAVs, and shaders deferred to EnsureResources() on
		// first Update() so that the shader pipeline has had a chance to
		// finish warming up by then.
		return true;
	}

	void AtmosphereLUTs::Destroy()
	{
		ReleaseResources();

		_transmittanceShader.reset();
		_multiScatteringShader.reset();
		_skyViewShader.reset();
		_aerialPerspectiveShader.reset();

		// _aerialPerspectiveVolume already released in ReleaseResources().
		SAFE_DELETE(_atmosphereCBuffer);
		SAFE_DELETE(_skyViewCBuffer);
		SAFE_DELETE(_skyRenderCBuffer);
	}

	void AtmosphereLUTs::SetSkyRenderParams(const math::Vector3& overcastColor, float overcastAmount)
	{
		if (_skyRenderCBuffer == nullptr)
			return;
		SkyRenderParamsCB cb{};
		cb.overcastColor = math::Vector4(overcastColor.x, overcastColor.y, overcastColor.z, 1.0f);
		cb.overcastAmount = std::clamp(overcastAmount, 0.0f, 1.0f);
		_skyRenderCBuffer->Write(&cb, sizeof(cb));
	}

	void AtmosphereLUTs::ReleaseResources()
	{
		if (_transmittanceUav)      { _transmittanceUav->Release();      _transmittanceUav      = nullptr; }
		if (_multiScatteringUav)    { _multiScatteringUav->Release();    _multiScatteringUav    = nullptr; }
		if (_skyViewUav)            { _skyViewUav->Release();            _skyViewUav            = nullptr; }
		if (_aerialPerspectiveUav)  { _aerialPerspectiveUav->Release();  _aerialPerspectiveUav  = nullptr; }
		if (_linearClampSampler)    { _linearClampSampler->Release();    _linearClampSampler    = nullptr; }
		SAFE_DELETE(_transmittanceLUT);
		SAFE_DELETE(_multiScatteringLUT);
		SAFE_DELETE(_skyViewLUT);
		SAFE_DELETE(_aerialPerspectiveVolume);
		_resourcesReady = false;
	}

	void AtmosphereLUTs::SetParams(const Params& params)
	{
		_params = params;
		_paramsDirty = true;
		// Param change invalidates the transmittance LUT (atmosphere
		// shape moved); MS and SkyView already recompute every frame.
		_needsTransmittanceRebuild = true;
	}

	namespace
	{
		// Helper: create an RGBA16F render-target+UAV-capable texture
		// for use as an atmosphere LUT. Same dims/format every time
		// makes Phase B alloc paths uniform.
		ITexture2D* CreateLutTexture2D(IGraphicsDevice* graphics, int32_t w, int32_t h, const char* debugName)
		{
			ITexture2D* tex = graphics->CreateTexture2D(
				w, h,
				DXGI_FORMAT_R16G16B16A16_FLOAT,
				1,
				D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
				1, 1, 0,
				nullptr,
				(D3D11_CPU_ACCESS_FLAG)0,
				D3D11_RTV_DIMENSION_UNKNOWN,
				D3D11_UAV_DIMENSION_TEXTURE2D,
				D3D11_SRV_DIMENSION_TEXTURE2D,
				D3D11_DSV_DIMENSION_UNKNOWN,
				D3D11_USAGE_DEFAULT,
				0);
			if (tex && debugName)
				tex->SetDebugName(debugName);
			return tex;
		}

		ID3D11UnorderedAccessView* CreateLutUav(ID3D11Device* device, ITexture2D* tex)
		{
			if (device == nullptr || tex == nullptr)
				return nullptr;
			auto* d3dTex = reinterpret_cast<ID3D11Resource*>(tex->GetNativePtr());
			if (d3dTex == nullptr)
				return nullptr;
			D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
			desc.Format        = DXGI_FORMAT_R16G16B16A16_FLOAT;
			desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			desc.Texture2D.MipSlice = 0;
			ID3D11UnorderedAccessView* uav = nullptr;
			HRESULT hr = device->CreateUnorderedAccessView(d3dTex, &desc, &uav);
			if (FAILED(hr))
				return nullptr;
			return uav;
		}
	}

	bool AtmosphereLUTs::EnsureResources()
	{
		if (_resourcesReady)
			return true;

		auto* graphics = g_pEnv != nullptr ? g_pEnv->_graphicsDevice : nullptr;
		if (graphics == nullptr)
			return false;
		// D3D11-only compute path (see Update) - no-op under D3D12.
		if (graphics->GetBackend() != GraphicsBackend::D3D11)
			return false;
		auto* device = reinterpret_cast<ID3D11Device*>(graphics->GetNativeDevice());
		if (device == nullptr)
			return false;

		// Textures. Failure on any one disables the whole subsystem - the
		// sky shaders fall back to the analytic path.
		_transmittanceLUT   = CreateLutTexture2D(graphics, 256, 64,  "AtmosphereTransmittanceLUT");
		_multiScatteringLUT = CreateLutTexture2D(graphics, 32,  32,  "AtmosphereMultiScatteringLUT");
		_skyViewLUT         = CreateLutTexture2D(graphics, 192, 108, "AtmosphereSkyViewLUT");
		if (!_transmittanceLUT || !_multiScatteringLUT || !_skyViewLUT)
		{
			LOG_WARN("AtmosphereLUTs::EnsureResources failed to allocate one or more LUT textures");
			ReleaseResources();
			return false;
		}

		_transmittanceUav   = CreateLutUav(device, _transmittanceLUT);
		_multiScatteringUav = CreateLutUav(device, _multiScatteringLUT);
		_skyViewUav         = CreateLutUav(device, _skyViewLUT);
		if (!_transmittanceUav || !_multiScatteringUav || !_skyViewUav)
		{
			LOG_WARN("AtmosphereLUTs::EnsureResources failed to create one or more LUT UAVs");
			ReleaseResources();
			return false;
		}

		// Aerial perspective 3D volume (32x32x32, RGBA16F). Camera-frustum-
		// aligned per-frame integral of (scattering, transmittance) from
		// camera to froxel centre. RWTexture3D in the compute shader, SRV
		// in the apply pass.
		_aerialPerspectiveVolume = g_pEnv->_graphicsDevice->CreateTexture3D(
			32, 32, 32,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			1,
			D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			1, 1, 0,
			nullptr,
			D3D11_RTV_DIMENSION_UNKNOWN,
			D3D11_UAV_DIMENSION_TEXTURE3D,
			D3D11_SRV_DIMENSION_TEXTURE3D,
			D3D11_DSV_DIMENSION_UNKNOWN);
		if (_aerialPerspectiveVolume == nullptr)
		{
			LOG_WARN("AtmosphereLUTs::EnsureResources failed to allocate AP volume");
			ReleaseResources();
			return false;
		}
		_aerialPerspectiveVolume->SetDebugName("AtmosphereAerialPerspectiveLUT");

		{
			auto* d3dTex = reinterpret_cast<ID3D11Resource*>(_aerialPerspectiveVolume->GetNativePtr());
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
			uavDesc.Texture3D.MipSlice    = 0;
			uavDesc.Texture3D.FirstWSlice = 0;
			uavDesc.Texture3D.WSize       = 32;
			HRESULT hr = device->CreateUnorderedAccessView(d3dTex, &uavDesc, &_aerialPerspectiveUav);
			if (FAILED(hr) || _aerialPerspectiveUav == nullptr)
			{
				LOG_WARN("AtmosphereLUTs::EnsureResources failed to create AP volume UAV");
				ReleaseResources();
				return false;
			}
		}

		// Linear-clamp sampler bound at s4 during dispatch. The LUT shaders
		// expect this slot; the engine's global samplers don't auto-bind on
		// the CS stage so we own a tiny one rather than depending on the
		// renderer to forward sampler state.
		{
			D3D11_SAMPLER_DESC sd = {};
			sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
			sd.MinLOD = 0.0f;
			sd.MaxLOD = D3D11_FLOAT32_MAX;
			HRESULT hr = device->CreateSamplerState(&sd, &_linearClampSampler);
			if (FAILED(hr) || _linearClampSampler == nullptr)
			{
				LOG_WARN("AtmosphereLUTs::EnsureResources failed to create linear-clamp sampler");
				ReleaseResources();
				return false;
			}
		}

		// Shaders. Failure on any one disables that LUT path but lets the
		// others continue - useful when iterating on a single shader.
		_transmittanceShader     = IShader::Create("EngineData.Shaders/AtmosphereTransmittanceLUT.hcs");
		_multiScatteringShader   = IShader::Create("EngineData.Shaders/AtmosphereMultiScatteringLUT.hcs");
		_skyViewShader           = IShader::Create("EngineData.Shaders/AtmosphereSkyViewLUT.hcs");
		_aerialPerspectiveShader = IShader::Create("EngineData.Shaders/AtmosphereAerialPerspectiveLUT.hcs");
		if (!_transmittanceShader || !_multiScatteringShader || !_skyViewShader || !_aerialPerspectiveShader)
		{
			LOG_WARN("AtmosphereLUTs::EnsureResources missing one or more compute shaders - sky LUTs disabled");
			ReleaseResources();
			return false;
		}

		_needsTransmittanceRebuild = true;
		_resourcesReady = true;
		return true;
	}

	namespace
	{
		// Dispatch a compute shader directly via the D3D11 context. No
		// abstract ITexture2D-UAV bind exists in the engine yet (Phase B5
		// will add it); for now we go through the raw context like
		// AutoExposure and DiffuseGI do, mirroring their save/restore
		// hazard handling.
		void DispatchLUT(ID3D11DeviceContext* context, IShader* shader,
			ID3D11UnorderedAccessView* uav,
			const std::vector<ID3D11ShaderResourceView*>& srvs,
			ID3D11SamplerState* sampler,
			IConstantBuffer* atmosCB, IConstantBuffer* skyViewCB,
			uint32_t groupsX, uint32_t groupsY,
			uint32_t groupsZ = 1u,
			IConstantBuffer* perFrameCB = nullptr)
		{
			auto* stage = shader->GetShaderStage(ShaderStage::ComputeShader);
			if (stage == nullptr)
				return;

			// Save / null out any RT bindings that might overlap our SRV
			// inputs - same hazard pattern AutoExposure uses.
			ID3D11RenderTargetView* prevRtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
			ID3D11DepthStencilView* prevDsv = nullptr;
			context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, prevRtvs, &prevDsv);
			context->OMSetRenderTargets(0, nullptr, nullptr);

			if (!srvs.empty())
				context->CSSetShaderResources(0, (UINT)srvs.size(), srvs.data());

			UINT uavInitCounts = 0;
			context->CSSetUnorderedAccessViews(0, 1, &uav, &uavInitCounts);

			// LUT shaders sample at register(s4). Engine global samplers
			// aren't auto-bound on CS, so always bind our private one.
			if (sampler)
				context->CSSetSamplers(4, 1, &sampler);

			if (atmosCB)
			{
				ID3D11Buffer* cb = reinterpret_cast<ID3D11Buffer*>(atmosCB->GetNativePtr());
				context->CSSetConstantBuffers(5, 1, &cb);
			}
			if (skyViewCB)
			{
				ID3D11Buffer* cb = reinterpret_cast<ID3D11Buffer*>(skyViewCB->GetNativePtr());
				context->CSSetConstantBuffers(6, 1, &cb);
			}
			// Aerial perspective needs the per-frame cbuffer at b0 (view-
			// projection inverse, eye pos). Other LUT generators don't.
			if (perFrameCB)
			{
				ID3D11Buffer* cb = reinterpret_cast<ID3D11Buffer*>(perFrameCB->GetNativePtr());
				context->CSSetConstantBuffers(0, 1, &cb);
			}

			context->CSSetShader(reinterpret_cast<ID3D11ComputeShader*>(stage->GetNativePtr()), nullptr, 0);
			context->Dispatch(groupsX, groupsY, groupsZ);

			// Unbind to release UAV hazards before any subsequent SRV bind.
			ID3D11UnorderedAccessView* nullUav = nullptr;
			context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);

			if (!srvs.empty())
			{
				std::vector<ID3D11ShaderResourceView*> nulls(srvs.size(), nullptr);
				context->CSSetShaderResources(0, (UINT)nulls.size(), nulls.data());
			}

			if (sampler)
			{
				ID3D11SamplerState* nullSampler = nullptr;
				context->CSSetSamplers(4, 1, &nullSampler);
			}

			// Don't null b0 - the engine's per-frame cbuffer is set up
			// for the rest of the frame and other CS passes may rely on
			// it staying valid. b5/b6 are atmosphere-specific so nulling
			// those is fine (and matches what we do for SRVs/UAVs).
			if (atmosCB)
			{
				ID3D11Buffer* nullCb = nullptr;
				context->CSSetConstantBuffers(5, 1, &nullCb);
			}
			if (skyViewCB)
			{
				ID3D11Buffer* nullCb = nullptr;
				context->CSSetConstantBuffers(6, 1, &nullCb);
			}

			context->CSSetShader(nullptr, nullptr, 0);

			// Restore the OM state we stomped.
			context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, prevRtvs, prevDsv);
			for (auto* rtv : prevRtvs) if (rtv) rtv->Release();
			if (prevDsv) prevDsv->Release();
		}
	}

	void AtmosphereLUTs::Update(float cameraWorldY, const math::Vector3& sunDirection, float sunIntensity)
	{
		auto* graphics = g_pEnv != nullptr ? g_pEnv->_graphicsDevice : nullptr;
		if (graphics == nullptr || _atmosphereCBuffer == nullptr || _skyViewCBuffer == nullptr)
			return;

		// The LUT compute passes cast GetNativeDevice()/Context() straight to
		// D3D11; under any other backend those land on the wrong vtable. The LUT
		// path is D3D11-only until Phase B5 ports it - no-op cleanly under D3D12
		// (the sky shaders fall back to their analytic path).
		if (graphics->GetBackend() != GraphicsBackend::D3D11)
			return;

		if (!r_atmosphereLUTs._val.b)
			return;

		// Param cbuffer write (atmospheric model parameters).
		if (_paramsDirty)
		{
			_atmosphereCBuffer->Write(&_params, sizeof(_params));
			_paramsDirty = false;
		}

		if (!EnsureResources())
			return;

		// Per-frame sky-view inputs.
		SkyViewParamsCB skyCB{};
		// Lift world Y (engine metres) into atmosphere space (Mm above
		// planet centre). max() clamps the camera to the ground shell so
		// the LUT never integrates from below the surface.
		skyCB.cameraHeightMM = _params.groundRadiusMM + cameraWorldY * 1.0e-6f;
		skyCB.cameraHeightMM = std::max(skyCB.cameraHeightMM, _params.groundRadiusMM + 1e-4f);
		math::Vector3 sunN = sunDirection;
		const float sunLen2 = sunN.x * sunN.x + sunN.y * sunN.y + sunN.z * sunN.z;
		if (sunLen2 > 1e-8f)
			sunN = sunN / std::sqrt(sunLen2);
		else
			sunN = math::Vector3(0.0f, 1.0f, 0.0f); // fallback: noon sun
		skyCB.sunDirection = sunN;
		skyCB.sunIntensity = sunIntensity;
		_skyViewCBuffer->Write(&skyCB, sizeof(skyCB));

		auto* context = reinterpret_cast<ID3D11DeviceContext*>(graphics->GetNativeDeviceContext());
		if (context == nullptr)
			return;

		// Transmittance LUT (256x64, 8x8 groups -> 32x8 groups).
		if (_needsTransmittanceRebuild)
		{
			DispatchLUT(context, _transmittanceShader.get(),
				_transmittanceUav,
				/*srvs*/ {},
				_linearClampSampler,
				_atmosphereCBuffer, nullptr,
				/*groupsX*/ 32u, /*groupsY*/ 8u);
			_needsTransmittanceRebuild = false;
		}

		// Multi-scattering LUT (32x32, 8x8 groups -> 4x4 groups).
		{
			ID3D11ShaderResourceView* srvs[1] = {};
			auto* transmittanceSrv = reinterpret_cast<ID3D11ShaderResourceView*>(_transmittanceLUT->GetNativeShaderView());
			srvs[0] = transmittanceSrv;
			DispatchLUT(context, _multiScatteringShader.get(),
				_multiScatteringUav,
				{ srvs[0] },
				_linearClampSampler,
				_atmosphereCBuffer, nullptr,
				/*groupsX*/ 4u, /*groupsY*/ 4u);
		}

		// Sky-view LUT (192x108, 8x8 groups -> 24x14 groups; round up for the 108 dim).
		{
			ID3D11ShaderResourceView* transmittanceSrv   = reinterpret_cast<ID3D11ShaderResourceView*>(_transmittanceLUT->GetNativeShaderView());
			ID3D11ShaderResourceView* multiScatteringSrv = reinterpret_cast<ID3D11ShaderResourceView*>(_multiScatteringLUT->GetNativeShaderView());
			DispatchLUT(context, _skyViewShader.get(),
				_skyViewUav,
				{ transmittanceSrv, multiScatteringSrv },
				_linearClampSampler,
				_atmosphereCBuffer, _skyViewCBuffer,
				/*groupsX*/ 24u, /*groupsY*/ 14u);
		}

		// Aerial perspective volume (32x32x32, 8x8x8 groups -> 4x4x4).
		// Needs the per-frame cbuffer at b0 for the view-projection
		// inverse to reconstruct world rays per froxel.
		if (_aerialPerspectiveShader != nullptr && _aerialPerspectiveUav != nullptr)
		{
			ID3D11ShaderResourceView* transmittanceSrv   = reinterpret_cast<ID3D11ShaderResourceView*>(_transmittanceLUT->GetNativeShaderView());
			ID3D11ShaderResourceView* multiScatteringSrv = reinterpret_cast<ID3D11ShaderResourceView*>(_multiScatteringLUT->GetNativeShaderView());
			IConstantBuffer* perFrameCB = graphics->GetEngineConstantBuffer(EngineConstantBuffer::PerFrameBuffer);
			DispatchLUT(context, _aerialPerspectiveShader.get(),
				_aerialPerspectiveUav,
				{ transmittanceSrv, multiScatteringSrv },
				_linearClampSampler,
				_atmosphereCBuffer, _skyViewCBuffer,
				/*groupsX*/ 4u, /*groupsY*/ 4u,
				/*groupsZ*/ 4u,
				/*perFrameCB*/ perFrameCB);
		}
	}
}
