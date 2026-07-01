#include "VolumetricScattering.hpp"
#include "../HexEngine.hpp"
#include "IGraphicsDevice.hpp"
#include "IConstantBuffer.hpp"
#include "IShader.hpp"
#include "ITexture2D.hpp"
#include "ITexture3D.hpp"
#include <d3d11.h>

namespace HexEngine
{
	// Temporal accumulation tuning. Base alpha is the EMA current-frame
	// weight when the camera is still (lower = smoother but slower to
	// converge); motion alpha is what it rises TO as a froxel's reprojected
	// position diverges from its current cell. The motion boost exists
	// because some scatter sources are not world-stable - the screen-space
	// emissive injection in particular moves with the camera, so history
	// reprojection drags its old distribution around as a ghost trail
	// unless the blend gets snappier under motion.
	HVar r_volumetricTemporalAlpha("r_volumetricTemporalAlpha", "Volumetric temporal blend weight when static (lower = smoother, slower)", 0.08f, 0.01f, 1.0f);
	HVar r_volumetricTemporalMotionAlpha("r_volumetricTemporalMotionAlpha", "Volumetric temporal blend weight under camera motion (higher = less ghosting)", 0.5f, 0.0f, 1.0f);

	namespace
	{
		// Both cbuffers are tiny - one float4 in the integrate case, four
		// float4s in the scatter case. Layouts match the matching cbuffers
		// in the shaders at register(b5).
		struct ScatterParamsCB
		{
			math::Vector4 volumeDimsAndFar;     // .xyz = (w,h,d), .w = far depth metres
			math::Vector4 sunDirAndIntensity;   // .xyz = sun dir, .w = intensity
			math::Vector4 sunColourAndPhaseG;   // .rgb = sun colour, .a = phase G
			math::Vector4 mediumParams;         // .x = base ext, .y = height density, .z = pivot, .w = falloff
			// World->light-clip for each shadow cascade. The scatter
			// shader walks 0..numCascades-1 per froxel and uses the
			// first one whose NDC bounds contain the position. Layout
			// is column-major after transpose (HLSL default).
			math::Matrix  sunCascadeVPs[VolumetricScattering::kMaxCascades];
			// .x = bias, .y = 1/shadowW, .z = 1/shadowH, .w = numCascades (float).
			math::Vector4 shadowParams;
			// Per-frame jitter applied to (uvw) of each froxel sample.
			// .xyz = halton offsets in [-0.5, 0.5] froxel-cell space, .w = unused.
			// Combined with the temporal EMA blend in the integrate pass this
			// is effectively spatial supersampling across frames - each frame
			// samples a different sub-froxel position and the blend averages
			// them into a stable image.
			math::Vector4 jitter;
			// Per-shadow-slot VP matrices for shadowed spot lights. Up to
			// kMaxShadowedSpots (4). Indexed by SLOT (0..3), not by forward-
			// light index; spotShadowSlotPerForward[] gives the mapping.
			math::Matrix  spotShadowVPs[VolumetricScattering::kMaxShadowedSpots];
			// For each forward spot (16 slots), the matching shadow-slot
			// index in [0..kMaxShadowedSpots-1], or -1 if unshadowed. Stored
			// as float4 per entry so HLSL cbuffer indexing stays 16-byte
			// aligned (shader only reads .x).
			math::Vector4 spotShadowSlotPerForward[VolumetricScattering::kMaxForwardSpots];
			// .x = NDC.z bias for spot-shadow comparison
			// .y = 1/shadowMapWidth, .z = 1/shadowMapHeight (PCF texel size)
			// .w = numShadowedSpots (loop gate; 0 disables spot shadowing)
			math::Vector4 spotShadowParams;
			// Point-shadow mapping: pointShadowSlotPerForward[i].x is the
			// cubemap slot (0..kMaxShadowedPoints-1) that holds the shadow
			// data for forward-point i, or -1 if i is unshadowed. Light
			// position + radius for the shadow comparison are read directly
			// from g_fwdPointPosRadius[i] in the shader (avoiding a parallel
			// per-slot array).
			math::Vector4 pointShadowSlotPerForward[VolumetricScattering::kMaxForwardPoints];
			// .x = linear-depth bias (metres). Point shadows compare in
			// linear depth (cubemap face NDC.z would compress nonlinearly
			// like spot shadows did, same problem).
			// .y = numShadowedPoints (loop gate).
			// .zw = unused.
			math::Vector4 pointShadowParams;
			// Screen-space emissive injection.
			// .x = strength multiplier (0 disables - shader gates the gbuffer
			//      sampling on it). .y = world-space falloff range (metres).
			// .zw = unused.
			math::Vector4 emissiveParams;
			// Ambient inscatter of the fog medium. .rgb = ambient colour
			// PRE-multiplied by strength on the CPU; shader adds
			// .rgb * extinction to the scatter radiance. .w unused.
			math::Vector4 fogAmbient;
		};

