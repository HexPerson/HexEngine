#include "DiffuseGI.hpp"

#include "../HexEngine.hpp"
#include "../Entity/Component/Camera.hpp"
#include "../Entity/Component/DirectionalLight.hpp"
#include "../Entity/Component/PointLight.hpp"
#include "../Entity/Component/SpotLight.hpp"
#include "../Entity/Component/StaticMeshComponent.hpp"
#include "../Entity/Component/Transform.hpp"
#include "../Graphics/DebugRenderer.hpp"
#include "../Graphics/Material.hpp"
#include "../Graphics/RenderStructs.hpp"
#include "../GUI/GuiRenderer.hpp"
#include "Scene.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_set>

namespace HexEngine
{
	HVar r_giEnable("r_giEnable", "Enable runtime diffuse global illumination", true, false, true);
	HVar r_giQuality("r_giQuality", "GI quality preset (0 = low, 1 = medium, 2 = high)", 2, 0, 2);
	HVar r_giHalfRes("r_giHalfRes", "Run GI trace pass at half resolution", true, false, true);
	HVar r_giMovementPreset("r_giMovementPreset", "GI movement stability preset (0=off, 1=stable, 2=ultra stable)", 0, 0, 2);
	HVar r_giProbeBudget("r_giProbeBudget", "Maximum dynamic probe/object updates per frame", 64, 4, 4096);
	HVar r_giRaysPerProbe("r_giRaysPerProbe", "Logical rays-per-probe budget hint", 1, 1, 8);
	HVar r_giHysteresis("r_giHysteresis", "Temporal history blend amount for GI resolve", 0.72f, 0.0f, 0.99f);
	HVar r_giHistoryReject("r_giHistoryReject", "Velocity threshold for GI history rejection", 0.006f, 0.0001f, 1.0f);
	HVar r_giJitterScale("r_giJitterScale", "World-space jitter scale for clipmap sampling", 0.45f, 0.0f, 1.5f);
	HVar r_giClipBlendWidth("r_giClipBlendWidth", "Width used to blend overlapping clipmaps", 0.50f, 0.01f, 0.95f);
	HVar r_giResolvePixelMotionStart("r_giResolvePixelMotionStart", "Pixel-motion threshold where GI history rejection begins", 1.25f, 0.0f, 8.0f);
	HVar r_giResolvePixelMotionStrength("r_giResolvePixelMotionStrength", "Strength of pixel-motion GI history rejection", 0.30f, 0.0f, 2.0f);
	HVar r_giResolveLumaReject("r_giResolveLumaReject", "Luminance-delta multiplier for GI history rejection", 1.0f, 0.0f, 8.0f);
	HVar r_giResolveDitherDark("r_giResolveDitherDark", "Dither amplitude on dark GI resolve regions", 0.0020f, 0.0f, 0.01f);
	HVar r_giResolveDitherBright("r_giResolveDitherBright", "Dither amplitude on bright GI resolve regions", 0.0008f, 0.0f, 0.01f);
	HVar r_giEnergyClamp("r_giEnergyClamp", "Maximum GI radiance contribution before clamp", 1.25f, 0.1f, 32.0f);
	HVar r_giIntensity("r_giIntensity", "Final GI intensity multiplier", 0.85f, 0.0f, 8.0f);
	HVar r_giSunInjection("r_giSunInjection", "Sunlight energy injected into the GI voxel clipmaps", 0.25f, 0.0f, 8.0f);
	HVar r_giSunDirectionalBoost("r_giSunDirectionalBoost", "Directional boost applied to sun-facing GI injection", 2.0f, 0.0f, 8.0f);
	HVar r_giSunDirectionality("r_giSunDirectionality", "Directional transport/shadowing strength for sun GI", 0.85f, 0.0f, 1.0f);
	HVar r_giDiffuseInjection("r_giDiffuseInjection", "Diffuse albedo energy injected into GI voxels", 0.08f, 0.0f, 4.0f);
	HVar r_giUnlitAlbedoInjection("r_giUnlitAlbedoInjection", "Baseline diffuse albedo injection independent of direct lighting", 0.0f, 0.0f, 1.0f);
	HVar r_giAlbedoBleedBoost("r_giAlbedoBleedBoost", "Boost for albedo-colored diffuse bounce injection", 3.0f, 0.0f, 12.0f);
	HVar r_giColourBleedStrength("r_giColourBleedStrength", "Extra multiplier for saturated color transfer (red/green/blue bleed)", 1.0f, 0.0f, 4.0f);
	HVar r_giEmissiveInjection("r_giEmissiveInjection", "Emissive energy injected into GI voxels", 0.75f, 0.0f, 16.0f);
	HVar r_giLocalLightInjection("r_giLocalLightInjection", "Point/spot light energy injected into GI voxels", 2.5f, 0.0f, 32.0f);
	HVar r_giDebugDisableLocalLightInjection("r_giDebugDisableLocalLightInjection", "Debug: disable local point/spot light injection into GI", false, false, true);
	HVar r_giDebugDisableBaseAndSunInjection("r_giDebugDisableBaseAndSunInjection", "Debug: disable base diffuse/emissive and sun GI injection", false, false, true);
	HVar r_giDebugDisableBaseInjection("r_giDebugDisableBaseInjection", "Debug: disable base diffuse/emissive GI injection only", false, false, true);
	HVar r_giDebugDisableSunInjection("r_giDebugDisableSunInjection", "Debug: disable sun GI injection only", false, false, true);
	HVar r_giMeshBaseInjectionNormalization("r_giMeshBaseInjectionNormalization", "Normalize base GI injection by sampled triangle count per mesh", 1.0f, 0.0f, 1.0f);
	HVar r_giMeshSunInjectionNormalization("r_giMeshSunInjectionNormalization", "Normalize sun GI injection by sampled triangle count per mesh", 0.0f, 0.0f, 1.0f);
	HVar r_giMeshBaseInjectionMinScale("r_giMeshBaseInjectionMinScale", "Minimum per-mesh scale after base GI normalization", 0.02f, 0.0f, 1.0f);
	HVar r_giMeshSunInjectionMinScale("r_giMeshSunInjectionMinScale", "Minimum per-mesh scale after sun GI normalization", 0.20f, 0.0f, 1.0f);
	HVar r_giLocalLightMaxPerMesh("r_giLocalLightMaxPerMesh", "Maximum local point/spot lights considered per mesh for GI injection", 20, 1, 64);
	HVar r_giLocalLightBaseSuppression("r_giLocalLightBaseSuppression", "How strongly local-light influence suppresses neutral/sun GI base around the light", 0.85f, 0.0f, 1.0f);
	HVar r_giLocalLightSunSuppression("r_giLocalLightSunSuppression", "How strongly local-light influence suppresses sun GI injection on the same triangles", 1.0f, 0.0f, 1.0f);
	HVar r_giLocalLightAlbedoWeight("r_giLocalLightAlbedoWeight", "How strongly local-light GI is tinted by receiver albedo (0=preserve light colour)", 0.0f, 0.0f, 1.0f);
	HVar r_giLocalLightsOnlyDebug("r_giLocalLightsOnlyDebug", "Debug mode: inject only local point/spot light bounce into GI", false, false, true);
	HVar r_giBaseSunSmallTriangleDamp("r_giBaseSunSmallTriangleDamp", "Damp base/sun GI injection from tiny triangles to avoid local over-injection hotspots", 0.85f, 0.0f, 1.0f);
	HVar r_giProbeGatherBoost("r_giProbeGatherBoost", "Probe raymarch energy boost before temporal filtering", 0.90f, 0.1f, 8.0f);
	HVar r_giScreenBounce("r_giScreenBounce", "Screen-space diffuse bounce assist intensity", 0.0f, 0.0f, 2.0f);
	HVar r_giUseTextureTint("r_giUseTextureTint", "Use albedo texture readback to tint GI injection (cached per material+UV subset)", true, false, true);
	HVar r_giGpuVoxelize("r_giGpuVoxelize", "Use GPU voxelization for clipmap radiance updates", true, false, true);
	HVar r_giGpuCandidateGen("r_giGpuCandidateGen", "Use GPU append-buffer candidate generation before voxel injection", false, false, true);
	HVar r_giGpuMaterialEval("r_giGpuMaterialEval", "Use GPU-side local-light evaluation during voxel injection", false, false, true);
	HVar r_giGpuMaterialEvalMaxLights("r_giGpuMaterialEvalMaxLights", "Maximum local GI lights uploaded/evaluated in GPU material eval mode", 24, 1, 64);
	HVar r_giGpuMaterialProxyBlend("r_giGpuMaterialProxyBlend", "Blend amount for GPU material proxy albedo in eval path (0=triangle albedo, 1=material proxy)", 0.15f, 0.0f, 1.0f);
	HVar r_giGpuComputeBaseSun("r_giGpuComputeBaseSun", "Compute base diffuse/sun/emissive GI injection in GPU eval path", false, false, true);
	HVar r_giGpuCompareMode("r_giGpuCompareMode", "CPU/GPU compare mode (0=off,1=log counters,2=verbose counters)", 0, 0, 2);
	HVar r_giTelemetry("r_giTelemetry", "Log GI stage telemetry counters", false, false, true);
	HVar r_giTelemetryLogFrames("r_giTelemetryLogFrames", "How often GI telemetry is logged (frames)", 120, 10, 2000);
	HVar r_giUseProbes("r_giUseProbes", "Use probe atlas contribution in GI trace (expensive CPU path)", false, false, true);
	HVar r_giVoxelDecay("r_giVoxelDecay", "Temporal decay applied to voxel radiance each frame", 0.965f, 0.5f, 0.999f);
	HVar r_giVoxelNeighbourBlend("r_giVoxelNeighbourBlend", "Blend factor for neighbouring voxel smoothing in GI trace", 0.25f, 0.0f, 1.0f);
	HVar r_giVoxelAlbedoInfluence("r_giVoxelAlbedoInfluence", "How strongly voxelized albedo tints GI bounce (0=energy only, 1=full albedo tint)", 1.0f, 0.0f, 1.0f);
	HVar r_giVoxelTriangleBudget("r_giVoxelTriangleBudget", "Maximum triangles injected into GPU voxel clipmap per update", 24000, 256, 300000);
	HVar r_giTriangleCacheFrames("r_giTriangleCacheFrames", "How many frames GI reuses cached voxel triangle lists before rebuilding", 10, 1, 120);
	HVar r_giDebugView("r_giDebugView", "GI debug view (0=off, 1=indirect, 2=probes, 3=voxel, 4=clipmap)", 0, 0, 4);
	HVar r_giVoxelResolution("r_giVoxelResolution", "Per-clipmap voxel resolution", 128, 16, 256);
	HVar r_giClipmapBaseExtent("r_giClipmapBaseExtent", "Half-extent of first GI clipmap in world units", 56.0f, 16.0f, 4096.0f);
}

namespace
{
	static float ElapsedMs(const std::chrono::high_resolution_clock::time_point& start)
	{
		using namespace std::chrono;
		return duration<float, std::milli>(high_resolution_clock::now() - start).count();
	}

	struct MaterialUvRect
	{
		float uMin = 0.0f;
		float vMin = 0.0f;
		float uMax = 1.0f;
		float vMax = 1.0f;
	};

	static MaterialUvRect ResolveMaterialUvRect(const HexEngine::StaticMeshComponent* meshComponent)
	{
		MaterialUvRect rect = {};
		if (meshComponent == nullptr)
			return rect;

		const auto mesh = meshComponent->GetMesh();
		if (!mesh)
			return rect;

		const auto& vertices = mesh->GetVertices();
		if (vertices.empty())
			return rect;

		const math::Vector2 uvScale = meshComponent->GetUVScale();
		float minU = std::numeric_limits<float>::infinity();
		float minV = std::numeric_limits<float>::infinity();
		float maxU = -std::numeric_limits<float>::infinity();
		float maxV = -std::numeric_limits<float>::infinity();

		for (const auto& vertex : vertices)
		{
			const float u = vertex._texcoord.x * uvScale.x;
			const float v = vertex._texcoord.y * uvScale.y;
			if (!std::isfinite(u) || !std::isfinite(v))
				continue;
			minU = std::min(minU, u);
			minV = std::min(minV, v);
			maxU = std::max(maxU, u);
			maxV = std::max(maxV, v);
		}

		if (!std::isfinite(minU) || !std::isfinite(minV) || !std::isfinite(maxU) || !std::isfinite(maxV))
			return rect;

		const bool usesOutOfRangeUv = (minU < 0.0f) || (minV < 0.0f) || (maxU > 1.0f) || (maxV > 1.0f);
		if (usesOutOfRangeUv)
		{
			// Wrapped/tiled UVs imply the whole texture can contribute.
			return rect;
		}

		rect.uMin = std::clamp(minU, 0.0f, 1.0f);
		rect.vMin = std::clamp(minV, 0.0f, 1.0f);
		rect.uMax = std::clamp(maxU, 0.0f, 1.0f);
		rect.vMax = std::clamp(maxV, 0.0f, 1.0f);
		if (rect.uMax < rect.uMin)
			std::swap(rect.uMax, rect.uMin);
		if (rect.vMax < rect.vMin)
			std::swap(rect.vMax, rect.vMin);

		return rect;
	}

	static uint16_t QuantizeUvToU16(float uv)
	{
		const float clamped = std::clamp(uv, 0.0f, 1.0f);
		return static_cast<uint16_t>(std::lround(clamped * 65535.0f));
	}

	static DXGI_FORMAT ResolveShadowSrvFormat(DXGI_FORMAT format)
	{
		switch (format)
		{
		case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
		case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_UNORM;
		default: return format;
		}
	}

	static bool SphereIntersectsAabb(const math::Vector3& center, float radius, const math::Vector3& aabbMin, const math::Vector3& aabbMax)
	{
		const math::Vector3 clamped(
			std::clamp(center.x, aabbMin.x, aabbMax.x),
			std::clamp(center.y, aabbMin.y, aabbMax.y),
			std::clamp(center.z, aabbMin.z, aabbMax.z));
		const math::Vector3 delta = center - clamped;
		return delta.LengthSquared() <= radius * radius;
	}

	static ID3D11ShaderResourceView* CreateShadowMapSrv(ID3D11Device* device, HexEngine::ITexture2D* depthTexture)
	{
		if (device == nullptr || depthTexture == nullptr || depthTexture->GetNativePtr() == nullptr)
			return nullptr;

		auto* texture = reinterpret_cast<ID3D11Texture2D*>(depthTexture->GetNativePtr());
		D3D11_TEXTURE2D_DESC textureDesc = {};
		texture->GetDesc(&textureDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = ResolveShadowSrvFormat(textureDesc.Format);
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;

		ID3D11ShaderResourceView* srv = nullptr;
		if (FAILED(device->CreateShaderResourceView(texture, &srvDesc, &srv)))
			return nullptr;

		return srv;
	}

	static void BuildSunShadowCasterBuffer(HexEngine::DirectionalLight* sun, HexEngine::PerShadowCasterBuffer& outBuffer)
	{
		outBuffer = {};
		if (sun == nullptr)
			return;

		const int32_t cascadeCount = std::clamp<int32_t>(sun->GetMaxSupportedShadowCascades(), 0, 6);
		for (int32_t i = 0; i < cascadeCount; ++i)
		{
			outBuffer._lightProjectionMatrix[i] = sun->GetProjectionMatrix(i).Transpose();
			outBuffer._lightViewMatrix[i] = sun->GetViewMatrix(i).Transpose();
			outBuffer._lightViewProjectionMatrix[i] = (sun->GetViewMatrix(i) * sun->GetProjectionMatrix(i)).Transpose();
		}

		if (auto* sunEntity = sun->GetEntity(); sunEntity != nullptr)
		{
			outBuffer._shadowCasterLightDir = sunEntity->GetWorldTM().Forward();
		}
		else
		{
			outBuffer._shadowCasterLightDir = math::Vector3(0.0f, -1.0f, 0.0f);
		}

		if (auto* shadowMap = sun->GetShadowMap(0); shadowMap != nullptr)
		{
			outBuffer._shadowConfig.shadowMapSize = shadowMap->GetViewport().width;
		}
		outBuffer._shadowConfig.passIndex = 0;
		outBuffer._shadowConfig.cascadeOverride = -1;
		outBuffer._shadowConfig.samples = 0.0f;
	}

	static uint8_t ToUNorm8(float value)
	{
		value = std::clamp(value, 0.0f, 1.0f);
		return static_cast<uint8_t>(value * 255.0f + 0.5f);
	}

	static math::Vector3 ComputeSunTint(HexEngine::Scene* scene)
	{
		if (scene == nullptr)
			return math::Vector3(0.2f, 0.22f, 0.25f);

		math::Vector3 base(scene->GetAmbientColour().x, scene->GetAmbientColour().y, scene->GetAmbientColour().z);
		base.x = std::max(base.x, 0.05f);
		base.y = std::max(base.y, 0.05f);
		base.z = std::max(base.z, 0.05f);

		auto* sun = scene->GetSunLight();
		if (sun == nullptr)
			return base;
		if (!sun->GetInjectIntoGI())
			return base;

		const auto sunColour = sun->GetDiffuseColour();
		const float sunStrength = std::max(0.0f, sun->GetLightMultiplier() * sun->GetLightStrength());
		const math::Vector3 scaledSun(sunColour.x, sunColour.y, sunColour.z);
		return base + scaledSun * (0.10f * sunStrength);
	}

	static math::Vector3 ComputeSunDirectionWS(HexEngine::Scene* scene)
	{
		math::Vector3 sunDirection(0.0f, -1.0f, 0.0f);
		if (scene == nullptr)
			return sunDirection;

		if (auto* sun = scene->GetSunLight(); sun != nullptr)
		{
			if (!sun->GetInjectIntoGI())
				return sunDirection;

			if (auto* sunEntity = sun->GetEntity(); sunEntity != nullptr)
			{
				if (auto* sunTransform = sunEntity->GetComponent<HexEngine::Transform>(); sunTransform != nullptr)
				{
					sunDirection = sunTransform->GetForward();
					if (sunDirection.LengthSquared() > 1e-6f)
						sunDirection.Normalize();
					else
						sunDirection = math::Vector3(0.0f, -1.0f, 0.0f);
				}
			}
		}

		return sunDirection;
	}

	static uint64_t HashMix64(uint64_t state, uint64_t v)
	{
		state ^= v + 0x9e3779b97f4a7c15ull + (state << 6) + (state >> 2);
		return state;
	}

	static uint64_t HashFloatBits(float value)
	{
		uint32_t bits = 0u;
		std::memcpy(&bits, &value, sizeof(float));
		return static_cast<uint64_t>(bits);
	}

	static uint64_t HashQuantizedFloat(float value, float step)
	{
		if (step <= 0.0f)
			return HashFloatBits(value);
		const float q = std::round(value / step) * step;
		return HashFloatBits(q);
	}

	static uint64_t ComputeInjectLightSignature(HexEngine::Scene* scene)
	{
		if (scene == nullptr)
			return 0ull;

		uint64_t h = 1469598103934665603ull;

		if (auto* sun = scene->GetSunLight(); sun != nullptr)
		{
			h = HashMix64(h, sun->GetInjectIntoGI() ? 1ull : 0ull);
			if (sun->GetInjectIntoGI())
			{
				const auto d = sun->GetDiffuseColour();
				// Sun direction changes are handled by dedicated relight logic; avoid
				// signature churn from tiny float jitter by hashing only quantized color/intensity.
				h = HashMix64(h, HashQuantizedFloat(d.x, 1.0f / 255.0f));
				h = HashMix64(h, HashQuantizedFloat(d.y, 1.0f / 255.0f));
				h = HashMix64(h, HashQuantizedFloat(d.z, 1.0f / 255.0f));
				h = HashMix64(h, HashQuantizedFloat(d.w, 1.0f / 255.0f));
			}
		}

		std::vector<HexEngine::PointLight*> pointLights;
		scene->GetComponents<HexEngine::PointLight>(pointLights);
		h = HashMix64(h, static_cast<uint64_t>(pointLights.size()));
		for (auto* light : pointLights)
		{
			if (light == nullptr)
				continue;
			auto* e = light->GetEntity();
			if (e == nullptr || e->IsPendingDeletion())
				continue;
			h = HashMix64(h, light->GetInjectIntoGI() ? 0xabcdu : 0xdef1u);
			if (!light->GetInjectIntoGI())
				continue;
			const auto p = e->GetPosition();
			const auto d = light->GetDiffuseColour();
			h = HashMix64(h, HashQuantizedFloat(p.x, 0.05f));
			h = HashMix64(h, HashQuantizedFloat(p.y, 0.05f));
			h = HashMix64(h, HashQuantizedFloat(p.z, 0.05f));
			h = HashMix64(h, HashQuantizedFloat(light->GetRadius(), 0.05f));
			h = HashMix64(h, HashQuantizedFloat(d.x, 1.0f / 255.0f));
			h = HashMix64(h, HashQuantizedFloat(d.y, 1.0f / 255.0f));
			h = HashMix64(h, HashQuantizedFloat(d.z, 1.0f / 255.0f));
			h = HashMix64(h, HashQuantizedFloat(d.w, 1.0f / 255.0f));
		}

		std::vector<HexEngine::SpotLight*> spotLights;
		scene->GetComponents<HexEngine::SpotLight>(spotLights);
		h = HashMix64(h, static_cast<uint64_t>(spotLights.size()));
		for (auto* light : spotLights)
		{
			if (light == nullptr)
				continue;
			auto* e = light->GetEntity();
			if (e == nullptr || e->IsPendingDeletion())
				continue;
			h = HashMix64(h, light->GetInjectIntoGI() ? 0x1234u : 0x5678u);
			if (!light->GetInjectIntoGI())
				continue;
			const auto p = e->GetPosition();
			const auto d = light->GetDiffuseColour();
			const auto f = e->GetWorldTM().Forward();
			h = HashMix64(h, HashQuantizedFloat(p.x, 0.05f));
			h = HashMix64(h, HashQuantizedFloat(p.y, 0.05f));
			h = HashMix64(h, HashQuantizedFloat(p.z, 0.05f));
			h = HashMix64(h, HashQuantizedFloat(f.x, 0.02f));
			h = HashMix64(h, HashQuantizedFloat(f.y, 0.02f));
			h = HashMix64(h, HashQuantizedFloat(f.z, 0.02f));
			h = HashMix64(h, HashQuantizedFloat(light->GetRadius(), 0.05f));
			h = HashMix64(h, HashQuantizedFloat(light->GetConeSize(), 0.10f));
			h = HashMix64(h, HashQuantizedFloat(d.x, 1.0f / 255.0f));
			h = HashMix64(h, HashQuantizedFloat(d.y, 1.0f / 255.0f));
			h = HashMix64(h, HashQuantizedFloat(d.z, 1.0f / 255.0f));
			h = HashMix64(h, HashQuantizedFloat(d.w, 1.0f / 255.0f));
		}

		return h;
	}

}

namespace HexEngine
{
	void DiffuseGI::Create(uint32_t width, uint32_t height)
	{
		Destroy();

		_width = width;
		_height = height;
		_halfWidth = std::max(1u, width / 2u);
		_halfHeight = std::max(1u, height / 2u);
		_frameCounter = 0;
		_activeClipmap = 0;
		_resolveStabilityBoost = 0.0f;
		_lastLocalLightsOnlyDebug = r_giLocalLightsOnlyDebug._val.b;
		_lastLocalLightInjection = r_giLocalLightInjection._val.f32;
		_lastLocalLightInjectionEnable = !r_giDebugDisableLocalLightInjection._val.b;
		_lastDisableBaseAndSunInjection = r_giDebugDisableBaseAndSunInjection._val.b;
		_lastDisableBaseInjection = r_giDebugDisableBaseInjection._val.b;
		_lastDisableSunInjection = r_giDebugDisableSunInjection._val.b;
		_lastLocalLightMaxPerMesh = r_giLocalLightMaxPerMesh._val.i32;
		_lastLocalLightBaseSuppression = r_giLocalLightBaseSuppression._val.f32;
		_lastLocalLightSunSuppression = r_giLocalLightSunSuppression._val.f32;
		_lastLocalLightAlbedoWeight = r_giLocalLightAlbedoWeight._val.f32;
		_lastBaseSunSmallTriangleDamp = r_giBaseSunSmallTriangleDamp._val.f32;
		_lastMeshBaseInjectionNormalization = r_giMeshBaseInjectionNormalization._val.f32;
		_lastMeshSunInjectionNormalization = r_giMeshSunInjectionNormalization._val.f32;
		_lastMeshBaseInjectionMinScale = r_giMeshBaseInjectionMinScale._val.f32;
		_lastMeshSunInjectionMinScale = r_giMeshSunInjectionMinScale._val.f32;
		_lastGpuComputeBaseSunEnabled = r_giGpuComputeBaseSun._val.b;
		_lastInjectLightSignature = ComputeInjectLightSignature(g_pEnv ? g_pEnv->_sceneManager->GetCurrentScene().get() : nullptr);
		_lastSunDirection = math::Vector3(0.0f, -1.0f, 0.0f);
		_lastSunDirectionInitialized = false;
		_sunRelightFramesRemaining = 0;
		for (uint32_t i = 0; i < ClipmapCount; ++i)
		{
			_cachedVoxelTriangles[i].clear();
			_cachedVoxelTrianglesValid[i] = false;
			_cachedVoxelTrianglesFrame[i] = 0ull;
			_clipmapWarmFramesRemaining[i] = 0u;
		}
		_meshTracking.clear();
		_materialAlbedoCache.clear();
		_materialTriangleAlbedoCache.clear();
		_giMeshProxies.clear();
		_giMaterialProxies.clear();
		_giLightProxies.clear();
		_giMaterialProxyLookup.clear();
		_stats = {};
		_statsFrameCounter = 0ull;
		for (auto& regions : _dirtyRegions)
		{
			regions.clear();
		}

		_traceShader = IShader::Create("EngineData.Shaders/DiffuseGITrace.hcs");
		_resolveShader = IShader::Create("EngineData.Shaders/DiffuseGIResolve.hcs");
		_fullScreenShader = IShader::Create("EngineData.Shaders/FullScreenQuad.hcs");
		_voxelizeShader = IShader::Create("EngineData.Shaders/DiffuseGIVoxelize.hcs");
		_voxelizeEvalShader = IShader::Create("EngineData.Shaders/DiffuseGIVoxelizeEval.hcs");
		_voxelCandidateShader = IShader::Create("EngineData.Shaders/DiffuseGIBuildCandidates.hcs");
		_voxelClearShader = IShader::Create("EngineData.Shaders/DiffuseGIClearVoxel.hcs");
		_voxelPropagateShader = IShader::Create("EngineData.Shaders/DiffuseGIPropagateVoxel.hcs");
		_voxelShiftShader = IShader::Create("EngineData.Shaders/DiffuseGIShiftVoxel.hcs");
		_constantBuffer = g_pEnv->_graphicsDevice->CreateConstantBuffer(sizeof(GIConstants));
		_voxelShiftConstantBuffer = g_pEnv->_graphicsDevice->CreateConstantBuffer(sizeof(VoxelShiftConstants));

		const DXGI_FORMAT colourFormat = static_cast<DXGI_FORMAT>(g_pEnv->_graphicsDevice->GetDesiredBackBufferFormat());
		const uint32_t halfWidth = r_giHalfRes._val.b ? _halfWidth : _width;
		const uint32_t halfHeight = r_giHalfRes._val.b ? _halfHeight : _height;

		_giHalfRes = g_pEnv->_graphicsDevice->CreateTexture2D(
			static_cast<int32_t>(halfWidth),
			static_cast<int32_t>(halfHeight),
			colourFormat,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			1,
			1,
			0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			D3D11_SRV_DIMENSION_TEXTURE2D,
			D3D11_DSV_DIMENSION_UNKNOWN,
			D3D11_USAGE_DEFAULT,
			0);

		_giResolved = g_pEnv->_graphicsDevice->CreateTexture2D(
			static_cast<int32_t>(_width),
			static_cast<int32_t>(_height),
			colourFormat,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			1,
			1,
			0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			D3D11_SRV_DIMENSION_TEXTURE2D,
			D3D11_DSV_DIMENSION_UNKNOWN,
			D3D11_USAGE_DEFAULT,
			0);

		_giHistory = g_pEnv->_graphicsDevice->CreateTexture2D(
			static_cast<int32_t>(_width),
			static_cast<int32_t>(_height),
			colourFormat,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			1,
			1,
			0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			D3D11_SRV_DIMENSION_TEXTURE2D,
			D3D11_DSV_DIMENSION_UNKNOWN,
			D3D11_USAGE_DEFAULT,
			0);

		if (_giHalfRes)
			_giHalfRes->SetDebugName("GI_HalfRes");
		if (_giResolved)
			_giResolved->SetDebugName("GI_Resolved");
		if (_giHistory)
			_giHistory->SetDebugName("GI_History");

		const bool clipmapsOk = CreateClipmapResources();
		_created =
			(_giHalfRes != nullptr &&
			 _giResolved != nullptr &&
			 _giHistory != nullptr &&
			 _constantBuffer != nullptr &&
			 _traceShader != nullptr &&
			 _resolveShader != nullptr &&
			 _fullScreenShader != nullptr &&
			 _voxelizeShader != nullptr &&
			 _voxelizeEvalShader != nullptr &&
			 _voxelClearShader != nullptr &&
			 _voxelPropagateShader != nullptr &&
			 _voxelShiftShader != nullptr &&
			 _voxelShiftConstantBuffer != nullptr &&
			 clipmapsOk);

		if (!_created)
		{
			LOG_CRIT("DiffuseGI failed to initialize resources. GI will remain disabled until recreated.");
		}
	}

