#include "DDGI.hpp"
#include "../HexEngine.hpp"
#include "../Entity/Component/Camera.hpp"
#include "../Entity/Component/PointLight.hpp"
#include "../Entity/Component/SpotLight.hpp"
#include "../Entity/Component/AreaLight.hpp"
#include "../Entity/Component/StaticMeshComponent.hpp"
#include "../Graphics/GBuffer.hpp"
#include "../Graphics/GpuProfiler.hpp"
#include "../Graphics/ISSAOProvider.hpp"
#include "../Graphics/DiffuseGIAOProvider.hpp"
#include "../Graphics/DiffuseGIAOProvider.hpp"
#include "../GUI/GuiRenderer.hpp"
#include "../Physics/PhysUtils.hpp"
#include "Scene.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>

namespace HexEngine
{
	extern HVar r_physicalLightUnits; // SceneRenderer.cpp
	extern HVar r_gpuTimings;         // GpuProfiler.cpp

	HVar r_ddgi("r_ddgi", "DDGI probe GI (compute ray traced). Replaces the voxel GI, flat ambient and sky fill while on. Default since 2026-11-24 (0 = old voxel GI)", true, false, true);
	HVar r_ddgiDebug("r_ddgiDebug", "DDGI debug view: 0 off, 1 indirect diffuse only, 2 probe irradiance only", 0, 0, 2);
	HVar r_ddgiSpacing("r_ddgiSpacing", "Cascade-0 probe spacing (m); cascade n spacing = spacing * r_ddgiCascadeScale^n", 1.0f, 0.25f, 8.0f);
	HVar r_ddgiCascadeScale("r_ddgiCascadeScale", "Spacing ratio between DDGI cascades", 3.0f, 1.5f, 8.0f);
	HVar r_ddgiProbesXZ("r_ddgiProbesXZ", "Probes per horizontal axis per cascade", 32, 8, 64);
	HVar r_ddgiProbesY("r_ddgiProbesY", "Probes on the vertical axis per cascade", 12, 4, 32);
	HVar r_ddgiPeriod0("r_ddgiPeriod0", "Cascade 0: update 1/N of its probes per frame", 4, 1, 64);
	HVar r_ddgiPeriod1("r_ddgiPeriod1", "Cascade 1: update 1/N of its probes per frame", 8, 1, 64);
	HVar r_ddgiPeriod2("r_ddgiPeriod2", "Cascade 2: update 1/N of its probes per frame", 16, 1, 64);
	HVar r_ddgiHysteresis("r_ddgiHysteresis", "Probe temporal blend (history weight); adaptive drop on large changes", 0.96f, 0.0f, 0.995f);
	HVar r_ddgiNormalBias("r_ddgiNormalBias", "Sample-point offset along the surface normal (fraction of probe spacing)", 0.40f, 0.0f, 1.0f);
	HVar r_ddgiViewBias("r_ddgiViewBias", "Sample-point offset toward the eye (fraction of probe spacing)", 0.30f, 0.0f, 1.0f);
	HVar r_ddgiBounce("r_ddgiBounce", "Multi-bounce feedback (previous probe irradiance at ray hits). Clamped < 1", 0.85f, 0.0f, 0.98f);
	HVar r_ddgiSky("r_ddgiSky", "Sky radiance scale for probe rays that miss (sky SH). 0.6 (2026-11-24, user pick): 1.0 is the physical sky fill, which read cool and flattened the warm weather haze outdoors", 0.6f, 0.0f, 8.0f);
	HVar r_ddgiSun("r_ddgiSun", "Sun irradiance scale at probe ray hits", 1.0f, 0.0f, 8.0f);
	HVar r_ddgiLocal("r_ddgiLocal", "Local light (point/spot) irradiance scale at probe ray hits", 1.0f, 0.0f, 8.0f);
	HVar r_ddgiEmissive("r_ddgiEmissive", "Emissive radiance scale at probe ray hits", 1.0f, 0.0f, 16.0f);
	HVar r_ddgiIntensity("r_ddgiIntensity", "Applied indirect diffuse multiplier", 1.0f, 0.0f, 8.0f);
	HVar r_ddgiDepthSharpness("r_ddgiDepthSharpness", "Depth-moment blend exponent", 50.0f, 1.0f, 200.0f);
	HVar r_ddgiCascadeBlend("r_ddgiCascadeBlend", "Cross-fade band between cascades (probes)", 2.0f, 0.5f, 8.0f);
	HVar r_ddgiRelocate("r_ddgiRelocate", "Move probes out of geometry / away from surfaces", true, false, true);
	HVar r_ddgiClassify("r_ddgiClassify", "Disable probes stuck inside geometry", true, false, true);
	HVar r_ddgiMaxLights("r_ddgiMaxLights", "Local lights shaded at probe ray hits (nearest to camera)", 32, 0, 128);
	HVar r_ddgiRayLength("r_ddgiRayLength", "Probe ray length in probe spacings (capped at 200 m)", 40.0f, 4.0f, 200.0f);
	HVar r_ddgiGroundPlane("r_ddgiGroundPlane", "Probe rays that miss downward hit a plane at the local terrain height (the voxel terrain is not in the BVH)", true, false, true);
	HVar r_ddgiGroundAlbedo("r_ddgiGroundAlbedo", "Ground-plane albedo luminance (greenish-grey tint)", 0.20f, 0.0f, 1.0f);
	HVar r_ddgiSSGI("r_ddgiSSGI", "Near-field screen-space bounce on top of the probes (contact range the probe grid cannot resolve)", true, false, true);
	HVar r_ddgiSSGIIntensity("r_ddgiSSGIIntensity", "DDGI SSGI strength", 2.0f, 0.0f, 8.0f);
	HVar r_ddgiSSGIRadius("r_ddgiSSGIRadius", "DDGI SSGI gather radius (m)", 2.5f, 0.25f, 8.0f);
	HVar r_ddgiSSGIMaxLuma("r_ddgiSSGIMaxLuma", "DDGI SSGI per-tap luminance safety cap (high on purpose: sun pools must bounce)", 16.0f, 0.1f, 256.0f);
	HVar r_ddgiSSGITaps("r_ddgiSSGITaps", "DDGI SSGI taps per pixel", 16, 4, 32);
	HVar r_ddgiSSGIBlur("r_ddgiSSGIBlur", "DDGI SSGI edge-aware denoise: 5x5 kernel stride in half-res texels (0 = off; 2 spans ~18 full-res pixels)", 2.0f, 0.0f, 4.0f);
	HVar r_ddgiAdaptive("r_ddgiAdaptive", "Probe history drop on real lighting changes: 0 fixed hysteresis, 1 noise-aware dual-rate (a fast per-probe mean must leave the slow history by several standard errors), 2 old fixed thresholds (fired on noise indoors: probes pulsed)", 1, 0, 2);
	HVar r_ddgiSampleJitter("r_ddgiSampleJitter", "Stochastic probe interpolation: each pixel samples the probes at a point jittered in its own surface plane by up to this many probe spacings, rotated per frame and resolved by TAA. Turns the box-shaped trilinear blend between 1 m probes ('lumps') into a smooth falloff at no extra lookups (0 = plain trilinear)", 0.5f, 0.0f, 1.0f);
	HVar r_ddgiFogIndoor("r_ddgiFogIndoor", "Weather fog density left in fully enclosed air (froxels scale extinction by DDGI sky visibility; 1 = no change, 0 = no fog indoors). Sun shafts are unaffected", 0.0f, 0.0f, 1.0f);
	HVar r_ddgiFogRange("r_ddgiFogRange", "Max distance (m) at which froxels consult the DDGI probes for indoor fog (beyond: open air, saves cost)", 64.0f, 0.0f, 400.0f);
	HVar r_ddgiSkySaturation("r_ddgiSkySaturation", "Saturation of the sky light the probes gather (1 = the sky's true blue, 0 = neutral). Art control: at midday the saturated sky fill washed every surface blue; lowering it keeps the fill amount but lets the sun's warmth dominate. 0.6 = 2026-11-24 warm-midday pass", 0.6f, 0.0f, 1.5f);
	HVar r_ddgiAO("r_ddgiAO", "Screen-space AO (HBAO+ plugin) on the DDGI probe term: contact shading the 1 m probe grid cannot resolve (0 = off)", 1.0f, 0.0f, 1.0f);
	HVar r_rtDebug("r_rtDebug", "Ray tracer validation view: 1 raster agreement, 2 instance, 3 normal, 4 albedo, 5 emissive", 0, 0, 5);