		struct IntegrateParamsCB
		{
			math::Vector4 volumeDimsAndFar;
			// .x = history blend alpha (current weight). Lower = more
			// history weight (smoother but only smooth when reprojection
			// is valid - off-screen pixels fall back to alpha=1).
			// .y = hasPrevViewProj (1.0 if previous frame valid, 0.0 on
			// first frame after init - integrate shader skips history
			// blend entirely when 0.0).
			math::Vector4 temporalParams;
			// World->prev-frame-clip matrix. Each froxel reconstructs
			// its current world position, projects through this matrix
			// to find its position in the previous frame's volume, and
			// samples history there. This is what makes camera motion
			// not drag the EMA into the wrong cell.
			math::Matrix  prevViewProj;
			// Previous-frame camera world position. Needed to compute
			// distance-along-prev-ray for the W axis reprojection, since
			// the volume's W encodes "distance along ray from eye" (not
			// view-space z). Without this the off-axis froxels would
			// reproject to slightly wrong W slices.
			math::Vector4 prevEyePos;
		};

		ID3D11UnorderedAccessView* CreateVolumeUav(ID3D11Device* device, ITexture3D* tex)
		{
			if (!device || !tex)
				return nullptr;
			auto* res = reinterpret_cast<ID3D11Resource*>(tex->GetNativePtr());
			if (!res)
				return nullptr;
			D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
			desc.Format        = DXGI_FORMAT_R16G16B16A16_FLOAT;
			desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
			desc.Texture3D.MipSlice    = 0;
			desc.Texture3D.FirstWSlice = 0;
			desc.Texture3D.WSize       = VolumetricScattering::kVolumeDepth;
			ID3D11UnorderedAccessView* uav = nullptr;
			HRESULT hr = device->CreateUnorderedAccessView(res, &desc, &uav);
			if (FAILED(hr))
				return nullptr;
			return uav;
		}
	}

	VolumetricScattering::VolumetricScattering() = default;

	VolumetricScattering::~VolumetricScattering()
	{
		Destroy();
	}

	bool VolumetricScattering::Create()
	{
		auto* graphics = g_pEnv != nullptr ? g_pEnv->_graphicsDevice : nullptr;
		if (!graphics)
		{
			LOG_WARN("VolumetricScattering::Create called before graphics device is ready");
			return false;
		}

		_scatterParamsCBuffer   = graphics->CreateConstantBuffer(sizeof(ScatterParamsCB));
		_integrateParamsCBuffer = graphics->CreateConstantBuffer(sizeof(IntegrateParamsCB));
		if (!_scatterParamsCBuffer || !_integrateParamsCBuffer)
		{
			LOG_WARN("VolumetricScattering::Create failed to allocate cbuffers");
			return false;
		}
		// Textures, UAVs, shaders allocated lazily in EnsureResources on
		// first Update so the shader pipeline has fully warmed up.
		return true;
	}

	void VolumetricScattering::Destroy()
	{
		ReleaseResources();
		_scatterShader.reset();
		_integrateShader.reset();
		SAFE_DELETE(_scatterParamsCBuffer);
		SAFE_DELETE(_integrateParamsCBuffer);
	}

	void VolumetricScattering::ReleaseResources()
	{
		if (_scatterUav)         { _scatterUav->Release();         _scatterUav         = nullptr; }
		for (uint32_t i = 0u; i < 2u; ++i)
		{
			if (_integrationUavs[i]) { _integrationUavs[i]->Release(); _integrationUavs[i] = nullptr; }
			SAFE_DELETE(_integrationVolumes[i]);
		}
		if (_shadowPointSampler) { _shadowPointSampler->Release(); _shadowPointSampler = nullptr; }
		if (_linearClampSampler) { _linearClampSampler->Release(); _linearClampSampler = nullptr; }
		if (_readbackStaging)    { _readbackStaging->Release();    _readbackStaging    = nullptr; }
		SAFE_DELETE(_scatterVolume);
		SAFE_DELETE(_pointShadowCubeArray);
		_writeIdx = 0u;
		_hasPrevViewProj = false;
		_resourcesReady = false;
	}