	void DiffuseGI::Destroy()
	{
		DestroyClipmapResources();
		_meshTracking.clear();
		_materialAlbedoCache.clear();
		_materialTriangleAlbedoCache.clear();
		_giMeshProxies.clear();
		_giMaterialProxies.clear();
		_giLightProxies.clear();
		_giMaterialProxyLookup.clear();
		for (auto& regions : _dirtyRegions)
		{
			regions.clear();
		}

		SAFE_DELETE(_giHalfRes);
		SAFE_DELETE(_giResolved);
		SAFE_DELETE(_giHistory);
		SAFE_DELETE(_constantBuffer);
		SAFE_DELETE(_voxelShiftConstantBuffer);
		SAFE_RELEASE(_voxelTriangleSrv);
		SAFE_RELEASE(_voxelTriangleBuffer);
		SAFE_RELEASE(_giLightSrv);
		SAFE_RELEASE(_giLightBuffer);
		SAFE_RELEASE(_giMaterialSrv);
		SAFE_RELEASE(_giMaterialBuffer);
		SAFE_RELEASE(_voxelCandidateSrv);
		SAFE_RELEASE(_voxelCandidateUav);
		SAFE_RELEASE(_voxelCandidateBuffer);
		SAFE_RELEASE(_voxelCandidateCountBuffer);
		SAFE_RELEASE(_voxelCandidateCountReadback);
		SAFE_RELEASE(_voxelCandidateDispatchArgs);
		_voxelTriangleCapacity = 0;
		_giLightCapacity = 0;
		_giMaterialCapacity = 0;
		_voxelCandidateCapacity = 0;
		_voxelTriangleUpload.clear();
		_gpuGiLightUpload.clear();
		_gpuGiMaterialUpload.clear();
		_stats = {};
		_statsFrameCounter = 0ull;

		_traceShader = nullptr;
		_resolveShader = nullptr;
		_fullScreenShader = nullptr;
		_voxelizeShader = nullptr;
		_voxelizeEvalShader = nullptr;
		_voxelCandidateShader = nullptr;
		_voxelClearShader = nullptr;
		_voxelPropagateShader = nullptr;
		_voxelShiftShader = nullptr;
		_created = false;
		_resolveStabilityBoost = 0.0f;
		_lastLocalLightsOnlyDebug = false;
		_lastLocalLightInjection = 1.0f;
		_lastLocalLightInjectionEnable = true;
		_lastDisableBaseAndSunInjection = false;
		_lastDisableBaseInjection = false;
		_lastDisableSunInjection = false;
		_lastLocalLightMaxPerMesh = 20;
		_lastLocalLightBaseSuppression = 0.85f;
		_lastLocalLightSunSuppression = 1.0f;
		_lastLocalLightAlbedoWeight = 0.0f;
		_lastBaseSunSmallTriangleDamp = 0.85f;
		_lastMeshBaseInjectionNormalization = 1.0f;
		_lastMeshSunInjectionNormalization = 0.0f;
		_lastMeshBaseInjectionMinScale = 0.02f;
		_lastMeshSunInjectionMinScale = 0.20f;
		_lastGpuComputeBaseSunEnabled = false;
		_lastInjectLightSignature = 0ull;
		_lastSunDirectionInitialized = false;
		_sunRelightFramesRemaining = 0;
		for (uint32_t i = 0; i < ClipmapCount; ++i)
		{
			_cachedVoxelTriangles[i].clear();
			_cachedVoxelTrianglesValid[i] = false;
			_cachedVoxelTrianglesFrame[i] = 0ull;
			_clipmapWarmFramesRemaining[i] = 0u;
		}
	}

	void DiffuseGI::Resize(uint32_t width, uint32_t height)
	{
		if (!_created)
		{
			Create(width, height);
			return;
		}

		Create(width, height);
	}

	void DiffuseGI::ApplyQualityPreset()
	{
		switch (r_giQuality._val.i32)
		{
		case 0:
			r_giVoxelResolution._val.i32 = std::min(r_giVoxelResolution._val.i32, 32);
			r_giProbeBudget._val.i32 = std::min(r_giProbeBudget._val.i32, 24);
			r_giRaysPerProbe._val.i32 = 1;
			r_giHalfRes._val.b = true;
			break;
		case 1:
			r_giVoxelResolution._val.i32 = std::clamp(r_giVoxelResolution._val.i32, 32, 40);
			r_giProbeBudget._val.i32 = std::clamp(r_giProbeBudget._val.i32, 24, 48);
			r_giRaysPerProbe._val.i32 = std::clamp(r_giRaysPerProbe._val.i32, 1, 2);
			break;
		case 2:
		default:
			r_giVoxelResolution._val.i32 = std::clamp(r_giVoxelResolution._val.i32, 40, 256);
			r_giProbeBudget._val.i32 = std::clamp(r_giProbeBudget._val.i32, 48, 64);
			r_giRaysPerProbe._val.i32 = std::clamp(r_giRaysPerProbe._val.i32, 1, 2);
			break;
		}

		int32_t movementPreset = std::clamp(r_giMovementPreset._val.i32, 0, 2);
		if (movementPreset == 0 && g_pEnv != nullptr && g_pEnv->IsEditorMode())
		{
			// In editor, default to a more stable profile unless the user explicitly picks another mode.
			movementPreset = 1;
		}

		if (movementPreset == 1)
		{
			r_giJitterScale._val.f32 = std::min(r_giJitterScale._val.f32, 0.38f);
			r_giClipBlendWidth._val.f32 = std::max(r_giClipBlendWidth._val.f32, 0.58f);
			r_giResolvePixelMotionStart._val.f32 = std::max(r_giResolvePixelMotionStart._val.f32, 1.6f);
			r_giResolvePixelMotionStrength._val.f32 = std::min(r_giResolvePixelMotionStrength._val.f32, 0.22f);
			r_giResolveLumaReject._val.f32 = std::min(r_giResolveLumaReject._val.f32, 0.85f);
			r_giResolveDitherDark._val.f32 = std::max(r_giResolveDitherDark._val.f32, 0.0022f);
			r_giResolveDitherBright._val.f32 = std::max(r_giResolveDitherBright._val.f32, 0.0010f);
		}
		else if (movementPreset >= 2)
		{
			r_giJitterScale._val.f32 = std::min(r_giJitterScale._val.f32, 0.30f);
			r_giClipBlendWidth._val.f32 = std::max(r_giClipBlendWidth._val.f32, 0.68f);
			r_giResolvePixelMotionStart._val.f32 = std::max(r_giResolvePixelMotionStart._val.f32, 2.0f);
			r_giResolvePixelMotionStrength._val.f32 = std::min(r_giResolvePixelMotionStrength._val.f32, 0.16f);
			r_giResolveLumaReject._val.f32 = std::min(r_giResolveLumaReject._val.f32, 0.65f);
			r_giResolveDitherDark._val.f32 = std::max(r_giResolveDitherDark._val.f32, 0.0028f);
			r_giResolveDitherBright._val.f32 = std::max(r_giResolveDitherBright._val.f32, 0.0014f);
		}
	}

	uint32_t DiffuseGI::GetVoxelResolution() const
	{
		return static_cast<uint32_t>(std::clamp(r_giVoxelResolution._val.i32, 16, 256));
	}

	float DiffuseGI::GetBaseExtent() const
	{
		return std::max(16.0f, r_giClipmapBaseExtent._val.f32);
	}

	uint32_t DiffuseGI::GetFrameBudget() const
	{
		return static_cast<uint32_t>(std::max(4, r_giProbeBudget._val.i32));
	}

	bool DiffuseGI::CreateClipmapResources()
	{
		const uint32_t resolution = GetVoxelResolution();
		const uint32_t probeAtlasWidth = ProbeGridX * ProbeGridZ;
		const uint32_t probeAtlasHeight = ProbeGridY;

		for (uint32_t i = 0; i < ClipmapCount; ++i)
		{
			auto& level = _clipmaps[i];
			level.resolution = resolution;
			level.extent = GetBaseExtent() * std::pow(2.0f, static_cast<float>(i));
			level.dirty = true;
			level.initialized = false;

			level.radianceVolume = g_pEnv->_graphicsDevice->CreateTexture3D(
				static_cast<int32_t>(resolution),
				static_cast<int32_t>(resolution),
				static_cast<int32_t>(resolution),
				DXGI_FORMAT_R32G32B32A32_FLOAT,
				1,
				D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
				1,
				1,
				0,
				nullptr,
				D3D11_RTV_DIMENSION_UNKNOWN,
				D3D11_UAV_DIMENSION_TEXTURE3D,
				D3D11_SRV_DIMENSION_TEXTURE3D,
				D3D11_DSV_DIMENSION_UNKNOWN);

			level.radianceScratchVolume = g_pEnv->_graphicsDevice->CreateTexture3D(
				static_cast<int32_t>(resolution),
				static_cast<int32_t>(resolution),
				static_cast<int32_t>(resolution),
				DXGI_FORMAT_R32G32B32A32_FLOAT,
				1,
				D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
				1,
				1,
				0,
				nullptr,
				D3D11_RTV_DIMENSION_UNKNOWN,
				D3D11_UAV_DIMENSION_TEXTURE3D,
				D3D11_SRV_DIMENSION_TEXTURE3D,
				D3D11_DSV_DIMENSION_UNKNOWN);

			level.albedoVolume = g_pEnv->_graphicsDevice->CreateTexture3D(
				static_cast<int32_t>(resolution),
				static_cast<int32_t>(resolution),
				static_cast<int32_t>(resolution),
				DXGI_FORMAT_R16G16B16A16_FLOAT,
				1,
				D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
				1,
				1,
				0,
				nullptr,
				D3D11_RTV_DIMENSION_UNKNOWN,
				D3D11_UAV_DIMENSION_TEXTURE3D,
				D3D11_SRV_DIMENSION_TEXTURE3D,
				D3D11_DSV_DIMENSION_UNKNOWN);

			level.albedoScratchVolume = g_pEnv->_graphicsDevice->CreateTexture3D(
				static_cast<int32_t>(resolution),
				static_cast<int32_t>(resolution),
				static_cast<int32_t>(resolution),
				DXGI_FORMAT_R16G16B16A16_FLOAT,
				1,
				D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
				1,
				1,
				0,
				nullptr,
				D3D11_RTV_DIMENSION_UNKNOWN,
				D3D11_UAV_DIMENSION_TEXTURE3D,
				D3D11_SRV_DIMENSION_TEXTURE3D,
				D3D11_DSV_DIMENSION_UNKNOWN);

			level.opacityVolume = g_pEnv->_graphicsDevice->CreateTexture3D(
				static_cast<int32_t>(resolution),
				static_cast<int32_t>(resolution),
				static_cast<int32_t>(resolution),
				DXGI_FORMAT_R8_UNORM,
				1,
				D3D11_BIND_SHADER_RESOURCE,
				1,
				1,
				0,
				nullptr,
				D3D11_RTV_DIMENSION_UNKNOWN,
				D3D11_UAV_DIMENSION_UNKNOWN,
				D3D11_SRV_DIMENSION_TEXTURE3D,
				D3D11_DSV_DIMENSION_UNKNOWN);

			level.probeIrradianceAtlas = g_pEnv->_graphicsDevice->CreateTexture2D(
				static_cast<int32_t>(probeAtlasWidth),
				static_cast<int32_t>(probeAtlasHeight),
				DXGI_FORMAT_R32G32B32A32_FLOAT,
				1,
				D3D11_BIND_SHADER_RESOURCE,
				1,
				1,
				0,
				nullptr,
				(D3D11_CPU_ACCESS_FLAG)0,
				D3D11_RTV_DIMENSION_UNKNOWN,
				D3D11_UAV_DIMENSION_UNKNOWN,
				D3D11_SRV_DIMENSION_TEXTURE2D,
				D3D11_DSV_DIMENSION_UNKNOWN,
				D3D11_USAGE_DEFAULT,
				0);

			level.probeVisibilityAtlas = g_pEnv->_graphicsDevice->CreateTexture2D(
				static_cast<int32_t>(probeAtlasWidth),
				static_cast<int32_t>(probeAtlasHeight),
				DXGI_FORMAT_R8_UNORM,
				1,
				D3D11_BIND_SHADER_RESOURCE,
				1,
				1,
				0,
				nullptr,
				(D3D11_CPU_ACCESS_FLAG)0,
				D3D11_RTV_DIMENSION_UNKNOWN,
				D3D11_UAV_DIMENSION_UNKNOWN,
				D3D11_SRV_DIMENSION_TEXTURE2D,
				D3D11_DSV_DIMENSION_UNKNOWN,
				D3D11_USAGE_DEFAULT,
				0);

			level.radianceCpu.resize(static_cast<size_t>(resolution) * resolution * resolution * 4u, 0.0f);
			level.opacityCpu.resize(static_cast<size_t>(resolution) * resolution * resolution, 0u);
			level.probeIrradianceCpu.resize(static_cast<size_t>(probeAtlasWidth) * probeAtlasHeight * 4u, 0.0f);
			level.probeVisibilityCpu.resize(static_cast<size_t>(probeAtlasWidth) * probeAtlasHeight, 0u);

			if (!level.radianceVolume || !level.radianceScratchVolume ||
				!level.albedoVolume || !level.albedoScratchVolume ||
				!level.opacityVolume || !level.probeIrradianceAtlas || !level.probeVisibilityAtlas)
			{
				LOG_CRIT("DiffuseGI failed to allocate one or more clipmap resources.");
				return false;
			}

			auto* device = reinterpret_cast<ID3D11Device*>(g_pEnv->_graphicsDevice->GetNativeDevice());
			if (device != nullptr)
			{
				D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
				uavDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
				uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
				uavDesc.Texture3D.MipSlice = 0;
				uavDesc.Texture3D.FirstWSlice = 0;
				uavDesc.Texture3D.WSize = resolution;
				HRESULT hr = device->CreateUnorderedAccessView(
					reinterpret_cast<ID3D11Texture3D*>(level.radianceVolume->GetNativePtr()),
					&uavDesc,
					&level.radianceUav);
				if (FAILED(hr) || level.radianceUav == nullptr)
				{
					LOG_CRIT("DiffuseGI failed to create radiance UAV for clipmap %u (hr=0x%X).", i, static_cast<uint32_t>(hr));
					return false;
				}

				hr = device->CreateUnorderedAccessView(
					reinterpret_cast<ID3D11Texture3D*>(level.radianceScratchVolume->GetNativePtr()),
					&uavDesc,
					&level.radianceScratchUav);
				if (FAILED(hr) || level.radianceScratchUav == nullptr)
				{
					LOG_CRIT("DiffuseGI failed to create radiance scratch UAV for clipmap %u (hr=0x%X).", i, static_cast<uint32_t>(hr));
					return false;
				}

				D3D11_UNORDERED_ACCESS_VIEW_DESC albedoUavDesc = {};
				albedoUavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
				albedoUavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
				albedoUavDesc.Texture3D.MipSlice = 0;
				albedoUavDesc.Texture3D.FirstWSlice = 0;
				albedoUavDesc.Texture3D.WSize = resolution;
				hr = device->CreateUnorderedAccessView(
					reinterpret_cast<ID3D11Texture3D*>(level.albedoVolume->GetNativePtr()),
					&albedoUavDesc,
					&level.albedoUav);
				if (FAILED(hr) || level.albedoUav == nullptr)
				{
					LOG_CRIT("DiffuseGI failed to create albedo UAV for clipmap %u (hr=0x%X).", i, static_cast<uint32_t>(hr));
					return false;
				}

				hr = device->CreateUnorderedAccessView(
					reinterpret_cast<ID3D11Texture3D*>(level.albedoScratchVolume->GetNativePtr()),
					&albedoUavDesc,
					&level.albedoScratchUav);
				if (FAILED(hr) || level.albedoScratchUav == nullptr)
				{
					LOG_CRIT("DiffuseGI failed to create albedo scratch UAV for clipmap %u (hr=0x%X).", i, static_cast<uint32_t>(hr));
					return false;
				}

				hr = device->CreateShaderResourceView(
					reinterpret_cast<ID3D11Resource*>(level.radianceVolume->GetNativePtr()),
					nullptr,
					&level.radianceSrv);
				if (FAILED(hr) || level.radianceSrv == nullptr)
				{
					LOG_CRIT("DiffuseGI failed to create radiance SRV for clipmap %u (hr=0x%X).", i, static_cast<uint32_t>(hr));
					return false;
				}

				hr = device->CreateShaderResourceView(
					reinterpret_cast<ID3D11Resource*>(level.radianceScratchVolume->GetNativePtr()),
					nullptr,
					&level.radianceScratchSrv);
				if (FAILED(hr) || level.radianceScratchSrv == nullptr)
				{
					LOG_CRIT("DiffuseGI failed to create radiance scratch SRV for clipmap %u (hr=0x%X).", i, static_cast<uint32_t>(hr));
					return false;
				}

				hr = device->CreateShaderResourceView(
					reinterpret_cast<ID3D11Resource*>(level.albedoVolume->GetNativePtr()),
					nullptr,
					&level.albedoSrv);
				if (FAILED(hr) || level.albedoSrv == nullptr)
				{
					LOG_CRIT("DiffuseGI failed to create albedo SRV for clipmap %u (hr=0x%X).", i, static_cast<uint32_t>(hr));
					return false;
				}

				hr = device->CreateShaderResourceView(
					reinterpret_cast<ID3D11Resource*>(level.albedoScratchVolume->GetNativePtr()),
					nullptr,
					&level.albedoScratchSrv);
				if (FAILED(hr) || level.albedoScratchSrv == nullptr)
				{
					LOG_CRIT("DiffuseGI failed to create albedo scratch SRV for clipmap %u (hr=0x%X).", i, static_cast<uint32_t>(hr));
					return false;
				}
			}
		}

		return true;
	}