	namespace
	{
		ID3D11Device* Dev() { return reinterpret_cast<ID3D11Device*>(g_pEnv->_graphicsDevice->GetNativeDevice()); }
		ID3D11DeviceContext* Ctx() { return reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext()); }

		ID3D11ComputeShader* ComputeOf(const std::shared_ptr<IShader>& shader)
		{
			if (!shader)
				return nullptr;
			auto* stage = shader->GetShaderStage(ShaderStage::ComputeShader);
			return stage ? reinterpret_cast<ID3D11ComputeShader*>(stage->GetNativePtr()) : nullptr;
		}

		int32_t FloorDiv(float v, float s)
		{
			return static_cast<int32_t>(std::floor(v / s));
		}
	}

	DDGI::~DDGI()
	{
		Destroy();
	}

	bool DDGI::IsActive() const
	{
		return r_ddgi._val.b && _ready;
	}

	bool DDGI::WantsRayTracing()
	{
		return r_ddgi._val.b || r_rtDebug._val.i32 > 0;
	}

	void DDGI::ReleaseCascades()
	{
		for (auto& c : _cascades)
		{
			SAFE_RELEASE(c.irradianceSrv); SAFE_RELEASE(c.irradianceUav); SAFE_RELEASE(c.irradiance);
			SAFE_RELEASE(c.depthSrv); SAFE_RELEASE(c.depthUav); SAFE_RELEASE(c.depth);
			SAFE_RELEASE(c.stateSrv); SAFE_RELEASE(c.stateUav); SAFE_RELEASE(c.state);
			SAFE_RELEASE(c.statsSrv); SAFE_RELEASE(c.statsUav); SAFE_RELEASE(c.stats);
			c.probeCount = 0;
		}
		SAFE_RELEASE(_raysSrv); SAFE_RELEASE(_raysUav); SAFE_RELEASE(_rays);
		_rayCapacityProbes = 0;
		_builtDims[0] = _builtDims[1] = _builtDims[2] = 0;
		_ready = false;
	}

	void DDGI::Destroy()
	{
		ReleaseCascades();
		SAFE_RELEASE(_constantBuffer);
		SAFE_RELEASE(_lightSrv);
		SAFE_RELEASE(_lightBuffer);
		_lightCapacity = 0;
		SAFE_RELEASE(_pointSampler);
		SAFE_RELEASE(_skyShSrv);
		_skyShIdentity = nullptr;
		SAFE_RELEASE(_rtDebugConstants);
		SAFE_DELETE(_ssgiRT);
		SAFE_DELETE(_ssgiBlurRT);
		SAFE_DELETE(_aoRT);
		_traceShader.reset();
		_blendIrrShader.reset();
		_blendDepthShader.reset();
		_applyShader.reset();
		_ssgiShader.reset();
		_ssgiBlurShader.reset();
		_rtDebugShader.reset();
		_rt.Destroy();
		_resourcesFailed = false;
	}

	bool DDGI::CreateStructured(uint32_t elements, uint32_t stride, ID3D11Buffer*& buffer, ID3D11ShaderResourceView*& srv, ID3D11UnorderedAccessView*& uav)
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = elements * stride;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = stride;
		// Zero-initialised: state.coord.w == 0 marks every probe "never
		// initialised", which the blend passes treat as fresh.
		std::vector<uint8_t> zeros(static_cast<size_t>(elements) * stride, 0u);
		D3D11_SUBRESOURCE_DATA init = {};
		init.pSysMem = zeros.data();
		if (FAILED(Dev()->CreateBuffer(&desc, &init, &buffer)))
			return false;
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.NumElements = elements;
		if (FAILED(Dev()->CreateShaderResourceView(buffer, &srvDesc, &srv)))
			return false;
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.NumElements = elements;
		return SUCCEEDED(Dev()->CreateUnorderedAccessView(buffer, &uavDesc, &uav));
	}

	bool DDGI::EnsureResources()
	{
		if (_resourcesFailed)
			return false;
		if (!_traceShader)
		{
			_traceShader = IShader::Create("EngineData.Shaders/DDGITrace.hcs");
			_blendIrrShader = IShader::Create("EngineData.Shaders/DDGIBlendIrradiance.hcs");
			_blendDepthShader = IShader::Create("EngineData.Shaders/DDGIBlendDepth.hcs");
			_applyShader = IShader::Create("EngineData.Shaders/DDGIApply.hcs");
			_ssgiShader = IShader::Create("EngineData.Shaders/DDGISSGI.hcs");
			_ssgiBlurShader = IShader::Create("EngineData.Shaders/DDGISSGIBlur.hcs");
			_rtDebugShader = IShader::Create("EngineData.Shaders/RtDebug.hcs");
			if (!ComputeOf(_traceShader) || !ComputeOf(_blendIrrShader) || !ComputeOf(_blendDepthShader) || !_applyShader)
			{
				LOG_WARN("DDGI: shaders failed to load - DDGI disabled");
				_resourcesFailed = true;
				return false;
			}
		}
		if (_constantBuffer == nullptr)
		{
			D3D11_BUFFER_DESC desc = {};
			desc.ByteWidth = (sizeof(Constants) + 15u) & ~15u;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			if (FAILED(Dev()->CreateBuffer(&desc, nullptr, &_constantBuffer)))
				return false;
			desc.ByteWidth = 16;
			Dev()->CreateBuffer(&desc, nullptr, &_rtDebugConstants);
		}
		if (_pointSampler == nullptr)
		{
			D3D11_SAMPLER_DESC s = {};
			s.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			s.MaxLOD = D3D11_FLOAT32_MAX;
			Dev()->CreateSamplerState(&s, &_pointSampler);
		}

		const int32_t dims[3] = { r_ddgiProbesXZ._val.i32, r_ddgiProbesY._val.i32, r_ddgiProbesXZ._val.i32 };
		if (dims[0] != _builtDims[0] || dims[1] != _builtDims[1] || dims[2] != _builtDims[2])
		{
			ReleaseCascades();
			const uint32_t probes = static_cast<uint32_t>(dims[0] * dims[1] * dims[2]);
			for (auto& c : _cascades)
			{
				if (!CreateStructured(probes * kIrrTile * kIrrTile, 8u, c.irradiance, c.irradianceSrv, c.irradianceUav) ||
					!CreateStructured(probes * kDepthTile * kDepthTile, 4u, c.depth, c.depthSrv, c.depthUav) ||
					!CreateStructured(probes, 32u, c.state, c.stateSrv, c.stateUav) ||
					!CreateStructured(probes, 8u, c.stats, c.statsSrv, c.statsUav))
				{
					LOG_WARN("DDGI: failed to allocate probe buffers");
					ReleaseCascades();
					_resourcesFailed = true;
					return false;
				}
				std::memcpy(c.dims, dims, sizeof(dims));
				c.probeCount = probes;
			}
			// Ray buffer sized for the largest single dispatch (period 1).
			_rayCapacityProbes = probes;
			if (!CreateStructured(probes * kRaysPerProbe, 16u, _rays, _raysSrv, _raysUav))
			{
				ReleaseCascades();
				_resourcesFailed = true;
				return false;
			}
			std::memcpy(_builtDims, dims, sizeof(dims));
		}
		return true;
	}

	ID3D11ShaderResourceView* DDGI::GetSkyShSrv(ITexture2D* skySH)
	{
		const void* identity = (skySH != nullptr) ? skySH->GetNativePtr() : nullptr;
		if (identity == _skyShIdentity)
			return _skyShSrv;
		SAFE_RELEASE(_skyShSrv);
		_skyShIdentity = identity;
		if (identity != nullptr)
		{
			auto* tex = reinterpret_cast<ID3D11Texture2D*>(const_cast<void*>(identity));
			Dev()->CreateShaderResourceView(tex, nullptr, &_skyShSrv);
		}
		return _skyShSrv;
	}

	namespace
	{
		// A light inside its own fixture (a floor lamp's PointLight child sits
		// inside the shade mesh) would be fully occluded in the probe trace:
		// every shadow ray toward it hits the shade. The raster shadow maps do
		// not see the shade that way, so the room is lit on screen while the
		// probes saw nothing. Shadow rays for a light therefore skip its DIRECT
		// PARENT's mesh instance (RtScene tags instances with entity index + 1),
		// but only for fixture-sized parents: a light parented to a whole
		// building mesh must keep that building's walls as occluders.
		constexpr float kMaxFixtureExtent = 2.5f; // m, largest world AABB dimension (a floor lamp is ~2.1 m tall)
		uint32_t FixtureKey(Entity* lightEntity)
		{
			Entity* parent = lightEntity != nullptr ? lightEntity->GetParent() : nullptr;
			if (parent == nullptr || parent->IsPendingDeletion() || parent->GetComponent<StaticMeshComponent>() == nullptr)
				return 0u;
			const auto& box = parent->GetWorldAABB();
			const float maxSize = 2.0f * std::max({ box.Extents.x, box.Extents.y, box.Extents.z });
			if (!(maxSize > 0.0f) || maxSize > kMaxFixtureExtent)
				return 0u;
			return parent->GetId().index + 1u;
		}

		void SetFixtureKey(float params[4], uint32_t key)
		{
			std::memcpy(&params[3], &key, sizeof(key));
		}
	}

	void DDGI::UpdateLights(Scene* scene, const math::Vector3& center, float radius)
	{
		struct Candidate
		{
			GpuLight light;
			float dist2;
		};
		std::vector<Candidate> candidates;
		const bool physical = r_physicalLightUnits._val.b;

		std::vector<PointLight*> points;
		scene->GetComponents<PointLight>(points);
		for (PointLight* l : points)
		{
			if (l == nullptr || !l->GetInjectIntoGI() || l->GetEntity() == nullptr || l->GetEntity()->IsPendingDeletion())
				continue;
			const auto diffuse = l->GetDiffuseColour();
			if (diffuse.w <= 0.0f)
				continue;
			const math::Vector3 pos = l->GetEntity()->GetWorldTM().Translation();
			const float range = std::max(0.01f, l->GetRadius());
			const float d = (pos - center).Length();
			if (d > radius + range)
				continue;
			// Same strength path as SceneRenderer::RenderPointLights.
			float strength = std::max(0.0f, l->GetLightStrength() * l->GetLightMultiplier());
			if (physical)
				strength /= 4.0f * 3.14159265f;
			Candidate c = {};
			c.light.positionRange[0] = pos.x; c.light.positionRange[1] = pos.y; c.light.positionRange[2] = pos.z; c.light.positionRange[3] = range;
			c.light.colourIntensity[0] = diffuse.x * strength; c.light.colourIntensity[1] = diffuse.y * strength; c.light.colourIntensity[2] = diffuse.z * strength;
			c.light.params[0] = 0.0f;
			SetFixtureKey(c.light.params, FixtureKey(l->GetEntity()));
			c.dist2 = d * d;
			candidates.push_back(c);
		}

		std::vector<SpotLight*> spots;
		scene->GetComponents<SpotLight>(spots);
		for (SpotLight* l : spots)
		{
			if (l == nullptr || !l->GetInjectIntoGI() || l->GetEntity() == nullptr || l->GetEntity()->IsPendingDeletion())
				continue;
			const auto diffuse = l->GetDiffuseColour();
			if (diffuse.w <= 0.0f)
				continue;
			const math::Matrix world = l->GetEntity()->GetWorldTM();
			const math::Vector3 pos = world.Translation();
			const float range = std::max(0.01f, l->GetRadius());
			const float d = (pos - center).Length();
			if (d > radius + range)
				continue;
			math::Vector3 dir = world.Forward();
			if (dir.LengthSquared() > 1e-8f)
				dir.Normalize();
			const float cosOuter = std::cos(l->GetOuterConeAngle() * 0.5f * 3.14159265f / 180.0f);
			const float cosInner = std::cos(l->GetInnerConeAngle() * 0.5f * 3.14159265f / 180.0f);
			// Same strength path as SceneRenderer::RenderSpotLights.
			float strength = std::max(0.0f, l->GetLightStrength() * l->GetLightMultiplier());
			if (physical)
				strength /= std::max(2.0f * 3.14159265f * (1.0f - cosOuter), 1e-4f);
			Candidate c = {};
			c.light.positionRange[0] = pos.x; c.light.positionRange[1] = pos.y; c.light.positionRange[2] = pos.z; c.light.positionRange[3] = range;
			c.light.directionCone[0] = dir.x; c.light.directionCone[1] = dir.y; c.light.directionCone[2] = dir.z; c.light.directionCone[3] = cosOuter;
			c.light.colourIntensity[0] = diffuse.x * strength; c.light.colourIntensity[1] = diffuse.y * strength; c.light.colourIntensity[2] = diffuse.z * strength;
			c.light.params[0] = 1.0f;
			c.light.params[1] = std::max(cosInner, cosOuter + 1e-3f);
			SetFixtureKey(c.light.params, FixtureKey(l->GetEntity()));
			c.dist2 = d * d;
			candidates.push_back(c);
		}

		std::vector<AreaLight*> areas;
		scene->GetComponents<AreaLight>(areas);
		for (AreaLight* l : areas)
		{
			if (l == nullptr || !l->GetInjectIntoGI() || l->GetEntity() == nullptr || l->GetEntity()->IsPendingDeletion())
				continue;
			const auto diffuse = l->GetDiffuseColour();
			float strength = std::max(0.0f, l->GetLightStrength() * l->GetLightMultiplier());
			if (diffuse.w <= 0.0f || strength <= 0.0f)
				continue;
			math::Vector3 centre, halfW, halfH, facing;
			l->GetWorldAxes(centre, halfW, halfH, facing);
			const float range = std::max({ 0.01f, l->GetRadius(), l->GetBoundingRadius() });
			const float d = (centre - center).Length();
			if (d > radius + range)
				continue;
			const bool isRect = l->GetShape() == AreaLight::Shape::Rect;
			// Same lumens -> intensity as ClusteredLighting (tube isotropic 4pi;
			// rect a cosine emitter over one hemisphere, half per side if two-sided).
			if (physical)
				strength /= isRect ? (l->GetTwoSided() ? 2.0f * 3.14159265f : 3.14159265f) : 4.0f * 3.14159265f;
			if (facing.LengthSquared() > 1e-8f)
				facing.Normalize();
			Candidate c = {};
			c.light.positionRange[0] = centre.x; c.light.positionRange[1] = centre.y; c.light.positionRange[2] = centre.z; c.light.positionRange[3] = range;
			c.light.directionCone[0] = facing.x; c.light.directionCone[1] = facing.y; c.light.directionCone[2] = facing.z;
			c.light.colourIntensity[0] = diffuse.x * strength; c.light.colourIntensity[1] = diffuse.y * strength; c.light.colourIntensity[2] = diffuse.z * strength;
			c.light.params[0] = isRect ? 2.0f : 0.0f;
			c.light.params[2] = l->GetTwoSided() ? 1.0f : 0.0f;
			SetFixtureKey(c.light.params, FixtureKey(l->GetEntity()));
			c.dist2 = d * d;
			candidates.push_back(c);
		}

		std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.dist2 < b.dist2; });
		const uint32_t count = std::min<uint32_t>(static_cast<uint32_t>(candidates.size()), static_cast<uint32_t>(std::max(0, r_ddgiMaxLights._val.i32)));
		_lightCount = count;
		const uint32_t capacity = std::max<uint32_t>(count, 16u);
		if (_lightBuffer == nullptr || _lightCapacity < capacity)
		{
			SAFE_RELEASE(_lightSrv);
			SAFE_RELEASE(_lightBuffer);
			D3D11_BUFFER_DESC desc = {};
			desc.ByteWidth = capacity * sizeof(GpuLight);
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			desc.StructureByteStride = sizeof(GpuLight);
			if (FAILED(Dev()->CreateBuffer(&desc, nullptr, &_lightBuffer)))
			{
				_lightCount = 0;
				return;
			}
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = DXGI_FORMAT_UNKNOWN;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			srvDesc.Buffer.NumElements = capacity;
			Dev()->CreateShaderResourceView(_lightBuffer, &srvDesc, &_lightSrv);
			_lightCapacity = capacity;
		}
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (count > 0 && SUCCEEDED(Ctx()->Map(_lightBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			auto* dst = reinterpret_cast<GpuLight*>(mapped.pData);
			for (uint32_t i = 0; i < count; ++i)
				dst[i] = candidates[i].light;
			Ctx()->Unmap(_lightBuffer, 0);
		}
	}

	void DDGI::FillConstants(uint32_t cascade, uint32_t period, uint32_t phase, uint32_t groups)
	{
		for (uint32_t c = 0; c < kCascades; ++c)
		{
			const Cascade& cc = _cascades[c];
			auto& dst = _constants.cascade[c];
			dst.gridOrigin[0] = cc.origin[0]; dst.gridOrigin[1] = cc.origin[1]; dst.gridOrigin[2] = cc.origin[2]; dst.gridOrigin[3] = 0;
			dst.dims[0] = cc.dims[0]; dst.dims[1] = cc.dims[1]; dst.dims[2] = cc.dims[2]; dst.dims[3] = static_cast<int32_t>(cc.probeCount);
			dst.spacing[0] = cc.spacing;
			dst.spacing[1] = std::min(200.0f, cc.spacing * r_ddgiRayLength._val.f32);
			dst.spacing[2] = 1.0f / cc.spacing;
			dst.spacing[3] = 0.0f;
		}

		// Fresh random rotation every dispatch so successive updates of a
		// probe sample new directions (uniform random quaternion).
		static std::mt19937 rng(0x5eedu);
		std::uniform_real_distribution<float> uni(0.0f, 1.0f);
		const float u1 = uni(rng), u2 = uni(rng), u3 = uni(rng);
		const float s1 = std::sqrt(1.0f - u1), s2 = std::sqrt(u1);
		const float qx = s1 * std::sin(6.2831853f * u2), qy = s1 * std::cos(6.2831853f * u2);
		const float qz = s2 * std::sin(6.2831853f * u3), qw = s2 * std::cos(6.2831853f * u3);
		const float r[9] = {
			1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy - qz * qw), 2 * (qx * qz + qy * qw),
			2 * (qx * qy + qz * qw), 1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz - qx * qw),
			2 * (qx * qz - qy * qw), 2 * (qy * qz + qx * qw), 1 - 2 * (qx * qx + qy * qy) };
		_constants.rayRotation0[0] = r[0]; _constants.rayRotation0[1] = r[1]; _constants.rayRotation0[2] = r[2]; _constants.rayRotation0[3] = 0.0f;
		_constants.rayRotation1[0] = r[3]; _constants.rayRotation1[1] = r[4]; _constants.rayRotation1[2] = r[5]; _constants.rayRotation1[3] = 0.0f;
		_constants.rayRotation2[0] = r[6]; _constants.rayRotation2[1] = r[7]; _constants.rayRotation2[2] = r[8]; _constants.rayRotation2[3] = 0.0f;

		_constants.update[0] = cascade;
		_constants.update[1] = period;
		_constants.update[2] = phase;
		_constants.update[3] = groups;
		_constants.params0[0] = r_ddgiHysteresis._val.f32;
		_constants.params0[1] = r_ddgiNormalBias._val.f32;
		_constants.params0[2] = r_ddgiViewBias._val.f32;
		_constants.params0[3] = std::min(r_ddgiBounce._val.f32, 0.98f);
		_constants.params1[0] = r_ddgiSky._val.f32;
		_constants.params1[1] = r_ddgiEmissive._val.f32;
		_constants.params1[2] = static_cast<float>(_lightCount);
		_constants.params1[3] = r_ddgiSun._val.f32;
		_constants.params2[0] = r_ddgiDepthSharpness._val.f32;
		_constants.params2[1] = r_ddgiIntensity._val.f32;
		_constants.params2[2] = static_cast<float>(r_ddgiDebug._val.i32);
		_constants.params2[3] = r_ddgiCascadeBlend._val.f32;
		_constants.params3[0] = r_ddgiLocal._val.f32;
		_constants.params3[1] = r_ddgiRelocate._val.b ? 1.0f : 0.0f;
		_constants.params3[2] = r_ddgiClassify._val.b ? 1.0f : 0.0f;
		_constants.params3[3] = static_cast<float>(_frame & 0xffffu);
		_constants.ground[0] = _groundHeight;
		_constants.ground[1] = (r_ddgiGroundPlane._val.b && _groundValid) ? 1.0f : 0.0f;
		_constants.ground[2] = _aoStrength; // DDGIApply: AO on the probe term (t7)
		_constants.ground[3] = static_cast<float>(r_ddgiAdaptive._val.i32); // DDGIBlendIrradiance change detector
		const float ga = r_ddgiGroundAlbedo._val.f32;
		_constants.groundAlbedo[0] = ga * 0.95f;
		_constants.groundAlbedo[1] = ga * 1.10f;
		_constants.groundAlbedo[2] = ga * 0.80f;
		_constants.groundAlbedo[3] = r_ddgiSSGIBlur._val.f32; // DDGISSGIBlur kernel stride (0 = off)
		_constants.ssgi[0] = r_ddgiSSGI._val.b ? r_ddgiSSGIIntensity._val.f32 : 0.0f;
		_constants.ssgi[1] = r_ddgiSSGIRadius._val.f32;
		_constants.ssgi[2] = r_ddgiSSGIMaxLuma._val.f32;
		_constants.ssgi[3] = static_cast<float>(r_ddgiSSGITaps._val.i32);
		_constants.params4[0] = r_ddgiSampleJitter._val.f32;
		_constants.params4[1] = r_ddgiFogIndoor._val.f32;   // VolumetricScatterDensity
		_constants.params4[2] = r_ddgiFogRange._val.f32;
		_constants.params4[3] = r_ddgiSkySaturation._val.f32; // sky fill saturation (DDGICommon DdgiSkyTint)
		Ctx()->UpdateSubresource(_constantBuffer, 0, nullptr, &_constants, 0, 0);
	}

	void DDGI::UpdateProbes(Scene* scene, Camera* camera, ITexture2D* skySH)
	{
		auto* ctx = Ctx();
		const math::Vector3 cam = camera->GetEntity()->GetPosition();

		float spacing = r_ddgiSpacing._val.f32;
		for (uint32_t c = 0; c < kCascades; ++c)
		{
			Cascade& cc = _cascades[c];
			cc.spacing = spacing;
			cc.origin[0] = FloorDiv(cam.x, spacing) - cc.dims[0] / 2;
			cc.origin[1] = FloorDiv(cam.y, spacing) - cc.dims[1] / 2;
			cc.origin[2] = FloorDiv(cam.z, spacing) - cc.dims[2] / 2;
			spacing *= r_ddgiCascadeScale._val.f32;
		}
		// Ground-plane height from a physics ray straight down (terrain
		// colliders exist even though the GPU-surface terrain has no mesh).
		if ((_frame % 30u) == 1u || !_groundValid)
		{
			RayCastOptions opts;
			opts.maxDistance = 1000.0f;
			opts.meshes = false;
			RayHit hit;
			if (PhysUtils::RayCast(scene, cam, math::Vector3(0.0f, -1.0f, 0.0f), opts, &hit))
			{
				_groundHeight = hit.position.y;
				_groundValid = true;
			}
		}
		const float outerRadius = _cascades[kCascades - 1].spacing * static_cast<float>(_cascades[kCascades - 1].dims[0]) * 0.5f;
		UpdateLights(scene, cam, outerRadius);

		ID3D11Buffer* perFrame = nullptr;
		if (auto* cb = g_pEnv->_graphicsDevice->GetEngineConstantBuffer(EngineConstantBuffer::PerFrameBuffer); cb != nullptr)
			perFrame = reinterpret_cast<ID3D11Buffer*>(cb->GetNativePtr());

		ID3D11ShaderResourceView* nullSrv[16] = {};
		ID3D11UnorderedAccessView* nullUav[3] = {};
		const uint32_t periods[kCascades] = {
			static_cast<uint32_t>(std::max(1, r_ddgiPeriod0._val.i32)),
			static_cast<uint32_t>(std::max(1, r_ddgiPeriod1._val.i32)),
			static_cast<uint32_t>(std::max(1, r_ddgiPeriod2._val.i32)) };

		for (uint32_t c = 0; c < kCascades; ++c)
		{
			Cascade& cc = _cascades[c];
			const uint32_t period = periods[c];
			const uint32_t phase = static_cast<uint32_t>(_frame % period);
			const uint32_t groups = (cc.probeCount + period - 1u) / period;
			if (groups == 0 || groups > _rayCapacityProbes)
				continue;
			FillConstants(c, period, phase, groups);

			ctx->CSSetConstantBuffers(0, 1, &perFrame);
			ctx->CSSetConstantBuffers(8, 1, &_constantBuffer);
			ctx->CSSetSamplers(2, 1, &_pointSampler);

			// --- Trace ---------------------------------------------------
			{
				GPU_SCOPE("DDGI.Trace");
				_rt.BindCS(ctx);
				ID3D11ShaderResourceView* probeSrvs[11] = {
					_cascades[0].irradianceSrv, _cascades[1].irradianceSrv, _cascades[2].irradianceSrv,
					_cascades[0].depthSrv, _cascades[1].depthSrv, _cascades[2].depthSrv,
					_cascades[0].stateSrv, _cascades[1].stateSrv, _cascades[2].stateSrv,
					GetSkyShSrv(skySH), _lightSrv };
				ctx->CSSetShaderResources(56, 11, probeSrvs);
				ctx->CSSetUnorderedAccessViews(0, 1, &_raysUav, nullptr);
				ctx->CSSetShader(ComputeOf(_traceShader), nullptr, 0);
				ctx->Dispatch(groups, 1, 1);
				ctx->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);
				ctx->CSSetShaderResources(52, 15, nullSrv);
			}

			// --- Blend irradiance (reads pre-update state) -----------------
			{
				GPU_SCOPE("DDGI.Blend");
				ID3D11ShaderResourceView* stateSrvs[3] = { _cascades[0].stateSrv, _cascades[1].stateSrv, _cascades[2].stateSrv };
				ctx->CSSetShaderResources(62, 3, stateSrvs);
				ctx->CSSetShaderResources(67, 1, &_raysSrv);
				ID3D11UnorderedAccessView* irrUavs[2] = { cc.irradianceUav, cc.statsUav };
				ctx->CSSetUnorderedAccessViews(0, 2, irrUavs, nullptr);
				ctx->CSSetShader(ComputeOf(_blendIrrShader), nullptr, 0);
				ctx->Dispatch(groups, 1, 1);
				ctx->CSSetUnorderedAccessViews(0, 2, nullUav, nullptr);
				ctx->CSSetShaderResources(62, 3, nullSrv);

				// --- Blend depth + relocate + write state ------------------
				ID3D11UnorderedAccessView* depthUavs[3] = { nullptr, cc.depthUav, cc.stateUav };
				ctx->CSSetUnorderedAccessViews(0, 3, depthUavs, nullptr);
				ctx->CSSetShader(ComputeOf(_blendDepthShader), nullptr, 0);
				ctx->Dispatch(groups, 1, 1);
				ctx->CSSetUnorderedAccessViews(0, 3, nullUav, nullptr);
				ctx->CSSetShaderResources(67, 1, nullSrv);
			}
		}
		ctx->CSSetShader(nullptr, nullptr, 0);
		ID3D11Buffer* nullCb = nullptr;
		ctx->CSSetConstantBuffers(8, 1, &nullCb);
	}

	float DDGI::RenderAmbientOcclusion(Camera* camera, const GBuffer& gbuffer, ITexture2D* beautyTarget)
	{
		// The probe grid is 1 m: it cannot shade the contact between a phone
		// box and the pavement, a shelf and the wall. The voxel path got this
		// from voxel AO; DDGI takes it from the screen-space AO plugin.
		//
		// The provider interface only "multiplies AO into a target" (HBAO+
		// output blend MULTIPLY_RGB), so it renders into a white-cleared
		// target here, which leaves exactly the AO term in it. The
		// in-engine fallback provider (DiffuseGIAOProvider) reads the voxel
		// field, which is frozen while DDGI owns GI - never use it.
		const float strength = r_ddgiAO._val.f32;
		auto* provider = g_pEnv->_ssaoProvider;
		if (strength <= 0.0f || provider == nullptr || dynamic_cast<DiffuseGIAOProvider*>(provider) != nullptr ||
			gbuffer.GetDepthBuffer() == nullptr)
			return 0.0f;
		auto* graphics = g_pEnv->_graphicsDevice;
		const int32_t w = beautyTarget->GetWidth();
		const int32_t h = beautyTarget->GetHeight();
		if (_aoRT == nullptr || _aoRT->GetWidth() != w || _aoRT->GetHeight() != h)
		{
			SAFE_DELETE(_aoRT);
			_aoRT = graphics->CreateTexture2D(
				w, h, DXGI_FORMAT_R8G8B8A8_UNORM, 1,
				D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 1, 1, 0, nullptr,
				(D3D11_CPU_ACCESS_FLAG)0, D3D11_RTV_DIMENSION_TEXTURE2D, D3D11_UAV_DIMENSION_UNKNOWN,
				D3D11_SRV_DIMENSION_TEXTURE2D, D3D11_DSV_DIMENSION_UNKNOWN, D3D11_USAGE_DEFAULT, 0);
			if (_aoRT == nullptr)
				return 0.0f;
			_aoRT->SetDebugName("DDGI_AO");
		}
		GPU_SCOPE("DDGI.AO");
		graphics->UnbindAllPixelShaderResources();
		_aoRT->ClearRenderTargetView(math::Color(1, 1, 1, 1));
		provider->ApplyAmbientOcclusion(camera, gbuffer.GetDepthBuffer(), nullptr, _aoRT);
		return strength;
	}

	void DDGI::Apply(const GBuffer& gbuffer, ITexture2D* beautyTarget)
	{
		auto* gui = g_pEnv->GetUIManager().GetRenderer();
		if (gui == nullptr || !_applyShader)
			return;
		GPU_SCOPE("DDGI.Apply");
		auto* ctx = Ctx();
		auto* graphics = g_pEnv->_graphicsDevice;

		const int32_t w = beautyTarget->GetWidth();
		const int32_t h = beautyTarget->GetHeight();

		const BlendState previous = graphics->GetBlendState();
		graphics->SetBlendState(BlendState::Opaque);
		ctx->PSSetConstantBuffers(8, 1, &_constantBuffer);

		// --- Near-field SSGI, half resolution ---------------------------
		// Full-res it cost ~0.35 ms per tap at 1440p; half-res gathers a
		// quarter of the pixels and Apply upsamples with a depth-aware
		// filter.
		const bool ssgi = _constants.ssgi[0] > 0.0001f && _ssgiShader != nullptr;
		ITexture2D* ssgiOut = nullptr; // what Apply reads: blurred if the denoise ran
		if (ssgi)
		{
			GPU_SCOPE("DDGI.SSGI");
			const int32_t hw = (w + 1) / 2;
			const int32_t hh = (h + 1) / 2;
			if (_ssgiRT == nullptr || _ssgiRT->GetWidth() != hw || _ssgiRT->GetHeight() != hh)
			{
				SAFE_DELETE(_ssgiRT);
				_ssgiRT = graphics->CreateTexture2D(
					hw, hh, DXGI_FORMAT_R16G16B16A16_FLOAT, 1,
					D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 1, 1, 0, nullptr,
					(D3D11_CPU_ACCESS_FLAG)0, D3D11_RTV_DIMENSION_TEXTURE2D, D3D11_UAV_DIMENSION_UNKNOWN,
					D3D11_SRV_DIMENSION_TEXTURE2D, D3D11_DSV_DIMENSION_UNKNOWN, D3D11_USAGE_DEFAULT, 0);
				if (_ssgiRT != nullptr)
					_ssgiRT->SetDebugName("DDGI_SSGIHalf");
			}
			if (_ssgiRT != nullptr)
			{
				graphics->SetRenderTarget(_ssgiRT);
				D3D11_VIEWPORT hvp = {};
				hvp.Width = static_cast<float>(hw);
				hvp.Height = static_cast<float>(hh);
				hvp.MaxDepth = 1.0f;
				graphics->SetViewport(hvp);
				graphics->UnbindAllPixelShaderResources();
				gbuffer.BindAsShaderResource();
				graphics->SetTexture2D(5, beautyTarget);
				gui->StartFrame();
				gui->FullScreenTexturedQuad(nullptr, _ssgiShader.get());
				gui->EndFrame();
				graphics->UnbindAllPixelShaderResources();
				ssgiOut = _ssgiRT;

				// Edge-aware denoise (same viewport, half res). The gather is
				// 16 rotated taps per pixel and fizzes on its own.
				if (r_ddgiSSGIBlur._val.f32 > 0.0f && _ssgiBlurShader != nullptr)
				{
					if (_ssgiBlurRT == nullptr || _ssgiBlurRT->GetWidth() != hw || _ssgiBlurRT->GetHeight() != hh)
					{
						SAFE_DELETE(_ssgiBlurRT);
						_ssgiBlurRT = graphics->CreateTexture2D(
							hw, hh, DXGI_FORMAT_R16G16B16A16_FLOAT, 1,
							D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 1, 1, 0, nullptr,
							(D3D11_CPU_ACCESS_FLAG)0, D3D11_RTV_DIMENSION_TEXTURE2D, D3D11_UAV_DIMENSION_UNKNOWN,
							D3D11_SRV_DIMENSION_TEXTURE2D, D3D11_DSV_DIMENSION_UNKNOWN, D3D11_USAGE_DEFAULT, 0);
						if (_ssgiBlurRT != nullptr)
							_ssgiBlurRT->SetDebugName("DDGI_SSGIBlur");
					}
					if (_ssgiBlurRT != nullptr)
					{
						graphics->SetRenderTarget(_ssgiBlurRT);
						D3D11_VIEWPORT bvp = {};
						bvp.Width = static_cast<float>(hw);
						bvp.Height = static_cast<float>(hh);
						bvp.MaxDepth = 1.0f;
						graphics->SetViewport(bvp);
						gbuffer.BindAsShaderResource();
						graphics->SetTexture2D(6, _ssgiRT);
						gui->StartFrame();
						gui->FullScreenTexturedQuad(nullptr, _ssgiBlurShader.get());
						gui->EndFrame();
						graphics->UnbindAllPixelShaderResources();
						ssgiOut = _ssgiBlurRT;
					}
				}
			}
		}

		// --- Apply: straight into beauty --------------------------------
		// Additive (ONE/ONE, destination alpha kept) in normal mode; debug
		// views replace beauty (opaque). The apply shader does not read the
		// lit beauty (only the SSGI gather above does), so it can render into
		// it directly.
		//
		// Do NOT route this through an intermediate target +
		// ITexture2D::BlendTo_Additive: that draws with the GUI renderer's
		// basic UI shader, which on an HDR back buffer (UIBasicHDR) applies
		// pow(saturate(x), 2.2) - it clipped every indirect value above 1 and
		// crushed dim bounce toward black (a 1x apply added ~1/10th of the
		// light the debug view showed).
		graphics->SetRenderTarget(beautyTarget);
		D3D11_VIEWPORT vp = {};
		vp.Width = static_cast<float>(w);
		vp.Height = static_cast<float>(h);
		vp.MaxDepth = 1.0f;
		graphics->SetViewport(vp);
		graphics->UnbindAllPixelShaderResources();
		gbuffer.BindAsShaderResource();
		if (ssgi && ssgiOut != nullptr)
			graphics->SetTexture2D(6, ssgiOut);
		if (_aoStrength > 0.0f && _aoRT != nullptr)
			graphics->SetTexture2D(7, _aoRT);
		ID3D11ShaderResourceView* probeSrvs[9] = {
			_cascades[0].irradianceSrv, _cascades[1].irradianceSrv, _cascades[2].irradianceSrv,
			_cascades[0].depthSrv, _cascades[1].depthSrv, _cascades[2].depthSrv,
			_cascades[0].stateSrv, _cascades[1].stateSrv, _cascades[2].stateSrv };
		ctx->PSSetShaderResources(56, 9, probeSrvs);
		// t65: sky SH for the beyond-the-probe-volume fallback.
		ctx->PSSetShaderResources(65, 1, &_skyShSrv);
		graphics->SetBlendState(r_ddgiDebug._val.i32 > 0 ? BlendState::Opaque : BlendState::Additive);
		gui->StartFrame();
		gui->FullScreenTexturedQuad(nullptr, _applyShader.get());
		gui->EndFrame();

		ID3D11ShaderResourceView* nullSrv[10] = {};
		ctx->PSSetShaderResources(56, 10, nullSrv);
		ID3D11Buffer* nullCb = nullptr;
		ctx->PSSetConstantBuffers(8, 1, &nullCb);
		graphics->UnbindAllPixelShaderResources();
		graphics->SetBlendState(previous);
	}

	void DDGI::BindForCS() const
	{
		if (g_pEnv == nullptr || g_pEnv->_graphicsDevice == nullptr || g_pEnv->_graphicsDevice->GetBackend() != GraphicsBackend::D3D11)
			return;
		auto* ctx = Ctx();
		if (!IsActive() || _constantBuffer == nullptr)
		{
			ID3D11Buffer* nullCb = nullptr;
			ctx->CSSetConstantBuffers(8, 1, &nullCb);
			return;
		}
		ID3D11ShaderResourceView* probeSrvs[9] = {
			_cascades[0].irradianceSrv, _cascades[1].irradianceSrv, _cascades[2].irradianceSrv,
			_cascades[0].depthSrv, _cascades[1].depthSrv, _cascades[2].depthSrv,
			_cascades[0].stateSrv, _cascades[1].stateSrv, _cascades[2].stateSrv };
		ctx->CSSetShaderResources(56, 9, probeSrvs);
		ctx->CSSetConstantBuffers(8, 1, &_constantBuffer);
	}

	void DDGI::UnbindCS() const
	{
		if (g_pEnv == nullptr || g_pEnv->_graphicsDevice == nullptr || g_pEnv->_graphicsDevice->GetBackend() != GraphicsBackend::D3D11)
			return;
		auto* ctx = Ctx();
		ID3D11ShaderResourceView* nullSrv[9] = {};
		ctx->CSSetShaderResources(56, 9, nullSrv);
		ID3D11Buffer* nullCb = nullptr;
		ctx->CSSetConstantBuffers(8, 1, &nullCb);
	}

	void DDGI::BindForReflectionPS() const
	{
		if (g_pEnv == nullptr || g_pEnv->_graphicsDevice == nullptr || g_pEnv->_graphicsDevice->GetBackend() != GraphicsBackend::D3D11)
			return;
		auto* ctx = Ctx();
		if (!IsActive() || _constantBuffer == nullptr)
		{
			ID3D11Buffer* nullCb = nullptr;
			ctx->PSSetConstantBuffers(8, 1, &nullCb);
			return;
		}
		ID3D11ShaderResourceView* probeSrvs[9] = {
			_cascades[0].irradianceSrv, _cascades[1].irradianceSrv, _cascades[2].irradianceSrv,
			_cascades[0].depthSrv, _cascades[1].depthSrv, _cascades[2].depthSrv,
			_cascades[0].stateSrv, _cascades[1].stateSrv, _cascades[2].stateSrv };
		ctx->PSSetShaderResources(56, 9, probeSrvs);
		ctx->PSSetConstantBuffers(8, 1, &_constantBuffer);
	}

	void DDGI::DrawRtDebug(const GBuffer& gbuffer, ITexture2D* beautyTarget)
	{
		auto* gui = g_pEnv->GetUIManager().GetRenderer();
		if (gui == nullptr || !_rtDebugShader || _rtDebugConstants == nullptr)
			return;
		GPU_SCOPE("RT.Debug");
		auto* ctx = Ctx();
		auto* graphics = g_pEnv->_graphicsDevice;
		const float params[4] = { static_cast<float>(r_rtDebug._val.i32), 0.0f, 0.0f, 0.0f };
		ctx->UpdateSubresource(_rtDebugConstants, 0, nullptr, params, 0, 0);

		graphics->SetRenderTarget(beautyTarget);
		D3D11_VIEWPORT vp = {};
		vp.Width = static_cast<float>(beautyTarget->GetWidth());
		vp.Height = static_cast<float>(beautyTarget->GetHeight());
		vp.MaxDepth = 1.0f;
		graphics->SetViewport(vp);
		graphics->UnbindAllPixelShaderResources();
		gbuffer.BindAsShaderResource();
		_rt.BindPS(ctx);
		ctx->PSSetConstantBuffers(8, 1, &_rtDebugConstants);
		const BlendState previous = graphics->GetBlendState();
		graphics->SetBlendState(BlendState::Opaque);
		gui->StartFrame();
		gui->FullScreenTexturedQuad(nullptr, _rtDebugShader.get());
		gui->EndFrame();
		graphics->SetBlendState(previous);
		RtScene::UnbindPS(ctx);
		graphics->UnbindAllPixelShaderResources();
	}

	void DDGI::Render(Scene* scene, Camera* camera, const GBuffer& gbuffer, ITexture2D* beautyTarget, ITexture2D* skySH)
	{
		_ready = false;
		if (scene == nullptr || camera == nullptr || beautyTarget == nullptr || camera->GetEntity() == nullptr ||
			g_pEnv->_graphicsDevice->GetBackend() != GraphicsBackend::D3D11)
			return;
		if (!WantsRayTracing())
			return;
		if (!EnsureResources())
			return;

		++_frame;
		const bool traceable = _rt.Update(scene, camera->GetEntity()->GetPosition());
		if (r_gpuTimings._val.b && (_frame % 120u) == 0u)
		{
			const auto& st = _rt.GetStats();
			LOG_INFO("RtScene: blas=%u tris=%u nodes=%u instances=%u pending=%u tlasRebuilds=%u tlasCpuMs=%.2f blasCpuMs=%.2f gpuMB=%.1f lights=%u",
				st.blasCount, st.blasTriangles, st.blasNodes, st.instances, st.pendingBlas, st.tlasRebuilds,
				st.lastTlasBuildMs, st.lastBlasBuildMs, static_cast<double>(st.gpuBytes) / (1024.0 * 1024.0), _lightCount);
		}
		if (!traceable)
			return;

		if (r_ddgi._val.b)
		{
			GPU_SCOPE("DDGI");
			_aoStrength = RenderAmbientOcclusion(camera, gbuffer, beautyTarget);
			UpdateProbes(scene, camera, skySH);
			Apply(gbuffer, beautyTarget);
			_ready = true;
		}
		if (r_rtDebug._val.i32 > 0)
			DrawRtDebug(gbuffer, beautyTarget);
	}
}