	bool VolumetricScattering::EnsureResources()
	{
		if (_resourcesReady)
			return true;

		auto* graphics = g_pEnv != nullptr ? g_pEnv->_graphicsDevice : nullptr;
		if (!graphics)
			return false;
		// The froxel compute path casts GetNativeDevice()/Context() straight to
		// D3D11 - under any other backend those land on the wrong vtable. The
		// scatter pipeline is D3D11-only until Phase B5 ports it, so no-op
		// cleanly under D3D12 (the scene renders without volumetric fog).
		if (graphics->GetBackend() != GraphicsBackend::D3D11)
			return false;
		auto* device = reinterpret_cast<ID3D11Device*>(graphics->GetNativeDevice());
		if (!device)
			return false;

		_scatterVolume = graphics->CreateTexture3D(
			(int32_t)kVolumeWidth, (int32_t)kVolumeHeight, (int32_t)kVolumeDepth,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			1,
			D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			1, 1, 0,
			nullptr,
			D3D11_RTV_DIMENSION_UNKNOWN,
			D3D11_UAV_DIMENSION_TEXTURE3D,
			D3D11_SRV_DIMENSION_TEXTURE3D,
			D3D11_DSV_DIMENSION_UNKNOWN);

		// Ping-pong integration volumes for temporal accumulation.
		for (uint32_t i = 0u; i < 2u; ++i)
		{
			_integrationVolumes[i] = graphics->CreateTexture3D(
				(int32_t)kVolumeWidth, (int32_t)kVolumeHeight, (int32_t)kVolumeDepth,
				DXGI_FORMAT_R16G16B16A16_FLOAT,
				1,
				D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
				1, 1, 0,
				nullptr,
				D3D11_RTV_DIMENSION_UNKNOWN,
				D3D11_UAV_DIMENSION_TEXTURE3D,
				D3D11_SRV_DIMENSION_TEXTURE3D,
				D3D11_DSV_DIMENSION_UNKNOWN);
		}

		if (!_scatterVolume || !_integrationVolumes[0] || !_integrationVolumes[1])
		{
			LOG_WARN("VolumetricScattering::EnsureResources failed to allocate volumes");
			ReleaseResources();
			return false;
		}
		_scatterVolume->SetDebugName("VolumetricScatterVolume");
		_integrationVolumes[0]->SetDebugName("VolumetricIntegrationVolumeA");
		_integrationVolumes[1]->SetDebugName("VolumetricIntegrationVolumeB");

		_scatterUav         = CreateVolumeUav(device, _scatterVolume);
		_integrationUavs[0] = CreateVolumeUav(device, _integrationVolumes[0]);
		_integrationUavs[1] = CreateVolumeUav(device, _integrationVolumes[1]);
		if (!_scatterUav || !_integrationUavs[0] || !_integrationUavs[1])
		{
			LOG_WARN("VolumetricScattering::EnsureResources failed to create UAVs");
			ReleaseResources();
			return false;
		}

		// Point-light shadow cubemap array. 6 faces * kMaxShadowedPoints cubes.
		// R32_TYPELESS so the engine's SRV reinterpretation gives R32_FLOAT
		// matching the per-face ShadowMap depth texture format. Used as SRV
		// only; each frame Update GPU-copies from each shadowed point's 6
		// per-face shadow maps into this array. Hardware does cubemap face
		// selection from the shader sample direction so the shader doesn't
		// need a 6-case switch per point.
		_pointShadowCubeArray = graphics->CreateTexture2D(
			(int32_t)kPointShadowFaceSize, (int32_t)kPointShadowFaceSize,
			DXGI_FORMAT_R32_TYPELESS,
			(int32_t)(6u * kMaxShadowedPoints),
			D3D11_BIND_SHADER_RESOURCE,
			1, 1, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			D3D11_RTV_DIMENSION_UNKNOWN,
			D3D11_UAV_DIMENSION_UNKNOWN,
			D3D11_SRV_DIMENSION_TEXTURECUBEARRAY,
			D3D11_DSV_DIMENSION_UNKNOWN);
		if (!_pointShadowCubeArray)
		{
			LOG_WARN("VolumetricScattering::EnsureResources failed to allocate point shadow cubemap array");
			ReleaseResources();
			return false;
		}
		_pointShadowCubeArray->SetDebugName("VolumetricPointShadowCubeArray");

		// Point-clamp sampler for shadow map reads on CS. CS samplers
		// don't auto-bind from the engine's PS-side state setup so we
		// own one here.
		{
			D3D11_SAMPLER_DESC sd = {};
			sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
			sd.MinLOD = 0.0f;
			sd.MaxLOD = D3D11_FLOAT32_MAX;
			HRESULT hr = device->CreateSamplerState(&sd, &_shadowPointSampler);
			if (FAILED(hr) || _shadowPointSampler == nullptr)
			{
				LOG_WARN("VolumetricScattering::EnsureResources failed to create shadow sampler");
				ReleaseResources();
				return false;
			}
		}
		// Linear-clamp sampler for history reprojection in integrate.
		// Reprojected UVW rarely lands exactly on a texel center so
		// linear filter is needed for smooth temporal accumulation
		// under sub-froxel motion.
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
				LOG_WARN("VolumetricScattering::EnsureResources failed to create linear sampler");
				ReleaseResources();
				return false;
			}
		}

		_scatterShader   = IShader::Create("EngineData.Shaders/VolumetricScatterDensity.hcs");
		_integrateShader = IShader::Create("EngineData.Shaders/VolumetricScatterIntegrate.hcs");
		if (!_scatterShader || !_integrateShader)
		{
			LOG_WARN("VolumetricScattering::EnsureResources missing compute shaders");
			ReleaseResources();
			return false;
		}

		_resourcesReady = true;
		return true;
	}

	void VolumetricScattering::Update(
		const math::Vector3& sunDirection,
		const math::Vector3& sunColour,
		float sunIntensity,
		float phaseG,
		float scatteringStrength,
		float baseExtinction,
		float heightDensity,
		float heightPivot,
		float heightFalloff,
		const math::Matrix (&cascadeVPs)[kMaxCascades],
		ITexture2D* const (&cascadeShadowMaps)[kMaxCascades],
		uint32_t numCascades,
		float shadowBias,
		const math::Vector2& shadowMapSize,
		const math::Matrix& currentViewProj,
		const math::Vector3& currentEyePos,
		ITexture2D* atmosphereTransmittanceLUT,
		IConstantBuffer* forwardLightsBuffer,
		const math::Matrix (&spotShadowVPs)[kMaxShadowedSpots],
		ITexture2D* const (&spotShadowMaps)[kMaxShadowedSpots],
		const int (&shadowSlotForForwardSpot)[kMaxForwardSpots],
		uint32_t numShadowedSpots,
		const math::Vector2& spotShadowMapSize,
		float spotShadowBias,
		ITexture2D* const pointShadowFaceMaps[kMaxShadowedPoints][6],
		const float (&pointShadowFarMetres)[kMaxShadowedPoints],
		const int (&shadowSlotForForwardPoint)[kMaxForwardPoints],
		uint32_t numShadowedPoints,
		float pointShadowBiasMetres,
		ITexture2D* gbufferDiffuse,
		ITexture2D* gbufferPosition,
		float emissiveStrength,
		float emissiveRangeMetres,
		const math::Vector3& fogAmbientColour,
		float fogAmbientStrength)
	{
		auto* graphics = g_pEnv != nullptr ? g_pEnv->_graphicsDevice : nullptr;
		if (!graphics || !_scatterParamsCBuffer || !_integrateParamsCBuffer)
			return;
		// D3D11-only compute path (see EnsureResources) - no-op under D3D12.
		if (graphics->GetBackend() != GraphicsBackend::D3D11)
			return;
		if (!EnsureResources())
			return;
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(graphics->GetNativeDeviceContext());
		if (!context)
			return;

		// Normalise sun dir.
		math::Vector3 sunN = sunDirection;
		const float len2 = sunN.x * sunN.x + sunN.y * sunN.y + sunN.z * sunN.z;
		if (len2 > 1e-8f)
			sunN = sunN / std::sqrt(len2);
		else
			sunN = math::Vector3(0.0f, 1.0f, 0.0f);

		// Cbuffer writes.
		ScatterParamsCB scatterCB{};
		scatterCB.volumeDimsAndFar  = math::Vector4((float)kVolumeWidth, (float)kVolumeHeight, (float)kVolumeDepth, kFarDepthM);
		// Fold scatteringStrength into the sun-intensity slot so the shader
		// doesn't need a separate uniform. This matches the legacy
		// VolumetricLighting shader's `env_volumetricStrength` multiplier.
		scatterCB.sunDirAndIntensity = math::Vector4(sunN.x, sunN.y, sunN.z, sunIntensity * scatteringStrength);
		scatterCB.sunColourAndPhaseG = math::Vector4(sunColour.x, sunColour.y, sunColour.z, phaseG);
		scatterCB.mediumParams       = math::Vector4(baseExtinction, heightDensity, heightPivot, heightFalloff);
		// HLSL cbuffer matrices are read column-major by default - the
		// engine's matrices are row-major, so transpose each cascade
		// VP into the cbuffer.
		const uint32_t cascadeCount = std::min(numCascades, kMaxCascades);
		for (uint32_t i = 0u; i < kMaxCascades; ++i)
		{
			// Unused slots get an identity matrix - the shader gates
			// on i < numCascades anyway, but identity keeps cbuffer
			// state deterministic and helps debugging.
			scatterCB.sunCascadeVPs[i] = (i < cascadeCount)
				? cascadeVPs[i].Transpose()
				: math::Matrix::Identity;
		}
		// shadowParams.yz = 1/shadowMapWidth, 1/shadowMapHeight for the
		// texel-space PCF offsets. .w = active cascade count so the
		// shader's loop knows when to stop.
		const float invSw = shadowMapSize.x > 0.0f ? 1.0f / shadowMapSize.x : 0.0f;
		const float invSh = shadowMapSize.y > 0.0f ? 1.0f / shadowMapSize.y : 0.0f;
		scatterCB.shadowParams       = math::Vector4(shadowBias, invSw, invSh, (float)cascadeCount);
		// Halton(2) / Halton(3) / Halton(5) jitter sequence. Maps each
		// frame to a quasi-random point in [-0.5, 0.5] froxel-cell space
		// so different frames sample different sub-froxel positions; the
		// EMA blend in the integrate pass averages them into a stable
		// image. 16-frame Halton period - the temporal blend's effective
		// window (~10 frames) covers it fully.
		auto halton = [](uint32_t i, uint32_t base) -> float {
			float f = 1.0f, r = 0.0f;
			while (i > 0u) {
				f /= (float)base;
				r += f * (float)(i % base);
				i /= base;
			}
			return r;
		};
		const uint32_t jf = (_jitterFrame % 16u) + 1u;   // 1-based avoids the 0,0,0 degenerate sample
		const float jx = halton(jf, 2u) - 0.5f;
		const float jy = halton(jf, 3u) - 0.5f;
		const float jz = halton(jf, 5u) - 0.5f;
		// .w = useTransmittance flag. 1.0 when the transmittance LUT
		// is available so the shader samples it to redden sunset rays;
		// 0.0 when no LUT (scatter falls back to raw sunColour, same
		// behaviour as before atmosphere was wired in).
		const float useTransmittance = atmosphereTransmittanceLUT != nullptr ? 1.0f : 0.0f;
		scatterCB.jitter = math::Vector4(jx, jy, jz, useTransmittance);

		// Spot shadow VPs. Transpose for HLSL column-major default,
		// same as the sun cascade matrices. Unused slots get Identity
		// so a stray sample (gated out by numShadowed anyway) wouldn't
		// dereference undefined memory.
		const uint32_t shadowedCount = std::min(numShadowedSpots, kMaxShadowedSpots);
		for (uint32_t s = 0u; s < kMaxShadowedSpots; ++s)
		{
			scatterCB.spotShadowVPs[s] = (s < shadowedCount)
				? spotShadowVPs[s].Transpose()
				: math::Matrix::Identity;
		}
		// For each forward spot slot, store the matching shadow slot
		// (or -1 if unshadowed) in the .x of the float4 entry.
		for (uint32_t i = 0u; i < kMaxForwardSpots; ++i)
		{
			const int slot = (shadowedCount > 0u) ? shadowSlotForForwardSpot[i] : -1;
			scatterCB.spotShadowSlotPerForward[i] = math::Vector4((float)slot, 0.0f, 0.0f, 0.0f);
		}
		// Texel size for PCF + bias + count.
		const float spotInvSw = spotShadowMapSize.x > 0.0f ? 1.0f / spotShadowMapSize.x : 0.0f;
		const float spotInvSh = spotShadowMapSize.y > 0.0f ? 1.0f / spotShadowMapSize.y : 0.0f;
		scatterCB.spotShadowParams = math::Vector4(spotShadowBias, spotInvSw, spotInvSh, (float)shadowedCount);

		// Point shadow mapping fill. Each forward point stores its shadow
		// slot index (or -1) in .x. The far-plane per slot is needed in
		// the shader to invert NDC.z back to linear depth - we pack it
		// into .y of the same slot's mapping entry... but slots are
		// keyed by forward-light index, not by shadow slot. Instead, the
		// shader looks the far up via the forward light's radius field
		// (g_fwdPointPosRadius[i].w) - PointLight uses radius as its
		// per-face far plane.
		const uint32_t shadowedPointCount = std::min(numShadowedPoints, kMaxShadowedPoints);
		for (uint32_t i = 0u; i < kMaxForwardPoints; ++i)
		{
			const int slot = (shadowedPointCount > 0u) ? shadowSlotForForwardPoint[i] : -1;
			scatterCB.pointShadowSlotPerForward[i] = math::Vector4((float)slot, 0.0f, 0.0f, 0.0f);
		}
		scatterCB.pointShadowParams = math::Vector4(pointShadowBiasMetres, (float)shadowedPointCount, 0.0f, 0.0f);
		// Emissive injection only runs when both gbuffer SRVs are available -
		// force strength to 0 otherwise so the shader's gate skips the taps.
		const bool emissiveValid = gbufferDiffuse != nullptr && gbufferPosition != nullptr;
		scatterCB.emissiveParams = math::Vector4(
			emissiveValid ? std::max(0.0f, emissiveStrength) : 0.0f,
			std::max(0.01f, emissiveRangeMetres), 0.0f, 0.0f);
		// Premultiply ambient colour by strength so the shader's per-froxel
		// cost is a single mad against the extinction.
		const float ambS = std::max(0.0f, fogAmbientStrength);
		scatterCB.fogAmbient = math::Vector4(
			fogAmbientColour.x * ambS, fogAmbientColour.y * ambS, fogAmbientColour.z * ambS, 0.0f);

		_scatterParamsCBuffer->Write(&scatterCB, sizeof(scatterCB));

		IntegrateParamsCB integrateCB{};
		integrateCB.volumeDimsAndFar = scatterCB.volumeDimsAndFar;
		// .x = base history blend alpha (camera still). World-stable scatter
		//      (sun, shadowed lights) reprojects correctly so a long window
		//      is safe; ~0.08 = ~12-frame average.
		// .y = valid-history flag. First frame after init has nothing to
		//      reproject against - shader skips the blend (uses pure current)
		//      so we don't seed the EMA with the volume's cleared state.
		// .z = motion alpha: the blend weight the shader ramps TO as a
		//      froxel's reprojected cell diverges from its current cell.
		//      Screen-space-derived scatter (emissive injection) is NOT
		//      world-stable, so under camera motion the long window smears
		//      it into a ghost trail; snapping toward current under motion
		//      clears the trail in 1-2 frames at the cost of a little more
		//      jitter noise while moving (masked by the motion itself).
		const float validHistory = _hasPrevViewProj ? 1.0f : 0.0f;
		integrateCB.temporalParams = math::Vector4(
			std::clamp(r_volumetricTemporalAlpha._val.f32, 0.01f, 1.0f),
			validHistory,
			std::clamp(r_volumetricTemporalMotionAlpha._val.f32, 0.0f, 1.0f),
			0.0f);
		// HLSL cbuffer matrices column-major by default - row-major
		// engine matrices need transposing same as the cascade VP.
		integrateCB.prevViewProj = _prevViewProj.Transpose();
		integrateCB.prevEyePos = math::Vector4(_prevEyePos.x, _prevEyePos.y, _prevEyePos.z, 0.0f);
		_integrateParamsCBuffer->Write(&integrateCB, sizeof(integrateCB));

		// Save / restore OM state same hazard pattern as AtmosphereLUTs.
		ID3D11RenderTargetView* prevRtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		ID3D11DepthStencilView* prevDsv = nullptr;
		context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, prevRtvs, &prevDsv);
		context->OMSetRenderTargets(0, nullptr, nullptr);

		// Copy per-face point-light shadow maps into the cubemap array.
		// PointLight stores 6 independent ShadowMap textures (one per
		// cube face); the volumetric needs them in a single
		// TextureCubeArray for hardware face selection during sampling.
		//
		// IMPORTANT: source textures are the per-face COLOUR render targets
		// (R32_FLOAT, plain SRV/RT-bound), NOT the DSV-bound R32_TYPELESS
		// depth textures. ShadowMapGeometry.shader writes the rasterized
		// NDC.z into the colour RT precisely so this copy has a clean
		// R32_FLOAT source. The earlier "copy from the depth map directly"
		// path read 0 across all faces on some D3D11 drivers - copying a
		// texture that still carried DEPTH_STENCIL bind state into a
		// non-depth array zeroed the destination, which the volumetric
		// then interpreted as "occluder right at the near plane" and
		// shadowed point-light contribution to a 2m cube around the light.
		//
		// Face order remapping: PointLight::gLightDirs is laid out as
		//   [Forward, Backward, Up, Down, Left, Right]
		// and SimpleMath is RIGHT-handed (Forward = (0,0,-1)), so in world
		// axes the engine face order is
		//   [-Z, +Z, +Y, -Y, -X, +X]
		// while D3D11 cubemap subresource order is
		//   [+X, -X, +Y, -Y, +Z, -Z]
		// giving the engine face -> cube subresource mapping:
		//   0 (-Z) -> 5   1 (+Z) -> 4   2 (+Y) -> 2   3 (-Y) -> 3   4 (-X) -> 1   5 (+X) -> 0
		// (An earlier version of this table assumed Forward = +Z and had the
		// two Z faces swapped - shadows from occluders north of a light fell
		// to the south. It went unnoticed because the dominant test occluder
		// was a flat roof on the -Y face, which maps identically either way.)
		//
		// KNOWN LIMITATION: faces are rendered with CreateLookAt view
		// matrices whose screen axes don't match D3D's per-face cube UV
		// conventions (those date from LH rendering), so face CONTENTS are
		// mirrored relative to what hardware cube lookup expects. Shadow
		// edges within a face can land mirrored about the face centre. The
		// per-pixel PointLight.shader path doesn't suffer this (it projects
		// through the same VP used to render the face); if volumetric edge
		// accuracy ever matters, switch SamplePointShadow to the same
		// VP-projection approach instead of hardware cube sampling.
		//
		// CopySubresourceRegion is a no-CPU GPU copy; R32_FLOAT ->
		// R32_TYPELESS within the same format family is legal in D3D11.
		if (_pointShadowCubeArray && shadowedPointCount > 0u)
		{
			static const uint32_t kEngineFaceToCubeFace[6] = { 5u, 4u, 2u, 3u, 1u, 0u };
			auto* dstRes = reinterpret_cast<ID3D11Resource*>(_pointShadowCubeArray->GetNativePtr());
			if (dstRes)
			{
				for (uint32_t slot = 0u; slot < shadowedPointCount; ++slot)
				{
					for (uint32_t engineFace = 0u; engineFace < 6u; ++engineFace)
					{
						auto* srcTex = pointShadowFaceMaps[slot][engineFace];
						if (srcTex == nullptr) continue;
						auto* srcRes = reinterpret_cast<ID3D11Resource*>(srcTex->GetNativePtr());
						if (srcRes == nullptr) continue;
						const uint32_t cubeFace = kEngineFaceToCubeFace[engineFace];
						const uint32_t dstSub = slot * 6u + cubeFace;
						context->CopySubresourceRegion(dstRes, dstSub, 0, 0, 0, srcRes, 0, nullptr);
					}
				}
			}
		}

		// CPU READBACK DIAGNOSTIC (r_pointShadowBias <= -9.5). Maps the first
		// shadowed point light's 6 cube-array faces AND their source RTs back
		// to the CPU and logs min/max/avg of a sparse sample grid. This is the
		// ground-truth check for "what does the cube array actually contain"
		// after several rounds of inferring it indirectly through the froxel
		// visualisation. Throttled to once per 60 frames; each Map() forces a
		// GPU sync stall so this is debug-only by construction.
		//
		// How to read the output:
		//   srcRT min=max=1.0           -> RT cleared but shadow pass never drew
		//                                  (PVS culls everything / pass skipped)
		//   srcRT varies, cube all ~0   -> copy into the cube array is broken
		//   both vary identically       -> data plumbing OK; bug is in the
		//                                  shader's comparison / face selection
		//   srcRT ~0 everywhere         -> clear-to-1 not landing (wrong RT
		//                                  bound, or another pass overwrites)
		if (pointShadowBiasMetres <= -9.5f && shadowedPointCount > 0u && (_jitterFrame % 60u) == 0u)
		{
			auto* device = reinterpret_cast<ID3D11Device*>(graphics->GetNativeDevice());
			if (_readbackStaging == nullptr && device != nullptr)
			{
				D3D11_TEXTURE2D_DESC sd = {};
				sd.Width = kPointShadowFaceSize;
				sd.Height = kPointShadowFaceSize;
				sd.MipLevels = 1;
				sd.ArraySize = 1;
				sd.Format = DXGI_FORMAT_R32_FLOAT;
				sd.SampleDesc.Count = 1;
				sd.Usage = D3D11_USAGE_STAGING;
				sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				device->CreateTexture2D(&sd, nullptr, &_readbackStaging);
			}

			auto logFaceStats = [&](ID3D11Resource* src, uint32_t subresource, const char* label, uint32_t face)
			{
				if (src == nullptr || _readbackStaging == nullptr)
					return;
				context->CopySubresourceRegion(_readbackStaging, 0, 0, 0, 0, src, subresource, nullptr);
				D3D11_MAPPED_SUBRESOURCE mapped = {};
				if (SUCCEEDED(context->Map(_readbackStaging, 0, D3D11_MAP_READ, 0, &mapped)))
				{
					float mn = 1e9f, mx = -1e9f;
					double sum = 0.0;
					int n = 0;
					for (uint32_t y = 0u; y < kPointShadowFaceSize; y += 64u)
					{
						const float* row = reinterpret_cast<const float*>(
							reinterpret_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch);
						for (uint32_t x = 0u; x < kPointShadowFaceSize; x += 64u)
						{
							const float v = row[x];
							mn = std::min(mn, v);
							mx = std::max(mx, v);
							sum += v;
							++n;
						}
					}
					context->Unmap(_readbackStaging, 0);
					LOG_WARN("PointShadowDbg %s face%u: min=%.5f max=%.5f avg=%.5f",
						label, face, mn, mx, (float)(sum / std::max(n, 1)));
				}
			};

			auto* cubeRes = _pointShadowCubeArray != nullptr
				? reinterpret_cast<ID3D11Resource*>(_pointShadowCubeArray->GetNativePtr())
				: nullptr;
			for (uint32_t f = 0u; f < 6u; ++f)
			{
				// Cube slot 0, subresources 0..5 (one mip each).
				logFaceStats(cubeRes, f, "cube ", f);
				auto* srcTex = pointShadowFaceMaps[0][f];
				if (srcTex != nullptr)
					logFaceStats(reinterpret_cast<ID3D11Resource*>(srcTex->GetNativePtr()), 0, "srcRT", f);
			}
		}

		// Per-frame cbuffer needed by scatter for view-projection inverse.
		auto* perFrameCB = graphics->GetEngineConstantBuffer(EngineConstantBuffer::PerFrameBuffer);
		ID3D11Buffer* perFrameNative = perFrameCB ? reinterpret_cast<ID3D11Buffer*>(perFrameCB->GetNativePtr()) : nullptr;
		if (perFrameNative)
			context->CSSetConstantBuffers(0, 1, &perFrameNative);

		// SCATTER PASS - 8x8x8 thread groups over 128x72x64 volume.
		{
			auto* stage = _scatterShader->GetShaderStage(ShaderStage::ComputeShader);
			if (stage)
			{
				ID3D11Buffer* cb = reinterpret_cast<ID3D11Buffer*>(_scatterParamsCBuffer->GetNativePtr());
				context->CSSetConstantBuffers(5, 1, &cb);

				// b7 = forward lights cbuffer (point + spot lights for
				// volumetric contribution). SceneRenderer populates this
				// before our dispatch. nullptr-safe - the shader's loops
				// run for `count` iterations from the cbuffer's counts
				// header, which stays 0 if nothing is bound.
				ID3D11Buffer* fwdLightsCb = forwardLightsBuffer
					? reinterpret_cast<ID3D11Buffer*>(forwardLightsBuffer->GetNativePtr())
					: nullptr;
				context->CSSetConstantBuffers(7, 1, &fwdLightsCb);

				// t0..t3  = shadow cascades 0..3 for per-froxel sun visibility
				// t4      = atmospheric transmittance LUT for sunset warmth
				// t5..t8  = shadow maps for up to 4 shadowed spot lights
				// t9      = TextureCubeArray of point-light shadow cubes
				// t10     = gbuffer diffuse  (emissive tint for froxel glow)
				// t11     = gbuffer position (.w = emissive intensity)
				// All nullptr-safe (shader gates on the relevant count/flag).
				constexpr uint32_t kScatterSrvCount = kMaxCascades + 1u + kMaxShadowedSpots + 1u + 2u;
				ID3D11ShaderResourceView* scatterSrvs[kScatterSrvCount] = {};
				for (uint32_t i = 0u; i < kMaxCascades; ++i)
				{
					if (i < cascadeCount && cascadeShadowMaps[i] != nullptr)
						scatterSrvs[i] = reinterpret_cast<ID3D11ShaderResourceView*>(
							cascadeShadowMaps[i]->GetNativeShaderView());
				}
				if (atmosphereTransmittanceLUT != nullptr)
					scatterSrvs[kMaxCascades] = reinterpret_cast<ID3D11ShaderResourceView*>(
						atmosphereTransmittanceLUT->GetNativeShaderView());
				for (uint32_t s = 0u; s < kMaxShadowedSpots; ++s)
				{
					if (s < shadowedCount && spotShadowMaps[s] != nullptr)
						scatterSrvs[kMaxCascades + 1u + s] = reinterpret_cast<ID3D11ShaderResourceView*>(
							spotShadowMaps[s]->GetNativeShaderView());
				}
				if (_pointShadowCubeArray != nullptr)
					scatterSrvs[kMaxCascades + 1u + kMaxShadowedSpots] = reinterpret_cast<ID3D11ShaderResourceView*>(
						_pointShadowCubeArray->GetNativeShaderView());
				if (emissiveValid)
				{
					scatterSrvs[kMaxCascades + 1u + kMaxShadowedSpots + 1u] =
						reinterpret_cast<ID3D11ShaderResourceView*>(gbufferDiffuse->GetNativeShaderView());
					scatterSrvs[kMaxCascades + 1u + kMaxShadowedSpots + 2u] =
						reinterpret_cast<ID3D11ShaderResourceView*>(gbufferPosition->GetNativeShaderView());
				}
				context->CSSetShaderResources(0, kScatterSrvCount, scatterSrvs);
				context->CSSetSamplers(2, 1, &_shadowPointSampler);
				// s4 = linear-clamp sampler for the transmittance LUT.
				// Reused from the integrate pass's history-reprojection
				// sampler - same MIN/MAG/MIP_LINEAR + CLAMP setup works
				// for both LUT sampling and 3D history sampling.
				context->CSSetSamplers(4, 1, &_linearClampSampler);

				UINT init = 0;
				context->CSSetUnorderedAccessViews(0, 1, &_scatterUav, &init);
				context->CSSetShader(reinterpret_cast<ID3D11ComputeShader*>(stage->GetNativePtr()), nullptr, 0);
				context->Dispatch(kVolumeWidth / 8u, kVolumeHeight / 8u, kVolumeDepth / 8u);
				ID3D11UnorderedAccessView* nullUav = nullptr;
				context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
				// +2 for the gbuffer diffuse/position emissive sources at t10/t11.
				// Clearing them matters more than the others: the gbuffer is
				// re-bound as OM render targets right after this dispatch, and
				// leaving them as CS SRVs would trigger D3D11's read/write
				// hazard resolution (forced unbind + debug-layer noise).
				constexpr uint32_t kScatterSrvCountClear = kMaxCascades + 1u + kMaxShadowedSpots + 1u + 2u;
				ID3D11ShaderResourceView* nullScatterSrvs[kScatterSrvCountClear] = {};
				context->CSSetShaderResources(0, kScatterSrvCountClear, nullScatterSrvs);
				ID3D11SamplerState* nullSampler = nullptr;
				context->CSSetSamplers(2, 1, &nullSampler);
				context->CSSetSamplers(4, 1, &nullSampler);
				ID3D11Buffer* nullCb = nullptr;
				context->CSSetConstantBuffers(5, 1, &nullCb);
				context->CSSetConstantBuffers(7, 1, &nullCb);
				context->CSSetShader(nullptr, nullptr, 0);
			}
		}

		// INTEGRATION PASS - 8x8x1 groups, each thread loops kVolumeDepth W slices.
		// Ping-pong: _writeIdx holds the index of the LAST written volume
		// (so the apply pass reads from it as the just-finished output).
		// This frame writes to the OTHER one, then we flip _writeIdx at
		// the end. History input is the previous-frame's volume, which is
		// the current value of _writeIdx (pre-flip).
		const uint32_t writeNow   = _writeIdx ^ 1u;
		const uint32_t historyNow = _writeIdx;
		{
			auto* stage = _integrateShader->GetShaderStage(ShaderStage::ComputeShader);
			if (stage)
			{
				ID3D11Buffer* cb = reinterpret_cast<ID3D11Buffer*>(_integrateParamsCBuffer->GetNativePtr());
				context->CSSetConstantBuffers(5, 1, &cb);
				auto* scatterSrv = reinterpret_cast<ID3D11ShaderResourceView*>(_scatterVolume->GetNativeShaderView());
				ID3D11ShaderResourceView* historySrv =
					reinterpret_cast<ID3D11ShaderResourceView*>(_integrationVolumes[historyNow]->GetNativeShaderView());
				ID3D11ShaderResourceView* srvs[2] = { scatterSrv, historySrv };
				context->CSSetShaderResources(0, 2, srvs);
				// Linear-clamp sampler at s4 for sub-froxel-accurate
				// history reprojection sampling.
				context->CSSetSamplers(4, 1, &_linearClampSampler);
				UINT init = 0;
				context->CSSetUnorderedAccessViews(0, 1, &_integrationUavs[writeNow], &init);
				context->CSSetShader(reinterpret_cast<ID3D11ComputeShader*>(stage->GetNativePtr()), nullptr, 0);
				context->Dispatch(kVolumeWidth / 8u, kVolumeHeight / 8u, 1u);
				ID3D11UnorderedAccessView* nullUav = nullptr;
				context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
				ID3D11ShaderResourceView* nullSrvs[2] = { nullptr, nullptr };
				context->CSSetShaderResources(0, 2, nullSrvs);
				ID3D11SamplerState* nullSampler = nullptr;
				context->CSSetSamplers(4, 1, &nullSampler);
				ID3D11Buffer* nullCb = nullptr;
				context->CSSetConstantBuffers(5, 1, &nullCb);
				context->CSSetShader(nullptr, nullptr, 0);
			}
		}

		// Promote the just-written volume to "current" so the apply pass
		// reads it via GetIntegrationVolume(). Advance jitter frame index
		// so next frame samples a different sub-froxel position. Store
		// this frame's viewProj as next frame's reprojection source.
		_writeIdx = writeNow;
		++_jitterFrame;
		_prevViewProj = currentViewProj;
		_prevEyePos = currentEyePos;
		_hasPrevViewProj = true;

		// Restore OM.
		context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, prevRtvs, prevDsv);
		for (auto* rtv : prevRtvs) if (rtv) rtv->Release();
		if (prevDsv) prevDsv->Release();
	}
}