	void DiffuseGI::DestroyClipmapResources()
	{
		for (auto& level : _clipmaps)
		{
			SAFE_RELEASE(level.radianceUav);
			SAFE_RELEASE(level.radianceScratchUav);
			SAFE_RELEASE(level.albedoUav);
			SAFE_RELEASE(level.albedoScratchUav);
			SAFE_RELEASE(level.radianceSrv);
			SAFE_RELEASE(level.radianceScratchSrv);
			SAFE_RELEASE(level.albedoSrv);
			SAFE_RELEASE(level.albedoScratchSrv);
			SAFE_DELETE(level.radianceVolume);
			SAFE_DELETE(level.radianceScratchVolume);
			SAFE_DELETE(level.albedoVolume);
			SAFE_DELETE(level.albedoScratchVolume);
			SAFE_DELETE(level.opacityVolume);
			SAFE_DELETE(level.probeIrradianceAtlas);
			SAFE_DELETE(level.probeVisibilityAtlas);
			level.radianceCpu.clear();
			level.opacityCpu.clear();
			level.probeIrradianceCpu.clear();
			level.probeVisibilityCpu.clear();
			level.initialized = false;
		}
	}

	void DiffuseGI::RebuildClipmapTransforms(const math::Vector3& cameraPosition)
	{
		const uint32_t resolution = GetVoxelResolution();
		const float baseExtent = GetBaseExtent();

		for (uint32_t i = 0; i < ClipmapCount; ++i)
		{
			auto& level = _clipmaps[i];
			const float extent = baseExtent * std::pow(2.0f, static_cast<float>(i));
			const float voxelSize = (extent * 2.0f) / static_cast<float>(resolution);
			const float snapFactor = (i == 0u) ? 1.0f : ((i == 1u) ? 2.0f : 3.0f);
			const float scrollStep = std::max(voxelSize * snapFactor, 1e-3f);

			const math::Vector3 snapped(
				std::round(cameraPosition.x / scrollStep) * scrollStep,
				std::round(cameraPosition.y / scrollStep) * scrollStep,
				std::round(cameraPosition.z / scrollStep) * scrollStep);
			// Keep sample center (level.center) tied to the actual shifted voxel volume.
			// targetCenter follows camera snapping immediately; pendingShiftWs carries the
			// delta until this clipmap is actually processed.
			if (!level.initialized)
			{
				level.center = snapped;
				level.targetCenter = snapped;
				level.pendingShiftWs = math::Vector3::Zero;
				level.dirty = true;
				_cachedVoxelTrianglesValid[i] = false;
				_cachedVoxelTrianglesFrame[i] = 0ull;
				_clipmapWarmFramesRemaining[i] = (i == 0u) ? 4u : 2u;
			}
			else
			{
				const bool targetChanged = (level.targetCenter - snapped).LengthSquared() > scrollStep * scrollStep * 0.25f;
				if (targetChanged)
				{
					level.targetCenter = snapped;
					level.dirty = true;

					const math::Vector3 pendingShift = level.targetCenter - level.center;
					const float shiftDistance = std::sqrt(pendingShift.LengthSquared());
					const bool largeJump = shiftDistance > extent * 0.20f;

					// Preserve cache on normal scrolling shifts; hard reset only on large jumps.
					if (largeJump)
					{
						_cachedVoxelTrianglesValid[i] = false;
						_cachedVoxelTrianglesFrame[i] = 0ull;
						_clipmapWarmFramesRemaining[i] = (i == 0u) ? 4u : 2u;
					}
					else
					{
						_clipmapWarmFramesRemaining[i] = std::max(_clipmapWarmFramesRemaining[i], (i == 0u) ? 2u : 1u);
					}
				}

				// Always keep pending shift as target-minus-sampled center so skipped clipmaps
				// accumulate full desired displacement without oscillating.
				level.pendingShiftWs = level.targetCenter - level.center;
			}

			level.extent = extent;
		}
	}

	void DiffuseGI::AddDirtyRegion(uint32_t levelIndex, const dx::BoundingBox& bounds)
	{
		if (levelIndex >= ClipmapCount)
			return;

		auto& regions = _dirtyRegions[levelIndex];
		regions.push_back(bounds);

		constexpr size_t MaxDirtyRegions = 128;
		if (regions.size() > MaxDirtyRegions)
		{
			const size_t trim = regions.size() - MaxDirtyRegions;
			regions.erase(regions.begin(), regions.begin() + trim);
		}
	}

	bool DiffuseGI::IsMeshStateDirty(StaticMeshComponent* smc, const math::Vector3& worldPos)
	{
		if (smc == nullptr)
			return false;

		MeshTrackingState state;
		state.position = worldPos;

		if (auto mat = smc->GetMaterial())
		{
			state.emissive = mat->_properties.emissiveColour;
			state.diffuse = mat->_properties.diffuseColour;
		}

		const auto nearlyEqual3 = [](const math::Vector3& a, const math::Vector3& b)
		{
			return (a - b).LengthSquared() <= (0.001f * 0.001f);
		};
		const auto nearlyEqual4 = [](const math::Vector4& a, const math::Vector4& b)
		{
			const math::Vector4 d = a - b;
			const float err = d.x * d.x + d.y * d.y + d.z * d.z + d.w * d.w;
			return err <= (0.0005f * 0.0005f);
		};

		auto it = _meshTracking.find(smc);
		if (it == _meshTracking.end())
		{
			_meshTracking.emplace(smc, state);
			return true;
		}

		const bool dirty =
			!nearlyEqual3(it->second.position, state.position) ||
			!nearlyEqual4(it->second.emissive, state.emissive) ||
			!nearlyEqual4(it->second.diffuse, state.diffuse);

		if (dirty)
		{
			it->second = state;
		}

		return dirty;
	}

	math::Vector3 DiffuseGI::GetMaterialAlbedoTint(const Material* material, const StaticMeshComponent* meshComponent)
	{
		if (material == nullptr)
			return math::Vector3(1.0f, 1.0f, 1.0f);

		const MaterialUvRect uvRect = ResolveMaterialUvRect(meshComponent);
		MaterialAlbedoCacheKey cacheKey = {};
		cacheKey.material = material;
		cacheKey.uMin = QuantizeUvToU16(uvRect.uMin);
		cacheKey.vMin = QuantizeUvToU16(uvRect.vMin);
		cacheKey.uMax = QuantizeUvToU16(uvRect.uMax);
		cacheKey.vMax = QuantizeUvToU16(uvRect.vMax);

		if (auto it = _materialAlbedoCache.find(cacheKey); it != _materialAlbedoCache.end())
			return it->second;

		math::Vector3 tint(
			std::clamp(material->_properties.diffuseColour.x, 0.0f, 1.0f),
			std::clamp(material->_properties.diffuseColour.y, 0.0f, 1.0f),
			std::clamp(material->_properties.diffuseColour.z, 0.0f, 1.0f));

		if (r_giUseTextureTint._val.b)
		{
			if (auto albedo = material->GetTexture(MaterialTexture::Albedo))
			{
				std::vector<uint8_t> pixels;
				albedo->GetPixels(pixels);

				const int32_t texW = std::max(1, albedo->GetWidth());
				const int32_t texH = std::max(1, albedo->GetHeight());
				const size_t minTightSize = static_cast<size_t>(texW) * static_cast<size_t>(texH) * 4u;
				if (pixels.size() >= minTightSize && texW > 0 && texH > 0)
				{
					const DXGI_FORMAT fmt = static_cast<DXGI_FORMAT>(albedo->GetFormat());
					const bool isBgra =
						(fmt == DXGI_FORMAT_B8G8R8A8_UNORM) ||
						(fmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) ||
						(fmt == DXGI_FORMAT_B8G8R8X8_UNORM) ||
						(fmt == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB);
					const int cR = isBgra ? 2 : 0;
					const int cG = 1;
					const int cB = isBgra ? 0 : 2;

					// GetPixels payload is treated as tightly packed base level for tint estimation.
					const int32_t sampleX0 = std::clamp(static_cast<int32_t>(std::floor(uvRect.uMin * static_cast<float>(texW - 1))), 0, texW - 1);
					const int32_t sampleY0 = std::clamp(static_cast<int32_t>(std::floor(uvRect.vMin * static_cast<float>(texH - 1))), 0, texH - 1);
					const int32_t sampleX1 = std::clamp(static_cast<int32_t>(std::ceil(uvRect.uMax * static_cast<float>(texW - 1))), 0, texW - 1);
					const int32_t sampleY1 = std::clamp(static_cast<int32_t>(std::ceil(uvRect.vMax * static_cast<float>(texH - 1))), 0, texH - 1);
					const int32_t regionW = std::max(1, sampleX1 - sampleX0 + 1);
					const int32_t regionH = std::max(1, sampleY1 - sampleY0 + 1);
					const size_t rowPitch = static_cast<size_t>(texW) * 4u;
					const size_t regionPixelCount = static_cast<size_t>(regionW) * static_cast<size_t>(regionH);
					const int32_t axisStep = std::max<int32_t>(1, static_cast<int32_t>(std::sqrt(static_cast<double>(regionPixelCount) / 8192.0)));

					double r = 0.0;
					double g = 0.0;
					double b = 0.0;
					double rs = 0.0;
					double gs = 0.0;
					double bs = 0.0;
					double satWeightSum = 0.0;
					size_t samples = 0u;
					for (int32_t y = sampleY0; y <= sampleY1; y += axisStep)
					{
						for (int32_t x = sampleX0; x <= sampleX1; x += axisStep)
						{
							const size_t idx = static_cast<size_t>(y) * rowPitch + static_cast<size_t>(x) * 4u;
							if (idx + 3u >= pixels.size())
								continue;

							const float srgbR = pixels[idx + static_cast<size_t>(cR)] / 255.0f;
							const float srgbG = pixels[idx + static_cast<size_t>(cG)] / 255.0f;
							const float srgbB = pixels[idx + static_cast<size_t>(cB)] / 255.0f;
							const float linR = static_cast<float>(std::pow(static_cast<double>(srgbR), 2.2));
							const float linG = static_cast<float>(std::pow(static_cast<double>(srgbG), 2.2));
							const float linB = static_cast<float>(std::pow(static_cast<double>(srgbB), 2.2));
							r += linR;
							g += linG;
							b += linB;

							const float maxC = std::max(linR, std::max(linG, linB));
							const float minC = std::min(linR, std::min(linG, linB));
							const float sat = std::max(0.0f, maxC - minC);
							const float satWeight = 0.05f + sat * sat * 4.0f;
							rs += linR * satWeight;
							gs += linG * satWeight;
							bs += linB * satWeight;
							satWeightSum += satWeight;
							++samples;
						}
					}

					if (samples > 0u)
					{
						const float inv = 1.0f / static_cast<float>(samples);
						const math::Vector3 avgTex(
							static_cast<float>(r) * inv,
							static_cast<float>(g) * inv,
							static_cast<float>(b) * inv);
						math::Vector3 satTex = avgTex;
						if (satWeightSum > 1e-5)
						{
							const float satInv = 1.0f / static_cast<float>(satWeightSum);
							satTex = math::Vector3(
								static_cast<float>(rs) * satInv,
								static_cast<float>(gs) * satInv,
								static_cast<float>(bs) * satInv);
						}
						const float avgMax = std::max(avgTex.x, std::max(avgTex.y, avgTex.z));
						const float avgMin = std::min(avgTex.x, std::min(avgTex.y, avgTex.z));
						const float avgSat = std::max(0.0f, avgMax - avgMin);
						const float colourBleedStrength = std::max(0.0f, r_giColourBleedStrength._val.f32);
						const float satMix = std::clamp((avgSat * 3.5f + 0.20f) * colourBleedStrength, 0.0f, 1.0f);
						const math::Vector3 texTint = avgTex * (1.0f - satMix) + satTex * satMix;
						tint = tint * texTint;
					}
				}
			}
		}

		tint.x = std::clamp(tint.x, 0.03f, 1.0f);
		tint.y = std::clamp(tint.y, 0.03f, 1.0f);
		tint.z = std::clamp(tint.z, 0.03f, 1.0f);
		const float tintLuma = tint.x * 0.2126f + tint.y * 0.7152f + tint.z * 0.0722f;
		const float tintMax = std::max(tint.x, std::max(tint.y, tint.z));
		const float tintMin = std::min(tint.x, std::min(tint.y, tint.z));
		const float tintSat = std::max(0.0f, tintMax - tintMin);
		const float colourBleedStrength = std::max(0.0f, r_giColourBleedStrength._val.f32);
		const float satBoostFactor = std::clamp(tintSat * 0.55f * colourBleedStrength, 0.0f, 0.65f);
		tint = math::Vector3(tintLuma, tintLuma, tintLuma) * (1.0f - satBoostFactor) + tint * (1.0f + satBoostFactor);
		tint.x = std::clamp(tint.x, 0.03f, 1.0f);
		tint.y = std::clamp(tint.y, 0.03f, 1.0f);
		tint.z = std::clamp(tint.z, 0.03f, 1.0f);

		_materialAlbedoCache[cacheKey] = tint;
		return tint;
	}

	void DiffuseGI::UpdateProbeAtlases(ClipmapLevel& level)
	{
		const uint32_t probeAtlasWidth = ProbeGridX * ProbeGridZ;
		const uint32_t probeAtlasHeight = ProbeGridY;
		const float probeHistory = level.dirty ? 0.0f : std::clamp(r_giHysteresis._val.f32, 0.0f, 0.95f);

		std::vector<float> previousIrradiance = level.probeIrradianceCpu;
		std::vector<uint8_t> previousVisibility = level.probeVisibilityCpu;
		std::fill(level.probeIrradianceCpu.begin(), level.probeIrradianceCpu.end(), 0.0f);
		std::fill(level.probeVisibilityCpu.begin(), level.probeVisibilityCpu.end(), 0u);

		const uint32_t voxelRes = level.resolution;
		const float scaleX = static_cast<float>(voxelRes - 1) / static_cast<float>(ProbeGridX - 1);
		const float scaleY = static_cast<float>(voxelRes - 1) / static_cast<float>(ProbeGridY - 1);
		const float scaleZ = static_cast<float>(voxelRes - 1) / static_cast<float>(ProbeGridZ - 1);
		const int32_t rayCount = std::clamp(r_giRaysPerProbe._val.i32 * 2, 2, 12);
		const int32_t raySteps = std::clamp(6 + r_giRaysPerProbe._val.i32, 6, 14);

		static const math::Vector3 kRayDirs[] =
		{
			math::Vector3(1.0f, 0.0f, 0.0f),
			math::Vector3(-1.0f, 0.0f, 0.0f),
			math::Vector3(0.0f, 1.0f, 0.0f),
			math::Vector3(0.0f, -1.0f, 0.0f),
			math::Vector3(0.0f, 0.0f, 1.0f),
			math::Vector3(0.0f, 0.0f, -1.0f),
			math::Vector3(0.577f, 0.577f, 0.577f),
			math::Vector3(-0.577f, 0.577f, 0.577f),
			math::Vector3(0.577f, -0.577f, 0.577f),
			math::Vector3(0.577f, 0.577f, -0.577f),
			math::Vector3(-0.707f, 0.0f, 0.707f),
			math::Vector3(0.707f, 0.0f, -0.707f)
		};
		const int32_t maxDirs = static_cast<int32_t>(std::size(kRayDirs));
		auto sampleVoxelTrilinear = [&](const math::Vector3& p, math::Vector3& radiance, float& opacity)
		{
			const float fx = std::clamp(p.x, 0.0f, static_cast<float>(voxelRes - 1u));
			const float fy = std::clamp(p.y, 0.0f, static_cast<float>(voxelRes - 1u));
			const float fz = std::clamp(p.z, 0.0f, static_cast<float>(voxelRes - 1u));

			const uint32_t x0 = static_cast<uint32_t>(std::floor(fx));
			const uint32_t y0 = static_cast<uint32_t>(std::floor(fy));
			const uint32_t z0 = static_cast<uint32_t>(std::floor(fz));
			const uint32_t x1 = std::min<uint32_t>(x0 + 1u, voxelRes - 1u);
			const uint32_t y1 = std::min<uint32_t>(y0 + 1u, voxelRes - 1u);
			const uint32_t z1 = std::min<uint32_t>(z0 + 1u, voxelRes - 1u);

			const float tx = fx - static_cast<float>(x0);
			const float ty = fy - static_cast<float>(y0);
			const float tz = fz - static_cast<float>(z0);

			const auto sampleAt = [&](uint32_t x, uint32_t y, uint32_t z, math::Vector3& outRad, float& outOpacity)
			{
				const size_t idx = (static_cast<size_t>(z) * voxelRes * voxelRes) + (static_cast<size_t>(y) * voxelRes) + x;
				const size_t ridx = idx * 4u;
				outRad = math::Vector3(
					level.radianceCpu[ridx + 0],
					level.radianceCpu[ridx + 1],
					level.radianceCpu[ridx + 2]);
				outOpacity = level.opacityCpu[idx] / 255.0f;
			};

			math::Vector3 c000, c100, c010, c110, c001, c101, c011, c111;
			float o000, o100, o010, o110, o001, o101, o011, o111;
			sampleAt(x0, y0, z0, c000, o000);
			sampleAt(x1, y0, z0, c100, o100);
			sampleAt(x0, y1, z0, c010, o010);
			sampleAt(x1, y1, z0, c110, o110);
			sampleAt(x0, y0, z1, c001, o001);
			sampleAt(x1, y0, z1, c101, o101);
			sampleAt(x0, y1, z1, c011, o011);
			sampleAt(x1, y1, z1, c111, o111);

			const math::Vector3 c00 = c000 * (1.0f - tx) + c100 * tx;
			const math::Vector3 c10 = c010 * (1.0f - tx) + c110 * tx;
			const math::Vector3 c01 = c001 * (1.0f - tx) + c101 * tx;
			const math::Vector3 c11 = c011 * (1.0f - tx) + c111 * tx;
			const math::Vector3 c0 = c00 * (1.0f - ty) + c10 * ty;
			const math::Vector3 c1 = c01 * (1.0f - ty) + c11 * ty;
			radiance = c0 * (1.0f - tz) + c1 * tz;

			const float o00 = o000 * (1.0f - tx) + o100 * tx;
			const float o10 = o010 * (1.0f - tx) + o110 * tx;
			const float o01 = o001 * (1.0f - tx) + o101 * tx;
			const float o11 = o011 * (1.0f - tx) + o111 * tx;
			const float o0 = o00 * (1.0f - ty) + o10 * ty;
			const float o1 = o01 * (1.0f - ty) + o11 * ty;
			opacity = o0 * (1.0f - tz) + o1 * tz;
		};

		for (uint32_t z = 0; z < ProbeGridZ; ++z)
		{
			for (uint32_t y = 0; y < ProbeGridY; ++y)
			{
				for (uint32_t x = 0; x < ProbeGridX; ++x)
				{
					const float vxBase = x * scaleX;
					const float vyBase = y * scaleY;
					const float vzBase = z * scaleZ;
					const math::Vector3 probeVoxel(vxBase, vyBase, vzBase);
					math::Vector3 accumulatedRadiance = math::Vector3::Zero;
					float accumulatedVisibility = 0.0f;

					for (int32_t ray = 0; ray < rayCount; ++ray)
					{
						// Use low-frequency temporal scrambling to avoid high-frequency probe shimmer while moving.
						const uint32_t temporalSlice = static_cast<uint32_t>(_frameCounter >> 3ull);
						const uint32_t scramble = (x * 73856093u) ^ (y * 19349663u) ^ (z * 83492791u) ^ temporalSlice;
						const math::Vector3 rayDir = kRayDirs[(ray + static_cast<int32_t>(scramble % static_cast<uint32_t>(maxDirs))) % maxDirs];
						float transmittance = 1.0f;
						math::Vector3 rayRadiance = math::Vector3::Zero;

						for (int32_t step = 1; step <= raySteps; ++step)
						{
							const math::Vector3 p = probeVoxel + rayDir * static_cast<float>(step);
							float opacity = 0.0f;
							math::Vector3 sampleRadiance = math::Vector3::Zero;
							sampleVoxelTrilinear(p, sampleRadiance, opacity);

							rayRadiance += sampleRadiance * transmittance * (0.40f * r_giProbeGatherBoost._val.f32);
							transmittance *= (1.0f - opacity * 0.60f);
							if (transmittance < 0.05f)
								break;
						}

						accumulatedRadiance += rayRadiance;
						accumulatedVisibility += transmittance;
					}

					const float invRayCount = 1.0f / static_cast<float>(std::max(rayCount, 1));
					accumulatedRadiance *= invRayCount * 1.15f;
					accumulatedVisibility = std::clamp(accumulatedVisibility * invRayCount, 0.0f, 1.0f);

					const uint32_t atlasX = x + z * ProbeGridX;
					const uint32_t atlasY = y;
					const size_t atlasIdx = static_cast<size_t>(atlasY) * probeAtlasWidth + atlasX;
					const size_t atlasRadiance = atlasIdx * 4u;

					const float prevR = previousIrradiance[atlasRadiance + 0];
					const float prevG = previousIrradiance[atlasRadiance + 1];
					const float prevB = previousIrradiance[atlasRadiance + 2];
					const float prevV = previousVisibility[atlasIdx] / 255.0f;

					const float currLum = std::max(0.0f, accumulatedRadiance.x * 0.2126f + accumulatedRadiance.y * 0.7152f + accumulatedRadiance.z * 0.0722f);
					const float prevLum = std::max(0.0f, prevR * 0.2126f + prevG * 0.7152f + prevB * 0.0722f);
					const float deltaLum = std::abs(currLum - prevLum);
					const float deltaNorm = deltaLum / std::max(0.08f, prevLum + currLum + 0.03f);
					const float temporalConfidence =
						std::clamp(
							accumulatedVisibility * 0.55f +
							prevV * 0.25f +
							std::clamp(currLum * 1.05f, 0.0f, 1.0f) * 0.20f,
							0.0f, 1.0f);
					const float rejectFromChange = std::clamp(deltaNorm * (0.45f + (1.0f - temporalConfidence) * 0.25f), 0.0f, 0.75f);
					const float adaptiveHistory = std::max(probeHistory * 0.45f, probeHistory * temporalConfidence * (1.0f - rejectFromChange));

					level.probeIrradianceCpu[atlasRadiance + 0] = std::lerp(accumulatedRadiance.x, prevR, adaptiveHistory);
					level.probeIrradianceCpu[atlasRadiance + 1] = std::lerp(accumulatedRadiance.y, prevG, adaptiveHistory);
					level.probeIrradianceCpu[atlasRadiance + 2] = std::lerp(accumulatedRadiance.z, prevB, adaptiveHistory);
					level.probeIrradianceCpu[atlasRadiance + 3] = 1.0f;
					level.probeVisibilityCpu[atlasIdx] = ToUNorm8(std::lerp(accumulatedVisibility, prevV, adaptiveHistory));
				}
			}
		}

		auto* context = reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext());
		if (!context)
			return;

		if (level.probeIrradianceAtlas)
		{
			context->UpdateSubresource(
				reinterpret_cast<ID3D11Texture2D*>(level.probeIrradianceAtlas->GetNativePtr()),
				0,
				nullptr,
				level.probeIrradianceCpu.data(),
				probeAtlasWidth * 4u * sizeof(float),
				0u);
		}

		if (level.probeVisibilityAtlas)
		{
			context->UpdateSubresource(
				reinterpret_cast<ID3D11Texture2D*>(level.probeVisibilityAtlas->GetNativePtr()),
				0,
				nullptr,
				level.probeVisibilityCpu.data(),
				probeAtlasWidth,
				0u);
		}
	}

	void DiffuseGI::UpdateClipmapData(Scene* scene, uint32_t levelIndex)
	{
		if (scene == nullptr || levelIndex >= ClipmapCount)
			return;

		auto& level = _clipmaps[levelIndex];
		const uint32_t voxelRes = level.resolution;
		const float extent = level.extent;
		const float invExtent = extent > 0.0f ? (1.0f / extent) : 0.0f;
		const math::Vector3 sunTint = ComputeSunTint(scene);
		const bool sunInjectEnabled = scene != nullptr && scene->GetSunLight() != nullptr && scene->GetSunLight()->GetInjectIntoGI();
		const float sunInjectScale = sunInjectEnabled ? 1.0f : 0.0f;
		const uint32_t frameBudget = GetFrameBudget();
		const dx::BoundingBox clipBounds(level.center, math::Vector3(extent, extent, extent));

		const bool bootstrap = !level.initialized;
		const float decay = level.dirty ? 0.9965f : 0.9985f;
		for (size_t i = 0; i < level.opacityCpu.size(); ++i)
		{
			if (bootstrap)
			{
				level.opacityCpu[i] = ToUNorm8(0.01f);
				const size_t c = i * 4u;
				level.radianceCpu[c + 0] = sunTint.x * 0.08f * sunInjectScale;
				level.radianceCpu[c + 1] = sunTint.y * 0.08f * sunInjectScale;
				level.radianceCpu[c + 2] = sunTint.z * 0.08f * sunInjectScale;
				level.radianceCpu[c + 3] = 1.0f;
			}
			else
			{
				level.opacityCpu[i] = static_cast<uint8_t>(std::max(1.0f, level.opacityCpu[i] * decay));
				const size_t c = i * 4u;
				level.radianceCpu[c + 0] = std::max(0.0005f, level.radianceCpu[c + 0] * decay);
				level.radianceCpu[c + 1] = std::max(0.0005f, level.radianceCpu[c + 1] * decay);
				level.radianceCpu[c + 2] = std::max(0.0005f, level.radianceCpu[c + 2] * decay);
				level.radianceCpu[c + 3] = 1.0f;
			}
		}

		if (bootstrap || level.dirty)
		{
			_dirtyRegions[levelIndex].clear();
			AddDirtyRegion(levelIndex, clipBounds);
		}

		std::vector<StaticMeshComponent*> meshesInBounds;
		scene->GatherStaticMeshesInBounds(clipBounds, meshesInBounds, true);

		std::vector<StaticMeshComponent*> prioritized;
		std::vector<StaticMeshComponent*> regular;
		prioritized.reserve(meshesInBounds.size());
		regular.reserve(meshesInBounds.size());

		std::unordered_set<StaticMeshComponent*> visibleSet;
		visibleSet.reserve(meshesInBounds.size());

		for (auto* smc : meshesInBounds)
		{
			if (smc == nullptr || smc->GetMesh() == nullptr)
				continue;

			auto* entity = smc->GetEntity();
			if (entity == nullptr || entity->IsPendingDeletion())
				continue;

			visibleSet.insert(smc);

			const math::Vector3 worldPos = entity->GetWorldTM().Translation();
			const bool meshDirty = IsMeshStateDirty(smc, worldPos);
			if (meshDirty)
			{
				AddDirtyRegion(levelIndex, entity->GetWorldAABB());
			}

			const auto& aabb = entity->GetWorldAABB();
			bool intersectsDirty = meshDirty;
			if (!intersectsDirty)
			{
				for (const auto& dirtyRegion : _dirtyRegions[levelIndex])
				{
					if (dirtyRegion.Intersects(aabb))
					{
						intersectsDirty = true;
						break;
					}
				}
			}

			if (intersectsDirty)
				prioritized.push_back(smc);
			else
				regular.push_back(smc);
		}

		if ((_frameCounter % 64ull) == 0ull)
		{
			for (auto it = _meshTracking.begin(); it != _meshTracking.end();)
			{
				if (visibleSet.find(it->first) == visibleSet.end())
					it = _meshTracking.erase(it);
				else
					++it;
			}
		}

		auto injectMesh = [&](StaticMeshComponent* smc)
		{
			if (smc == nullptr)
				return false;

			Entity* entity = smc->GetEntity();
			if (entity == nullptr || entity->IsPendingDeletion())
				return false;

			const auto& worldAabb = entity->GetWorldOBB();
			const math::Vector3 aabbCenter(worldAabb.Center.x, worldAabb.Center.y, worldAabb.Center.z);
			const math::Vector3 aabbExtents(worldAabb.Extents.x, worldAabb.Extents.y, worldAabb.Extents.z);
			math::Vector3 worldMin = aabbCenter - aabbExtents;
			math::Vector3 worldMax = aabbCenter + aabbExtents;

			const math::Vector3 uvwMin = ((worldMin - level.center) * invExtent) * 0.5f + math::Vector3(0.5f);
			const math::Vector3 uvwMax = ((worldMax - level.center) * invExtent) * 0.5f + math::Vector3(0.5f);

			const math::Vector3 uvwLo(
				std::clamp(std::min(uvwMin.x, uvwMax.x), 0.0f, 1.0f),
				std::clamp(std::min(uvwMin.y, uvwMax.y), 0.0f, 1.0f),
				std::clamp(std::min(uvwMin.z, uvwMax.z), 0.0f, 1.0f));
			const math::Vector3 uvwHi(
				std::clamp(std::max(uvwMin.x, uvwMax.x), 0.0f, 1.0f),
				std::clamp(std::max(uvwMin.y, uvwMax.y), 0.0f, 1.0f),
				std::clamp(std::max(uvwMin.z, uvwMax.z), 0.0f, 1.0f));

			const uint32_t vx0 = std::min<uint32_t>(voxelRes - 1, static_cast<uint32_t>(uvwLo.x * static_cast<float>(voxelRes - 1)));
			const uint32_t vy0 = std::min<uint32_t>(voxelRes - 1, static_cast<uint32_t>(uvwLo.y * static_cast<float>(voxelRes - 1)));
			const uint32_t vz0 = std::min<uint32_t>(voxelRes - 1, static_cast<uint32_t>(uvwLo.z * static_cast<float>(voxelRes - 1)));
			const uint32_t vx1 = std::min<uint32_t>(voxelRes - 1, static_cast<uint32_t>(uvwHi.x * static_cast<float>(voxelRes - 1)));
			const uint32_t vy1 = std::min<uint32_t>(voxelRes - 1, static_cast<uint32_t>(uvwHi.y * static_cast<float>(voxelRes - 1)));
			const uint32_t vz1 = std::min<uint32_t>(voxelRes - 1, static_cast<uint32_t>(uvwHi.z * static_cast<float>(voxelRes - 1)));

			uint32_t step = 1;
			const uint32_t spanX = (vx1 >= vx0) ? (vx1 - vx0 + 1u) : 1u;
			const uint32_t spanY = (vy1 >= vy0) ? (vy1 - vy0 + 1u) : 1u;
			const uint32_t spanZ = (vz1 >= vz0) ? (vz1 - vz0 + 1u) : 1u;
			constexpr uint32_t MaxVoxelWritesPerMesh = 196u;
			while (((spanX + step - 1u) / step) * ((spanY + step - 1u) / step) * ((spanZ + step - 1u) / step) > MaxVoxelWritesPerMesh)
			{
				++step;
			}

			math::Vector3 injection = sunTint * std::max(0.0f, r_giSunInjection._val.f32) * sunInjectScale;
			if (auto mat = smc->GetMaterial())
			{
				const auto emissive = mat->_properties.emissiveColour;
				const math::Vector3 albedoTint = GetMaterialAlbedoTint(mat.get(), smc);
				const float albedoMax = std::max(albedoTint.x, std::max(albedoTint.y, albedoTint.z));
				const float albedoMin = std::min(albedoTint.x, std::min(albedoTint.y, albedoTint.z));
				const float albedoChroma = std::max(0.0f, albedoMax - albedoMin);
				const float colourBleedStrength = std::max(0.0f, r_giColourBleedStrength._val.f32);
				const float colourBleedBoost = std::clamp(1.0f + albedoChroma * colourBleedStrength * 0.8f, 1.0f, 2.0f);
				injection += albedoTint * std::max(0.0f, r_giDiffuseInjection._val.f32) * colourBleedBoost;
				injection += math::Vector3(emissive.x, emissive.y, emissive.z) * std::max(0.0f, emissive.w) * std::max(0.0f, r_giEmissiveInjection._val.f32);
			}

			const float clipAttenuation = 1.0f / (1.0f + 0.20f * static_cast<float>(levelIndex));
			injection *= clipAttenuation;

			for (uint32_t vz = vz0; vz <= vz1; vz += step)
			{
				for (uint32_t vy = vy0; vy <= vy1; vy += step)
				{
					for (uint32_t vx = vx0; vx <= vx1; vx += step)
					{
						const size_t voxelIdx = (static_cast<size_t>(vz) * voxelRes * voxelRes) + (static_cast<size_t>(vy) * voxelRes) + vx;
						const size_t radianceOffset = voxelIdx * 4u;

						level.radianceCpu[radianceOffset + 0] = std::min(32.0f, level.radianceCpu[radianceOffset + 0] + injection.x);
						level.radianceCpu[radianceOffset + 1] = std::min(32.0f, level.radianceCpu[radianceOffset + 1] + injection.y);
						level.radianceCpu[radianceOffset + 2] = std::min(32.0f, level.radianceCpu[radianceOffset + 2] + injection.z);
						level.radianceCpu[radianceOffset + 3] = 1.0f;
						level.opacityCpu[voxelIdx] = std::max<uint8_t>(level.opacityCpu[voxelIdx], ToUNorm8(0.85f));
					}
				}
			}

			return true;
		};

		const uint32_t updateBudget = level.dirty
			? std::min<uint32_t>(frameBudget * 2u, 256u)
			: frameBudget;

		uint32_t processed = 0;
		for (auto* smc : prioritized)
		{
			if (injectMesh(smc) && ++processed >= updateBudget)
				break;
		}

		if (processed < updateBudget)
		{
			for (auto* smc : regular)
			{
				if (injectMesh(smc) && ++processed >= updateBudget)
					break;
			}
		}

		if (!_dirtyRegions[levelIndex].empty())
		{
			const size_t popCount = (processed >= updateBudget) ? 1u : std::min<size_t>(_dirtyRegions[levelIndex].size(), 8u);
			_dirtyRegions[levelIndex].erase(_dirtyRegions[levelIndex].begin(), _dirtyRegions[levelIndex].begin() + popCount);
		}

		for (auto& otherLevel : _dirtyRegions)
		{
			if (otherLevel.size() > 128u)
			{
				otherLevel.erase(otherLevel.begin(), otherLevel.begin() + (otherLevel.size() - 128u));
			}
		}

		auto* context = reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext());
		if (context != nullptr && level.radianceVolume != nullptr && level.opacityVolume != nullptr)
		{
			const uint32_t rowPitchRadiance = voxelRes * 4u * sizeof(float);
			const uint32_t slicePitchRadiance = rowPitchRadiance * voxelRes;
			const uint32_t rowPitchOpacity = voxelRes;
			const uint32_t slicePitchOpacity = rowPitchOpacity * voxelRes;

			context->UpdateSubresource(
				reinterpret_cast<ID3D11Texture3D*>(level.radianceVolume->GetNativePtr()),
				0,
				nullptr,
				level.radianceCpu.data(),
				rowPitchRadiance,
				slicePitchRadiance);

			context->UpdateSubresource(
				reinterpret_cast<ID3D11Texture3D*>(level.opacityVolume->GetNativePtr()),
				0,
				nullptr,
				level.opacityCpu.data(),
				rowPitchOpacity,
				slicePitchOpacity);
		}

		if (r_giUseProbes._val.b)
		{
			UpdateProbeAtlases(level);
		}
		level.initialized = true;
		level.dirty = false;
	}

	void DiffuseGI::UpdateConstants(Scene* scene)
	{
		for (uint32_t i = 0; i < ClipmapCount; ++i)
		{
			const auto& level = _clipmaps[i];
			const float voxelSize = (level.extent * 2.0f) / static_cast<float>(std::max(1u, level.resolution));
			_constants.clipCenterExtent[i] = math::Vector4(level.center.x, level.center.y, level.center.z, level.extent);
			_constants.clipVoxelInfo[i] = math::Vector4(
				voxelSize,
				voxelSize > 0.0f ? 1.0f / voxelSize : 0.0f,
				static_cast<float>(level.resolution),
				static_cast<float>(std::max(1, r_giRaysPerProbe._val.i32)));
		}

		_constants.params0 = math::Vector4(
			r_giIntensity._val.f32,
			r_giEnergyClamp._val.f32,
			static_cast<float>(r_giDebugView._val.i32),
			static_cast<float>(_activeClipmap));

		const uint32_t giWidth = _giHalfRes ? static_cast<uint32_t>(std::max(1, _giHalfRes->GetWidth())) : 1u;
		const uint32_t giHeight = _giHalfRes ? static_cast<uint32_t>(std::max(1, _giHalfRes->GetHeight())) : 1u;
		const bool clipmapWarmActive = (_clipmapWarmFramesRemaining[0] > 0u) || (_clipmapWarmFramesRemaining[1] > 0u);
		const float stabilityTarget = clipmapWarmActive ? 1.0f : 0.0f;
		_resolveStabilityBoost = std::clamp(_resolveStabilityBoost + (stabilityTarget - _resolveStabilityBoost) * 0.22f, 0.0f, 1.0f);
		const float baseHysteresis = r_giHysteresis._val.f32;
		const float baseHistoryReject = r_giHistoryReject._val.f32;
		const float warmHysteresis = std::max(baseHysteresis, 0.88f);
		const float warmHistoryReject = std::max(baseHistoryReject, 0.015f);
		const float effectiveHysteresis = (_sunRelightFramesRemaining > 0u)
			? std::min(baseHysteresis, 0.45f)
			: (baseHysteresis + (warmHysteresis - baseHysteresis) * _resolveStabilityBoost);
		const float effectiveHistoryReject = (_sunRelightFramesRemaining > 0u)
			? std::max(baseHistoryReject, 0.0035f)
			: (baseHistoryReject + (warmHistoryReject - baseHistoryReject) * _resolveStabilityBoost);
		_constants.params1 = math::Vector4(
			effectiveHysteresis,
			effectiveHistoryReject,
			1.0f / static_cast<float>(giWidth),
			1.0f / static_cast<float>(giHeight));
		const float baseDecay = std::clamp(r_giVoxelDecay._val.f32, 0.5f, 0.999f);
		const float effectiveDecay = (_sunRelightFramesRemaining > 0u)
			? std::min(baseDecay, 0.72f)
			: baseDecay;
		float shiftSettle = 0.0f;
		for (uint32_t i = 0; i < ClipmapCount; ++i)
		{
			const bool hasPendingShift = _clipmaps[i].pendingShiftWs.LengthSquared() > 1e-8f;
			const float warmFactor = std::clamp(static_cast<float>(_clipmapWarmFramesRemaining[i]) / 4.0f, 0.0f, 1.0f);
			shiftSettle = std::max(shiftSettle, hasPendingShift ? 1.0f : warmFactor);
		}
		_constants.params2 = math::Vector4(
			(r_giLocalLightsOnlyDebug._val.b || r_giDebugDisableLocalLightInjection._val.b) ? 0.0f : r_giScreenBounce._val.f32,
			r_giUseProbes._val.b ? 0.35f : 0.0f,
			effectiveDecay,
			r_giGpuVoxelize._val.b ? 1.0f : 0.0f);
		_constants.params6 = math::Vector4(
			std::clamp(r_giVoxelNeighbourBlend._val.f32, 0.0f, 1.0f),
			shiftSettle,
			std::clamp(r_giVoxelAlbedoInfluence._val.f32, 0.0f, 1.0f),
			0.0f);
		_constants.params7 = math::Vector4(
			std::clamp(r_giGpuMaterialProxyBlend._val.f32, 0.0f, 1.0f),
			(r_giGpuMaterialEval._val.b && r_giGpuComputeBaseSun._val.b) ? 1.0f : 0.0f,
			0.0f,
			0.0f);
		_constants.params8 = math::Vector4(
			std::max(0.0f, r_giDiffuseInjection._val.f32),
			std::max(0.0f, r_giSunInjection._val.f32),
			std::max(0.0f, r_giSunDirectionalBoost._val.f32),
			std::max(0.0f, r_giEmissiveInjection._val.f32));

		const math::Vector3 sunDirection = ComputeSunDirectionWS(scene);
		float sunStrength = 0.0f;
		if (scene != nullptr)
		{
			if (auto* sun = scene->GetSunLight(); sun != nullptr)
			{
				if (sun->GetInjectIntoGI())
					sunStrength = std::max(0.0f, sun->GetLightStrength() * sun->GetLightMultiplier());
			}
		}
		const float sunPresence = std::clamp(sunStrength * std::max(0.0f, r_giSunInjection._val.f32), 0.0f, 1.0f);
		const float sunDirectionality = r_giLocalLightsOnlyDebug._val.b
			? 0.0f
			: std::clamp(r_giSunDirectionality._val.f32, 0.0f, 1.0f) * sunPresence;
		_constants.params3 = math::Vector4(sunDirection.x, sunDirection.y, sunDirection.z, sunDirectionality);
		const int32_t movementPreset = (r_giMovementPreset._val.i32 == 0 && g_pEnv != nullptr && g_pEnv->IsEditorMode())
			? 1
			: std::clamp(r_giMovementPreset._val.i32, 0, 2);
		const float basePixelMotionStart = std::max(0.0f, r_giResolvePixelMotionStart._val.f32);
		const float basePixelMotionStrength = std::max(0.0f, r_giResolvePixelMotionStrength._val.f32);
		const float warmPixelMotionStart = std::max(basePixelMotionStart, 2.2f);
		const float warmPixelMotionStrength = std::min(basePixelMotionStrength, 0.12f);
		const float effectivePixelMotionStart = basePixelMotionStart + (warmPixelMotionStart - basePixelMotionStart) * _resolveStabilityBoost;
		const float effectivePixelMotionStrength = basePixelMotionStrength + (warmPixelMotionStrength - basePixelMotionStrength) * _resolveStabilityBoost;
		_constants.params4 = math::Vector4(
			std::max(0.0f, r_giJitterScale._val.f32),
			std::clamp(r_giClipBlendWidth._val.f32, 0.01f, 0.95f),
			effectivePixelMotionStart,
			effectivePixelMotionStrength);
		_constants.params5 = math::Vector4(
			std::max(0.0f, r_giResolveLumaReject._val.f32),
			std::max(0.0f, r_giResolveDitherDark._val.f32),
			std::max(0.0f, r_giResolveDitherBright._val.f32),
			static_cast<float>(movementPreset));

		if (_constantBuffer)
		{
			_constantBuffer->Write(&_constants, sizeof(_constants));
		}
	}

	void DiffuseGI::Update(Scene* scene, Camera* camera)
	{
		if (!_created || !r_giEnable._val.b || scene == nullptr || camera == nullptr)
			return;

		if (camera->GetEntity() == nullptr)
			return;

		ApplyQualityPreset();
		_stats = {};
		_statsFrameCounter = _frameCounter;

		const bool localLightsOnlyDebug = r_giLocalLightsOnlyDebug._val.b;
		if (localLightsOnlyDebug != _lastLocalLightsOnlyDebug)
		{
			// Flush stale voxel history when changing local-light-only debug mode.
			// This avoids old neutral/sun energy contaminating the local-light color test.
			DestroyClipmapResources();
			if (!CreateClipmapResources())
			{
				_created = false;
				return;
			}
			for (auto& regions : _dirtyRegions)
			{
				regions.clear();
			}
			for (uint32_t i = 0; i < ClipmapCount; ++i)
			{
				_cachedVoxelTriangles[i].clear();
				_cachedVoxelTrianglesValid[i] = false;
				_cachedVoxelTrianglesFrame[i] = 0ull;
				_clipmapWarmFramesRemaining[i] = 0u;
			}
			if (_giHistory != nullptr)
			{
				_giHistory->ClearRenderTargetView(math::Color(0, 0, 0, 0));
			}
			if (_giResolved != nullptr)
			{
				_giResolved->ClearRenderTargetView(math::Color(0, 0, 0, 0));
			}
			if (_giHalfRes != nullptr)
			{
				_giHalfRes->ClearRenderTargetView(math::Color(0, 0, 0, 0));
			}
			_lastLocalLightsOnlyDebug = localLightsOnlyDebug;
		}

		const bool localLightTuningChanged =
			(std::abs(r_giLocalLightInjection._val.f32 - _lastLocalLightInjection) > 1e-4f) ||
			((!r_giDebugDisableLocalLightInjection._val.b) != _lastLocalLightInjectionEnable) ||
			(r_giDebugDisableBaseAndSunInjection._val.b != _lastDisableBaseAndSunInjection) ||
			(r_giDebugDisableBaseInjection._val.b != _lastDisableBaseInjection) ||
			(r_giDebugDisableSunInjection._val.b != _lastDisableSunInjection) ||
			(r_giLocalLightMaxPerMesh._val.i32 != _lastLocalLightMaxPerMesh) ||
			(std::abs(r_giLocalLightBaseSuppression._val.f32 - _lastLocalLightBaseSuppression) > 1e-4f) ||
			(std::abs(r_giLocalLightSunSuppression._val.f32 - _lastLocalLightSunSuppression) > 1e-4f) ||
			(std::abs(r_giLocalLightAlbedoWeight._val.f32 - _lastLocalLightAlbedoWeight) > 1e-4f) ||
			(std::abs(r_giBaseSunSmallTriangleDamp._val.f32 - _lastBaseSunSmallTriangleDamp) > 1e-4f) ||
			(std::abs(r_giMeshBaseInjectionNormalization._val.f32 - _lastMeshBaseInjectionNormalization) > 1e-4f) ||
			(std::abs(r_giMeshSunInjectionNormalization._val.f32 - _lastMeshSunInjectionNormalization) > 1e-4f) ||
			(std::abs(r_giMeshBaseInjectionMinScale._val.f32 - _lastMeshBaseInjectionMinScale) > 1e-4f) ||
			(std::abs(r_giMeshSunInjectionMinScale._val.f32 - _lastMeshSunInjectionMinScale) > 1e-4f) ||
			(r_giGpuComputeBaseSun._val.b != _lastGpuComputeBaseSunEnabled);
		const bool localLightModeToggled = ((!r_giDebugDisableLocalLightInjection._val.b) != _lastLocalLightInjectionEnable);
		const bool baseSunModeToggled = (r_giDebugDisableBaseAndSunInjection._val.b != _lastDisableBaseAndSunInjection);
		const bool baseOnlyModeToggled = (r_giDebugDisableBaseInjection._val.b != _lastDisableBaseInjection);
		const bool sunOnlyModeToggled = (r_giDebugDisableSunInjection._val.b != _lastDisableSunInjection);
		const uint64_t currentInjectLightSignature = ComputeInjectLightSignature(scene);
		const bool injectLightSetChanged = (currentInjectLightSignature != _lastInjectLightSignature);
		if (localLightModeToggled || baseSunModeToggled || baseOnlyModeToggled || sunOnlyModeToggled)
		{
			// Hard reset when local injection mode flips so no historical local-light energy remains.
			DestroyClipmapResources();
			if (!CreateClipmapResources())
			{
				_created = false;
				return;
			}
			for (auto& regions : _dirtyRegions)
			{
				regions.clear();
			}
			for (uint32_t i = 0; i < ClipmapCount; ++i)
			{
				_cachedVoxelTriangles[i].clear();
				_cachedVoxelTrianglesValid[i] = false;
				_cachedVoxelTrianglesFrame[i] = 0ull;
				_clipmapWarmFramesRemaining[i] = 0u;
			}
			if (_giHistory != nullptr)
			{
				_giHistory->ClearRenderTargetView(math::Color(0, 0, 0, 0));
			}
			if (_giResolved != nullptr)
			{
				_giResolved->ClearRenderTargetView(math::Color(0, 0, 0, 0));
			}
			if (_giHalfRes != nullptr)
			{
				_giHalfRes->ClearRenderTargetView(math::Color(0, 0, 0, 0));
			}
		}
		if (localLightTuningChanged || injectLightSetChanged)
		{
			for (uint32_t i = 0; i < ClipmapCount; ++i)
			{
				_cachedVoxelTriangles[i].clear();
				_cachedVoxelTrianglesValid[i] = false;
				_cachedVoxelTrianglesFrame[i] = 0ull;
				_clipmapWarmFramesRemaining[i] = std::max(_clipmapWarmFramesRemaining[i], 2u);
				_clipmaps[i].dirty = true;
			}
			_lastLocalLightInjection = r_giLocalLightInjection._val.f32;
			_lastLocalLightInjectionEnable = !r_giDebugDisableLocalLightInjection._val.b;
			_lastDisableBaseAndSunInjection = r_giDebugDisableBaseAndSunInjection._val.b;
			_lastDisableBaseInjection = r_giDebugDisableBaseInjection._val.b;
			_lastDisableSunInjection = r_giDebugDisableSunInjection._val.b;
			_lastLocalLightMaxPerMesh = r_giLocalLightMaxPerMesh._val.i32;
			_lastLocalLightBaseSuppression = r_giLocalLightBaseSuppression._val.f32;
			_lastLocalLightSunSuppression = r_giLocalLightSunSuppression._val.f32;
			_lastLocalLightAlbedoWeight = r_giLocalLightAlbedoWeight._val.f32;
			_lastBaseSunSmallTriangleDamp = r_giBaseSunSmallTriangleDamp._val.f32;
			_lastMeshBaseInjectionNormalization = r_giMeshBaseInjectionNormalization._val.f32;
			_lastMeshSunInjectionNormalization = r_giMeshSunInjectionNormalization._val.f32;
			_lastMeshBaseInjectionMinScale = r_giMeshBaseInjectionMinScale._val.f32;
			_lastMeshSunInjectionMinScale = r_giMeshSunInjectionMinScale._val.f32;
			_lastGpuComputeBaseSunEnabled = r_giGpuComputeBaseSun._val.b;
			_lastInjectLightSignature = currentInjectLightSignature;
		}

		const uint32_t expectedHalfWidth = r_giHalfRes._val.b ? _halfWidth : _width;
		const uint32_t expectedHalfHeight = r_giHalfRes._val.b ? _halfHeight : _height;
		if (_giHalfRes != nullptr &&
			(static_cast<uint32_t>(_giHalfRes->GetWidth()) != expectedHalfWidth ||
			 static_cast<uint32_t>(_giHalfRes->GetHeight()) != expectedHalfHeight))
		{
			Create(_width, _height);
			if (!_created)
				return;
		}

		const uint32_t desiredResolution = GetVoxelResolution();
		if (_clipmaps[0].resolution != desiredResolution)
		{
			DestroyClipmapResources();
			if (!CreateClipmapResources())
			{
				_created = false;
				return;
			}
			for (auto& regions : _dirtyRegions)
			{
				regions.clear();
			}
			for (uint32_t i = 0; i < ClipmapCount; ++i)
			{
				_cachedVoxelTriangles[i].clear();
				_cachedVoxelTrianglesValid[i] = false;
				_cachedVoxelTrianglesFrame[i] = 0ull;
			}
		}

		RebuildClipmapTransforms(camera->GetEntity()->GetPosition());
		const math::Vector3 currentSunDirection = ComputeSunDirectionWS(scene);
		if (!_lastSunDirectionInitialized)
		{
			_lastSunDirection = currentSunDirection;
			_lastSunDirectionInitialized = true;
		}
		else
		{
			const float sunDirDot = std::clamp(_lastSunDirection.Dot(currentSunDirection), -1.0f, 1.0f);
			if (sunDirDot < 0.9995f)
			{
				_sunRelightFramesRemaining = 10u;
				for (auto& level : _clipmaps)
				{
					level.dirty = true;
				}
			}
			_lastSunDirection = currentSunDirection;
		}

		// Keep the nearest clipmap hot every frame for stable local bounce.
		_activeClipmap = 0;
		UpdateConstants(scene);
		if (!r_giGpuVoxelize._val.b)
		{
			UpdateClipmapData(scene, 0);
			if ((_frameCounter & 1ull) == 0ull)
			{
				const uint32_t farClip = 1u + static_cast<uint32_t>((_frameCounter / 2ull) % (ClipmapCount - 1u));
				UpdateClipmapData(scene, farClip);
			}
		}
		else
		{
			if (_sunRelightFramesRemaining > 0u)
			{
				for (uint32_t i = 0u; i < ClipmapCount; ++i)
				{
					RunGpuVoxelization(scene, i);
				}
			}
			else
			{
				RunGpuVoxelization(scene, 0);
				if ((_frameCounter % 4ull) == 0ull)
				{
					const uint32_t farClip = 1u + static_cast<uint32_t>((_frameCounter / 4ull) % (ClipmapCount - 1u));
					RunGpuVoxelization(scene, farClip);
				}
			}
		}
		for (uint32_t i = 0; i < ClipmapCount; ++i)
		{
			if (_clipmapWarmFramesRemaining[i] > 0u)
			{
				--_clipmapWarmFramesRemaining[i];
			}
		}
		if (_sunRelightFramesRemaining > 0u)
		{
			--_sunRelightFramesRemaining;
		}
		const bool telemetryEnabled = r_giTelemetry._val.b || (r_giGpuCompareMode._val.i32 > 0);
		const uint64_t telemetryPeriod = static_cast<uint64_t>(std::max(1, r_giTelemetryLogFrames._val.i32));
		if (telemetryEnabled && ((_frameCounter % telemetryPeriod) == 0ull))
		{
			LOG_INFO(
				"GI telemetry: frame=%llu build=%.3fms upload=%.3fms candidate=%.3fms dispatch=%.3fms tri=%u cand=%u gpuLights=%u uploadBytes=%llu gpuCandidate=%s gpuMaterialEval=%s gpuComputeBaseSun=%s",
				static_cast<unsigned long long>(_frameCounter),
				_stats.cpuTriangleBuildMs,
				_stats.cpuUploadMs,
				_stats.candidateBuildMs,
				_stats.gpuDispatchMs,
				_stats.sourceTriangleCount,
				_stats.candidateTriangleCount,
				_stats.gpuLightCount,
				static_cast<unsigned long long>(_stats.uploadBytes),
				r_giGpuCandidateGen._val.b ? "on" : "off",
				r_giGpuMaterialEval._val.b ? "on" : "off",
				r_giGpuComputeBaseSun._val.b ? "on" : "off");
			if (r_giGpuCompareMode._val.i32 > 1)
			{
				const float candidateRatio = (_stats.sourceTriangleCount > 0u)
					? (static_cast<float>(_stats.candidateTriangleCount) / static_cast<float>(_stats.sourceTriangleCount))
					: 0.0f;
				LOG_INFO(
					"GI compare: frame=%llu candidateRatio=%.3f candidateCull=%u gpuLights=%u/%d warm0=%u",
					static_cast<unsigned long long>(_frameCounter),
					candidateRatio,
					(_stats.sourceTriangleCount > _stats.candidateTriangleCount)
						? (_stats.sourceTriangleCount - _stats.candidateTriangleCount)
						: 0u,
					_stats.gpuLightCount,
					r_giGpuMaterialEvalMaxLights._val.i32,
					_clipmapWarmFramesRemaining[0]);
			}
		}
		UpdateConstants(scene);
		++_frameCounter;
	}

	bool DiffuseGI::EnsureGpuVoxelTriangleBuffer(uint32_t elementCapacity)
	{
		if (elementCapacity == 0u)
			return false;
		if (_voxelTriangleBuffer != nullptr && _voxelTriangleSrv != nullptr && _voxelTriangleCapacity >= elementCapacity)
			return true;

		SAFE_RELEASE(_voxelTriangleSrv);
		SAFE_RELEASE(_voxelTriangleBuffer);
		_voxelTriangleCapacity = 0;

		auto* device = reinterpret_cast<ID3D11Device*>(g_pEnv->_graphicsDevice->GetNativeDevice());
		if (device == nullptr)
			return false;

		D3D11_BUFFER_DESC desc = {};
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.ByteWidth = std::max<uint32_t>(1u, elementCapacity) * static_cast<uint32_t>(sizeof(GpuVoxelTriangle));
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(GpuVoxelTriangle);

		if (FAILED(device->CreateBuffer(&desc, nullptr, &_voxelTriangleBuffer)) || _voxelTriangleBuffer == nullptr)
		{
			LOG_CRIT("DiffuseGI failed to create GPU voxel triangle buffer.");
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = elementCapacity;

		if (FAILED(device->CreateShaderResourceView(_voxelTriangleBuffer, &srvDesc, &_voxelTriangleSrv)) || _voxelTriangleSrv == nullptr)
		{
			LOG_CRIT("DiffuseGI failed to create GPU voxel triangle SRV.");
			SAFE_RELEASE(_voxelTriangleBuffer);
			return false;
		}

		_voxelTriangleCapacity = elementCapacity;
		return true;
	}

	bool DiffuseGI::EnsureGpuGiLightBuffer(uint32_t elementCapacity)
	{
		if (elementCapacity == 0u)
			return false;
		if (_giLightBuffer != nullptr && _giLightSrv != nullptr && _giLightCapacity >= elementCapacity)
			return true;

		SAFE_RELEASE(_giLightSrv);
		SAFE_RELEASE(_giLightBuffer);
		_giLightCapacity = 0u;

		auto* device = reinterpret_cast<ID3D11Device*>(g_pEnv->_graphicsDevice->GetNativeDevice());
		if (device == nullptr)
			return false;

		D3D11_BUFFER_DESC desc = {};
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.ByteWidth = std::max<uint32_t>(1u, elementCapacity) * static_cast<uint32_t>(sizeof(GpuGiLight));
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(GpuGiLight);
		if (FAILED(device->CreateBuffer(&desc, nullptr, &_giLightBuffer)) || _giLightBuffer == nullptr)
		{
			LOG_CRIT("DiffuseGI failed to create GPU GI light buffer.");
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = elementCapacity;
		if (FAILED(device->CreateShaderResourceView(_giLightBuffer, &srvDesc, &_giLightSrv)) || _giLightSrv == nullptr)
		{
			LOG_CRIT("DiffuseGI failed to create GPU GI light SRV.");
			SAFE_RELEASE(_giLightBuffer);
			return false;
		}

		_giLightCapacity = elementCapacity;
		return true;
	}

	bool DiffuseGI::EnsureGpuGiMaterialBuffer(uint32_t elementCapacity)
	{
		if (elementCapacity == 0u)
			return false;
		if (_giMaterialBuffer != nullptr && _giMaterialSrv != nullptr && _giMaterialCapacity >= elementCapacity)
			return true;

		SAFE_RELEASE(_giMaterialSrv);
		SAFE_RELEASE(_giMaterialBuffer);
		_giMaterialCapacity = 0u;

		auto* device = reinterpret_cast<ID3D11Device*>(g_pEnv->_graphicsDevice->GetNativeDevice());
		if (device == nullptr)
			return false;

		D3D11_BUFFER_DESC desc = {};
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.ByteWidth = std::max<uint32_t>(1u, elementCapacity) * static_cast<uint32_t>(sizeof(GpuGiMaterial));
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(GpuGiMaterial);
		if (FAILED(device->CreateBuffer(&desc, nullptr, &_giMaterialBuffer)) || _giMaterialBuffer == nullptr)
		{
			LOG_CRIT("DiffuseGI failed to create GPU GI material buffer.");
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = elementCapacity;
		if (FAILED(device->CreateShaderResourceView(_giMaterialBuffer, &srvDesc, &_giMaterialSrv)) || _giMaterialSrv == nullptr)
		{
			LOG_CRIT("DiffuseGI failed to create GPU GI material SRV.");
			SAFE_RELEASE(_giMaterialBuffer);
			return false;
		}

		_giMaterialCapacity = elementCapacity;
		return true;
	}

	bool DiffuseGI::EnsureGpuVoxelCandidateBuffer(uint32_t elementCapacity)
	{
		if (elementCapacity == 0u)
			return false;
		if (_voxelCandidateBuffer != nullptr &&
			_voxelCandidateSrv != nullptr &&
			_voxelCandidateUav != nullptr &&
			_voxelCandidateCountBuffer != nullptr &&
			_voxelCandidateCountReadback != nullptr &&
			_voxelCandidateDispatchArgs != nullptr &&
			_voxelCandidateCapacity >= elementCapacity)
		{
			return true;
		}

		SAFE_RELEASE(_voxelCandidateSrv);
		SAFE_RELEASE(_voxelCandidateUav);
		SAFE_RELEASE(_voxelCandidateBuffer);
		SAFE_RELEASE(_voxelCandidateCountBuffer);
		SAFE_RELEASE(_voxelCandidateCountReadback);
		SAFE_RELEASE(_voxelCandidateDispatchArgs);
		_voxelCandidateCapacity = 0u;

		auto* device = reinterpret_cast<ID3D11Device*>(g_pEnv->_graphicsDevice->GetNativeDevice());
		if (device == nullptr)
			return false;

		D3D11_BUFFER_DESC desc = {};
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		desc.ByteWidth = std::max<uint32_t>(1u, elementCapacity) * static_cast<uint32_t>(sizeof(GpuVoxelTriangle));
		desc.CPUAccessFlags = 0u;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(GpuVoxelTriangle);

		if (FAILED(device->CreateBuffer(&desc, nullptr, &_voxelCandidateBuffer)) || _voxelCandidateBuffer == nullptr)
		{
			LOG_CRIT("DiffuseGI failed to create GPU voxel candidate buffer.");
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = elementCapacity;
		if (FAILED(device->CreateShaderResourceView(_voxelCandidateBuffer, &srvDesc, &_voxelCandidateSrv)) || _voxelCandidateSrv == nullptr)
		{
			LOG_CRIT("DiffuseGI failed to create GPU voxel candidate SRV.");
			SAFE_RELEASE(_voxelCandidateBuffer);
			return false;
		}

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.NumElements = elementCapacity;
		uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
		if (FAILED(device->CreateUnorderedAccessView(_voxelCandidateBuffer, &uavDesc, &_voxelCandidateUav)) || _voxelCandidateUav == nullptr)
		{
			LOG_CRIT("DiffuseGI failed to create GPU voxel candidate UAV.");
			SAFE_RELEASE(_voxelCandidateSrv);
			SAFE_RELEASE(_voxelCandidateBuffer);
			return false;
		}

		D3D11_BUFFER_DESC countDesc = {};
		countDesc.ByteWidth = sizeof(uint32_t);
		countDesc.Usage = D3D11_USAGE_DEFAULT;
		countDesc.BindFlags = 0u;
		countDesc.CPUAccessFlags = 0u;
		countDesc.MiscFlags = 0u;
		if (FAILED(device->CreateBuffer(&countDesc, nullptr, &_voxelCandidateCountBuffer)) || _voxelCandidateCountBuffer == nullptr)
		{
			LOG_CRIT("DiffuseGI failed to create GPU voxel candidate count buffer.");
			SAFE_RELEASE(_voxelCandidateUav);
			SAFE_RELEASE(_voxelCandidateSrv);
			SAFE_RELEASE(_voxelCandidateBuffer);
			return false;
		}

		D3D11_BUFFER_DESC countReadbackDesc = {};
		countReadbackDesc.ByteWidth = sizeof(uint32_t);
		countReadbackDesc.Usage = D3D11_USAGE_STAGING;
		countReadbackDesc.BindFlags = 0u;
		countReadbackDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		countReadbackDesc.MiscFlags = 0u;
		if (FAILED(device->CreateBuffer(&countReadbackDesc, nullptr, &_voxelCandidateCountReadback)) || _voxelCandidateCountReadback == nullptr)
		{
			LOG_CRIT("DiffuseGI failed to create GPU voxel candidate count readback buffer.");
			SAFE_RELEASE(_voxelCandidateCountBuffer);
			SAFE_RELEASE(_voxelCandidateUav);
			SAFE_RELEASE(_voxelCandidateSrv);
			SAFE_RELEASE(_voxelCandidateBuffer);
			return false;
		}

		D3D11_BUFFER_DESC argsDesc = {};
		argsDesc.ByteWidth = sizeof(uint32_t) * 3u;
		argsDesc.Usage = D3D11_USAGE_DEFAULT;
		argsDesc.BindFlags = 0u;
		argsDesc.CPUAccessFlags = 0u;
		argsDesc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
		if (FAILED(device->CreateBuffer(&argsDesc, nullptr, &_voxelCandidateDispatchArgs)) || _voxelCandidateDispatchArgs == nullptr)
		{
			LOG_CRIT("DiffuseGI failed to create GPU voxel candidate dispatch args buffer.");
			SAFE_RELEASE(_voxelCandidateCountReadback);
			SAFE_RELEASE(_voxelCandidateCountBuffer);
			SAFE_RELEASE(_voxelCandidateUav);
			SAFE_RELEASE(_voxelCandidateSrv);
			SAFE_RELEASE(_voxelCandidateBuffer);
			return false;
		}

		_voxelCandidateCapacity = elementCapacity;
		return true;
	}

	void DiffuseGI::ExtractGiSceneProxies(
		Scene* scene,
		const GiClipmapParams& clipmapParams,
		std::vector<GiMeshInstanceProxy>& outMeshes,
		std::vector<GiMaterialProxy>& outMaterials,
		std::vector<GiLocalLightProxy>& outLights)
	{
		outMeshes.clear();
		outMaterials.clear();
		outLights.clear();
		_giMaterialProxyLookup.clear();
		if (scene == nullptr)
			return;

		const dx::BoundingBox clipBounds(
			clipmapParams.center,
			math::Vector3(clipmapParams.extent, clipmapParams.extent, clipmapParams.extent));
		std::vector<StaticMeshComponent*> meshesInBounds;
		scene->GatherStaticMeshesInBounds(clipBounds, meshesInBounds, true);
		outMeshes.reserve(meshesInBounds.size());
		outMaterials.reserve(meshesInBounds.size());

		for (auto* smc : meshesInBounds)
		{
			if (smc == nullptr || smc->GetMesh() == nullptr)
				continue;

			auto* entity = smc->GetEntity();
			if (entity == nullptr || entity->IsPendingDeletion())
				continue;

			auto meshPtr = smc->GetMesh();
			if (!meshPtr)
				continue;

			const auto materialPtr = smc->GetMaterial();
			const Material* material = materialPtr ? materialPtr.get() : nullptr;
			uint32_t materialProxyIndex = 0u;
			if (material != nullptr)
			{
				auto it = _giMaterialProxyLookup.find(material);
				if (it == _giMaterialProxyLookup.end())
				{
					GiMaterialProxy proxy = {};
					proxy.material = material;
					proxy.diffuse = material->_properties.diffuseColour;
					proxy.emissive = material->_properties.emissiveColour;
					proxy.index = static_cast<uint32_t>(outMaterials.size());
					outMaterials.push_back(proxy);
					materialProxyIndex = proxy.index;
					_giMaterialProxyLookup.emplace(material, materialProxyIndex);
				}
				else
				{
					materialProxyIndex = it->second;
				}
			}

			const auto& worldAabb = entity->GetWorldAABB();
			const math::Vector3 aabbCenter(worldAabb.Center.x, worldAabb.Center.y, worldAabb.Center.z);
			const math::Vector3 aabbExtents(worldAabb.Extents.x, worldAabb.Extents.y, worldAabb.Extents.z);

			GiMeshInstanceProxy proxy = {};
			proxy.component = smc;
			proxy.entity = entity;
			proxy.mesh = meshPtr.get();
			proxy.material = material;
			proxy.materialProxyIndex = materialProxyIndex;
			proxy.worldTransform = entity->GetWorldTM();
			proxy.uvScale = smc->GetUVScale();
			proxy.aabbMin = aabbCenter - aabbExtents;
			proxy.aabbMax = aabbCenter + aabbExtents;
			outMeshes.push_back(proxy);
		}

		ExtractGiLocalLights(scene, clipmapParams, outLights);
	}

	void DiffuseGI::ExtractGiLocalLights(
		Scene* scene,
		const GiClipmapParams& clipmapParams,
		std::vector<GiLocalLightProxy>& outLights)
	{
		outLights.clear();
		if (scene == nullptr)
			return;

		const math::Vector3 clipHalfExtents(clipmapParams.extent, clipmapParams.extent, clipmapParams.extent);
		const math::Vector3 clipMin = clipmapParams.center - clipHalfExtents;
		const math::Vector3 clipMax = clipmapParams.center + clipHalfExtents;

		std::vector<PointLight*> pointLights;
		scene->GetComponents<PointLight>(pointLights);
		outLights.reserve(pointLights.size());
		for (auto* light : pointLights)
		{
			if (light == nullptr || !light->GetInjectIntoGI())
				continue;
			auto* lightEntity = light->GetEntity();
			if (lightEntity == nullptr || lightEntity->IsPendingDeletion())
				continue;

			const auto diffuse = light->GetDiffuseColour();
			const float lightEnergy = std::max(0.0f, diffuse.w);
			const math::Vector3 scaledColour(diffuse.x * lightEnergy, diffuse.y * lightEnergy, diffuse.z * lightEnergy);
			if (scaledColour.LengthSquared() <= 1e-8f)
				continue;

			GiLocalLightProxy proxy = {};
			proxy.position = lightEntity->GetPosition();
			if (proxy.position.LengthSquared() <= 1e-8f)
				proxy.position = lightEntity->GetWorldTM().Translation();
			proxy.radius = std::max(0.01f, light->GetRadius());
			if (!SphereIntersectsAabb(proxy.position, proxy.radius, clipMin, clipMax))
				continue;
			proxy.direction = math::Vector3(0.0f, 0.0f, 1.0f);
			proxy.colour = scaledColour;
			proxy.coneExponent = 1.0f;
			proxy.isSpot = false;
			outLights.push_back(proxy);
		}

		std::vector<SpotLight*> spotLights;
		scene->GetComponents<SpotLight>(spotLights);
		outLights.reserve(outLights.size() + spotLights.size());
		for (auto* light : spotLights)
		{
			if (light == nullptr || !light->GetInjectIntoGI())
				continue;
			auto* lightEntity = light->GetEntity();
			if (lightEntity == nullptr || lightEntity->IsPendingDeletion())
				continue;

			const auto diffuse = light->GetDiffuseColour();
			const float lightEnergy = std::max(0.0f, diffuse.w);
			const math::Vector3 scaledColour(diffuse.x * lightEnergy, diffuse.y * lightEnergy, diffuse.z * lightEnergy);
			if (scaledColour.LengthSquared() <= 1e-8f)
				continue;

			math::Vector3 lightDir = lightEntity->GetWorldTM().Forward();
			if (lightDir.LengthSquared() <= 1e-8f)
				lightDir = math::Vector3(0.0f, 0.0f, 1.0f);
			else
				lightDir.Normalize();

			GiLocalLightProxy proxy = {};
			proxy.position = lightEntity->GetPosition();
			if (proxy.position.LengthSquared() <= 1e-8f)
				proxy.position = lightEntity->GetWorldTM().Translation();
			proxy.radius = std::max(0.01f, light->GetRadius());
			if (!SphereIntersectsAabb(proxy.position, proxy.radius, clipMin, clipMax))
				continue;
			proxy.direction = lightDir;
			proxy.colour = scaledColour;
			proxy.coneExponent = std::clamp(light->GetConeSize(), 1.0f, 128.0f);
			proxy.isSpot = true;
			outLights.push_back(proxy);
		}
	}

	uint32_t DiffuseGI::BuildGpuVoxelCandidateList(uint32_t levelIndex, uint32_t sourceTriangleCount, bool& outDispatchIndirectReady)
	{
		outDispatchIndirectReady = false;
		if (sourceTriangleCount == 0u || levelIndex >= ClipmapCount)
			return 0u;
		if (!r_giGpuCandidateGen._val.b)
			return sourceTriangleCount;
		if (_voxelCandidateShader == nullptr)
			return sourceTriangleCount;
		if (!EnsureGpuVoxelCandidateBuffer(sourceTriangleCount))
			return sourceTriangleCount;

		auto* context = reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext());
		auto* stage = _voxelCandidateShader->GetShaderStage(ShaderStage::ComputeShader);
		if (context == nullptr || stage == nullptr || _voxelTriangleSrv == nullptr)
			return sourceTriangleCount;

		const auto candidateStart = std::chrono::high_resolution_clock::now();
		ID3D11Buffer* giCb = _constantBuffer ? reinterpret_cast<ID3D11Buffer*>(_constantBuffer->GetNativePtr()) : nullptr;
		if (giCb != nullptr)
		{
			context->CSSetConstantBuffers(4, 1, &giCb);
		}

		ID3D11ShaderResourceView* inputSrv[1] = { _voxelTriangleSrv };
		ID3D11UnorderedAccessView* outputUav[1] = { _voxelCandidateUav };
		UINT initialCounts[1] = { 0u };
		context->CSSetShaderResources(0, 1, inputSrv);
		context->CSSetUnorderedAccessViews(0, 1, outputUav, initialCounts);
		context->CSSetShader(reinterpret_cast<ID3D11ComputeShader*>(stage->GetNativePtr()), nullptr, 0);
		const uint32_t groups = (sourceTriangleCount + 63u) / 64u;
		context->Dispatch(std::max<uint32_t>(groups, 1u), 1u, 1u);

		ID3D11ShaderResourceView* nullSrv[1] = {};
		ID3D11UnorderedAccessView* nullUav[1] = {};
		context->CSSetShaderResources(0, 1, nullSrv);
		context->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);
		context->CSSetShader(nullptr, nullptr, 0);

		const uint32_t dispatchInit[3] = { 0u, 1u, 1u };
		context->UpdateSubresource(_voxelCandidateDispatchArgs, 0, nullptr, dispatchInit, 0u, 0u);
		context->CopyStructureCount(_voxelCandidateDispatchArgs, 0u, _voxelCandidateUav);
		outDispatchIndirectReady = true;

		uint32_t candidateCount = sourceTriangleCount;
		const bool readbackRequired =
			(r_giGpuCompareMode._val.i32 > 0) ||
			(r_giTelemetry._val.b && ((_frameCounter % static_cast<uint64_t>(std::max(1, r_giTelemetryLogFrames._val.i32))) == 0ull));
		if (readbackRequired)
		{
			context->CopyStructureCount(_voxelCandidateCountBuffer, 0u, _voxelCandidateUav);
			context->CopyResource(_voxelCandidateCountReadback, _voxelCandidateCountBuffer);
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (SUCCEEDED(context->Map(_voxelCandidateCountReadback, 0, D3D11_MAP_READ, 0, &mapped)))
			{
				candidateCount = std::min(sourceTriangleCount, *reinterpret_cast<uint32_t*>(mapped.pData));
				context->Unmap(_voxelCandidateCountReadback, 0);
			}
		}

		_stats.candidateBuildMs += ElapsedMs(candidateStart);
		return candidateCount;
	}

	uint32_t DiffuseGI::BuildGpuVoxelTriangleList(Scene* scene, uint32_t levelIndex, std::vector<GpuVoxelTriangle>& out)
	{
		const auto buildStart = std::chrono::high_resolution_clock::now();
		out.clear();
		if (scene == nullptr || levelIndex >= ClipmapCount)
		{
			_stats.cpuTriangleBuildMs = ElapsedMs(buildStart);
			_stats.sourceTriangleCount = 0u;
			return 0u;
		}
		const bool localLightsOnlyDebug = r_giLocalLightsOnlyDebug._val.b;
		const bool localLightInjectionEnabled = !r_giDebugDisableLocalLightInjection._val.b && !r_giGpuMaterialEval._val.b;
		const bool gpuComputeBaseSunEnabled =
			r_giGpuMaterialEval._val.b &&
			r_giGpuComputeBaseSun._val.b &&
			!localLightsOnlyDebug &&
			!r_giDebugDisableBaseAndSunInjection._val.b &&
			!r_giDebugDisableBaseInjection._val.b &&
			!r_giDebugDisableSunInjection._val.b;
		const bool disableBaseAndSunInjection = r_giDebugDisableBaseAndSunInjection._val.b;
		const bool disableBaseInjection = disableBaseAndSunInjection || r_giDebugDisableBaseInjection._val.b;
		const bool disableSunInjection = disableBaseAndSunInjection || r_giDebugDisableSunInjection._val.b;

		auto& level = _clipmaps[levelIndex];
		const float voxelSizeWorld = (level.extent * 2.0f) / static_cast<float>(std::max(1u, level.resolution));
		const uint64_t cacheAge = (_frameCounter >= _cachedVoxelTrianglesFrame[levelIndex])
			? (_frameCounter - _cachedVoxelTrianglesFrame[levelIndex])
			: 0ull;
		const uint64_t cacheFrames = static_cast<uint64_t>(std::max(1, r_giTriangleCacheFrames._val.i32));
		if (_cachedVoxelTrianglesValid[levelIndex] && !level.dirty && cacheAge < cacheFrames)
		{
			out = _cachedVoxelTriangles[levelIndex];
			_stats.cpuTriangleBuildMs = ElapsedMs(buildStart);
			_stats.sourceTriangleCount = static_cast<uint32_t>(out.size());
			return static_cast<uint32_t>(out.size());
		}

		const math::Vector3 clipMin = level.center - math::Vector3(level.extent, level.extent, level.extent);
		const math::Vector3 clipMax = level.center + math::Vector3(level.extent, level.extent, level.extent);
		GiClipmapParams clipmapParams = {};
		clipmapParams.center = level.center;
		clipmapParams.extent = level.extent;
		clipmapParams.resolution = level.resolution;
		clipmapParams.levelIndex = levelIndex;
		clipmapParams.dirty = level.dirty;
		ExtractGiSceneProxies(scene, clipmapParams, _giMeshProxies, _giMaterialProxies, _giLightProxies);
		std::unordered_map<StaticMeshComponent*, uint32_t> meshMaterialProxyIndex;
		meshMaterialProxyIndex.reserve(_giMeshProxies.size());
		for (const auto& meshProxy : _giMeshProxies)
		{
			if (meshProxy.component != nullptr)
			{
				meshMaterialProxyIndex[meshProxy.component] = meshProxy.materialProxyIndex;
			}
		}
		std::vector<StaticMeshComponent*> meshesInBounds;
		meshesInBounds.reserve(_giMeshProxies.size());
		for (const auto& meshProxy : _giMeshProxies)
		{
			if (meshProxy.component != nullptr)
			{
				meshesInBounds.push_back(meshProxy.component);
			}
		}

		struct LocalLightSample
		{
			math::Vector3 position = math::Vector3::Zero;
			math::Vector3 direction = math::Vector3(0.0f, 0.0f, 1.0f);
			math::Vector3 colour = math::Vector3::Zero;
			float radius = 0.0f;
			float coneExponent = 1.0f;
			bool isSpot = false;
		};
		std::vector<LocalLightSample> localLights;
		if (localLightInjectionEnabled && !_giLightProxies.empty())
		{
			localLights.reserve(_giLightProxies.size());
			for (const auto& lightProxy : _giLightProxies)
			{
				LocalLightSample sample = {};
				sample.position = lightProxy.position;
				sample.direction = lightProxy.direction;
				sample.colour = lightProxy.colour;
				sample.radius = lightProxy.radius;
				sample.coneExponent = lightProxy.coneExponent;
				sample.isSpot = lightProxy.isSpot;
				localLights.push_back(sample);
			}
		}

		uint32_t budget = static_cast<uint32_t>(std::max(256, r_giVoxelTriangleBudget._val.i32));
		if (level.dirty)
		{
			budget = std::min<uint32_t>(budget * 3u, 300000u);
		}
		if (_clipmapWarmFramesRemaining[levelIndex] > 0u)
		{
			budget = std::min<uint32_t>(budget * 3u, 300000u);
		}

		out.reserve(std::min<uint32_t>(budget, static_cast<uint32_t>(meshesInBounds.size()) * 64u));
		const math::Vector3 sunTint = ComputeSunTint(scene);
		const math::Vector3 sunDirection = ComputeSunDirectionWS(scene);
		float sunStrength = 0.0f;
		if (auto* sun = scene->GetSunLight(); sun != nullptr)
		{
			if (sun->GetInjectIntoGI())
				sunStrength = std::max(0.0f, sun->GetLightMultiplier() * sun->GetLightStrength());
		}
		const float clipAttenuation = 1.0f / (1.0f + 0.20f * static_cast<float>(levelIndex));
		auto getMaterialAlbedoData = [&](const Material* material) -> const MaterialTriangleAlbedoCacheEntry&
		{
			auto it = _materialTriangleAlbedoCache.find(material);
			if (it != _materialTriangleAlbedoCache.end())
				return it->second;

			MaterialTriangleAlbedoCacheEntry data = {};
			if (material != nullptr)
			{
				data.diffuseTint = math::Vector3(
					std::clamp(material->_properties.diffuseColour.x, 0.0f, 1.0f),
					std::clamp(material->_properties.diffuseColour.y, 0.0f, 1.0f),
					std::clamp(material->_properties.diffuseColour.z, 0.0f, 1.0f));

				if (auto albedoTex = material->GetTexture(MaterialTexture::Albedo))
				{
					data.width = std::max(1, albedoTex->GetWidth());
					data.height = std::max(1, albedoTex->GetHeight());
					albedoTex->GetPixels(data.pixels);
					const size_t minTightSize = static_cast<size_t>(data.width) * static_cast<size_t>(data.height) * 4u;
					if (data.pixels.size() >= minTightSize)
					{
						const DXGI_FORMAT fmt = static_cast<DXGI_FORMAT>(albedoTex->GetFormat());
						data.isBgra =
							(fmt == DXGI_FORMAT_B8G8R8A8_UNORM) ||
							(fmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) ||
							(fmt == DXGI_FORMAT_B8G8R8X8_UNORM) ||
							(fmt == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB);
						data.hasTexture = true;
					}
				}
			}

			auto [insertedIt, _] = _materialTriangleAlbedoCache.emplace(material, std::move(data));
			return insertedIt->second;
		};

		auto sampleLinearAlbedo = [&](const MaterialTriangleAlbedoCacheEntry& data, float u, float v) -> math::Vector3
		{
			if (!data.hasTexture || data.width <= 0 || data.height <= 0)
				return data.diffuseTint;

			static float srgbToLinear[256] = {};
			static bool srgbToLinearInit = false;
			if (!srgbToLinearInit)
			{
				for (int32_t i = 0; i < 256; ++i)
				{
					const float s = static_cast<float>(i) / 255.0f;
					srgbToLinear[i] = static_cast<float>(std::pow(static_cast<double>(s), 2.2));
				}
				srgbToLinearInit = true;
			}

			u = u - std::floor(u);
			v = v - std::floor(v);
			if (u < 0.0f)
				u += 1.0f;
			if (v < 0.0f)
				v += 1.0f;

			const int32_t x = std::clamp(static_cast<int32_t>(u * static_cast<float>(data.width - 1) + 0.5f), 0, data.width - 1);
			const int32_t y = std::clamp(static_cast<int32_t>(v * static_cast<float>(data.height - 1) + 0.5f), 0, data.height - 1);
			const int cR = data.isBgra ? 2 : 0;
			const int cG = 1;
			const int cB = data.isBgra ? 0 : 2;
			const size_t rowPitch = static_cast<size_t>(data.width) * 4u;
			const size_t idx = static_cast<size_t>(y) * rowPitch + static_cast<size_t>(x) * 4u;
			if (idx + 3u >= data.pixels.size())
				return data.diffuseTint;

			const float tr = srgbToLinear[data.pixels[idx + static_cast<size_t>(cR)]];
			const float tg = srgbToLinear[data.pixels[idx + static_cast<size_t>(cG)]];
			const float tb = srgbToLinear[data.pixels[idx + static_cast<size_t>(cB)]];
			return math::Vector3(
				std::clamp(tr * data.diffuseTint.x, 0.0f, 1.0f),
				std::clamp(tg * data.diffuseTint.y, 0.0f, 1.0f),
				std::clamp(tb * data.diffuseTint.z, 0.0f, 1.0f));
		};

		for (auto* smc : meshesInBounds)
		{
			if (smc == nullptr || smc->GetMesh() == nullptr)
				continue;

			auto* entity = smc->GetEntity();
			if (entity == nullptr || entity->IsPendingDeletion())
				continue;

			const float diffuseInject = std::max(0.0f, r_giDiffuseInjection._val.f32);
			const float sunInject = std::max(0.0f, r_giSunInjection._val.f32);
			const float sunDirectionalBoost = std::max(0.0f, r_giSunDirectionalBoost._val.f32);
			const float emissiveInject = std::max(0.0f, r_giEmissiveInjection._val.f32);
			const float bleedBoost = std::max(0.0f, r_giAlbedoBleedBoost._val.f32);
			const float colourBleedStrength = std::max(0.0f, r_giColourBleedStrength._val.f32);
			math::Vector3 emissiveTint = math::Vector3::Zero;
			float emissiveStrength = 0.0f;
			const Material* material = nullptr;
			if (auto mat = smc->GetMaterial())
			{
				const auto emissive = mat->_properties.emissiveColour;
				material = mat.get();
				emissiveTint = math::Vector3(emissive.x, emissive.y, emissive.z);
				emissiveStrength = std::max(0.0f, emissive.w);
			}

			auto mesh = smc->GetMesh();
			if (!mesh)
				continue;

			const auto& vertices = mesh->GetVertices();
			const auto& indices = mesh->GetIndices();
			if (vertices.empty() || indices.size() < 3u)
				continue;

			const uint32_t remaining = budget > out.size() ? (budget - static_cast<uint32_t>(out.size())) : 0u;
			if (remaining == 0u)
				break;
			const uint32_t triangleCount = static_cast<uint32_t>(indices.size() / 3u);
			const uint32_t triStep = std::max<uint32_t>(1u, (triangleCount + remaining - 1u) / remaining);
			const uint32_t sampledTriCount = std::max<uint32_t>(1u, (triangleCount + triStep - 1u) / triStep);
			const float meshBaseInjectionNormalize = std::clamp(r_giMeshBaseInjectionNormalization._val.f32, 0.0f, 1.0f);
			const float meshSunInjectionNormalize = std::clamp(r_giMeshSunInjectionNormalization._val.f32, 0.0f, 1.0f);
			const float meshBaseMinScale = std::clamp(r_giMeshBaseInjectionMinScale._val.f32, 0.0f, 1.0f);
			const float meshSunMinScale = std::clamp(r_giMeshSunInjectionMinScale._val.f32, 0.0f, 1.0f);
			const float meshBaseInjectionScale = std::lerp(
				1.0f,
				std::max(meshBaseMinScale, 1.0f / static_cast<float>(sampledTriCount)),
				meshBaseInjectionNormalize);
			const float meshSunInjectionScale = std::lerp(
				1.0f,
				std::max(meshSunMinScale, 1.0f / static_cast<float>(sampledTriCount)),
				meshSunInjectionNormalize);
			const auto& worldTM = entity->GetWorldTM();
			const math::Vector2 uvScale = smc->GetUVScale();
			uint32_t materialProxyIndex = 0u;
			if (auto materialProxyIt = meshMaterialProxyIndex.find(smc); materialProxyIt != meshMaterialProxyIndex.end())
			{
				materialProxyIndex = materialProxyIt->second;
			}
			const MaterialTriangleAlbedoCacheEntry* matAlbedoData = nullptr;
			if (!gpuComputeBaseSunEnabled)
			{
				matAlbedoData = &getMaterialAlbedoData(material);
			}
			const auto& entityAabb = entity->GetWorldAABB();
			const math::Vector3 entityCenter(entityAabb.Center.x, entityAabb.Center.y, entityAabb.Center.z);
			const math::Vector3 entityExtents(entityAabb.Extents.x, entityAabb.Extents.y, entityAabb.Extents.z);
			const math::Vector3 entityMin = entityCenter - entityExtents;
			const math::Vector3 entityMax = entityCenter + entityExtents;
			std::vector<uint32_t> meshLocalLightIndices;
			if (!localLights.empty())
			{
				struct MeshLightCandidate
				{
					uint32_t lightIndex = 0u;
					float distanceSq = 0.0f;
				};
				std::vector<MeshLightCandidate> meshLightCandidates;
				meshLightCandidates.reserve(localLights.size());

				for (uint32_t lightIndex = 0u; lightIndex < static_cast<uint32_t>(localLights.size()); ++lightIndex)
				{
					const auto& light = localLights[lightIndex];
					float dx = 0.0f;
					if (light.position.x < entityMin.x)
						dx = entityMin.x - light.position.x;
					else if (light.position.x > entityMax.x)
						dx = light.position.x - entityMax.x;

					float dy = 0.0f;
					if (light.position.y < entityMin.y)
						dy = entityMin.y - light.position.y;
					else if (light.position.y > entityMax.y)
						dy = light.position.y - entityMax.y;

					float dz = 0.0f;
					if (light.position.z < entityMin.z)
						dz = entityMin.z - light.position.z;
					else if (light.position.z > entityMax.z)
						dz = light.position.z - entityMax.z;

					const float distanceSq = dx * dx + dy * dy + dz * dz;
					const float radiusSq = light.radius * light.radius;
					if (distanceSq > radiusSq)
						continue;

					MeshLightCandidate candidate = {};
					candidate.lightIndex = lightIndex;
					candidate.distanceSq = distanceSq;
					meshLightCandidates.push_back(candidate);
				}

				if (!meshLightCandidates.empty())
				{
					std::sort(meshLightCandidates.begin(), meshLightCandidates.end(),
						[](const MeshLightCandidate& a, const MeshLightCandidate& b)
						{
							return a.distanceSq < b.distanceSq;
						});
					const uint32_t maxPerMesh = static_cast<uint32_t>(std::max(1, r_giLocalLightMaxPerMesh._val.i32));
					const uint32_t keepCount = std::min<uint32_t>(maxPerMesh, static_cast<uint32_t>(meshLightCandidates.size()));
					meshLocalLightIndices.reserve(keepCount);
					for (uint32_t i = 0u; i < keepCount; ++i)
					{
						meshLocalLightIndices.push_back(meshLightCandidates[i].lightIndex);
					}
				}
			}
			const bool meshFullyInsideClip =
				(entityMin.x >= clipMin.x && entityMax.x <= clipMax.x) &&
				(entityMin.y >= clipMin.y && entityMax.y <= clipMax.y) &&
				(entityMin.z >= clipMin.z && entityMax.z <= clipMax.z);

			for (uint32_t tri = 0u; tri < triangleCount && out.size() < budget; tri += triStep)
			{
				const uint32_t i0 = indices[tri * 3u + 0u];
				const uint32_t i1 = indices[tri * 3u + 1u];
				const uint32_t i2 = indices[tri * 3u + 2u];
				if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
					continue;

				const math::Vector4 v0w = math::Vector4::Transform(vertices[i0]._position, worldTM);
				const math::Vector4 v1w = math::Vector4::Transform(vertices[i1]._position, worldTM);
				const math::Vector4 v2w = math::Vector4::Transform(vertices[i2]._position, worldTM);

				const math::Vector3 p0(v0w.x, v0w.y, v0w.z);
				const math::Vector3 p1(v1w.x, v1w.y, v1w.z);
				const math::Vector3 p2(v2w.x, v2w.y, v2w.z);
				math::Vector3 faceNormal = (p1 - p0).Cross(p2 - p0);
				const float faceNormalLengthSq = faceNormal.LengthSquared();
				if (faceNormalLengthSq <= 1e-8f)
					continue;
				const float triArea = 0.5f * std::sqrt(faceNormalLengthSq);
				const float voxelAreaRef = std::max(1e-6f, voxelSizeWorld * voxelSizeWorld);
				const float triAreaNorm = std::clamp(std::sqrt(triArea / voxelAreaRef), 0.15f, 1.0f);
				const float smallTriDamp = std::clamp(r_giBaseSunSmallTriangleDamp._val.f32, 0.0f, 1.0f);
				const float baseSunAreaWeight = std::lerp(1.0f, triAreaNorm, smallTriDamp);
				faceNormal.Normalize();

				math::Vector3 triAlbedo = math::Vector3(0.75f, 0.75f, 0.75f);
				math::Vector3 triBaseInjection = math::Vector3::Zero;
				math::Vector3 sunEnergyTint = math::Vector3::Zero;
				float directSunBounce = 0.0f;
				float directionalDiffuseBounce = 0.0f;
				if (gpuComputeBaseSunEnabled)
				{
					if (materialProxyIndex < _giMaterialProxies.size())
					{
						const auto& materialProxy = _giMaterialProxies[materialProxyIndex];
						triAlbedo.x = std::clamp(materialProxy.diffuse.x, 0.03f, 1.0f);
						triAlbedo.y = std::clamp(materialProxy.diffuse.y, 0.03f, 1.0f);
						triAlbedo.z = std::clamp(materialProxy.diffuse.z, 0.03f, 1.0f);
					}
				}
				else
				{
					const float u0 = vertices[i0]._texcoord.x * uvScale.x;
					const float v0 = vertices[i0]._texcoord.y * uvScale.y;
					const float u1 = vertices[i1]._texcoord.x * uvScale.x;
					const float v1 = vertices[i1]._texcoord.y * uvScale.y;
					const float u2 = vertices[i2]._texcoord.x * uvScale.x;
					const float v2 = vertices[i2]._texcoord.y * uvScale.y;
					triAlbedo = sampleLinearAlbedo(*matAlbedoData, (u0 + u1 + u2) / 3.0f, (v0 + v1 + v2) / 3.0f);
					triAlbedo.x = std::clamp(triAlbedo.x, 0.03f, 1.0f);
					triAlbedo.y = std::clamp(triAlbedo.y, 0.03f, 1.0f);
					triAlbedo.z = std::clamp(triAlbedo.z, 0.03f, 1.0f);
					const float triAlbedoMax = std::max(triAlbedo.x, std::max(triAlbedo.y, triAlbedo.z));
					const float triAlbedoMin = std::min(triAlbedo.x, std::min(triAlbedo.y, triAlbedo.z));
					const float triAlbedoChroma = std::max(0.0f, triAlbedoMax - triAlbedoMin);
					const float triAlbedoLuma = std::clamp(triAlbedo.Dot(math::Vector3(0.2126f, 0.7152f, 0.0722f)), 0.02f, 1.0f);
					const float colourBleedBoost = std::clamp(1.0f + triAlbedoChroma * colourBleedStrength * (0.75f + bleedBoost * 0.10f), 1.0f, 3.0f);
					const float unlitBase = std::max(0.0f, r_giUnlitAlbedoInjection._val.f32);
					triBaseInjection = (
						math::Vector3(1.0f, 1.0f, 1.0f) * (triAlbedoLuma * diffuseInject * unlitBase * (0.55f + bleedBoost * 0.30f) * colourBleedBoost) +
						(emissiveTint * emissiveStrength * emissiveInject)) * clipAttenuation;
					sunEnergyTint = sunTint * (triAlbedoLuma * clipAttenuation);
					const float sunFacing = std::max(faceNormal.Dot(-sunDirection), 0.0f);
					directSunBounce = sunFacing * sunStrength * sunInject * (0.50f + bleedBoost * 0.34f) * sunDirectionalBoost;
					directionalDiffuseBounce = sunFacing * sunStrength * diffuseInject * (0.28f + bleedBoost * 0.18f) * sunDirectionalBoost * colourBleedBoost;
				}

				if (!meshFullyInsideClip)
				{
					const math::Vector3 triMin(
						std::min(p0.x, std::min(p1.x, p2.x)),
						std::min(p0.y, std::min(p1.y, p2.y)),
						std::min(p0.z, std::min(p1.z, p2.z)));
					const math::Vector3 triMax(
						std::max(p0.x, std::max(p1.x, p2.x)),
						std::max(p0.y, std::max(p1.y, p2.y)),
						std::max(p0.z, std::max(p1.z, p2.z)));
					if (triMax.x < clipMin.x || triMin.x > clipMax.x ||
						triMax.y < clipMin.y || triMin.y > clipMax.y ||
						triMax.z < clipMin.z || triMin.z > clipMax.z)
					{
						continue;
					}
				}

				GpuVoxelTriangle entry = {};
				entry.p0 = math::Vector4(p0.x, p0.y, p0.z, static_cast<float>(materialProxyIndex));
				entry.p1 = math::Vector4(p1.x, p1.y, p1.z, 1.0f);
				entry.p2 = math::Vector4(p2.x, p2.y, p2.z, 1.0f);

				if (gpuComputeBaseSunEnabled)
				{
					entry.radianceOpacity = math::Vector4(0.0f, 0.0f, 0.0f, 0.92f);
					entry.albedoWeight = math::Vector4(triAlbedo.x, triAlbedo.y, triAlbedo.z, triAreaNorm);
					out.push_back(entry);
					continue;
				}

				math::Vector3 localLightBounce = math::Vector3::Zero;
				float localLightInfluence = 0.0f;
				float localLightAttenuation = 0.0f;
				if (!meshLocalLightIndices.empty())
				{
					const math::Vector3 triCenter = (p0 + p1 + p2) / 3.0f;
					math::Vector3 localIrradiance = math::Vector3::Zero;
					for (uint32_t localLightIdx : meshLocalLightIndices)
					{
						const auto& light = localLights[localLightIdx];
						const math::Vector3 toLight = light.position - triCenter;
						const float dist2 = toLight.LengthSquared();
						const float radius2 = light.radius * light.radius;
						if (dist2 <= 1e-8f || dist2 >= radius2)
							continue;

						const float dist = std::sqrt(dist2);
						const math::Vector3 toLightDir = toLight / dist;
						const float ndotl = std::max(faceNormal.Dot(toLightDir), 0.0f);
						if (ndotl <= 0.0f)
							continue;

						float attenuation = std::clamp(1.0f - std::clamp(dist / light.radius, 0.0f, 1.0f), 0.0f, 1.0f);
						attenuation = attenuation * attenuation;
						if (light.isSpot)
						{
							const float spotCos = std::clamp(light.direction.Dot(-toLightDir), -1.0f, 1.0f);
							if (spotCos <= 0.0f)
								continue;
							const float coneAtten = std::pow(spotCos, light.coneExponent);
							// Match current direct spotlight behavior more closely (narrower effective footprint).
							attenuation *= coneAtten;
							attenuation *= coneAtten;
						}

						const float lightContribution = attenuation * ndotl;
						localIrradiance += light.colour * lightContribution;
						// Use a broader suppression term than raw irradiance so neutral base GI is reduced
						// across the local-light footprint (not only at normal-facing hotspots).
						const float suppressionInfluence = attenuation * (0.35f + 0.65f * ndotl);
						localLightInfluence = std::max(localLightInfluence, suppressionInfluence);
						localLightAttenuation = std::max(localLightAttenuation, attenuation);
					}

					const float localLum = localIrradiance.Dot(math::Vector3(0.2126f, 0.7152f, 0.0722f));
					localIrradiance = localIrradiance / (1.0f + localLum * 0.5f);
					const float localInject = std::max(0.0f, r_giLocalLightInjection._val.f32);
					localLightBounce = localIrradiance * (localInject * (0.55f + bleedBoost * 0.12f));
				}

				math::Vector3 sunInjection = disableSunInjection
					? math::Vector3::Zero
					: (sunEnergyTint * (directSunBounce + directionalDiffuseBounce) * baseSunAreaWeight * meshSunInjectionScale);
				math::Vector3 baseInjection = (disableBaseInjection ? math::Vector3::Zero : (triBaseInjection * baseSunAreaWeight * meshBaseInjectionScale)) + sunInjection;
				if (localLightsOnlyDebug)
				{
					baseInjection = math::Vector3::Zero;
					sunInjection = math::Vector3::Zero;
				}
				if (localLightsOnlyDebug && localLightBounce.LengthSquared() <= 1e-8f)
				{
					// In local-light debug mode, only keep triangles that actually receive local-light bounce.
					continue;
				}
				const float baseSuppression = std::clamp(r_giLocalLightBaseSuppression._val.f32, 0.0f, 1.0f);
				const float localBounceLum = localLightBounce.Dot(math::Vector3(0.2126f, 0.7152f, 0.0722f));
				const float localInfluenceCurve = std::sqrt(std::clamp(localLightInfluence, 0.0f, 1.0f));
				const float localAttenuationCurve = std::pow(std::clamp(localLightAttenuation, 0.0f, 1.0f), 0.40f);
				const float localLumCurve = std::clamp(localBounceLum * 0.8f, 0.0f, 1.0f);
				const float localDominance = std::clamp(std::max(std::max(localInfluenceCurve, localLumCurve), localAttenuationCurve) * 1.35f, 0.0f, 1.0f);
				const float sunSuppression = std::clamp(r_giLocalLightSunSuppression._val.f32, 0.0f, 1.0f);
				const float sunSuppressionMask = localDominance * sunSuppression;
				sunInjection *= (1.0f - sunSuppressionMask);
				baseInjection = (disableBaseInjection ? math::Vector3::Zero : (triBaseInjection * baseSunAreaWeight * meshBaseInjectionScale)) + sunInjection;
				const float baseScale = 1.0f - localDominance * baseSuppression;
				if (localBounceLum > 1e-5f)
				{
					const math::Vector3 lumaWeights(0.2126f, 0.7152f, 0.0722f);
					const float baseLum = baseInjection.Dot(lumaWeights);
					math::Vector3 localTint = localLightBounce / localBounceLum;
					localTint.x = std::clamp(localTint.x, 0.0f, 4.0f);
					localTint.y = std::clamp(localTint.y, 0.0f, 4.0f);
					localTint.z = std::clamp(localTint.z, 0.0f, 4.0f);
					const math::Vector3 baseTintedByLocal = localTint * baseLum;
					const float tintAmount = std::clamp(localDominance * baseSuppression * 0.80f, 0.0f, 1.0f);
					baseInjection = baseInjection * (1.0f - tintAmount) + baseTintedByLocal * tintAmount;
				}
				const math::Vector3 triInjectionFinal =
					baseInjection * baseScale + localLightBounce;
				entry.radianceOpacity = math::Vector4(
					std::clamp(triInjectionFinal.x, 0.0f, 32.0f),
					std::clamp(triInjectionFinal.y, 0.0f, 32.0f),
					std::clamp(triInjectionFinal.z, 0.0f, 32.0f),
					0.92f);
				entry.albedoWeight = math::Vector4(triAlbedo.x, triAlbedo.y, triAlbedo.z, triAreaNorm);
				out.push_back(entry);
			}
		}

		_cachedVoxelTriangles[levelIndex] = out;
		_cachedVoxelTrianglesValid[levelIndex] = true;
		_cachedVoxelTrianglesFrame[levelIndex] = _frameCounter;
		_stats.cpuTriangleBuildMs = ElapsedMs(buildStart);
		_stats.sourceTriangleCount = static_cast<uint32_t>(out.size());
		return static_cast<uint32_t>(out.size());
	}

	void DiffuseGI::RunGpuVoxelization(Scene* scene, uint32_t levelIndex)
	{
		if (!_created || !r_giGpuVoxelize._val.b || levelIndex >= ClipmapCount || scene == nullptr)
			return;

		const bool useGpuMaterialEval = r_giGpuMaterialEval._val.b;
		auto* voxelizeStage = useGpuMaterialEval
			? (_voxelizeEvalShader ? _voxelizeEvalShader->GetShaderStage(ShaderStage::ComputeShader) : nullptr)
			: (_voxelizeShader ? _voxelizeShader->GetShaderStage(ShaderStage::ComputeShader) : nullptr);
		auto* clearStage = _voxelClearShader ? _voxelClearShader->GetShaderStage(ShaderStage::ComputeShader) : nullptr;
		auto* propagateStage = _voxelPropagateShader ? _voxelPropagateShader->GetShaderStage(ShaderStage::ComputeShader) : nullptr;
		auto* shiftStage = _voxelShiftShader ? _voxelShiftShader->GetShaderStage(ShaderStage::ComputeShader) : nullptr;
		if (voxelizeStage == nullptr || clearStage == nullptr || propagateStage == nullptr || shiftStage == nullptr)
			return;

		auto& level = _clipmaps[levelIndex];
		if (level.radianceUav == nullptr || level.radianceScratchUav == nullptr ||
			level.albedoUav == nullptr || level.albedoScratchUav == nullptr ||
			level.radianceSrv == nullptr || level.radianceScratchSrv == nullptr ||
			level.albedoSrv == nullptr || level.albedoScratchSrv == nullptr ||
			level.radianceVolume == nullptr || level.radianceScratchVolume == nullptr ||
			level.albedoVolume == nullptr || level.albedoScratchVolume == nullptr)
			return;

		const uint32_t triangleCount = BuildGpuVoxelTriangleList(scene, levelIndex, _voxelTriangleUpload);
		const bool hasTriangles = (triangleCount > 0u) && EnsureGpuVoxelTriangleBuffer(triangleCount);
		_stats.sourceTriangleCount = triangleCount;
		_stats.candidateTriangleCount = triangleCount;
		if (!hasTriangles && level.initialized)
		{
			// Avoid one-frame GI collapse from transient empty triangle gathers after clipmap shifts.
			// Keep previous radiance and retry with a warm budget on subsequent frames.
			_clipmapWarmFramesRemaining[levelIndex] = std::max(_clipmapWarmFramesRemaining[levelIndex], (levelIndex == 0u) ? 3u : 1u);
			level.dirty = true;
			return;
		}

		auto* device = reinterpret_cast<ID3D11Device*>(g_pEnv->_graphicsDevice->GetNativeDevice());
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext());
		if (device == nullptr || context == nullptr)
			return;

		const uint32_t clearGroups = (level.resolution + 7u) / 8u;
		if (level.initialized && level.pendingShiftWs.LengthSquared() > 1e-8f && level.radianceSrv != nullptr && level.radianceScratchUav != nullptr)
		{
			const float voxelSize = (level.extent * 2.0f) / static_cast<float>(std::max(1u, level.resolution));
			const int32_t shiftX = static_cast<int32_t>(std::round(level.pendingShiftWs.x / std::max(voxelSize, 1e-6f)));
			const int32_t shiftY = static_cast<int32_t>(std::round(level.pendingShiftWs.y / std::max(voxelSize, 1e-6f)));
			const int32_t shiftZ = static_cast<int32_t>(std::round(level.pendingShiftWs.z / std::max(voxelSize, 1e-6f)));
			bool appliedShift = false;
			math::Vector3 appliedShiftWs = math::Vector3::Zero;
			if (shiftX != 0 || shiftY != 0 || shiftZ != 0)
			{
				if (_voxelShiftConstantBuffer != nullptr)
				{
					VoxelShiftConstants shiftConstants = {};
					shiftConstants.offsetX = shiftX;
					shiftConstants.offsetY = shiftY;
					shiftConstants.offsetZ = shiftZ;
					_voxelShiftConstantBuffer->Write(&shiftConstants, sizeof(shiftConstants));
				}

				ID3D11Buffer* shiftCb = _voxelShiftConstantBuffer ? reinterpret_cast<ID3D11Buffer*>(_voxelShiftConstantBuffer->GetNativePtr()) : nullptr;
				if (shiftCb != nullptr)
				{
					context->CSSetConstantBuffers(5, 1, &shiftCb);
				}
				ID3D11ShaderResourceView* shiftSrv[2] = { level.radianceSrv, level.albedoSrv };
				ID3D11UnorderedAccessView* shiftUav[2] = { level.radianceScratchUav, level.albedoScratchUav };
				context->CSSetShaderResources(0, 2, shiftSrv);
				context->CSSetUnorderedAccessViews(0, 2, shiftUav, nullptr);
				context->CSSetShader(reinterpret_cast<ID3D11ComputeShader*>(shiftStage->GetNativePtr()), nullptr, 0);
				context->Dispatch(clearGroups, clearGroups, clearGroups);

				ID3D11ShaderResourceView* nullShiftSrv[2] = {};
				ID3D11UnorderedAccessView* nullShiftUav[2] = {};
				ID3D11Buffer* nullShiftCb[1] = {};
				context->CSSetShaderResources(0, 2, nullShiftSrv);
				context->CSSetUnorderedAccessViews(0, 2, nullShiftUav, nullptr);
				context->CSSetConstantBuffers(5, 1, nullShiftCb);
				context->CSSetShader(nullptr, nullptr, 0);

				context->CopyResource(
					reinterpret_cast<ID3D11Resource*>(level.radianceVolume->GetNativePtr()),
					reinterpret_cast<ID3D11Resource*>(level.radianceScratchVolume->GetNativePtr()));
				context->CopyResource(
					reinterpret_cast<ID3D11Resource*>(level.albedoVolume->GetNativePtr()),
					reinterpret_cast<ID3D11Resource*>(level.albedoScratchVolume->GetNativePtr()));

				appliedShift = true;
				appliedShiftWs = math::Vector3(
					static_cast<float>(shiftX) * voxelSize,
					static_cast<float>(shiftY) * voxelSize,
					static_cast<float>(shiftZ) * voxelSize);
			}

			if (appliedShift)
			{
				level.center += appliedShiftWs;
			}
			else
			{
				// Pending shift was below one voxel after quantization; snap sampled center to target.
				level.center = level.targetCenter;
			}
			level.pendingShiftWs = level.targetCenter - level.center;
			if (level.pendingShiftWs.LengthSquared() <= 1e-8f)
			{
				level.pendingShiftWs = math::Vector3::Zero;
			}
		}

		if (hasTriangles)
		{
			const auto uploadStart = std::chrono::high_resolution_clock::now();
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (SUCCEEDED(context->Map(_voxelTriangleBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				memcpy(mapped.pData, _voxelTriangleUpload.data(), triangleCount * sizeof(GpuVoxelTriangle));
				context->Unmap(_voxelTriangleBuffer, 0);
				_stats.uploadBytes += static_cast<uint64_t>(triangleCount) * static_cast<uint64_t>(sizeof(GpuVoxelTriangle));
				_stats.cpuUploadMs += ElapsedMs(uploadStart);
			}
			else
			{
				return;
			}
		}

		uint32_t gpuLightCount = 0u;
		uint32_t gpuMaterialCount = 0u;
		if (useGpuMaterialEval)
		{
			_gpuGiMaterialUpload.clear();
			_gpuGiMaterialUpload.reserve(_giMaterialProxies.size());
			for (const auto& materialProxy : _giMaterialProxies)
			{
				GpuGiMaterial packedMaterial = {};
				packedMaterial.diffuse = materialProxy.diffuse;
				packedMaterial.emissive = materialProxy.emissive;
				_gpuGiMaterialUpload.push_back(packedMaterial);
			}
			gpuMaterialCount = static_cast<uint32_t>(_gpuGiMaterialUpload.size());
			if (gpuMaterialCount > 0u && EnsureGpuGiMaterialBuffer(gpuMaterialCount))
			{
				D3D11_MAPPED_SUBRESOURCE mappedMaterial = {};
				if (SUCCEEDED(context->Map(_giMaterialBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedMaterial)))
				{
					memcpy(mappedMaterial.pData, _gpuGiMaterialUpload.data(), gpuMaterialCount * sizeof(GpuGiMaterial));
					context->Unmap(_giMaterialBuffer, 0);
					_stats.uploadBytes += static_cast<uint64_t>(gpuMaterialCount) * static_cast<uint64_t>(sizeof(GpuGiMaterial));
				}
				else
				{
					gpuMaterialCount = 0u;
				}
			}
			else
			{
				gpuMaterialCount = 0u;
			}

			GiClipmapParams clipmapParams = {};
			clipmapParams.center = level.center;
			clipmapParams.extent = level.extent;
			clipmapParams.resolution = level.resolution;
			clipmapParams.levelIndex = levelIndex;
			clipmapParams.dirty = level.dirty;
			ExtractGiLocalLights(scene, clipmapParams, _giLightProxies);

			_gpuGiLightUpload.clear();
			_gpuGiLightUpload.reserve(_giLightProxies.size());

			struct RankedGpuLight
			{
				GpuGiLight packed = {};
				float score = 0.0f;
			};
			std::vector<RankedGpuLight> rankedLights;
			rankedLights.reserve(_giLightProxies.size());
			for (const auto& light : _giLightProxies)
			{
				GpuGiLight packed = {};
				packed.positionRadius = math::Vector4(light.position.x, light.position.y, light.position.z, std::max(0.01f, light.radius));
				packed.directionCone = math::Vector4(light.direction.x, light.direction.y, light.direction.z, std::clamp(light.coneExponent, 1.0f, 128.0f));
				packed.colourType = math::Vector4(light.colour.x, light.colour.y, light.colour.z, light.isSpot ? 1.0f : 0.0f);
				const float luminance = std::max(0.0f, light.colour.x * 0.2126f + light.colour.y * 0.7152f + light.colour.z * 0.0722f);
				const float radiusSq = std::max(0.01f, light.radius * light.radius);
				const math::Vector3 clampedPos(
					std::clamp(light.position.x, level.center.x - level.extent, level.center.x + level.extent),
					std::clamp(light.position.y, level.center.y - level.extent, level.center.y + level.extent),
					std::clamp(light.position.z, level.center.z - level.extent, level.center.z + level.extent));
				const float distSq = (light.position - clampedPos).LengthSquared();

				RankedGpuLight ranked = {};
				ranked.packed = packed;
				ranked.score = (luminance * radiusSq) / (1.0f + distSq);
				rankedLights.push_back(ranked);
			}
			std::sort(rankedLights.begin(), rankedLights.end(), [](const RankedGpuLight& a, const RankedGpuLight& b)
			{
				return a.score > b.score;
			});

			const uint32_t maxGpuLights = static_cast<uint32_t>(std::clamp(r_giGpuMaterialEvalMaxLights._val.i32, 1, 64));
			const uint32_t selectedLightCount = std::min<uint32_t>(maxGpuLights, static_cast<uint32_t>(rankedLights.size()));
			for (uint32_t i = 0u; i < selectedLightCount; ++i)
			{
				_gpuGiLightUpload.push_back(rankedLights[i].packed);
			}
			gpuLightCount = static_cast<uint32_t>(_gpuGiLightUpload.size());
			if (gpuLightCount > 0u && EnsureGpuGiLightBuffer(gpuLightCount))
			{
				D3D11_MAPPED_SUBRESOURCE mappedLight = {};
				if (SUCCEEDED(context->Map(_giLightBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedLight)))
				{
					memcpy(mappedLight.pData, _gpuGiLightUpload.data(), gpuLightCount * sizeof(GpuGiLight));
					context->Unmap(_giLightBuffer, 0);
					_stats.uploadBytes += static_cast<uint64_t>(gpuLightCount) * static_cast<uint64_t>(sizeof(GpuGiLight));
				}
				else
				{
					gpuLightCount = 0u;
				}
			}
			else
			{
				gpuLightCount = 0u;
			}
		}
		_stats.gpuLightCount = gpuLightCount;

		_constants.params0.w = static_cast<float>(levelIndex);
		_constants.clipCenterExtent[levelIndex].x = level.center.x;
		_constants.clipCenterExtent[levelIndex].y = level.center.y;
		_constants.clipCenterExtent[levelIndex].z = level.center.z;
		_constants.clipCenterExtent[levelIndex].w = level.extent;
		_constants.params6.w = static_cast<float>(gpuLightCount);
		if (_constantBuffer)
		{
			_constantBuffer->Write(&_constants, sizeof(_constants));
		}
		uint32_t injectionTriangleCount = triangleCount;
		ID3D11ShaderResourceView* injectionTriangleSrv = _voxelTriangleSrv;
		bool useCandidateIndirectDispatch = false;
		if (hasTriangles && r_giGpuCandidateGen._val.b)
		{
			bool candidateIndirectReady = false;
			const uint32_t candidateCount = BuildGpuVoxelCandidateList(levelIndex, triangleCount, candidateIndirectReady);
			if (_voxelCandidateSrv != nullptr)
			{
				injectionTriangleCount = candidateCount;
				injectionTriangleSrv = _voxelCandidateSrv;
				useCandidateIndirectDispatch = candidateIndirectReady && (_voxelCandidateDispatchArgs != nullptr);
			}
		}
		_stats.candidateTriangleCount = injectionTriangleCount;

		ID3D11Buffer* perFrameCb = nullptr;
		if (auto* cb = g_pEnv->_graphicsDevice->GetEngineConstantBuffer(EngineConstantBuffer::PerFrameBuffer); cb != nullptr)
		{
			perFrameCb = reinterpret_cast<ID3D11Buffer*>(cb->GetNativePtr());
		}
		ID3D11Buffer* perShadowCasterCb = nullptr;
		DirectionalLight* sun = scene->GetSunLight();
		if (sun != nullptr && sun->GetDoesCastShadows() && sun->GetInjectIntoGI())
		{
			if (auto* cb = g_pEnv->_graphicsDevice->GetEngineConstantBuffer(EngineConstantBuffer::PerShadowCasterBuffer); cb != nullptr)
			{
				PerShadowCasterBuffer shadowBufferData = {};
				BuildSunShadowCasterBuffer(sun, shadowBufferData);
				cb->Write(&shadowBufferData, sizeof(shadowBufferData));
				perShadowCasterCb = reinterpret_cast<ID3D11Buffer*>(cb->GetNativePtr());
			}
		}
		ID3D11Buffer* giCb = _constantBuffer ? reinterpret_cast<ID3D11Buffer*>(_constantBuffer->GetNativePtr()) : nullptr;
		ID3D11ShaderResourceView* sunShadowSrvs[6] = {};

		if (perFrameCb != nullptr)
			context->CSSetConstantBuffers(0, 1, &perFrameCb);
		{
			ID3D11Buffer* shadowCbToBind = perShadowCasterCb;
			context->CSSetConstantBuffers(2, 1, &shadowCbToBind);
		}
		if (giCb != nullptr)
			context->CSSetConstantBuffers(4, 1, &giCb);

		if (sun != nullptr && sun->GetDoesCastShadows() && sun->GetInjectIntoGI())
		{
			for (int32_t i = 0; i < 6; ++i)
			{
				auto* shadowMap = sun->GetShadowMap(i);
				if (shadowMap == nullptr)
					continue;

				sunShadowSrvs[i] = CreateShadowMapSrv(device, shadowMap->GetDepthMap());
			}
		}
		const uint32_t shadowSrvSlot = useGpuMaterialEval ? 5u : 3u;
		context->CSSetShaderResources(shadowSrvSlot, 6, sunShadowSrvs);

		// Preserve previous frame radiance for approximate sun-visibility queries during injection.
		context->CopyResource(
			reinterpret_cast<ID3D11Resource*>(level.radianceScratchVolume->GetNativePtr()),
			reinterpret_cast<ID3D11Resource*>(level.radianceVolume->GetNativePtr()));
		context->CopyResource(
			reinterpret_cast<ID3D11Resource*>(level.albedoScratchVolume->GetNativePtr()),
			reinterpret_cast<ID3D11Resource*>(level.albedoVolume->GetNativePtr()));

		const auto dispatchStart = std::chrono::high_resolution_clock::now();
		ID3D11UnorderedAccessView* clearUav[2] = { level.radianceUav, level.albedoUav };
		context->CSSetUnorderedAccessViews(0, 2, clearUav, nullptr);
		context->CSSetShader(reinterpret_cast<ID3D11ComputeShader*>(clearStage->GetNativePtr()), nullptr, 0);
		context->Dispatch(clearGroups, clearGroups, clearGroups);

		if (hasTriangles && injectionTriangleSrv != nullptr && (injectionTriangleCount > 0u || useCandidateIndirectDispatch))
		{
			if (useGpuMaterialEval)
			{
				ID3D11ShaderResourceView* voxelizeSrvsEval[5] =
				{
					injectionTriangleSrv,
					level.radianceScratchSrv,
					level.albedoScratchSrv,
					(gpuLightCount > 0u) ? _giLightSrv : nullptr,
					(gpuMaterialCount > 0u) ? _giMaterialSrv : nullptr
				};
				context->CSSetShaderResources(0, 5, voxelizeSrvsEval);
			}
			else
			{
				ID3D11ShaderResourceView* voxelizeSrvs[3] = { injectionTriangleSrv, level.radianceScratchSrv, level.albedoScratchSrv };
				context->CSSetShaderResources(0, 3, voxelizeSrvs);
			}
			context->CSSetUnorderedAccessViews(0, 2, clearUav, nullptr);
			context->CSSetShader(reinterpret_cast<ID3D11ComputeShader*>(voxelizeStage->GetNativePtr()), nullptr, 0);
			if (useCandidateIndirectDispatch)
			{
				context->DispatchIndirect(_voxelCandidateDispatchArgs, 0u);
			}
			else
			{
				const uint32_t groups = (injectionTriangleCount + 63u) / 64u;
				context->Dispatch(std::max<uint32_t>(groups, 1u), 1u, 1u);
			}
		}

		// Break UAV/SRV hazards between injection and propagation passes.
		ID3D11ShaderResourceView* nullSrvBetweenPasses[5] = {};
		ID3D11UnorderedAccessView* nullUavBetweenPasses[2] = {};
		context->CSSetShaderResources(0, 5, nullSrvBetweenPasses);
		context->CSSetUnorderedAccessViews(0, 2, nullUavBetweenPasses, nullptr);

		ID3D11ShaderResourceView* radianceSrcSrv = level.radianceSrv;
		if (radianceSrcSrv != nullptr)
		{
			ID3D11ShaderResourceView* srcSrv[1] = { radianceSrcSrv };
			ID3D11UnorderedAccessView* dstUav[1] = { level.radianceScratchUav };

			context->CSSetShaderResources(0, 1, srcSrv);
			context->CSSetUnorderedAccessViews(0, 1, dstUav, nullptr);
			context->CSSetShader(reinterpret_cast<ID3D11ComputeShader*>(propagateStage->GetNativePtr()), nullptr, 0);
			context->Dispatch(clearGroups, clearGroups, clearGroups);

			ID3D11ShaderResourceView* nullSrvForProp[1] = {};
			ID3D11UnorderedAccessView* nullUavForProp[1] = {};
			context->CSSetShaderResources(0, 1, nullSrvForProp);
			context->CSSetUnorderedAccessViews(0, 1, nullUavForProp, nullptr);
			context->CSSetShader(nullptr, nullptr, 0);

			context->CopyResource(
				reinterpret_cast<ID3D11Resource*>(level.radianceVolume->GetNativePtr()),
				reinterpret_cast<ID3D11Resource*>(level.radianceScratchVolume->GetNativePtr()));

			// Second propagation iteration to improve bounce spread in larger interiors.
			context->CSSetShaderResources(0, 1, srcSrv);
			context->CSSetUnorderedAccessViews(0, 1, dstUav, nullptr);
			context->CSSetShader(reinterpret_cast<ID3D11ComputeShader*>(propagateStage->GetNativePtr()), nullptr, 0);
			context->Dispatch(clearGroups, clearGroups, clearGroups);

			context->CSSetShaderResources(0, 1, nullSrvForProp);
			context->CSSetUnorderedAccessViews(0, 1, nullUavForProp, nullptr);
			context->CSSetShader(nullptr, nullptr, 0);

			context->CopyResource(
				reinterpret_cast<ID3D11Resource*>(level.radianceVolume->GetNativePtr()),
				reinterpret_cast<ID3D11Resource*>(level.radianceScratchVolume->GetNativePtr()));
		}

		ID3D11ShaderResourceView* nullSrvs[5] = {};
		ID3D11ShaderResourceView* nullShadowSrvs[6] = {};
		ID3D11UnorderedAccessView* nullUavs[2] = {};
		context->CSSetShaderResources(0, 5, nullSrvs);
		context->CSSetShaderResources(shadowSrvSlot, 6, nullShadowSrvs);
		context->CSSetUnorderedAccessViews(0, 2, nullUavs, nullptr);
		context->CSSetShader(nullptr, nullptr, 0);
		for (auto* srv : sunShadowSrvs)
		{
			SAFE_RELEASE(srv);
		}
		_stats.gpuDispatchMs += ElapsedMs(dispatchStart);

		level.initialized = true;
		level.dirty = false;
	}

	void DiffuseGI::RenderTracePass(const GBuffer& gbuffer, ITexture2D* beautyTarget)
	{
		if (!_traceShader || !_giHalfRes || beautyTarget == nullptr)
			return;

		auto* guiRenderer = g_pEnv->GetUIManager().GetRenderer();
		if (guiRenderer == nullptr)
			return;

		_giHalfRes->ClearRenderTargetView(math::Color(0, 0, 0, 0));
		g_pEnv->_graphicsDevice->SetRenderTarget(_giHalfRes);

		D3D11_VIEWPORT vp = {};
		vp.TopLeftX = 0.0f;
		vp.TopLeftY = 0.0f;
		vp.Width = static_cast<float>(_giHalfRes->GetWidth());
		vp.Height = static_cast<float>(_giHalfRes->GetHeight());
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		g_pEnv->_graphicsDevice->SetViewport(vp);

		g_pEnv->_graphicsDevice->UnbindAllPixelShaderResources();
		gbuffer.BindAsShaderResource();
		for (uint32_t i = 0; i < ClipmapCount; ++i)
		{
			g_pEnv->_graphicsDevice->SetTexture3D(_clipmaps[i].radianceVolume);
			g_pEnv->_graphicsDevice->SetTexture3D(_clipmaps[i].opacityVolume);
			g_pEnv->_graphicsDevice->SetTexture3D(_clipmaps[i].albedoVolume);
			g_pEnv->_graphicsDevice->SetTexture2D(_clipmaps[i].probeIrradianceAtlas);
			g_pEnv->_graphicsDevice->SetTexture2D(_clipmaps[i].probeVisibilityAtlas);
		}
		g_pEnv->_graphicsDevice->SetTexture2D(beautyTarget);
		g_pEnv->_graphicsDevice->SetConstantBufferPS(4, _constantBuffer);

		guiRenderer->StartFrame();
		guiRenderer->FullScreenTexturedQuad(nullptr, _traceShader.get());
		guiRenderer->EndFrame();

		g_pEnv->_graphicsDevice->UnbindAllPixelShaderResources();
	}

	void DiffuseGI::RenderResolvePass(const GBuffer& gbuffer)
	{
		if (!_resolveShader || !_giResolved || !_giHistory || !_giHalfRes)
			return;

		auto* guiRenderer = g_pEnv->GetUIManager().GetRenderer();
		if (guiRenderer == nullptr)
			return;

		_giResolved->ClearRenderTargetView(math::Color(0, 0, 0, 0));
		g_pEnv->_graphicsDevice->SetRenderTarget(_giResolved);

		D3D11_VIEWPORT vp = {};
		vp.TopLeftX = 0.0f;
		vp.TopLeftY = 0.0f;
		vp.Width = static_cast<float>(_giResolved->GetWidth());
		vp.Height = static_cast<float>(_giResolved->GetHeight());
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		g_pEnv->_graphicsDevice->SetViewport(vp);

		g_pEnv->_graphicsDevice->UnbindAllPixelShaderResources();
		g_pEnv->_graphicsDevice->SetTexture2D(_giHalfRes);
		g_pEnv->_graphicsDevice->SetTexture2D(_giHistory);
		g_pEnv->_graphicsDevice->SetTexture2D(gbuffer.GetVelocity());
		g_pEnv->_graphicsDevice->SetTexture2D(gbuffer.GetNormal());
		g_pEnv->_graphicsDevice->SetConstantBufferPS(4, _constantBuffer);

		guiRenderer->StartFrame();
		guiRenderer->FullScreenTexturedQuad(nullptr, _resolveShader.get());
		guiRenderer->EndFrame();

		_giResolved->CopyTo(_giHistory);
		g_pEnv->_graphicsDevice->UnbindAllPixelShaderResources();
	}

	void DiffuseGI::CompositeToBeauty(ITexture2D* beautyTarget)
	{
		if (beautyTarget == nullptr || _giResolved == nullptr)
			return;

		const int32_t debugView = r_giDebugView._val.i32;
		if (debugView == 1 || debugView == 3 || debugView == 4)
		{
			auto* guiRenderer = g_pEnv->GetUIManager().GetRenderer();
			if (guiRenderer == nullptr)
				return;

			g_pEnv->_graphicsDevice->SetRenderTarget(beautyTarget);
			g_pEnv->_graphicsDevice->SetBlendState(BlendState::Opaque);
			guiRenderer->StartFrame();
			guiRenderer->FullScreenTexturedQuad(_giResolved, _fullScreenShader.get());
			guiRenderer->EndFrame();
			return;
		}

		_giResolved->BlendTo_Additive(beautyTarget, nullptr);
	}

	void DiffuseGI::DebugDrawProbeGrid(uint32_t levelIndex) const
	{
		if (r_giDebugView._val.i32 != 2 || g_pEnv->_debugRenderer == nullptr || levelIndex >= ClipmapCount)
			return;

		const auto& level = _clipmaps[levelIndex];
		const float extent = level.extent;
		const float stepX = (extent * 2.0f) / static_cast<float>(ProbeGridX);
		const float stepY = (extent * 2.0f) / static_cast<float>(ProbeGridY);
		const float stepZ = (extent * 2.0f) / static_cast<float>(ProbeGridZ);
		const float markerExtent = std::max(0.15f, std::min(stepX, std::min(stepY, stepZ)) * 0.075f);

		const uint32_t stride = std::max(1u, 3u - static_cast<uint32_t>(r_giQuality._val.i32));
		for (uint32_t z = 0; z < ProbeGridZ; z += stride)
		{
			for (uint32_t y = 0; y < ProbeGridY; y += stride)
			{
				for (uint32_t x = 0; x < ProbeGridX; x += stride)
				{
					math::Vector3 center = level.center;
					center.x += (-extent) + (x + 0.5f) * stepX;
					center.y += (-extent) + (y + 0.5f) * stepY;
					center.z += (-extent) + (z + 0.5f) * stepZ;

					dx::BoundingBox marker(center, math::Vector3(markerExtent, markerExtent, markerExtent));
					g_pEnv->_debugRenderer->DrawAABB(marker, math::Color(HEX_RGBA_TO_FLOAT4(96, 210, 255, 220)));
				}
			}
		}
	}

	void DiffuseGI::Render(Scene* scene, Camera* camera, const GBuffer& gbuffer, ITexture2D* beautyTarget)
	{
		if (!_created || !r_giEnable._val.b || scene == nullptr || camera == nullptr || beautyTarget == nullptr)
			return;

		if (!_traceShader || !_resolveShader || !_fullScreenShader || !_giHalfRes || !_giResolved || !_giHistory || !_constantBuffer)
		{
			LOG_WARN("DiffuseGI is enabled but resources are incomplete; skipping frame.");
			return;
		}

		RenderTracePass(gbuffer, beautyTarget);
		RenderResolvePass(gbuffer);
		CompositeToBeauty(beautyTarget);
		DebugDrawProbeGrid(_activeClipmap);
	}
}
