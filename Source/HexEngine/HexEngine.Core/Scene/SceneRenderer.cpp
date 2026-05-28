
#include "SceneRenderer.hpp"
#include "../HexEngine.hpp"
#include "../Entity/Component/SpotLight.hpp"
#include "../Entity/Component/PointLight.hpp"
#include "../Entity/Component/DecalComponent.hpp"
#include "../Graphics/IVertexBuffer.hpp"
#include "../Graphics/IIndexBuffer.hpp"
#include "../Math/FloatMath.hpp"
#include <fastnoiselite/Cpp/FastNoiseLite.h>
#include <cstdint>
#include <unordered_set>
#include <algorithm>
#include <vector>

namespace HexEngine
{
	const int32_t MaxShadowCasters = 4;

	HVar env_zenithExponent("env_zenithExponent", "Atmospheric zenith component", 4.12f, 1.0f, 10.0f);
	HVar env_atmospherePreset("env_atmospherePreset", "Named atmosphere preset selector", 0, 0, 4);
	HVar env_anisotropicIntensity("env_anisotropicIntensity", "Atmospheric scattering intensity", 0.38f, 0.0f, 10.0f);
	HVar env_density("env_density", "The density of the atmosphere", 0.11f, 0.0f, 4.0f);
	HVar env_rayleighStrength("env_rayleighStrength", "Strength of Rayleigh scattering in the physical atmosphere", 1.0f, 0.0f, 4.0f);
	HVar env_mieStrength("env_mieStrength", "Strength of Mie scattering / aerial haze in the physical atmosphere", 1.0f, 0.0f, 4.0f);
	HVar env_ambientSkyStrength("env_ambientSkyStrength", "Amount of ambient sky fill added to atmospheric scattering", 1.0f, 0.0f, 4.0f);
	HVar env_sunHazeStrength("env_sunHazeStrength", "Strength of sun-facing atmospheric haze in the physical sky", 1.0f, 0.0f, 4.0f);
	HVar env_sunsetWarmStrength("env_sunsetWarmStrength", "Strength of warm sunset colours near the sun and horizon", 1.0f, 0.0f, 4.0f);
	HVar env_sunsetCoolStrength("env_sunsetCoolStrength", "Strength of cool purple/violet sunset colours away from the sun", 1.0f, 0.0f, 4.0f);
	HVar env_sunsetGlowStrength("env_sunsetGlowStrength", "Strength of the sunset sun halo and dusk glow", 1.0f, 0.0f, 4.0f);
	HVar env_volumetricLighting("r_volumetricLighting", "Enable or disable volumetric lighting", true, false, true);
	HVar env_volumetricScattering("env_volumetricScattering", "The amount of scattering used in volumetric lighting calculations", -0.43f, -2.0f, 2.0f);
	HVar env_volumetricStrength("env_volumetricStrength", "The strength multiplier of volumetric lighting", 1.0f, 0.1f, 5.0f);
	HVar env_volumetricSteps("env_volumetricSteps", "The number of iterations over which to calculate volumetric lighting", 100.0f, 10.0f, 500.0f);
	HVar r_volumetricQuality("r_volumetricQuality", "Volumetric quality preset (0 = performance, 1 = balanced, 2 = quality)", 1, 0, 2);
	HVar env_volumetricPointInsideMin("env_volumetricPointInsideMin", "Point-light volumetric gain when camera is at light center", 0.58f, 0.0f, 2.0f);
	HVar env_volumetricPointInsideMax("env_volumetricPointInsideMax", "Point-light volumetric gain when camera is near light radius edge", 0.92f, 0.0f, 2.0f);
	HVar env_volumetricSpotInsideMin("env_volumetricSpotInsideMin", "Spot-light volumetric gain when camera is at light center", 0.52f, 0.0f, 2.0f);
	HVar env_volumetricSpotInsideMax("env_volumetricSpotInsideMax", "Spot-light volumetric gain when camera is near light radius edge", 0.88f, 0.0f, 2.0f);
	HVar env_waterNormalInfluence("env_waterNormalInfluence", "The strength of the normal maps when rendering water", 0.4f, 0.0f, 4.0f);
	HVar env_volumetricStepIncrement("env_volumetricStepIncrement", "Global scale multiplier applied to adaptive volumetric ray-march step size", 1.0f, 0.1f, 100.0f);
	HVar r_cloudEnable("r_cloudEnable", "Enable or disable volumetric cloud rendering", true, false, true);
	HVar r_cloudFollowCameraXZ("r_cloudFollowCameraXZ", "Anchor cloud volume to camera X/Z position", true, false, true);
	HVar r_cloudCastShadows("r_cloudCastShadows", "Enable cloud shadows on scene lighting", true, false, true);
	HVar r_cloudShadowStrength("r_cloudShadowStrength", "Strength of cloud-cast shadows on scene lighting", 0.6f, 0.0f, 1.0f);
	HVar r_cloudShadowSteps("r_cloudShadowSteps", "Cloud shadow ray-march step count", 6, 1, 32);
	HVar r_cloudQuality("r_cloudQuality", "Cloud quality preset (0 = performance, 1 = balanced, 2 = quality)", 1, 0, 2);
	HVar r_cloudAabbMin("r_cloudAabbMin", "Minimum world-space bounds of cloud AABB", math::Vector3(-600.0f, 100.0f, -600.0f), math::Vector3(-25000.0f, -500.0f, -25000.0f), math::Vector3(25000.0f, 8000.0f, 25000.0f));
	HVar r_cloudAabbMax("r_cloudAabbMax", "Maximum world-space bounds of cloud AABB", math::Vector3(600.0f, 200.0f, 600.0f), math::Vector3(-25000.0f, -500.0f, -25000.0f), math::Vector3(25000.0f, 8000.0f, 25000.0f));
	HVar r_cloudDensity("r_cloudDensity", "Cloud density multiplier", 1.0f, 0.05f, 8.0f);
	HVar r_cloudCoverage("r_cloudCoverage", "Cloud coverage amount", 0.56f, 0.01f, 1.0f);
	HVar r_cloudErosion("r_cloudErosion", "Small-scale erosion amount", 0.34f, 0.0f, 1.0f);
	HVar r_cloudLightAbsorption("r_cloudLightAbsorption", "Absorption multiplier for cloud self-shadowing", 0.14f, 0.0f, 8.0f);
	HVar r_cloudViewAbsorption("r_cloudViewAbsorption", "Absorption multiplier along view ray inside clouds", 0.42f, 0.0f, 8.0f);
	HVar r_cloudPowderStrength("r_cloudPowderStrength", "Powder term to brighten cloud edges", 0.38f, 0.0f, 2.0f);
	HVar r_cloudAmbientStrength("r_cloudAmbientStrength", "Ambient skylight contribution for cloud interiors", 0.52f, 0.0f, 2.0f);
	HVar r_cloudShadowFloor("r_cloudShadowFloor", "Minimum transmittance floor for cloud self-shadowing", 0.20f, 0.0f, 1.0f);
	HVar r_cloudAnisotropy("r_cloudAnisotropy", "Anisotropy term for cloud phase function", 0.35f, -0.95f, 0.95f);
	HVar r_cloudSilverLiningStrength("r_cloudSilverLiningStrength", "Stylized rim/silver-lining strength", 0.55f, 0.0f, 4.0f);
	HVar r_cloudSilverLiningExponent("r_cloudSilverLiningExponent", "Stylized rim/silver-lining falloff", 5.0f, 0.5f, 16.0f);
	HVar r_cloudMultiScatterStrength("r_cloudMultiScatterStrength", "Stylized multi-scattering lift in dense cloud regions", 0.45f, 0.0f, 3.0f);
	HVar r_cloudHeightTintStrength("r_cloudHeightTintStrength", "Stylized height-based cloud tint strength", 0.35f, 0.0f, 1.0f);
	HVar r_cloudTintWarmth("r_cloudTintWarmth", "Warm bias for cloud lift/tint lighting", 0.28f, 0.0f, 1.0f);
	HVar r_cloudSkyTintInfluence("r_cloudSkyTintInfluence", "How much skylight chroma influences cloud tint", 0.42f, 0.0f, 1.0f);
	HVar r_cloudDirectionalDiffuse("r_cloudDirectionalDiffuse", "Directional-derivative diffuse lighting strength", 0.78f, 0.0f, 2.0f);
	HVar r_cloudAmbientOcclusion("r_cloudAmbientOcclusion", "Local ambient occlusion strength for cloud interiors", 0.45f, 0.0f, 1.0f);
	HVar r_cloudShapeScale("r_cloudShapeScale", "Base cloud shape noise scale", 0.00017f, 0.00001f, 0.01f);
	HVar r_cloudDetailScale("r_cloudDetailScale", "Cloud detail noise scale", 0.00125f, 0.00005f, 0.05f);
	HVar r_cloudWindDirection("r_cloudWindDirection", "Cloud wind direction", math::Vector3(1.0f, 0.0f, 0.15f), math::Vector3(-1.0f, -1.0f, -1.0f), math::Vector3(1.0f, 1.0f, 1.0f));
	HVar r_cloudWindSpeed("r_cloudWindSpeed", "Cloud wind speed", 34.0f, 0.0f, 500.0f);
	HVar r_cloudAnimationSpeed("r_cloudAnimationSpeed", "Cloud animation speed multiplier", 1.0f, 0.0f, 10.0f);
	HVar r_cloudViewSteps("r_cloudViewSteps", "Base view ray-march step count", 72.0f, 8.0f, 256.0f);
	HVar r_cloudLightSteps("r_cloudLightSteps", "Base light ray-march step count", 12.0f, 2.0f, 64.0f);
	HVar r_cloudStepScale("r_cloudStepScale", "Global cloud march step scale", 1.0f, 0.25f, 8.0f);
	HVar r_cloudMaxDistance("r_cloudMaxDistance", "Maximum cloud trace distance from camera", 20000.0f, 100.0f, 100000.0f);
	HVar r_gamma("r_gamma", "The amount of gamma correction to apply", 2.2f, 0.1f, 5.0f);
	HVar r_shadowCascades("r_shadowCascades", "The number of cascades to calculate with shadow mapping", 4, 1, 4);
	HVar r_shadowCascadeRange("r_shadowCascadeRange", "The depth of one shadow cascade, except the last (which will occupy all remaining space", 100.0f, 1.0f, 10000.0f);
	HVar r_penumbraFilterMaxSize("r_penumbraFilterMaxSize", "The maximum filter size for penumbra calculation", 0.002f, 0.0f, 10.0f);
	HVar r_shadowFilterMaxSize("r_shadowFilterMaxSize", "The maximum size of the shadow filter", 0.21f, 0.0f, 10.0f);
	HVar r_shadowBiasMultiplier("r_shadowBiasMultiplier", "The bias multiplier to use when calculating normal offset", 0.0002f, 0.0f, 1.0f);
	HVar r_shadowCascadeBlendRange("r_shadowCascadeBlendRange", "The distance to use for blending shadow cascades together", 10.0f, 1.0f, 1000.0f);
	HVar r_debugScene("r_debugScene", "Draw debugging info for the current scene", 0, 0, 1);
	HVar r_waterResolution("r_waterResolution", "The resolution multiplier at which to render water, a value of 1.0f is full resolution", 1.0f, 0.1f, 1.0f);
	HVar r_bloomLuminanceThreshold("r_bloomLuminanceThreshold", "Reference luminance where physically-based bloom starts to respond strongly", 1.0f, 0.0f, 32.0f);
	HVar r_bloomPhysicalIntensity("r_bloomPhysicalIntensity", "Strength multiplier for physically-based bloom", 0.35f, 0.0f, 8.0f);
	HVar r_bloomPhysicalClamp("r_bloomPhysicalClamp", "Clamp physically-based bloom prefilter output (0 disables clamp)", 0.0f, 0.0f, 128.0f);
	HVar r_fxaa("r_fxaa", "Whether or not to use the FXAA anti-aliasing method", 1, 0, 1);
	HVar r_fog("r_fog", "Enable or disable fog effect", 1, 0, 1);
	HVar r_fogDensity("r_fogDensity", "How dense the fog should be", 0.0030f, 0.0f, 1.0f);
	HVar r_fogStartDistance("r_fogStartDistance", "Distance from the camera before height fog starts accumulating", 45.0f, 0.0f, 5000.0f);
	HVar r_fogHeightDensity("r_fogHeightDensity", "Additional distance-scaled fog density contributed by low-altitude air", 0.00310f, 0.0f, 1.0f);
	HVar r_fogHeightFalloff("r_fogHeightFalloff", "How quickly height fog thins out as altitude increases", 0.0150f, 0.0001f, 1.0f);
	HVar r_fogHeightPivot("r_fogHeightPivot", "Reference world height used for the fog height falloff curve", 18.0f, -5000.0f, 5000.0f);
	HVar r_fogSkyTintInfluence("r_fogSkyTintInfluence", "How strongly the sampled atmosphere colour tints distant fog", 0.26f, 0.0f, 1.0f);
	HVar r_fogFarDesaturate("r_fogFarDesaturate", "How much distant fog desaturates toward luminance", 0.18f, 0.0f, 1.0f);
	HVar r_fogAtmosphereBlendStart("r_fogAtmosphereBlendStart", "Distance where fog begins blending fully into the atmosphere colour", 220.0f, 0.0f, 50000.0f);
	HVar r_fogAtmosphereBlendRange("r_fogAtmosphereBlendRange", "Distance span over which far fog transitions into the atmosphere colour", 380.0f, 1.0f, 50000.0f);
	HVar r_fogSunsetRange("r_fogSunsetRange", "Sun elevation range around horizon where sunset fog warmth ramps in/out", 0.30f, 0.01f, 1.0f);
	HVar r_fogSunsetWarmthStrength("r_fogSunsetWarmthStrength", "Strength of sunset warm hue contribution to atmospheric fog tint", 0.30f, 0.0f, 1.0f);
	HVar r_fogFarAtmosphereMatchStrength("r_fogFarAtmosphereMatchStrength", "How strongly very distant fog converges toward atmosphere colour", 0.60f, 0.0f, 1.0f);
	HVar r_lodPartition("r_lodPartition", "The value that determines where LOD partitions occur", 250.0f, 10.0f, 5000.0f);
	HVar r_frustumSphereBoundsMultiplier("r_frustumSphereBoundsMultiplier", "The multiplier applied to the frustum bounds in order to calculate culling", 1.15f, 1.0f, 4.0f);
	HVar r_shadowMinimumLodThreshold("r_shadowMinimumLodLevel", "The lowest LOD level allowed for shadow maps. A high number will improve performance at the expensve of shadow fidelity", 0, 0, 3);
	HVar r_taa("r_taa", "Enable or disable temporal anti-aliasing", true, false, true);
	HVar r_shadowNearClip("r_shadowNearClip", "How much clipping offset to apply to directional lights, larger scenes typically require a higher value", 150.0f, -1000.0f, 1000.0f);
	HVar r_colourFilter("r_colourFilter", "The filter colour to use for colour grading", math::Vector3(1.00f, 0.98f, 0.97f), math::Vector3(0.0f), math::Vector3(1.0f));
	HVar r_shadowSamples("r_shadowSamples", "How many samples to use in shadow map filtering", 32, 2, 128);
	// Screen-space contact shadows - fills the near-camera detail gap PCSS cascades
	// can't resolve. Cheap (one extra ray-march in the deferred light pass) and
	// hugely improves perceived shadow quality on close-up geometry.
	// Contact shadows ship OFF by default. The current implementation uses
	// interleaved-gradient-noise screen-space jitter, which is camera-space
	// stable on a static camera but produces per-pixel noise that shifts as
	// the camera moves. Under TAA the per-pixel noise should average out, but
	// on grazing-angle geometry (notably volumetric-terrain slopes at mid-
	// depth) adjacent pixels disagree about whether the ray enters the
	// terrain, and TAA can't fully reconcile that frame-to-frame - visible
	// flicker on the slope. Re-enabling needs a TAA-friendly noise source
	// (e.g. blue noise indexed by world position or frame-stable jitter)
	// and probably a distance fade so the cost+artifact concentrate near the
	// camera where contact shadows actually add value.
	HVar r_contactShadows("r_contactShadows", "Enable screen-space contact shadows on the directional light", false, false, true);
	HVar r_contactShadowSteps("r_contactShadowSteps", "Ray-march step count for contact shadows", 12, 4, 64);
	HVar r_contactShadowLength("r_contactShadowLength", "Maximum world-space length of the contact shadow ray (metres)", 1.5f, 0.05f, 32.0f);
	HVar r_contactShadowThickness("r_contactShadowThickness", "Thickness window for blocker acceptance (metres) - prevents see-through behind walls", 0.2f, 0.01f, 5.0f);
	// Distance fade for contact shadows. Pixels beyond this many metres from
	// the camera get a reduced contact-shadow contribution that smoothly fades
	// to zero over the next half of this range, hiding the screen-space jitter
	// noise on distant pixels (where terrain dominates the artifact). Tweakable
	// because aggressive fade nukes the effect entirely on long view distances.
	HVar r_contactShadowFadeStart("r_contactShadowFadeStart", "View-space depth (m) where contact shadows begin to fade out", 25.0f, 1.0f, 1000.0f);
	// Screen-space subsurface scattering. Cheap (2 fullscreen passes, 11 taps each)
	// but huge for character / foliage / wax fidelity. Gated by the per-pixel
	// features gbuffer so non-SSS pixels pay a single texture sample + early-out.
	HVar r_sss("r_sss", "Enable screen-space subsurface scattering post-process", true, false, true);
	HVar r_sssRadius("r_sssRadius", "World-space SSS scattering radius (metres)", 0.012f, 0.0f, 0.5f);
	HVar r_sssIntensity("r_sssIntensity", "Global multiplier on SSS blur intensity (final blended fraction)", 1.0f, 0.0f, 2.0f);

	// Bokeh depth-of-field. Single-pass, 32-tap Vogel disk gathered around each
	// pixel proportional to its circle-of-confusion. Runs after tonemap so the
	// bokeh discs reflect post-tonemap colour - matters because pre-tonemap HDR
	// highlights would saturate the gathered colour buckets and lose the bokeh
	// "ball" shape on bright sources. r_dofAperture = 0 disables (the shader's
	// early-out skips the gather for sharp pixels too, so the cost is one
	// texture read on the dominant in-focus region).
	HVar r_dof("r_dof", "Enable bokeh depth-of-field post-process", false, false, true);
	HVar r_dofFocusDistance("r_dofFocusDistance", "Depth of the in-focus plane (metres)", 8.0f, 0.1f, 1000.0f);
	HVar r_dofFocusRange("r_dofFocusRange", "Width of the fully-sharp band around the focus plane (metres)", 4.0f, 0.0f, 50.0f);
	// Aperture is the strongest dial here - at 1.0 a pixel at 2x focus distance
	// already reaches ~50% CoC, and the user wants the chunkiest blur usually
	// concentrated on the far field only. 0.4 is a subtle photo-realistic default
	// that gives noticeable but not overwhelming bokeh; users wanting cinema
	// shallow-DoF should bump to 1-2.
	HVar r_dofAperture("r_dofAperture", "Blur scale - bigger aperture = stronger out-of-focus blur", 0.4f, 0.0f, 8.0f);
	// maxBlur is the pixel radius at coc=1.0 (which the hyperbolic curve never
	// quite reaches). At 1080p, 8px gives a soft photographic bokeh; 16+ is
	// cinematic; 32+ is dreamy/extreme. The previous 24 default combined with
	// the old saturate() coc curve produced a near-uniform screen blur on most
	// scenes, which read as "grey screen".
	HVar r_dofMaxBlur("r_dofMaxBlur", "Maximum CoC radius in pixels (clamps far-field blur from exploding)", 8.0f, 1.0f, 96.0f);

	// Auto-puddles: procedural fullscreen pass driven by world-space noise +
	// surface normal + weather puddleAmount. Off by default to preserve existing
	// scene look; ticking the HVar (or future Settings UI toggle) starts painting
	// puddles wherever the surface is flat enough and the noise mask matches.
	HVar r_autoPuddles("r_autoPuddles", "Enable procedural puddles driven by weather + surface normal + noise", false, false, true);
	// Larger scale = bigger, sparser puddles. 5 m gives kerb-scale puddles; 15 m
	// gives big floods. Picks the world-space size of each noise cell.
	HVar r_autoPuddlesScale("r_autoPuddlesScale", "World-space noise scale for procedural puddles (metres per cell)", 5.0f, 0.5f, 50.0f);
	// Threshold cuts how much of the surface receives puddles. 0.55 means the top
	// ~45% of noise values become puddles - giving a typical wet-pavement look.
	// Higher = sparser puddles (only the very-puddle-prone spots).
	HVar r_autoPuddlesThreshold("r_autoPuddlesThreshold", "Noise threshold for procedural puddles (higher = sparser)", 0.55f, 0.0f, 0.95f);
	// Minimum dot(normal, world-up) for a pixel to be considered for puddles. 0.9
	// gives a ~25-degree allowance off horizontal which catches roads with normal
	// camber but skips walls. Drop towards 0.7 if you want puddles on sloped
	// surfaces too.
	HVar r_autoPuddlesNormalCutoff("r_autoPuddlesNormalCutoff", "Min surface normal.y for procedural puddles (1.0 = perfectly flat)", 0.9f, 0.0f, 1.0f);
	// Master opacity scale. 1.0 = full strength; lower for subtler "always-damp"
	// look or higher to exaggerate during heavy rain.
	HVar r_autoPuddlesOpacity("r_autoPuddlesOpacity", "Master opacity scale for procedural puddles", 1.0f, 0.0f, 1.0f);
	// How much puddles darken the underlying albedo. 0 = mat-only (just smoother),
	// 1 = full black puddle. 0.4 is a realistic "dark water on light pavement" look.
	HVar r_autoPuddlesDarken("r_autoPuddlesDarken", "Albedo darkening applied where puddles are (0 = none, 1 = full black)", 0.4f, 0.0f, 1.0f);
	// Debug / "look at the puddles without setting up rain" override. The shader
	// uses max(g_weatherSurface.puddleAmount, this) as its effective rain value -
	// so at 0 (default) the system follows the weather as normal, at 1 it forces
	// full-strength puddles regardless of the weather state. Useful for verifying
	// the system is wired correctly when no WeatherController is animating
	// puddleAmount yet.
	HVar r_autoPuddlesForceRain("r_autoPuddlesForceRain", "Override puddleAmount minimum (0 = follow weather, 1 = force full puddles)", 0.0f, 0.0f, 1.0f);

		static int32_t GetVolumetricEffectiveSteps()
		{
			const int32_t qualityPreset = r_volumetricQuality._val.i32 < 0 ? 0 : (r_volumetricQuality._val.i32 > 2 ? 2 : r_volumetricQuality._val.i32);
			const int32_t baseSteps = static_cast<int32_t>(env_volumetricSteps._val.f32);

			float stepScale = 1.0f;
			switch (qualityPreset)
			{
			case 0: // performance
				stepScale = 0.70f;
				break;
			case 2: // quality
				stepScale = 1.45f;
				break;
			case 1: // balanced
			default:
				stepScale = 1.00f;
				break;
			}

			int32_t effectiveSteps = static_cast<int32_t>(baseSteps * stepScale);
			if (effectiveSteps < 8)
				effectiveSteps = 8;
			if (effectiveSteps > 768)
				effectiveSteps = 768;

			return effectiveSteps;
		}

		struct CloudConstants
		{
			math::Vector4 boundsMin;
			math::Vector4 boundsMax;
			math::Vector4 params0; // x=density, y=coverage, z=erosion, w=maxDistance
			math::Vector4 params1; // x=absorption, y=powder, z=anisotropy, w=stepScale
			math::Vector4 params2; // x=shapeScale, y=detailScale, z=windSpeed, w=animationSpeed
			math::Vector4 params3; // x=viewAbsorption, y=ambientStrength, z=shadowFloor, w=phaseBoost
			math::Vector4 params4; // x=silverLiningStrength, y=silverLiningExponent, z=multiScatterStrength, w=heightTintStrength
			math::Vector4 params5; // x=tintWarmth, y=skyTintInfluence, z=directionalDiffuse, w=ambientOcclusion
			math::Vector4 windDirection; // xyz=windDir, w=qualityPreset
			math::Vector4 windOffset; // xyz=accumulated wind offset, w=reserved
			math::Vector4 marchParams; // x=viewSteps, y=lightSteps, z=reserved, w=reserved
		};

		static int32_t GetCloudQualityPreset()
		{
			return std::clamp(r_cloudQuality._val.i32, 0, 2);
		}

		static int32_t GetCloudEffectiveSteps(const float baseSteps, const int32_t minSteps, const int32_t maxSteps)
		{
			float scale = 1.0f;
			switch (GetCloudQualityPreset())
			{
			case 0:
				scale = 0.75f;
				break;
			case 2:
				scale = 1.35f;
				break;
			case 1:
			default:
				scale = 1.0f;
				break;
			}

			const int32_t steps = static_cast<int32_t>(baseSteps * scale);
			return std::clamp(steps, minSteps, maxSteps);
		}

		static bool BuildCloudConstants(Camera* camera, CloudConstants& constants)
		{
			static math::Vector3 accumulatedWindOffset = math::Vector3::Zero;
			static uint64_t lastCloudFrame = 0ull;

			if (camera == nullptr)
				return false;

			math::Vector3 boundsMin = r_cloudAabbMin._val.v3;
			math::Vector3 boundsMax = r_cloudAabbMax._val.v3;

			if (boundsMin.x > boundsMax.x) std::swap(boundsMin.x, boundsMax.x);
			if (boundsMin.y > boundsMax.y) std::swap(boundsMin.y, boundsMax.y);
			if (boundsMin.z > boundsMax.z) std::swap(boundsMin.z, boundsMax.z);

			if (r_cloudFollowCameraXZ._val.b)
			{
				const math::Vector3 originalCenter = (boundsMin + boundsMax) * 0.5f;
				const math::Vector3 halfExtent = (boundsMax - boundsMin) * 0.5f;
				const math::Vector3 cameraPos = camera->GetEntity() ? camera->GetEntity()->GetPosition() : math::Vector3::Zero;
				const math::Vector3 recentered(cameraPos.x, originalCenter.y, cameraPos.z);
				boundsMin = recentered - halfExtent;
				boundsMax = recentered + halfExtent;
			}

			const math::Vector3 extents = boundsMax - boundsMin;
			if (extents.x < 1.0f || extents.y < 1.0f || extents.z < 1.0f)
				return false;

			math::Vector3 windDir = r_cloudWindDirection._val.v3;
			if (windDir.LengthSquared() <= 0.0001f)
				windDir = math::Vector3::Forward;
			else
				windDir.Normalize();

			const uint64_t frameNow = (g_pEnv && g_pEnv->_timeManager) ? g_pEnv->_timeManager->_frameCount : 0ull;
			if (frameNow != lastCloudFrame)
			{
				const float dt = (g_pEnv && g_pEnv->_timeManager) ? std::clamp((float)g_pEnv->_timeManager->_frameTime, 0.0f, 0.1f) : (1.0f / 60.0f);
				const float windDistance = r_cloudWindSpeed._val.f32 * r_cloudAnimationSpeed._val.f32 * dt * 0.01f;
				accumulatedWindOffset += windDir * windDistance;
				lastCloudFrame = frameNow;
			}

			constants = {};
			constants.boundsMin = math::Vector4(boundsMin.x, boundsMin.y, boundsMin.z, 0.0f);
			constants.boundsMax = math::Vector4(boundsMax.x, boundsMax.y, boundsMax.z, 0.0f);
			constants.params0 = math::Vector4(
				r_cloudDensity._val.f32,
				r_cloudCoverage._val.f32,
				r_cloudErosion._val.f32,
				r_cloudMaxDistance._val.f32);
			constants.params1 = math::Vector4(
				r_cloudLightAbsorption._val.f32,
				r_cloudPowderStrength._val.f32,
				r_cloudAnisotropy._val.f32,
				r_cloudStepScale._val.f32);
			constants.params2 = math::Vector4(
				r_cloudShapeScale._val.f32,
				r_cloudDetailScale._val.f32,
				r_cloudWindSpeed._val.f32,
				r_cloudAnimationSpeed._val.f32);
			constants.params3 = math::Vector4(
				r_cloudViewAbsorption._val.f32,
				r_cloudAmbientStrength._val.f32,
				r_cloudShadowFloor._val.f32,
				1.55f);
			constants.params4 = math::Vector4(
				r_cloudSilverLiningStrength._val.f32,
				r_cloudSilverLiningExponent._val.f32,
				r_cloudMultiScatterStrength._val.f32,
				r_cloudHeightTintStrength._val.f32);
			constants.params5 = math::Vector4(
				r_cloudTintWarmth._val.f32,
				r_cloudSkyTintInfluence._val.f32,
				r_cloudDirectionalDiffuse._val.f32,
				r_cloudAmbientOcclusion._val.f32);
			constants.windDirection = math::Vector4(windDir.x, windDir.y, windDir.z, (float)GetCloudQualityPreset());
			constants.windOffset = math::Vector4(accumulatedWindOffset.x, accumulatedWindOffset.y, accumulatedWindOffset.z, 0.0f);
			constants.marchParams = math::Vector4(
				(float)GetCloudEffectiveSteps(r_cloudViewSteps._val.f32, 8, 256),
				(float)GetCloudEffectiveSteps(r_cloudLightSteps._val.f32, 2, 64),
				(float)GetCloudEffectiveSteps((float)r_cloudShadowSteps._val.i32, 1, 32),
				(r_cloudEnable._val.b && r_cloudCastShadows._val.b) ? r_cloudShadowStrength._val.f32 : 0.0f);

			return true;
		}

		static ITexture3D* CreateCloudNoiseVolume(int32_t resolution, int32_t seed, float frequency, int32_t octaves, float gain, float lacunarity)
		{
			FastNoiseLite noise;
			noise.SetSeed(seed);
			noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
			noise.SetFrequency(frequency);
			noise.SetFractalType(FastNoiseLite::FractalType_FBm);
			noise.SetFractalOctaves(octaves);
			noise.SetFractalGain(gain);
			noise.SetFractalLacunarity(lacunarity);

			std::vector<float> noiseData(static_cast<size_t>(resolution) * static_cast<size_t>(resolution) * static_cast<size_t>(resolution), 0.0f);

			for (int32_t z = 0; z < resolution; ++z)
			{
				for (int32_t y = 0; y < resolution; ++y)
				{
					for (int32_t x = 0; x < resolution; ++x)
					{
						const float n = noise.GetNoise(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
						const float normalized = std::clamp(n * 0.5f + 0.5f, 0.0f, 1.0f);
						const int32_t idx = (z * resolution * resolution) + (y * resolution) + x;
						noiseData[idx] = normalized;
					}
				}
			}

			D3D11_SUBRESOURCE_DATA initialData = {};
			initialData.pSysMem = noiseData.data();
			initialData.SysMemPitch = static_cast<UINT>(resolution * sizeof(float));
			initialData.SysMemSlicePitch = static_cast<UINT>(resolution * resolution * sizeof(float));

			return g_pEnv->_graphicsDevice->CreateTexture3D(
				resolution,
				resolution,
				resolution,
				DXGI_FORMAT_R32_FLOAT,
				1,
				D3D11_BIND_SHADER_RESOURCE,
				0,
				1,
				0,
				&initialData,
				D3D11_RTV_DIMENSION_UNKNOWN,
				D3D11_UAV_DIMENSION_UNKNOWN,
				D3D11_SRV_DIMENSION_TEXTURE3D);
		}
	HVar r_contrast("r_contrast", "How much contrast to apply to the final render", 1.0f, 0.1f, 3.0f);
	HVar r_exposure("r_exposure", "How much exposure to apply to the final render", 1.0f, 0.1f, 3.0f);
	HVar r_hueShift("r_hueShift", "How much to adjust the hue by in the final render", 0.0f, -3.0f, 3.0f);
	HVar r_saturation("r_saturation", "How much to saturate the final render", 1.05f, 0.1f, 3.0f);
	// HDR display calibration. scRGB linear is defined such that 1.0 = 80
	// nits, but DWM composition and Independent Flip pick different paper-
	// white targets, so the HDR tonemap shader needs to know the target
	// nits to scale into. 200 nits is the Windows 11 default; users can
	// adjust if they've moved Windows' "SDR content brightness" slider.
	// Peak nits should match the display's MaxLuminance - common values
	// are 600 (HDR600), 1000 (HDR1000), 1500+ (mastering displays).
	HVar r_hdrPaperWhiteNits("r_hdrPaperWhiteNits", "Target nits for post-tonemap 'screen white' on HDR displays (Win11 default 200)", 200.0f, 80.0f, 1000.0f);
	HVar r_hdrPeakNits("r_hdrPeakNits", "Display peak luminance for HDR highlight headroom (match display's HDR rating)", 1000.0f, 200.0f, 4000.0f);
	// Tonemap operator selector. Maps to ApplyTonemap in TonemapOperators.shader.
	// 0=Reinhard 1=ReinhardExtended 2=ACES Fitted (default) 3=Uncharted 2 / Hable
	// 4=Lottes 5=Linear (debug pass-through). Affects both SDR Tonemap.hcs and
	// HDR TonemapHDR.hcs base curve.
	HVar r_tonemapOperator("r_tonemapOperator", "Tonemap operator: 0=Reinhard 1=ReinhardExt 2=ACES 3=Uncharted2 4=Lottes 5=Linear", 2, 0, 5);
	HVar r_interpolate("r_interpolate", "Interpolates entities that have a mesh component and have interpolation enabled", true, false, true);
	HVar r_ssr("r_ssr", "Screen-space reflections", true, false, true);
	HVar r_ssrDenoise("r_ssrDenoise", "Run NRD on SSR output (0 = passthrough raw SSR, 1 = denoise)", true, false, true);
	HVar r_performantShadowMaps("r_performantShadowMaps", "Improve shadow map performance, may introduce some slight shadow stuttering", false, false, true);
	HVar r_chromaticAbberation("r_chromaticAbberation", "How much chromatic abberation to apply", 1.0f, 0.0f, 10.0f);
	HVar r_profileDisableDirectionalLights("r_profileDisableDirectionalLights", "Disable directional light rendering for profiling", false, false, true);
	HVar r_profileDisablePointLights("r_profileDisablePointLights", "Disable point light rendering for profiling", false, false, true);
	HVar r_profileDisableSpotLights("r_profileDisableSpotLights", "Disable spot light rendering for profiling", false, false, true);
	HVar r_profileDisablePost("r_profileDisablePost", "Disable post-processing overlays for profiling", false, false, true);
	HVar r_profileDisableBloom("r_profileDisableBloom", "Disable bloom for profiling", false, false, true);
	HVar r_profileAlbedoOnly("r_profileAlbedoOnly", "Display only the GBuffer albedo/diffuse target for profiling", false, false, true);
	HVar r_debugBypassLighting("r_debugBypassLighting", "Bypass deferred light accumulation and copy gbuffer diffuse directly to beauty target", false, false, true);
	HVar r_debugLightingPass("r_debugLightingPass", "Log deferred lighting stage diagnostics", false, false, true);
	HVar r_debugBypassFog("r_debugBypassFog", "Bypass fog compositing pass", false, false, true);
	HVar r_debugPresentCopy("r_debugPresentCopy", "Present by direct texture copy instead of fullscreen tonemap/overlay shaders", false, false, true);
	HVar r_debugForceGBufferBeforePresent("r_debugForceGBufferBeforePresent", "Force beauty target from gbuffer diffuse immediately before present", false, false, true);
	HVar r_gpuCullStatsLog("r_gpuCullStatsLog", "Log GPU culling counters periodically", false, false, true);
	extern HVar r_gpuCullEnable;
	extern HVar r_gpuCullDepthPrepassFallback;

	SceneRenderer::SceneRenderer()
	{}

	void SceneRenderer::Create()
	{
		_gbuffer.Create(g_pEnv->_graphicsDevice, g_pEnv->_graphicsDevice->GetCurrentMSAALevel());

		_sphereMesh = Mesh::Create("EngineData.Models/Primitives/sphere.hmesh");

		_pointLightMaterial = Material::Create("EngineData.Materials/PointLight.hmat");
		_spotLightMaterial = Material::Create("EngineData.Materials/SpotLight.hmat");

		CreateShaders();

		uint32_t width, height;
		g_pEnv->_graphicsDevice->GetBackBufferDimensions(width, height);

		CreateRenderTargets(width, height);

		if (_cloudConstantBuffer == nullptr)
		{
			_cloudConstantBuffer = g_pEnv->_graphicsDevice->CreateConstantBuffer(sizeof(CloudConstants));
		}

		if (_forwardLightsBuffer == nullptr)
		{
			_forwardLightsBuffer = g_pEnv->_graphicsDevice->CreateConstantBuffer(sizeof(ForwardLightConstants));
		}

		if (_cloudShapeNoise == nullptr || _cloudDetailNoise == nullptr)
		{
			const int32_t shapeResolution = 96;
			const int32_t detailResolution = 64;
			const int32_t randomSeed = std::max(1, GetRandomInt());

			SAFE_DELETE(_cloudShapeNoise);
			SAFE_DELETE(_cloudDetailNoise);

			_cloudShapeNoise = CreateCloudNoiseVolume(shapeResolution, randomSeed, 0.025f, 5, 0.52f, 2.0f);
			_cloudDetailNoise = CreateCloudNoiseVolume(detailResolution, randomSeed + 137, 0.09f, 4, 0.45f, 2.1f);
		}

		_gpuVisibilityCulling.Create();
		_autoExposure.Create();
	}

	void SceneRenderer::Resize(int32_t width, int32_t height)
	{
		std::unique_lock lock(_lock);

		SAFE_DELETE(_beautyRT);
		SAFE_DELETE(_shadowMapsRT);
		SAFE_DELETE(_shadowMapsAccumulator);
		SAFE_DELETE(_fogBuffer);
		SAFE_DELETE(_volumetricLightingBuffer);
		SAFE_DELETE(_cloudsBuffer);
		SAFE_DELETE(_atmosphereRT);
		SAFE_DELETE(_waterAccumulationRT);
		SAFE_DELETE(_lightAccumulationBuffer);
		SAFE_DELETE(_particleRT);
		SAFE_DELETE(_ssrDiffuseTexture);
		SAFE_DELETE(_ssrDiffuseHitInfo);
		SAFE_DELETE(_ssrTexture);
		SAFE_DELETE(_ssrHitInfo);
		SAFE_DELETE(_dlssTarget);
		// Position-copy RT shadows the GBuffer position layout (RGBA32F).
		// Recreated below in CreateRenderTargets at the new dimensions.
		SAFE_DELETE(_decalPositionCopy);

		SAFE_DELETE(_bloomEffect);

		_taa.Destroy();

		_gbuffer.Resize(width, height, g_pEnv->_graphicsDevice->GetCurrentMSAALevel());

		CreateRenderTargets(width, height);
		_gpuVisibilityCulling.Resize((uint32_t)width, (uint32_t)height);
	}

	void SceneRenderer::Destroy()
	{
		std::unique_lock lock(_lock);

		_diffuseGi.Destroy();
		_gbuffer.Destroy();

		//SAFE_DELETE(_clouds);

		SAFE_DELETE(_beautyRT);
		SAFE_DELETE(_shadowMapsRT);
		SAFE_DELETE(_shadowMapsAccumulator);
		SAFE_DELETE(_fogBuffer);
		SAFE_DELETE(_volumetricLightingBuffer);
		SAFE_DELETE(_cloudsBuffer);
		SAFE_DELETE(_atmosphereRT);
		SAFE_DELETE(_waterAccumulationRT);
		SAFE_DELETE(_lightAccumulationBuffer);
		SAFE_DELETE(_pointLightBuffer);
		SAFE_DELETE(_particleRT);
		SAFE_DELETE(_ssrDiffuseTexture);
		SAFE_DELETE(_ssrDiffuseHitInfo);
		SAFE_DELETE(_ssrTexture);
		SAFE_DELETE(_ssrHitInfo);
		SAFE_DELETE(_dlssTarget);
		SAFE_DELETE(_waterRT);
		SAFE_DELETE(_cloudShapeNoise);
		SAFE_DELETE(_cloudDetailNoise);
		SAFE_DELETE(_cloudConstantBuffer);
		SAFE_DELETE(_forwardLightsBuffer);
		SAFE_DELETE(_subsurfaceIntermediateRT);
		SAFE_DELETE(_subsurfaceParamsBuffer);
		SAFE_DELETE(_bokehDoFParamsBuffer);
		SAFE_DELETE(_decalConstantsBuffer);
		SAFE_DELETE(_decalPositionCopy);
		SAFE_DELETE(_decalCubeVB);
		SAFE_DELETE(_decalCubeIB);
		SAFE_DELETE(_autoPuddlesConstantsBuffer);
		SAFE_DELETE(_autoPuddlesQuadVB);
		SAFE_DELETE(_autoPuddlesQuadIB);
		_gpuVisibilityCulling.Destroy();
		_autoExposure.Destroy();

		//SAFE_DELETE(_waterDSV);

		//SAFE_DELETE(_volumetricBlur);
		//SAFE_DELETE(_waterBlur);
		//SAFE_DELETE(_blueNoise);
		SAFE_DELETE(_bloomEffect);
		//SAFE_DELETE(_ssrBlur);
	}

	void SceneRenderer::CreateShaders()
	{
		_compositionShader			= IShader::Create("EngineData.Shaders/Deferred.hcs");	
		_fxaa						= IShader::Create("EngineData.Shaders/FXAA.hcs");
		_fogEffect					= IShader::Create("EngineData.Shaders/PostFog.hcs");
		_volumetricLighting			= IShader::Create("EngineData.Shaders/VolumetricLighting.hcs");
		_volumetricClouds			= IShader::Create("EngineData.Shaders/VolumetricClouds.hcs");
		_bilateralUpsample			= IShader::Create("EngineData.Shaders/BilateralUpsample.hcs");
		_pointLightShader			= IShader::Create("EngineData.Shaders/PointLight.hcs");
		_spotLightShader			= IShader::Create("EngineData.Shaders/SpotLight.hcs");
		_ssrShader					= IShader::Create("EngineData.Shaders/SSR.hcs");
		_vignetteShader				= IShader::Create("EngineData.Shaders/Vignette.hcs");
		_chromaticAberrationShader	= IShader::Create("EngineData.Shaders/ChromaticAbberation.hcs");
		_colourGradingShader		= IShader::Create("EngineData.Shaders/ColourGrade.hcs");
		_ssrResolve					= IShader::Create("EngineData.Shaders/SSRResolve.hcs");
		_tonemapShader				= IShader::Create("EngineData.Shaders/Tonemap.hcs");
		_hdrOutputShader			= IShader::Create("EngineData.Shaders/TonemapHDR.hcs");
		_basicDenoise				= IShader::Create("EngineData.Shaders/BasicDenoise.hcs");
		_waterBlitEffect			= IShader::Create("EngineData.Shaders/WaterBlit.hcs");
		_fullScreenQuadShader		= IShader::Create("EngineData.Shaders/FullScreenQuad.hcs");
		_subsurfaceShader			= IShader::Create("EngineData.Shaders/SubsurfaceScattering.hcs");
		_bokehDoFShader				= IShader::Create("EngineData.Shaders/BokehDoF.hcs");
		_decalShader				= IShader::Create("EngineData.Shaders/Decal.hcs");
		_autoPuddlesShader			= IShader::Create("EngineData.Shaders/AutoPuddles.hcs");

		// Tiny float4 cbuffer that drives the SSS pass direction + radius + intensity.
		// Built lazily once and reused across frames since the layout is constant.
		if (_subsurfaceParamsBuffer == nullptr)
		{
			_subsurfaceParamsBuffer = g_pEnv->_graphicsDevice->CreateConstantBuffer(sizeof(math::Vector4));
		}

		// DoF params cbuffer at b6: (focusDistance, focusRange, aperture, maxCocPixels).
		if (_bokehDoFParamsBuffer == nullptr)
		{
			_bokehDoFParamsBuffer = g_pEnv->_graphicsDevice->CreateConstantBuffer(sizeof(math::Vector4));
		}

		// Decal constants cbuffer at b4 (PerFrameBuffer is b0, PerObject b1, etc;
		// b4 is otherwise used by the GI / cloud passes which don't overlap with
		// the decal pass in time). Layout: matrix (64b) + 3 float4 (48b) = 112b.
		if (_decalConstantsBuffer == nullptr)
		{
			_decalConstantsBuffer = g_pEnv->_graphicsDevice->CreateConstantBuffer(sizeof(math::Matrix) + sizeof(math::Vector4) * 3);
		}

		// Auto-puddles cbuffer at b4 (same slot as the decal constants - the two
		// can't run simultaneously since they share render targets, so reusing
		// the slot is fine; we rebind right before each draw). Layout: 2 float4 = 32b.
		if (_autoPuddlesConstantsBuffer == nullptr)
		{
			_autoPuddlesConstantsBuffer = g_pEnv->_graphicsDevice->CreateConstantBuffer(sizeof(math::Vector4) * 2);
		}

		// Fullscreen quad in clip space. The auto-puddle shader is direct-clip-
		// space (no view/world transform), so we just need a (-1,-1)..(1,1) quad.
		if (_autoPuddlesQuadVB == nullptr)
		{
			const math::Vector3 quadVerts[4] = {
				{ -1.0f, -1.0f, 0.0f },
				{  1.0f, -1.0f, 0.0f },
				{  1.0f,  1.0f, 0.0f },
				{ -1.0f,  1.0f, 0.0f },
			};
			_autoPuddlesQuadVB = g_pEnv->_graphicsDevice->CreateVertexBuffer(
				(int32_t)sizeof(quadVerts),
				(uint32_t)sizeof(math::Vector3),
				D3D11_USAGE_IMMUTABLE,
				0,
				(void*)quadVerts);
		}
		if (_autoPuddlesQuadIB == nullptr)
		{
			const uint32_t quadIndices[6] = { 0, 2, 1,  0, 3, 2 };
			_autoPuddlesQuadIB = g_pEnv->_graphicsDevice->CreateIndexBuffer(
				(int32_t)sizeof(quadIndices),
				(uint32_t)sizeof(uint32_t),
				D3D11_USAGE_IMMUTABLE,
				0,
				(void*)quadIndices);
		}

		// Unit cube VB / IB shared by every decal. 8 vertices, 36 indices (12 tris).
		// Vertices are at [-0.5, 0.5]^3 so the decal world matrix (entity transform
		// with extents baked into scale) places them in world space directly.
		if (_decalCubeVB == nullptr)
		{
			const math::Vector3 cubeVerts[8] = {
				{ -0.5f, -0.5f, -0.5f }, {  0.5f, -0.5f, -0.5f },
				{  0.5f,  0.5f, -0.5f }, { -0.5f,  0.5f, -0.5f },
				{ -0.5f, -0.5f,  0.5f }, {  0.5f, -0.5f,  0.5f },
				{  0.5f,  0.5f,  0.5f }, { -0.5f,  0.5f,  0.5f },
			};
			_decalCubeVB = g_pEnv->_graphicsDevice->CreateVertexBuffer(
				(int32_t)sizeof(cubeVerts),
				(uint32_t)sizeof(math::Vector3),
				D3D11_USAGE_IMMUTABLE,
				0,
				(void*)cubeVerts);
		}
		if (_decalCubeIB == nullptr)
		{
			// Two triangles per cube face, CCW when viewed from outside. Cull mode
			// is None during the decal pass so winding doesn't matter for visibility
			// - but using consistent CCW keeps the geometry valid if some future
			// debug-rendering path reuses the buffer.
			const uint32_t cubeIndices[36] = {
				0, 2, 1,  0, 3, 2, // -Z
				4, 5, 6,  4, 6, 7, // +Z
				0, 4, 7,  0, 7, 3, // -X
				1, 2, 6,  1, 6, 5, // +X
				0, 1, 5,  0, 5, 4, // -Y
				3, 7, 6,  3, 6, 2, // +Y
			};
			_decalCubeIB = g_pEnv->_graphicsDevice->CreateIndexBuffer(
				(int32_t)sizeof(cubeIndices),
				(uint32_t)sizeof(uint32_t),
				D3D11_USAGE_IMMUTABLE,
				0,
				(void*)cubeIndices);
		}

		if (!_compositionShader)
		{
			LOG_CRIT("SceneRenderer: failed to load required deferred composition shader EngineData.Shaders/Deferred.hcs");
		}
		if (!_pointLightShader)
		{
			LOG_CRIT("SceneRenderer: failed to load point light shader EngineData.Shaders/PointLight.hcs");
		}
		if (!_spotLightShader)
		{
			LOG_CRIT("SceneRenderer: failed to load spot light shader EngineData.Shaders/SpotLight.hcs");
		}
		if (!_fogEffect)
		{
			LOG_CRIT("SceneRenderer: failed to load fog shader EngineData.Shaders/PostFog.hcs");
		}

		//_volumetricBlur = new BlurEffect(_volumetricLightingBuffer, BlurType::Gaussian, 2);
		//_waterBlur = new BlurEffect(_waterAccumulationRT, BlurType::Gaussian, 2);

		_blueNoise					= ITexture2D::Create("EngineData.Textures/LDR_RGBA_0.png");

		

		//_ssrBlur = new BlurEffect(_ssrTexture, BlurType::Gaussian, 1);

		
	}

	void SceneRenderer::CreateRenderTargets(int32_t width, int32_t height)
	{
		std::unique_lock lock(_lock);

		auto MsaaLevel = g_pEnv->_graphicsDevice->GetCurrentMSAALevel();

		const DXGI_FORMAT BEAUTY_FORMAT = (DXGI_FORMAT)g_pEnv->_graphicsDevice->GetDesiredBackBufferFormat();

		_beautyRT = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,
			height,
			BEAUTY_FORMAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, MsaaLevel, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D,
			D3D11_DSV_DIMENSION_UNKNOWN,
			D3D11_USAGE_DEFAULT,
			D3D11_RESOURCE_MISC_SHARED);

		_beautyRT->SetDebugName("_beautyRT");

		if (!_beautyRT)
		{
			LOG_CRIT("Failed to create composition render target");
			return;
		}

		_waterRT = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,
			height,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, MsaaLevel, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D);
		_waterRT->SetDebugName("_waterRT");

		// SSS intermediate RT - same format as beauty so the two-pass separable
		// blur preserves HDR precision through the horizontal step. Reused every
		// frame between the H and V passes, no temporal history kept here.
		SAFE_DELETE(_subsurfaceIntermediateRT);
		_subsurfaceIntermediateRT = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,
			height,
			BEAUTY_FORMAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, MsaaLevel, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D);
		if (_subsurfaceIntermediateRT) _subsurfaceIntermediateRT->SetDebugName("_sssIntermediate");

		_particleRT = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,
			height,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, MsaaLevel, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D);

		_ssrDiffuseTexture = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,
			height,
			BEAUTY_FORMAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, MsaaLevel, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D);

		_ssrDiffuseTexture->SetDebugName("_ssrDiffuseTexture");

		_ssrDiffuseHitInfo = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,
			height,
			BEAUTY_FORMAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, MsaaLevel, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D);

		_ssrDiffuseHitInfo->SetDebugName("_ssrDiffuseHitInfo");

		_ssrTexture = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,// / 2,
			height,// / 2,
			BEAUTY_FORMAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, MsaaLevel, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D);

		_ssrTexture->SetDebugName("_ssrSpecularTexture");

		_ssrHitInfo = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,// / 2,
			height,// / 2,
			BEAUTY_FORMAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, MsaaLevel, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D);

		_ssrHitInfo->SetDebugName("_ssrSpecularHitInfo");

		_ssrHistory = g_pEnv->_graphicsDevice->CreateTexture(_ssrTexture);
		_ssrResolved = g_pEnv->_graphicsDevice->CreateTexture(_ssrTexture);

		_ssrHistory->SetDebugName("_ssrHistory");
		_ssrResolved->SetDebugName("_ssrResolved");

		// Position-copy RT for the decal pass. Same R32G32B32A32_FLOAT format as
		// GBuffer position so a straight CopyTo works. MSAA disabled - decals
		// don't need per-sample data and we want a plain SRV for the PS to sample
		// with a point sampler. Recreated alongside the SSR/shadow RTs whenever
		// the viewport resizes.
		SAFE_DELETE(_decalPositionCopy);
		_decalPositionCopy = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,
			height,
			DXGI_FORMAT_R32G32B32A32_FLOAT,
			1,
			D3D11_BIND_SHADER_RESOURCE,
			0, 1, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			D3D11_RTV_DIMENSION_UNKNOWN,
			D3D11_UAV_DIMENSION_UNKNOWN,
			D3D11_SRV_DIMENSION_TEXTURE2D);
		_decalPositionCopy->SetDebugName("_decalPositionCopy");

		_shadowMapsRT = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,
			height,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, 1, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			/*MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS :*/ D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			/*MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS :*/ D3D11_SRV_DIMENSION_TEXTURE2D);

		_shadowMapsAccumulator = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,
			height,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, 1, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			/*MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS :*/ D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			/*MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS :*/ D3D11_SRV_DIMENSION_TEXTURE2D);

		_waterAccumulationRT = g_pEnv->_graphicsDevice->CreateTexture2D(
			(int32_t)((float)width * r_waterResolution._val.f32),
			(int32_t)((float)height * r_waterResolution._val.f32),
			DXGI_FORMAT_R8G8B8A8_UNORM,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, MsaaLevel, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D);

		/*_waterDSV = g_pEnv->_graphicsDevice->CreateTexture2D(
			width * r_waterResolution._val.f32,
			height * r_waterResolution._val.f32,
			DXGI_FORMAT_R32_TYPELESS,
			1,
			D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL,
			0, MsaaLevel, 0,
			D3D11_RTV_DIMENSION_UNKNOWN,
			D3D11_UAV_DIMENSION_UNKNOWN,
			D3D11_SRV_DIMENSION_TEXTURE2D,
			D3D11_DSV_DIMENSION_TEXTURE2D);*/

		_fogBuffer = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,
			height,
			BEAUTY_FORMAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, MsaaLevel, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D);

		_lightAccumulationBuffer = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,
			height,
			BEAUTY_FORMAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, MsaaLevel, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D);

		_pointLightBuffer = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,
			height,
			BEAUTY_FORMAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, MsaaLevel, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D);

		_volumetricLightingBuffer = g_pEnv->_graphicsDevice->CreateTexture2D(
			width / 2,
			height / 2,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, 1, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			/*MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS :*/ D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			/*MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS :*/ D3D11_SRV_DIMENSION_TEXTURE2D);

		_cloudsBuffer = g_pEnv->_graphicsDevice->CreateTexture2D(
			width / 2,
			height / 2,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0,
			1,
			0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			D3D11_SRV_DIMENSION_TEXTURE2D);

		if (_cloudsBuffer)
		{
			_cloudsBuffer->SetDebugName("_cloudsBuffer");
		}

		_atmosphereRT = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,
			height,
			BEAUTY_FORMAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, MsaaLevel, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D);

		// DLSS target should be full screen
		uint32_t backBufferWidth, backBufferHeight;
		g_pEnv->_graphicsDevice->GetBackBufferDimensions(backBufferWidth, backBufferHeight);

		_dlssTarget = g_pEnv->_graphicsDevice->CreateTexture2D(
			backBufferWidth,
			backBufferHeight,
			BEAUTY_FORMAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			0, MsaaLevel, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_TEXTURE2D,
			MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D);

		_dlssTarget->SetDebugName("_dlssTarget");

		_bloomEffect = new Bloom();
		_bloomEffect->Create(width / 4, height / 4);

		_taa.Create(_beautyRT);
		_diffuseGi.Create(static_cast<uint32_t>(width), static_cast<uint32_t>(height));

		g_pEnv->_denoiserProvider->CreateBuffers(width, height, _ssrDiffuseTexture, _ssrDiffuseHitInfo, _ssrTexture, _ssrHitInfo, _gbuffer.GetNormal(), _gbuffer.GetSpecular(), _gbuffer.GetVelocity());
	}

	const std::vector<Light*>& SceneRenderer::GetShadowCasters() const
	{
		return _shadowCasters;
	}

	void SceneRenderer::ClearShadowCasters()
	{
		_shadowCasters.clear();
	}

	void SceneRenderer::RemoveShadowCaster(Light* light)
	{
		auto it = std::remove(_shadowCasters.begin(), _shadowCasters.end(), light);

		if (it != _shadowCasters.end())
			_shadowCasters.erase(it);
	}

	/// <summary>
	/// Render the scene from the perspective of the given camera
	/// </summary>
	/// <param name="scene"></param>
	/// <param name="camera"></param>
	void SceneRenderer::RenderScene(Scene* scene, Camera* camera, SceneFlags flags)
	{
		std::unique_lock lock(_lock);

		if (!camera)
			return;

		if (!_sphereEntity)
		{
			_sphereEntity = g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity("LightSphere");
			_sphereEntity->SetLayer(Layer::Invisible);
			_sphereEntity->SetFlag(EntityFlags::DoNotSave | EntityFlags::ExcludeFromHLOD);
			auto sphereMeshRenderer = _sphereEntity->AddComponent<StaticMeshComponent>();
			sphereMeshRenderer->SetMesh(_sphereMesh);
			sphereMeshRenderer->SetMaterial(_pointLightMaterial);
		}
		// TODO move this somewhere
		/*if (_clouds == nullptr)
		{
			_clouds = new CloudVolume(math::Vector3(-5000.0f, -50.0f, -5000.0f), math::Vector3(5000.0f, 50.0f, 5000.0f), 128);
			_clouds->Generate();
		}*/
		// The order to render a scene in is
		// - Shadowmap
		// - Opaque
		// - Water
		// - Transparent
		// - Post processing effects

		_currentScene = scene;
		_currentCamera = camera;
		_cameraEntity = camera->GetEntity();

		_currentShadowCasterForComposition = nullptr;
		_currentShadowMapForComposition = nullptr;
		_gpuVisibilityCulling.BeginFrame(g_pEnv->_timeManager ? g_pEnv->_timeManager->_frameCount : 0u, _currentCamera);

		assert(_cameraEntity && "Camera entity cannot be null");

		

		if ((flags & SceneFlags::PostProcessingEnabled) != (SceneFlags)0)
		{
			if (g_pEnv->_streamlineProvider && g_pEnv->_streamlineProvider->IsEnabled())
			{
				g_pEnv->_streamlineProvider->BeginFrame();
				SetStreamlineConstants();
			}
		}			

		// Collect shadow casters first
		//
		CollectShadowCasters();
		
		// Clear the composition RT
		_beautyRT->ClearRenderTargetView(math::Color(0, 0, 0, 1));
		//_currentCamera->GetFullScreenRenderTarget()->ClearRenderTargetView(math::Color(0, 0, 0, 1));

		if(auto rt = _currentCamera->GetRenderTarget(); rt != nullptr)
			rt->ClearRenderTargetView(math::Color(0, 0, 0, 0));

		// Set up the Gbuffer in preparation for rendering
		//
		_gbuffer.Clear();		

		for (auto& caster : _shadowCasters)
		{
			RenderShadowMaps(caster);
		}

		// set up the viewport
		auto bbvp = _currentCamera->GetViewport();
		g_pEnv->_graphicsDevice->SetViewports({ bbvp });

		auto sunLight = _currentScene->GetSunLight();

		SetupPerFrameBuffer(
			_currentCamera->GetViewMatrix(),
			_currentCamera->GetProjectionMatrix(),
			_currentCamera->GetViewMatrixPrev(),
			_currentCamera->GetProjectionMatrixPrev(),
			r_shadowCascades._val.i32,
			sunLight ? sunLight->GetEntity()->GetComponent<Transform>()->GetForward() : math::Vector3::Forward,
			_currentCamera->GetViewport(),
			6,
			sunLight ? sunLight->GetLightMultiplier() : 1.0f
		);
		_gbuffer.SetAsRenderTargets(_currentCamera->GetViewport());

		if (r_gpuCullEnable._val.b && r_gpuCullDepthPrepassFallback._val.b && !_gpuVisibilityCulling.HasUsableHistory())
		{
			_currentScene->RenderEntities(
				_currentCamera->GetPVS(),
				LAYERMASK(Layer::StaticGeometry) | LAYERMASK(Layer::DynamicGeometry) | LAYERMASK(Layer::Grass),
				MeshRenderFlags::MeshRenderShadowMap);
			_gpuVisibilityCulling.BuildDepthPyramid(_gbuffer.GetDepthBuffer());
			_gpuVisibilityCulling.MarkDepthFallbackUsed();
			_gbuffer.Clear();
			_gbuffer.SetAsRenderTargets(_currentCamera->GetViewport());
		}

		RenderOpaque();
		_gpuVisibilityCulling.BuildDepthPyramid(_gbuffer.GetDepthBuffer());

		// Deferred decal pass. Runs straight after the opaque GBuffer fill (and
		// after the depth-pyramid build, which only reads the depth buffer that
		// the decal pass leaves alone) and before lighting / SSR. Decals modify
		// diffuse + mat in-place so the subsequent lighting pass sees the patched
		// surface naturally - puddles get smooth-roughness lighting, blood gets
		// dark albedo, etc. v1 is gated on the decal shader + component being
		// present (RenderDecals early-outs if there are no decals).
		RenderDecals();
		//AccumulateShadowMaps();
		
		
		if(HEX_HASFLAG(flags, SceneFlags::PostProcessingEnabled))
			SetStreamlineConstants();	

		_gbuffer.GetDiffuse()->CopyTo(_beautyRT);		

		//RenderWater();
		RenderPostProcessing(flags);

		//RenderTransparent();

		//if (r_debugScene._val.i32 == 1)
		{
			g_pEnv->_graphicsDevice->SetBlendState(BlendState::Transparency);

			_currentScene->RenderDebug(_currentCamera->GetPVS());

			g_pEnv->_navMeshProvider->DebugRender();

			g_pEnv->_graphicsDevice->SetBlendState(BlendState::Opaque);
		}

		//g_pEnv->_graphicsDevice->SetRenderTarget(g_pEnv->_graphicsDevice->GetBackBuffer());

		if (r_gpuCullStatsLog._val.b && g_pEnv && g_pEnv->_timeManager && (g_pEnv->_timeManager->_frameCount % 120ull) == 0ull)
		{
			const auto& stats = _gpuVisibilityCulling.GetStats();
			LOG_INFO("GPUCull: candidates=%u vis=%u frustumReject=%u occlusionReject=%u draws=%u buildMs=%.3f frustumMs=%.3f occlusionMs=%.3f hzbHistory=%d",
				stats.totalCandidates,
				stats.visibleInstances,
				stats.frustumRejected,
				stats.occlusionRejected,
				stats.submittedDraws,
				stats.cpuBuildMs,
				stats.gpuFrustumMs,
				stats.gpuOcclusionMs,
				_gpuVisibilityCulling.HasUsableHistory() ? 1 : 0);
		}
	}

	void SceneRenderer::CollectShadowCasters()
	{
		// Clear the list of shadow casters ever frame
		//
		_shadowCasters.clear();

		// Always put the main sun light in the scene
		//
		//_shadowCasters.push_back(_currentScene->GetSunLight());

		// Next, get a list of lights and work out whether or not to include them
		//
		std::vector<Light*> pvs;
		std::unordered_set<Light*> uniqueLights;

		auto gatherFromEntities = [&](ComponentId componentId)
		{
			std::vector<Entity*> entitiesWithComponent;
			_currentScene->GetEntities((1 << componentId), entitiesWithComponent);

			for (auto* entity : entitiesWithComponent)
			{
				if (entity == nullptr || entity->IsPendingDeletion())
					continue;

				auto* component = entity->GetComponentByID(componentId);
				auto* light = component ? component->CastAs<Light>() : nullptr;
				if (light != nullptr)
				{
					uniqueLights.insert(light);
				}
			}
		};

		gatherFromEntities(DirectionalLight::_GetComponentId());
		gatherFromEntities(SpotLight::_GetComponentId());
		gatherFromEntities(PointLight::_GetComponentId());

		for (auto* light : uniqueLights)
		{
			if (!light)
				continue;

			auto* owner = light->GetEntity();
			if (owner == nullptr || owner->IsPendingDeletion())
				continue;

			if (light->GetDoesCastShadows() == false)
				continue;

			//if (light == _currentScene->GetSunLight())
			//	continue;

			// if its a valid light and casts shadows, add it to the pvs
			//
			pvs.push_back(light);

			// We check that 
			//if (pvs.size() + 1 >= MaxShadowCasters)
			//	break;
		}

		// now that we know which lights are potentially affecting the scene, we should sort them according to priority
		//
		std::sort(pvs.begin(), pvs.end(), [this](Light* left, Light* right) {

			auto leftDist = (left->GetEntity()->GetPosition() - _currentCamera->GetEntity()->GetPosition()).Length();
			auto rightDist = (right->GetEntity()->GetPosition() - _currentCamera->GetEntity()->GetPosition()).Length();

			return leftDist > rightDist;
			});

		// Finally, tack on the shadow casters to the sun light
		//
		_shadowCasters.insert(_shadowCasters.end(), pvs.begin(), pvs.begin() + (pvs.size() < MaxShadowCasters ? pvs.size() : MaxShadowCasters));
	}

	

	void SceneRenderer::SetupPerFrameBuffer(
		const math::Matrix& viewMatrix,
		const math::Matrix& projectionMatrix,
		const math::Matrix& viewMatrixPrev,
		const math::Matrix& projectionMatrixPrev,
		int32_t numCascades,
		const math::Vector3& lightDir,
		const math::Viewport& viewport,
		int passIdx,
		float lightMultiplier,
		bool forceCascade)
	{
		// update the per frame buffer with our numeric data
		//
		auto perFrameBuffer = g_pEnv->_graphicsDevice->GetEngineConstantBuffer(EngineConstantBuffer::PerFrameBuffer);

		if (perFrameBuffer)
		{
			PerFrameConstantBuffer bufferData = {};

			// Camera data
			bufferData._viewMatrix = viewMatrix.Transpose();
			bufferData._projectionMatrix = projectionMatrix.Transpose();
			bufferData._viewProjectionMatrix = (viewMatrix * projectionMatrix).Transpose();
			bufferData._viewMatrixInverse = viewMatrix.Invert().Transpose();
			bufferData._projectionMatrixInverse = projectionMatrix.Invert().Transpose();
			bufferData._viewProjectionMatrixInverse = (viewMatrix * projectionMatrix).Invert().Transpose();

			

			bufferData._viewMatrixPrev = viewMatrixPrev.Transpose();
			bufferData._projectionMatrixPrev = projectionMatrixPrev.Transpose();
			bufferData._viewProjectionMatrixPrev = (viewMatrixPrev * projectionMatrixPrev).Transpose();
			bufferData._viewMatrixInversePrev = viewMatrixPrev.Invert().Transpose();
			bufferData._projectionMatrixInversePrev = projectionMatrixPrev.Invert().Transpose();
			bufferData._viewProjectionMatrixInversePrev = (viewMatrixPrev * projectionMatrixPrev).Invert().Transpose();

			// The view matrix is built with (entity.position + camera.viewOffset)
			// (see Camera::ConstructViewMatrix), so the actual eye position is
			// shifted by viewOffset from the camera entity's transform. If we
			// only put the bare entity position into g_eyePos here, every
			// shader that computes view-relative vectors from g_eyePos (SSR's
			// eyeVector, PBR specular Fresnel, sky-dir, etc.) disagrees with
			// where the view matrix actually places the eye - on a wet road
			// with a player-eye-offset of (0, 1.4, 0), the SSR reflection
			// vector calculation produced near-horizontal rays for ground
			// pixels (eyeVector was treated as ground-to-ground horizontal
			// instead of head-to-ground downward), which then never exited
			// the top of screen during raymarch and hit SSR's lastInScreenTex
			// fallback - producing long vertical stripe artifacts on the road
			// that did NOT appear in the editor because the editor's
			// free-cam uses no viewOffset.
			const math::Vector3 eyePos = _cameraEntity->GetPosition()
				+ (_currentCamera ? _currentCamera->GetViewOffset() : math::Vector3::Zero);
			bufferData._eyePos = math::Vector4(eyePos.x, eyePos.y, eyePos.z, 1.0f);
			bufferData._eyeDir = _currentCamera ? math::Vector4(_currentCamera->GetLookDir()) : math::Vector4();

			bufferData._colourGrading.colourFilter = r_colourFilter._val.v3;
			bufferData._colourGrading.contrast = r_contrast._val.f32;
			// Auto exposure multiplies into the user-set r_exposure. When auto exposure is
			// disabled the multiplier is 1.0, so r_exposure passes through unchanged.
			bufferData._colourGrading.exposure = r_exposure._val.f32 * _autoExposure.GetExposureMultiplier();
			bufferData._colourGrading.hueShift = r_hueShift._val.f32;
			bufferData._colourGrading.saturation = r_saturation._val.f32;
			// One-time auto-calibration of r_hdrPeakNits from the active display's
			// IDXGIOutput6::GetDesc1::MaxLuminance. Saves users from having to know
			// whether they have an HDR400/HDR600/HDR1000+ panel - the system already
			// knows. Only runs once per session, only when the device reports a real
			// value, and only if the user hasn't already touched the HVar (so manual
			// overrides via console / saved config keep working). Wrapped in a static
			// init flag rather than guarded against the default 1000.0 sentinel
			// because the user might legitimately want to pin 1000 as their target.
			static bool sHdrPeakAutoCalibrated = false;
			if (!sHdrPeakAutoCalibrated)
			{
				if (g_pEnv && g_pEnv->_graphicsDevice)
				{
					const float devicePeak = g_pEnv->_graphicsDevice->GetDisplayPeakNits();
					if (devicePeak > 0.0f)
					{
						r_hdrPeakNits._val.f32 = std::clamp(devicePeak, r_hdrPeakNits._min.f32, r_hdrPeakNits._max.f32);
						LOG_INFO("r_hdrPeakNits auto-calibrated to %.1f nits from display MaxLuminance", r_hdrPeakNits._val.f32);
					}
				}
				sHdrPeakAutoCalibrated = true;
			}

			bufferData._hdrPaperWhiteNits = r_hdrPaperWhiteNits._val.f32;
			bufferData._hdrPeakNits = r_hdrPeakNits._val.f32;
			bufferData._tonemapOperator = static_cast<float>(std::clamp(r_tonemapOperator._val.i32, 0, 5));
			bufferData._weatherSurface = _currentScene->GetWeatherSurfaceParams();

			// Shadowmap data
			
			const auto maxShadowCascades = r_shadowCascades._val.i32;// shadowCaster->GetMaxSupportedShadowCascades();

			float cascadeStart = 0.0f;

			float mul = (_currentCamera->GetFarZ() / (float)numCascades) * (float)maxShadowCascades;;

			for (int i = 0; i < 4; ++i)
			{
				// Calculate the shadow map cascade ranges
				//
				//float start = min(1.0f, (float)(i + 0) / (float)numCascades);
				//float end = min(1.0f, (float)(i + 1) / (float)numCascades);

				//float start = cascadeStart;
				float end = (cascadeStart + r_shadowCascadeRange._val.f32) / _currentCamera->GetFarZ();

				if (i >= maxShadowCascades - 1)
					end = 1.0f;

				// Calculate the frustum splits
				//
				((float*)&bufferData._frustumSplits.x)[i] = end * _currentCamera->GetFarZ();

				cascadeStart += r_shadowCascadeRange._val.f32;
			}

			//bufferData._lightViewMatrix = shadowCaster->GetViewMatrix().Transpose();

			const float sunProxyDistance = _currentCamera ? (_currentCamera->GetFarZ() * 0.5f) : 500.0f;
			const math::Vector3 sunProxyPos = _cameraEntity->GetPosition() - (lightDir * sunProxyDistance);
			bufferData._lightPosition = math::Vector4(sunProxyPos.x, sunProxyPos.y, sunProxyPos.z, 1.0f);
			bufferData._lightDirection = math::Vector4(lightDir.x, lightDir.y, lightDir.z, 1.0f);
			bufferData._globalLight[0] = lightMultiplier;

			const auto& fogColour = _currentScene->GetFogColour();

			bufferData._globalLight[1] = fogColour.R();
			bufferData._globalLight[2] = fogColour.G();
			bufferData._globalLight[3] = fogColour.B();

			bufferData._screenWidth = (int32_t)viewport.width;// : 0;
			bufferData._screenHeight = (int32_t)viewport.height;// : 0;

			// atmosphere
			bufferData._atmosphere.zenithExponent = env_zenithExponent._val.f32;
			bufferData._atmosphere.anisotropicIntensity = env_anisotropicIntensity._val.f32;
			bufferData._atmosphere.density = env_density._val.f32;
			bufferData._atmosphere.rayleighStrength = env_rayleighStrength._val.f32;
			bufferData._atmosphere.mieStrength = env_mieStrength._val.f32;
			bufferData._atmosphere.ambientStrength = env_ambientSkyStrength._val.f32;
			bufferData._atmosphere.sunHazeStrength = env_sunHazeStrength._val.f32;
			bufferData._atmosphere.sunsetWarmStrength = env_sunsetWarmStrength._val.f32;
			bufferData._atmosphere.sunsetCoolStrength = env_sunsetCoolStrength._val.f32;
			bufferData._atmosphere.sunsetGlowStrength = env_sunsetGlowStrength._val.f32;
			bufferData._atmosphere.atmospherePad0 = 0.0f;
			bufferData._atmosphere.fogDensity = r_fogDensity._val.f32;
			bufferData._atmosphere.fogStartDistance = r_fogStartDistance._val.f32;
			bufferData._atmosphere.fogHeightDensity = r_fogHeightDensity._val.f32;
			bufferData._atmosphere.fogHeightFalloff = r_fogHeightFalloff._val.f32;
			bufferData._atmosphere.fogHeightPivot = r_fogHeightPivot._val.f32;
			bufferData._atmosphere.fogSkyTintInfluence = r_fogSkyTintInfluence._val.f32;
			bufferData._atmosphere.fogFarDesaturate = r_fogFarDesaturate._val.f32;
			bufferData._atmosphere.fogAtmosphereBlendStart = r_fogAtmosphereBlendStart._val.f32;
			bufferData._atmosphere.fogAtmosphereBlendRange = r_fogAtmosphereBlendRange._val.f32;
			bufferData._atmosphere.fogSunsetRange = r_fogSunsetRange._val.f32;
			bufferData._atmosphere.fogSunsetWarmthStrength = r_fogSunsetWarmthStrength._val.f32;
			bufferData._atmosphere.fogFarAtmosphereMatchStrength = r_fogFarAtmosphereMatchStrength._val.f32;
			bufferData._atmosphere.fog_pad0 = 0.0f;

			// Sunset/night-aware ambient + fog/atmosphere modulation.
			//
			// Background: the weather plugin's presets are tuned for daylight conditions -
			// fog densities of 0.004-0.009 (vs scene default 0.003), Mie strengths of 1.45-
			// 2.45 (vs default 1.0). At night those values still drive AtmospherePhysical's
			// inscatter integration, and combined with the `max(g_globalLight[0], 0.35f) *
			// 19.4f` sun-floor inside that integration, the resulting `physicalFogColour` in
			// PostFog stays much brighter than appropriate for a night scene. Visible
			// symptom: a flat white/whitewash fog at night when the weather component is
			// enabled, even though no weather is enabled the scene-default fog looks fine.
			//
			// Fix: at the single point where atmosphere parameters enter the per-frame
			// cbuffer, modulate them by the same sun-elevation curve I use for ambient.
			//   - Ambient tints warm at sunset, cool at night (PostFog also reads this as
			//     ambientFogBase so fog colour matches).
			//   - Fog density, height-density and Mie strength are scaled down at night so
			//     daylight-tuned presets don't blow out the night sky.
			{
				const math::Vector4 staticAmbient = _currentScene->GetAmbientColour();
				const float sunElevation = -lightDir.y; // lightDir points from sun to scene; -y -> sun above horizon
				const float sunsetRange = 0.30f;        // matches r_fogSunsetRange default; tunable later if needed
				const float sunsetWeight = std::clamp((sunsetRange - sunElevation) / sunsetRange, 0.0f, 1.0f)
				                           * (1.0f - std::clamp((-0.02f - sunElevation) / 0.10f, 0.0f, 1.0f));
				const float nightWeight = std::clamp((-sunElevation + 0.02f) / 0.20f, 0.0f, 1.0f);

				// --- Ambient tint ---
				// Warm and cool tints applied multiplicatively so the preset's brightness
				// envelope is preserved - only the hue shifts.
				const math::Vector3 warmTint(1.40f, 0.78f, 0.50f);
				const math::Vector3 coolNightTint(0.55f, 0.65f, 0.95f);

				const float sunsetW = std::min(0.85f, sunsetWeight * env_sunsetWarmStrength._val.f32 * 0.85f);
				const float nightW  = std::min(0.85f, nightWeight  * env_sunsetCoolStrength._val.f32 * 0.65f);

				math::Vector3 ambientRgb(staticAmbient.x, staticAmbient.y, staticAmbient.z);
				const math::Vector3 ambientWarm(ambientRgb.x * warmTint.x, ambientRgb.y * warmTint.y, ambientRgb.z * warmTint.z);
				ambientRgb.x = ambientRgb.x + (ambientWarm.x - ambientRgb.x) * sunsetW;
				ambientRgb.y = ambientRgb.y + (ambientWarm.y - ambientRgb.y) * sunsetW;
				ambientRgb.z = ambientRgb.z + (ambientWarm.z - ambientRgb.z) * sunsetW;

				const math::Vector3 ambientCool(ambientRgb.x * coolNightTint.x, ambientRgb.y * coolNightTint.y, ambientRgb.z * coolNightTint.z);
				ambientRgb.x = ambientRgb.x + (ambientCool.x - ambientRgb.x) * nightW;
				ambientRgb.y = ambientRgb.y + (ambientCool.y - ambientRgb.y) * nightW;
				ambientRgb.z = ambientRgb.z + (ambientCool.z - ambientRgb.z) * nightW;

				bufferData._atmosphere.ambientLight = math::Vector4(ambientRgb.x, ambientRgb.y, ambientRgb.z, staticAmbient.w);

				// --- Fog / atmosphere dimming at night ---
				// At night, dim the parameters that drive PostFog's physical inscatter so
				// daylight-tuned weather presets don't produce a bright fog whiteout. We don't
				// zero them out (fog should still be visible at night, just darker) - 60%
				// scale at deep night is the sweet spot that keeps fog readable without
				// looking artificial.
				const float fogNightDim = 1.0f - nightWeight * 0.60f;     // -> 0.40 at deep night
				const float mieNightDim = 1.0f - nightWeight * 0.55f;     // -> 0.45 at deep night
				const float densityNightDim = 1.0f - nightWeight * 0.45f; // -> 0.55 at deep night
				bufferData._atmosphere.fogDensity       *= fogNightDim;
				bufferData._atmosphere.fogHeightDensity *= fogNightDim;
				bufferData._atmosphere.mieStrength      *= mieNightDim;
				bufferData._atmosphere.density          *= densityNightDim;
			}

			bufferData._atmosphere.volumetricScattering = env_volumetricScattering._val.f32;
			bufferData._atmosphere.volumetricStrength = env_volumetricStrength._val.f32;
			bufferData._atmosphere.volumetricSteps = GetVolumetricEffectiveSteps();
			bufferData._atmosphere.volumetricStepIncrement = env_volumetricStepIncrement._val.f32;
			bufferData._atmosphere.volumetricQuality = r_volumetricQuality._val.i32;
			bufferData._atmosphere.volumetricPointInsideMin = env_volumetricPointInsideMin._val.f32;
			bufferData._atmosphere.volumetricPointInsideMax = env_volumetricPointInsideMax._val.f32;
			bufferData._atmosphere.volumetricSpotInsideMin = env_volumetricSpotInsideMin._val.f32;
			bufferData._atmosphere.volumetricSpotInsideMax = env_volumetricSpotInsideMax._val.f32;

			// bloom
			bufferData._bloom.luminosityThreshold = r_bloomLuminanceThreshold._val.f32;
			bufferData._bloom.viewportScale = 4.0f;
			bufferData._bloom.bloomIntensity = r_bloomPhysicalIntensity._val.f32;
			bufferData._bloom.bloomClamp = r_bloomPhysicalClamp._val.f32;

			bufferData._time = g_pEnv->_timeManager->GetTime();
			bufferData._frame = (uint32_t)g_pEnv->_timeManager->_frameCount;
			bufferData._gamma = r_gamma._val.f32;			

			// ocean
			bufferData._oceanConfig = _currentScene->GetOcean();

			bufferData._jitterOffsets = _taa.GetJitterOffset(viewport.width, viewport.height);
			_denoiseFD.jitter = bufferData._jitterOffsets;

			bufferData._chromaticAbberationAmmount = r_chromaticAbberation._val.f32;

			perFrameBuffer->Write(&bufferData, sizeof(bufferData));

			// set the per frame constant buffers
			g_pEnv->_graphicsDevice->SetConstantBufferVS(0, perFrameBuffer);
			g_pEnv->_graphicsDevice->SetConstantBufferPS(0, perFrameBuffer);
		}
	}

	void SceneRenderer::SetupPerShadowCasterBuffer(Light* shadowCaster, bool forceCascade, int32_t passIdx, int32_t lightIndex, int32_t numSamples, float coneSize)
	{
		// update the per frame buffer with our numeric data
		//
		auto perFrameBuffer = g_pEnv->_graphicsDevice->GetEngineConstantBuffer(EngineConstantBuffer::PerShadowCasterBuffer);

		if (perFrameBuffer)
		{
			PerShadowCasterBuffer bufferData = {};
			bufferData._shadowConfig.passIndex = passIdx;
			bufferData._shadowConfig.cascadeOverride = forceCascade ? passIdx : -1;
			bufferData._shadowConfig.lightIndex = lightIndex;

			// Shadowmap data
			if (shadowCaster != nullptr)
			{
				const auto maxShadowCascades = shadowCaster->GetMaxSupportedShadowCascades();

				//for (int i = 0; i < maxShadowCascades; ++i)
				//{
				//	//_shadowMap[i]->SetRenderState();

				//	// Calculate the shadow map cascade ranges
				//	//
				//	float start = (float)(i + 0) / (float)maxShadowCascades;
				//	float end = (float)(i + 1) / (float)maxShadowCascades;

				//	// Construct the matrices for shadow mapping
				//	//
				//	shadowCaster->ConstructMatrices(_currentCamera, start, end, i);

				//	bufferData._lightProjectionMatrix[i] = shadowCaster->GetProjectionMatrix(i).Transpose();
				//	bufferData._lightViewMatrix[i] = shadowCaster->GetViewMatrix(i).Transpose();
				//}				

				float cascadeStart = 0.0f;

				for (int i = 0; i < maxShadowCascades; ++i)
				{
					// Calculate the shadow map cascade ranges
					//
					//float start = min(1.0f, (float)(i + 0) / (float)numCascades);
					//float end = min(1.0f, (float)(i + 1) / (float)numCascades);

					/*float start = cascadeStart / _currentCamera->GetFarZ();
					float end = (cascadeStart + r_shadowCascadeRange._val.f32) / _currentCamera->GetFarZ();

					if (i == maxShadowCascades - 1)
						end = 1.0f;

					shadowCaster->ConstructMatrices(_currentCamera, start, end, i);*/

					bufferData._lightProjectionMatrix[i] = shadowCaster->GetProjectionMatrix(i).Transpose();
					bufferData._lightViewMatrix[i] = shadowCaster->GetViewMatrix(i).Transpose();
					bufferData._lightViewProjectionMatrix[i] = (shadowCaster->GetViewMatrix(i) * shadowCaster->GetProjectionMatrix(i)).Transpose();

					

					cascadeStart += r_shadowCascadeRange._val.f32;
				}

				auto shadowMapSize = shadowCaster->GetShadowMap() ? shadowCaster->GetShadowMap()->GetViewport().width : 0.0f;
				float shadowVarsMultiplier = (8192.0f / shadowMapSize);

				if (shadowVarsMultiplier > 1.0f)
					shadowVarsMultiplier *= 2.0f;

				// shadow
				bufferData._shadowConfig.penumbraFilterMaxSize = r_penumbraFilterMaxSize._val.f32 * shadowVarsMultiplier;
				bufferData._shadowConfig.shadowFilterMaxSize = r_shadowFilterMaxSize._val.f32 * shadowVarsMultiplier;
				bufferData._shadowConfig.biasMultiplier = r_shadowBiasMultiplier._val.f32;
				bufferData._shadowConfig.samples = (float)numSamples;
				bufferData._shadowConfig.cascadeBlendRange = r_shadowCascadeBlendRange._val.f32;

				bufferData._shadowCasterLightDir = shadowCaster->GetEntity()->GetWorldTM().Forward();
				bufferData._lightRadius = shadowCaster->GetRadius();
				bufferData._spotLightConeSize = (coneSize);

				// Populate the soft-cone cosines for SpotLights so the new physical
				// shader can do smoothstep(cosOuter, cosInner, cosTheta). Done here
				// (rather than at every spot-light draw site) so any future caller of
				// SetupPerShadowCasterBuffer that hands in a SpotLight automatically
				// gets the per-light cone state - the previous coneSize-only field was
				// the source of "all batched spot lights share the last light's cone"
				// because callers pushed it via a per-pass constant just before doing a
				// single batched DrawIndexedInstanced.
				if (auto* spot = dynamic_cast<SpotLight*>(shadowCaster); spot != nullptr)
				{
					constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
					const float innerHalfRad = std::max(0.0f, spot->GetInnerConeAngle()) * 0.5f * kDegToRad;
					const float outerHalfRad = std::max(0.0001f, spot->GetOuterConeAngle()) * 0.5f * kDegToRad;
					bufferData._spotLightCosInner = std::cos(innerHalfRad);
					bufferData._spotLightCosOuter = std::cos(outerHalfRad);
				}
				else
				{
					bufferData._spotLightCosInner = 0.0f;
					bufferData._spotLightCosOuter = 0.0f;
				}

				bufferData._shadowConfig.shadowMapSize = shadowMapSize;

				// Contact shadow params - only populated for the directional light
				// (other shadow casters leave the vec4 zeroed, which the shader reads as
				// "disabled" via the .x channel check). Sun is the dominant light source
				// and the one most likely to need the near-field detail contact shadows
				// provide; spot/point shadow maps already have enough resolution for
				// their typical use cases.
				if (dynamic_cast<DirectionalLight*>(shadowCaster) != nullptr && r_contactShadows._val.b)
				{
					// .x intentionally carries the fade-start distance rather than a
					// boolean enable flag. The deferred shader treats .x > 0 as enabled
					// AND uses the magnitude as the start of a smoothstep distance fade
					// out to 1.5x that range, so contact shadows concentrate near the
					// camera (where they're visible + useful) and don't add noise to
					// distant terrain (where TAA can't reconcile the screen-space jitter
					// across camera motion - the volumetric-terrain mid-depth flicker
					// we saw). Disabling contact shadows still zeros the whole vec4 via
					// the else branch below.
					bufferData._shadowConfig.contactShadowParams = math::Vector4(
						r_contactShadowFadeStart._val.f32,
						static_cast<float>(r_contactShadowSteps._val.i32),
						r_contactShadowLength._val.f32,
						r_contactShadowThickness._val.f32);
				}
				else
				{
					bufferData._shadowConfig.contactShadowParams = math::Vector4::Zero;
				}
			}

			perFrameBuffer->Write(&bufferData, sizeof(bufferData));

			// set the per frame constant buffers
			g_pEnv->_graphicsDevice->SetConstantBufferVS(2, perFrameBuffer);
			g_pEnv->_graphicsDevice->SetConstantBufferPS(2, perFrameBuffer);
		}
	}

	void SceneRenderer::RenderOpaque()
	{
		PROFILE();

		/*SceneRenderParameters params;
		params.passIndex = 6;
		params.camera = _currentCamera;
		params.isShadowPass = false;
		params.frustumSliceBounds = _currentCamera->GetFrustumSphere();*/

		//_currentScene->RenderSkySphere();

		// Pass 0: base sky only (no direct sun/mie). This is what fog samples from _atmosphereRT.
		SetupPerShadowCasterBuffer(nullptr, false, 0, 0, 0, 0.0f);
		_currentScene->RenderEntities(
			_currentCamera->GetPVS(),
			LAYERMASK(Layer::Sky),
			MeshRenderFlags::MeshRenderNormal);

		_gbuffer.GetDiffuse()->CopyTo(_atmosphereRT);

		// Pass 1: full sky for the visible frame (includes sun/sunset/mie).
		SetupPerShadowCasterBuffer(nullptr, false, 1, 0, 0, 0.0f);
		_currentScene->RenderEntities(
			_currentCamera->GetPVS(),
			LAYERMASK(Layer::Sky),
			MeshRenderFlags::MeshRenderNormal);

		//g_pEnv->_graphicsDevice->SetCullingMode(CullingMode::FrontFace);

		_currentScene->RenderEntities(
			_currentCamera->GetPVS(),
			LAYERMASK(Layer::StaticGeometry) | LAYERMASK(Layer::DynamicGeometry) | LAYERMASK(Layer::Grass),
			MeshRenderFlags::MeshRenderNormal);
		_currentScene->RenderCustom(_currentScene, _currentCamera, MeshRenderFlags::MeshRenderNormal);

		g_pEnv->_graphicsDevice->SetBlendState(BlendState::Transparency);

		//g_pEnv->_graphicsDevice->EnableDepthBuffer(false);
		//_currentScene->RenderTransparent(params);
		//g_pEnv->_graphicsDevice->EnableDepthBuffer(true);

		// Render all particles to their own RT, then blend that back to the diffuse gbuffer
		//{
		//	g_pEnv->_graphicsDevice->SetBlendState(BlendState::Additive);

		//	g_pEnv->_graphicsDevice->SetRenderTarget(_particleRT, g_pEnv->_graphicsDevice->GetDepthStencil());
		//	_particleRT->ClearRenderTargetView(math::Color(0, 0, 0, 0));
		//	

		//_currentScene->RenderParticles(params);

		//	//
		//	_particleRT->BlendTo_Additive(_gbuffer.GetDiffuse());
		//	_particleRT->BlendTo_Additive(_compositionRT);

		//	_gbuffer.SetAsRenderTargets();
		//}

		g_pEnv->_graphicsDevice->SetBlendState(BlendState::Opaque);

		/*if(r_debugScene._val.i32 == 1)
			_currentScene->RenderDebug(params);*/
	}

	void SceneRenderer::RenderShadowMaps(Light* shadowCaster)
	{
		PROFILE();

		// Enable front face culling for shadow maps
		//
		//g_pEnv->_graphicsDevice->SetCullingMode(CullingMode::NoCulling);
		{
			float cascadeStart = 0.0f;

			for (auto i = 0; i < shadowCaster->GetMaxSupportedShadowCascades(); ++i)
			{
				auto shadowMap = shadowCaster->GetShadowMap(i);

				if (!shadowMap)
					continue;

				/*auto lightFlags = shadowCaster->GetFlags();

				if (!(_currentCamera->HasMovedThisFrame() ||
					(lightFlags & LightFlags::RebuildShadowMatrices | LightFlags::RebuildPVS) == (LightFlags::RebuildShadowMatrices | LightFlags::RebuildPVS)))
					continue;*/

				if (r_performantShadowMaps._val.b)
				{
					if ((g_pEnv->_timeManager->_frameCount + i) % 2 == 0)
						continue;
				}
				// Set as the current render target
				//
				shadowMap->SetRenderTarget();	

				bool shouldOverrideCascade = shadowCaster->CastAs<PointLight>() != nullptr;

				
				float start = cascadeStart / _currentCamera->GetFarZ();
				float end = (cascadeStart + r_shadowCascadeRange._val.f32) / _currentCamera->GetFarZ();

				if (i == shadowCaster->GetMaxSupportedShadowCascades() - 1)
					end = 1.0f;

				if (start == 1.0f && end == 1.0f)
				{
					LOG_DEBUG("Cannot render shadow map cascade where start and end are both 1.0f");
					return;
				}

				if (shadowCaster->GetEntity()->GetName().find("PointLight") != std::string::npos)
				{
					bool a = false;
				}

				{
					shadowCaster->ConstructMatrices(_currentCamera, start, end, i);

					PVSParams params;
					params.lodPartition = r_lodPartition._val.f32;
					params.shapeType = PVSParams::ShapeType::Sphere;
					params.shape.sphere = shadowCaster->GetLightBoundingSphere(i);
					//params.shapeType = PVSParams::ShapeType::Frustum;
					//params.shape.frustum = shadowCaster->GetLightBoundingFrustum(i);
					params.isShadow = true;
					params.camera = _currentCamera;

					shadowCaster->GetPVS(i)->CalculateVisibility(_currentScene, params);					
				}
				

				cascadeStart += r_shadowCascadeRange._val.f32;

				SetupPerFrameBuffer(
					shadowCaster->GetViewMatrix(i),
					shadowCaster->GetProjectionMatrix(i),
					shadowCaster->GetViewMatrixPrev(i),
					shadowCaster->GetProjectionMatrixPrev(i),
					shadowCaster->GetMaxSupportedShadowCascades(),
					shadowCaster->GetEntity()->GetComponent<Transform>()->GetForward(),
					shadowMap->GetViewport(),
					i,
					1.0f/*_currentScene->GetSunLight()->GetLightMultiplier()*/,
					shouldOverrideCascade);

				

				_currentScene->RenderEntities(
					shadowCaster->GetPVS(i),
					LAYERMASK(Layer::StaticGeometry) | LAYERMASK(Layer::DynamicGeometry),
					MeshRenderFlags::MeshRenderShadowMap);

				/*if (r_taa._val.b)
				{
					if (auto guiRenderer = g_pEnv->_uiManager->GetRenderer(); guiRenderer != nullptr)
					{
						guiRenderer->StartFrame();
						_taa.Resolve(shadowMap->GetRenderTarget(), shadowMap->GetRenderTarget(), _gbuffer.GetSpecular(), guiRenderer);

						guiRenderer->EndFrame();
					}
				}*/

				// render the transparent parts of the scene next
				//g_pEnv->_graphicsDevice->SetBlendState(BlendState::Additive);

				//_currentScene->RenderTransparent(params);

				//g_pEnv->_graphicsDevice->SetBlendState(BlendState::Opaque);
			}

			shadowCaster->ClearFlag(LightFlags::RebuildShadowMatrices | LightFlags::RebuildPVS);
		}

		g_pEnv->_graphicsDevice->ClearScissorRect();
		// Switch back to back face culling
		//
		//g_pEnv->_graphicsDevice->SetCullingMode(CullingMode::FrontFace);
	}
#if 0
	void SceneRenderer::RenderWater()
	{
		PROFILE();

		/// RENDER WATER
		SceneRenderParameters params;
		params.passIndex = 6;
		params.camera = _currentCamera;
		params.isShadowPass = false;

		const auto& bbvp = g_pEnv->_graphicsDevice->GetBackBufferViewport();

		// set the shadow viewport
		//
		D3D11_VIEWPORT vp;
		vp.TopLeftX = 0;
		vp.TopLeftY = 0;
		vp.Width = bbvp.Width * r_waterResolution._val.f32;
		vp.Height = bbvp.Height * r_waterResolution._val.f32;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		g_pEnv->_graphicsDevice->SetViewports({ vp });

		auto guiRenderer = g_pEnv->_uiManager->GetRenderer();
		

		g_pEnv->_graphicsDevice->SetRenderTarget(_waterAccumulationRT, g_pEnv->_graphicsDevice->GetDepthStencil()/*_waterDSV*/);
		_waterAccumulationRT->ClearRenderTargetView(math::Color(0, 0, 0, 0));
		//_waterDSV->ClearDepth(D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL);

		g_pEnv->_graphicsDevice->EnableDepthBuffer(true);

		//guiRenderer->StartFrame();

		//guiRenderer->FullScreenTexturedQuad(_gbuffer.GetDiffuse());

		if (_currentCamera->GetEntity()->GetPosition().y < 0.0f)
			g_pEnv->_graphicsDevice->SetCullingMode(CullingMode::FrontFace);
		else
			g_pEnv->_graphicsDevice->SetCullingMode(CullingMode::BackFace);

		
		//_gbuffer.GetDiffuse()->CopyTo(_waterAccumulationRT);

		_currentScene->RenderWater(params, false, nullptr);

		//_waterAccumulationRT->CopyTo(_compositionRT);
		//_waterAccumulationRT->CopyTo(_gbuffer.GetDiffuse());

		

		guiRenderer->StartFrame();

		g_pEnv->_graphicsDevice->SetBlendState(BlendState::Transparency);
		//_waterBlur->Render(guiRenderer);

		g_pEnv->_graphicsDevice->SetViewports({ bbvp });

		

		g_pEnv->_graphicsDevice->SetRenderTarget(_compositionRT);
		guiRenderer->FullScreenTexturedQuad(_waterAccumulationRT);

		g_pEnv->_graphicsDevice->SetRenderTarget(_gbuffer.GetDiffuse());
		guiRenderer->FullScreenTexturedQuad(_waterAccumulationRT);

		guiRenderer->EndFrame();

		g_pEnv->_graphicsDevice->SetBlendState(BlendState::Opaque);
	}
#endif

	inline void matrixOrthoNormalInvert(math::Matrix& result, const math::Matrix& mat)
	{
		// Transpose the first 3x3
		result.m[0][0] = mat.m[0][0];
		result.m[0][1] = mat.m[1][0];
		result.m[0][2] = mat.m[2][0];
		result.m[1][0] = mat.m[0][1];
		result.m[1][1] = mat.m[1][1];
		result.m[1][2] = mat.m[2][1];
		result.m[2][0] = mat.m[0][2];
		result.m[2][1] = mat.m[1][2];
		result.m[2][2] = mat.m[2][2];

		// Invert the translation
		result.m[3][0] = -((mat.m[3][0] * mat.m[0][0]) + (mat.m[3][1] * mat.m[0][1]) + (mat.m[3][2] * mat.m[0][2]));
		result.m[3][1] = -((mat.m[3][0] * mat.m[1][0]) + (mat.m[3][1] * mat.m[1][1]) + (mat.m[3][2] * mat.m[1][2]));
		result.m[3][2] = -((mat.m[3][0] * mat.m[2][0]) + (mat.m[3][1] * mat.m[2][1]) + (mat.m[3][2] * mat.m[2][2]));

		// Fill in the remaining constants
		result.m[0][3] = 0.0f;
		result.m[1][3] = 0.0f;
		result.m[2][3] = 0.0f;
		result.m[3][3] = 1.0f;
	}

	inline void calcCameraToPrevCamera(math::Matrix& outCameraToPrevCamera, const math::Matrix& cameraToWorld, const math::Matrix& cameraToWorldPrev)
	{
		// Create translated versions of cameraToWorld and cameraToWorldPrev, translated to
		// so that the current camera is effectively at the world origin.
		// CC == 'Camera-Centred'
		math::Matrix cameraToCcWorld = cameraToWorld;
		*(math::Vector4*)&cameraToCcWorld.m[3][0] = math::Vector4(0, 0, 0, 1);
		math::Matrix cameraToCcWorldPrev = cameraToWorldPrev;
		cameraToCcWorldPrev.m[3][0] -= cameraToWorld.m[3][0];
		cameraToCcWorldPrev.m[3][1] -= cameraToWorld.m[3][1];
		cameraToCcWorldPrev.m[3][2] -= cameraToWorld.m[3][2];

		// We can use an optimised invert if we assume that the camera matrix is orthonormal
		math::Matrix ccWorldToCameraPrev;
		matrixOrthoNormalInvert(ccWorldToCameraPrev, cameraToCcWorldPrev);
		//matrixMul(outCameraToPrevCamera, cameraToCcWorld, ccWorldToCameraPrev);
		outCameraToPrevCamera = cameraToCcWorld * ccWorldToCameraPrev;
	}

	void SceneRenderer::SetStreamlineConstants()
	{
		auto sl = g_pEnv->_streamlineProvider;

		if (!sl || !sl->IsEnabled())
			return;

		if (!_currentCamera)
			return;

		if (_currentCamera->IsDLSSEnabled() == false)
		{
			g_pEnv->_streamlineProvider->SetDLSSOptions(0.5f, false, false, DLSSMode::Off, g_pEnv->GetScreenWidth(), g_pEnv->GetScreenHeight());
			return;
		}

		StreamlineConstants consts;

		auto viewReprojection = _currentCamera->GetViewMatrix().Invert() * _currentCamera->GetViewMatrixPrev();
		auto reprojectionMatrix = _currentCamera->GetProjectionMatrix().Invert() * viewReprojection * _currentCamera->GetProjectionMatrixPrev();

		const auto& cameraTransform = _currentCamera->GetEntity()->GetComponent<Transform>();

		consts.cameraAspectRatio = _currentCamera->GetAspectRatio();
		consts.cameraFOV = ToRadian(_currentCamera->GetFov());
		consts.cameraFar = _currentCamera->GetFarZ();
		consts.cameraNear = _currentCamera->GetNearZ();
		consts.cameraMotionIncluded = true;// false;

		consts.cameraPos = cameraTransform->GetPosition();
		consts.cameraFwd = cameraTransform->GetForward();
		consts.cameraUp = cameraTransform->GetUp();
		consts.cameraRight = cameraTransform->GetRight();

		consts.cameraViewToClip = _currentCamera->GetProjectionMatrix();
		consts.clipToCameraView = _currentCamera->GetProjectionMatrix().Invert();

#if 1
		math::Matrix cameraViewToWorld(
			math::Vector4(consts.cameraRight.x, consts.cameraRight.y, consts.cameraRight.z, 0.f),
			math::Vector4(consts.cameraUp.x, consts.cameraUp.y, consts.cameraUp.z, 0.f),
			math::Vector4(consts.cameraFwd.x, consts.cameraFwd.y, consts.cameraFwd.z, 0.f),
			math::Vector4(consts.cameraPos.x, consts.cameraPos.y, consts.cameraPos.z, 1.f)
		);

		const auto& prevPosition = cameraTransform->GetPosition(TransformState::Previous);
		const auto& prevForward = cameraTransform->GetForward(TransformState::Previous);
		const auto& prevUp = cameraTransform->GetUp(TransformState::Previous);
		const auto& prevRight = cameraTransform->GetRight(TransformState::Previous);

		math::Matrix cameraViewToWorldPrev(
			math::Vector4(prevRight.x, prevRight.y, prevRight.z, 0.f),
			math::Vector4(prevUp.x, prevUp.y, prevUp.z, 0.f),
			math::Vector4(prevForward.x, prevForward.y, prevForward.z, 0.f),
			math::Vector4(prevPosition.x, prevPosition.y, prevPosition.z, 1.f)
		);

		math::Matrix cameraViewToPrevCameraView;
		calcCameraToPrevCamera(cameraViewToPrevCameraView, cameraViewToWorld, cameraViewToWorldPrev);

		math::Matrix clipToPrevCameraView;
		//matrixMul(clipToPrevCameraView, consts.clipToCameraView, cameraViewToPrevCameraView);
		clipToPrevCameraView = consts.clipToCameraView * cameraViewToPrevCameraView;

		//matrixMul(consts.clipToPrevClip, clipToPrevCameraView, cameraViewToClipPrev);
		consts.clipToPrevClip = clipToPrevCameraView * _currentCamera->GetProjectionMatrixPrev();

		//matrixFullInvert(consts.prevClipToClip, consts.clipToPrevClip);
		consts.prevClipToClip = consts.clipToPrevClip.Invert();

#else
		consts.clipToPrevClip = reprojectionMatrix;
		consts.prevClipToClip = reprojectionMatrix.Invert();
#endif

		consts.depthInverted = false;
		consts.jitterOffset = _taa.GetJitterOffset(_currentCamera->GetViewport().width, _currentCamera->GetViewport().height);
		//consts.mvecScale = { 1.0f / m_RenderingRectSize.x , 1.0f / m_RenderingRectSize.y }; // This are scale factors used to normalize mvec (to -1,1) and donut has mvec in pixel space
		
		consts.reset = false;
		consts.motionVectors3D = false;
		consts.motionVectorsInvalidValue = FLT_MIN;
		consts.motionVectorsJittered = false;

		sl->SetCommonConstants(consts);
	}

	void SceneRenderer::RenderPostProcessing(SceneFlags flags)
	{
		PROFILE();

		const bool profileAlbedoOnly = r_profileAlbedoOnly._val.b;
		bool canPostProcess = (flags & SceneFlags::PostProcessingEnabled) != (SceneFlags)0 && !r_profileDisablePost._val.b && !profileAlbedoOnly;

		if (profileAlbedoOnly)
		{
			_gbuffer.GetDiffuse()->CopyTo(_beautyRT);
		}
		else if (canPostProcess)
		{
			if (g_pEnv->_ssaoProvider)
			{
				g_pEnv->_ssaoProvider->ApplyAmbientOcclusion(
					_currentCamera,
					_gbuffer.GetDepthBuffer(),
					nullptr/*_gbuffer.GetNormal()*/,
					_beautyRT);
			}

			//_gbuffer.GetDiffuse()->CopyTo(_compositionRT);

			RenderLights();
			RenderDiffuseGI();
			// SSS sits after direct lighting + GI but BEFORE transparency / fog /
			// volumetrics. We want the scatter to operate on opaque lit colour in
			// linear HDR space; running it after fog would blur fog through skin
			// (incorrect), and after transparency would force overlapping
			// alpha-blended geometry to be re-sampled into the scatter kernel.
			RenderSubsurfaceScattering();
			RenderTransparent();
			RenderFog();
			//RenderWater();
			RenderVolumetricLighting();
			RenderVolumetricClouds();

			// don't bother doing this if we don't need to, its expensive!
			if(_currentScene->DidAnyDrawnItemReflect())
				RenderSSR();

			if (r_taa._val.b)
			{
				_taa.Resolve(_beautyRT, _beautyRT, _gbuffer.GetVelocity(), _gbuffer.GetNormal(), g_pEnv->GetUIManager().GetRenderer());
			}
			
			if (!r_profileDisableBloom._val.b)
			{
				_bloomEffect->Render(_currentCamera, _beautyRT, _beautyRT);
			}

			// Auto exposure: sample the post-bloom beauty for adaptive eye-adaption metering.
			// Runs after bloom so bright bloom glare is counted by the meter (matching how
			// the viewer perceives the scene); runs before colour grading so the resulting
			// multiplier can be applied via r_exposure in the per-frame buffer.
			//
			// Passing sun elevation lets AutoExposure switch its target luma and max
			// multiplier to night-time values as the sun descends - without this, the meter
			// drives a dark night scene back up toward daytime middle-grey, undoing the
			// authored mood entirely.
			{
				const float dt = (g_pEnv && g_pEnv->_timeManager)
					? std::clamp(static_cast<float>(g_pEnv->_timeManager->_frameTime), 0.0f, 0.1f)
					: (1.0f / 60.0f);
				float sunElevation = 1.0f;
				if (auto* sunLight = _currentScene ? _currentScene->GetSunLight() : nullptr)
				{
					if (auto* sunTransform = sunLight->GetEntity() ? sunLight->GetEntity()->GetComponent<Transform>() : nullptr)
					{
						sunElevation = -sunTransform->GetForward().y;
					}
				}
				_autoExposure.Update(_beautyRT, dt, sunElevation);
			}
			
			//_beautyRT->GetPixels(_denoiseFD.colour);
			//_gbuffer.GetNormal()->GetPixels(_denoiseFD.normals);
			//_gbuffer.GetDiffuse()->GetPixels(_denoiseFD.albedo);

		}
		else
		{
			g_pEnv->_ssaoProvider->ApplyAmbientOcclusion(
				_currentCamera,
				_gbuffer.GetDepthBuffer(),
				nullptr/*_gbuffer.GetNormal()*/,
				_beautyRT);

			//_gbuffer.GetDiffuse()->CopyTo(_compositionRT);
			RenderLights();
		}

		// Switch back to the back buffer
		//g_pEnv->_graphicsDevice->SetRenderTarget(g_pEnv->_graphicsDevice->GetBackBuffer());
		

		

		if (auto guiRenderer = g_pEnv->GetUIManager().GetRenderer(); guiRenderer != nullptr)
		{
			if (r_debugForceGBufferBeforePresent._val.b && _gbuffer.GetDiffuse() != nullptr && _beautyRT != nullptr)
			{
				_gbuffer.GetDiffuse()->CopyTo(_beautyRT);
			}

			if (r_debugPresentCopy._val.b && _currentCamera && _currentCamera->GetRenderTarget())
			{
				_beautyRT->CopyTo(_currentCamera->GetRenderTarget());
				return;
			}

			guiRenderer->StartFrame();

			if (canPostProcess)
			{
#if 1
				if (_currentCamera->IsDLSSEnabled() && g_pEnv->_streamlineProvider != nullptr)
				{
					_dlssTarget->ClearRenderTargetView(math::Color(0, 0, 0, 0));

						g_pEnv->_streamlineProvider->PrepareFrameResources(
							_beautyRT->GetNativePtr(),
							_dlssTarget->GetNativePtr(),
							_gbuffer.GetVelocity()->GetNativePtr(),
							_gbuffer.GetDepthBuffer()->GetNativePtr(),
							g_pEnv->_graphicsDevice->GetNativeDeviceContext());

						g_pEnv->_streamlineProvider->EvaluateFeature(StreamlineFeature::DLSS, g_pEnv->_graphicsDevice->GetNativeDeviceContext());

					g_pEnv->_graphicsDevice->SetRenderTarget(_currentCamera->GetRenderTarget());

					RenderOverlays(flags, _dlssTarget, _currentCamera->GetRenderTarget());
				}
				else
#endif
				{

					GFX_PERF_BEGIN(0xFFFFFFFF, L"RenderOverlays");
					{
						g_pEnv->_graphicsDevice->SetRenderTarget(_currentCamera->GetRenderTarget());
						RenderOverlays(flags, _beautyRT, _currentCamera->GetRenderTarget());
					}
					GFX_PERF_END();
				}				
			}
			else
			{
				auto outputShader = _tonemapShader.get();
				if (auto backBuffer = g_pEnv->_graphicsDevice->GetBackBuffer(); backBuffer != nullptr && backBuffer->GetFormat() == DXGI_FORMAT_R16G16B16A16_FLOAT)
				{
					outputShader = _hdrOutputShader.get();
				}

				g_pEnv->_graphicsDevice->SetRenderTarget(_currentCamera->GetRenderTarget());
				g_pEnv->_graphicsDevice->SetViewport(g_pEnv->_graphicsDevice->GetBackBufferViewport());
				guiRenderer->FullScreenTexturedQuad(_beautyRT, outputShader);
			}

			guiRenderer->EndFrame();
		}		
	}

	void SceneRenderer::RenderOverlays(SceneFlags flags, ITexture2D* beauty, ITexture2D* renderTarget)
	{
		const bool profileAlbedoOnly = r_profileAlbedoOnly._val.b;
		bool canPostProcess = (flags & SceneFlags::PostProcessingEnabled) != (SceneFlags)0 && !r_profileDisablePost._val.b && !profileAlbedoOnly;

		if (!canPostProcess || !_currentCamera)
			return;

		if (r_debugPresentCopy._val.b)
		{
			beauty->CopyTo(renderTarget);
			return;
		}

		g_pEnv->_graphicsDevice->SetViewport(g_pEnv->_graphicsDevice->GetBackBufferViewport());
		g_pEnv->_graphicsDevice->SetBlendState(BlendState::Opaque);

		if (auto guiRenderer = g_pEnv->GetUIManager().GetRenderer(); guiRenderer != nullptr)
		{
			guiRenderer->StartFrame();

			auto outputShader = _tonemapShader.get();
			if (auto backBuffer = g_pEnv->_graphicsDevice->GetBackBuffer(); backBuffer != nullptr && backBuffer->GetFormat() == DXGI_FORMAT_R16G16B16A16_FLOAT)
			{
				outputShader = _hdrOutputShader.get();
			}

			// Bokeh DoF runs FIRST in the overlay chain so it gathers
			// pre-tonemap linear HDR colour. The "big bright bokeh ball" look
			// depends on this - sampling post-tonemap colour clamps bright
			// highlights to roughly 1.0 and squashes the disc shape on the
			// brightest sources (the most visually distinctive bokeh pixels).
			// Internally this swaps beauty <-> _subsurfaceIntermediateRT, which
			// SSS has already finished using by this point.
			GFX_PERF_BEGIN(0xFFFFFFFF, L"Bokeh DoF");
			{
				RenderBokehDoF();
			}
			GFX_PERF_END();

			GFX_PERF_BEGIN(0xFFFFFFFF, L"Colour grading");
			{
				guiRenderer->FullScreenTexturedQuad(beauty, _colourGradingShader.get());
				renderTarget->CopyTo(beauty);
			}
			GFX_PERF_END();

			GFX_PERF_BEGIN(0xFFFFFFFF, L"Vignette");
			{
				guiRenderer->FullScreenTexturedQuad(beauty, _vignetteShader.get());
				renderTarget->CopyTo(beauty);
			}
			GFX_PERF_END();

			if (r_chromaticAbberation._val.f32 > 0.0f)
			{
				GFX_PERF_BEGIN(0xFFFFFFFF, L"Chromatic abberration");
				{
					guiRenderer->FullScreenTexturedQuad(beauty, _chromaticAberrationShader.get());
					renderTarget->CopyTo(beauty);
				}
				GFX_PERF_END();
			}			

			if (r_fxaa._val.i32 && canPostProcess)
			{
				GFX_PERF_BEGIN(0xFFFFFFFF, L"FXAA");
				{
					guiRenderer->FullScreenTexturedQuad(beauty, _fxaa.get());
					renderTarget->CopyTo(beauty);
				}
				GFX_PERF_END();
			}

			GFX_PERF_BEGIN(0xFFFFFFFF, L"Display output");
			{
				guiRenderer->FullScreenTexturedQuad(beauty, outputShader);
			}
			GFX_PERF_END();

			const int32_t DebugImageSize = 250;

			// Render the debug targets
			if (r_debugScene._val.b && canPostProcess)
			{
				_gbuffer.RenderDebugTargets(10, 10, DebugImageSize, guiRenderer);


				int32_t xpos = 10;

				guiRenderer->FillTexturedQuad(_ssrDiffuseTexture, DebugImageSize * 5 + 10, 10, DebugImageSize, DebugImageSize, math::Color(1, 1, 1, 1));

				guiRenderer->FillTexturedQuad(_ssrDiffuseHitInfo, DebugImageSize * 6 + 20, 10, DebugImageSize, DebugImageSize, math::Color(1, 1, 1, 1));

				guiRenderer->FillTexturedQuad(_ssrTexture, DebugImageSize * 7 + 30, 10, DebugImageSize, DebugImageSize, math::Color(1, 1, 1, 1));

				guiRenderer->FillTexturedQuad(_ssrHitInfo, DebugImageSize * 8 + 40, 10, DebugImageSize, DebugImageSize, math::Color(1, 1, 1, 1));

				guiRenderer->FillTexturedQuad(_ssrResolved, DebugImageSize * 9 + 50, 10, DebugImageSize, DebugImageSize, math::Color(1, 1, 1, 1));

				//guiRenderer->FillTexturedQuad(_dlssTarget, 150 * 7 + 20, 10, 150, 150, math::Color(1, 1, 1, 1));

#if 0
				for (auto& caster : _shadowCasters)
				{
					for (auto i = 0; i < caster->GetMaxSupportedShadowCascades(); ++i)
					{
						auto map = caster->GetShadowMap(i);

						if (!map)
							continue;

						map->RenderDebugTargets(xpos, 170, DebugImageSize, guiRenderer);
						xpos += 160;
					}
				}

				// draw the shadow accumulator
				guiRenderer->FillTexturedQuad(_shadowMapsAccumulator, xpos, 170, DebugImageSize, DebugImageSize, math::Color(1, 1, 1, 1));
#endif
			}
		}
	}

	void SceneRenderer::RenderLights()
	{
		if (r_debugBypassLighting._val.b)
		{
			if (_gbuffer.GetDiffuse() != nullptr && _beautyRT != nullptr)
				_gbuffer.GetDiffuse()->CopyTo(_beautyRT);
			return;
		}

		// switch to the light accumulation buffer
		g_pEnv->_graphicsDevice->SetRenderTarget(_lightAccumulationBuffer);
		_lightAccumulationBuffer->ClearRenderTargetView(math::Color(0, 0, 0, 0));

		g_pEnv->_graphicsDevice->SetBlendState(BlendState::Additive);
		//g_pEnv->_graphicsDevice->EnableDepthBuffer(false);

		std::vector<DirectionalLight*> directionalLights;
		const bool hasDirectionalLights = _currentScene->GetComponents<DirectionalLight>(directionalLights);

		if (r_debugLightingPass._val.b)
		{
			static uint64_t s_lastLightDiagFrame = 0;
			const uint64_t frameNow = static_cast<uint64_t>(g_pEnv->_timeManager ? g_pEnv->_timeManager->_frameCount : 0);
			if (frameNow - s_lastLightDiagFrame >= 60)
			{
				s_lastLightDiagFrame = frameNow;
				LOG_INFO(
					"RenderLights: directional=%d pointEnabled=%d spotEnabled=%d compShader=%d pointShader=%d spotShader=%d",
					static_cast<int32_t>(directionalLights.size()),
					!r_profileDisablePointLights._val.b ? 1 : 0,
					!r_profileDisableSpotLights._val.b ? 1 : 0,
					_compositionShader ? 1 : 0,
					_pointLightShader ? 1 : 0,
					_spotLightShader ? 1 : 0);
			}
		}

		if (hasDirectionalLights && !_compositionShader)
		{
			LOG_WARN("RenderLights: directional lights present but deferred composition shader is missing. Preserving base beauty as fallback.");
			_beautyRT->CopyTo(_lightAccumulationBuffer);
		}
		if (!r_profileDisableDirectionalLights._val.b && hasDirectionalLights)
		{
			if (_compositionShader)
				RenderDirectionalLights();
		}
		else if (!hasDirectionalLights)
		{
			// Keep a sane base when a scene has no directional light; local lights will add on top.
			_beautyRT->CopyTo(_lightAccumulationBuffer);
		}
		if (!r_profileDisablePointLights._val.b)
			RenderPointLights();
		if (!r_profileDisableSpotLights._val.b)
			RenderSpotLights();

		//_lightAccumulationBuffer->BlendTo_Additive(_gbuffer.GetDiffuse());
		//_lightAccumulationBuffer->BlendTo_Additive(_compositionRT);

		//_lightAccumulationBuffer->CopyTo(_gbuffer.GetDiffuse());
		_lightAccumulationBuffer->CopyTo(_beautyRT);
		
		g_pEnv->_graphicsDevice->SetBlendState(BlendState::Opaque);
		g_pEnv->_graphicsDevice->SetDepthBufferState(DepthBufferState::DepthDefault);
		//g_pEnv->_graphicsDevice->EnableDepthBuffer(true);
	}

	void SceneRenderer::RenderDiffuseGI()
	{
		PROFILE();

		if (!_currentScene || !_currentCamera || !_beautyRT)
			return;

		_diffuseGi.Update(_currentScene, _currentCamera);
		_diffuseGi.Render(_currentScene, _currentCamera, _gbuffer, _beautyRT);
	}

	void SceneRenderer::RenderDirectionalLights()
	{
#if 1
		if (!_compositionShader || !_lightAccumulationBuffer || !_beautyRT)
			return;

		std::vector<DirectionalLight*> directionalLights;
		if (_currentScene->GetComponents<DirectionalLight>(directionalLights) == false)
			return;

		CloudConstants cloudConstants = {};
		const bool hasCloudShadowData = (_cloudConstantBuffer != nullptr && _cloudShapeNoise != nullptr && _cloudDetailNoise != nullptr)
			&& BuildCloudConstants(_currentCamera, cloudConstants);

		if (hasCloudShadowData)
		{
			_cloudConstantBuffer->Write(&cloudConstants, sizeof(cloudConstants));
			g_pEnv->_graphicsDevice->SetConstantBufferPS(4, _cloudConstantBuffer);
		}

		

		//g_pEnv->_graphicsDevice->SetRenderTarget(_compositionRT);

		// Clear the render target
		//
		//_compositionRT->ClearRenderTargetView(math::Color(0, 0, 0, 1));

		// Bind the gbuffer as a resource for the composition shader
		//
		

		if (auto guiRenderer = g_pEnv->GetUIManager().GetRenderer(); guiRenderer != nullptr)
		{
			guiRenderer->StartFrame();

			// For each shadow caster in the scene we have to run a composition pass, so that each light's shadowmap can be incorporated
			//
			for (auto& caster : directionalLights)
			{
				DirectionalLight* light = (DirectionalLight*)caster;

				//_currentShadowCasterForComposition = caster;

				_gbuffer.BindAsShaderResource();
				g_pEnv->_graphicsDevice->SetTexture2D(_beautyRT);

				// Bind each shadowmap
				for (auto i = 0; i < light->GetMaxSupportedShadowCascades(); ++i)
				{
					auto shadowMap = light->GetShadowMap(i);

					if (!shadowMap)
					{
						g_pEnv->_graphicsDevice->SetTexture2D(nullptr);
						continue;
					}

					shadowMap->BindAsShaderResource();
				}

				if (hasCloudShadowData)
				{
					// Deferred uses SHADOWMAPS at t6..t11, so skip t10/t11 before binding cloud 3D noise at t12/t13.
					g_pEnv->_graphicsDevice->SetTexture2D(nullptr);
					g_pEnv->_graphicsDevice->SetTexture2D(nullptr);
					g_pEnv->_graphicsDevice->SetTexture3D(_cloudShapeNoise);
					g_pEnv->_graphicsDevice->SetTexture3D(_cloudDetailNoise);
				}
				else
				{
					// Keep register progression consistent even when cloud shadows are disabled.
					g_pEnv->_graphicsDevice->SetTexture2D(nullptr);
					g_pEnv->_graphicsDevice->SetTexture2D(nullptr);
					g_pEnv->_graphicsDevice->SetTexture3D(nullptr);
					g_pEnv->_graphicsDevice->SetTexture3D(nullptr);
				}

				// Material-features RT at the slot Deferred.shader's
				// GBUFFER_FEATURES_RESOURCE binds to (t14). The extended shading
				// model lobes (clearcoat / anisotropy / sheen) read this; Standard
				// PBR pixels see (0,0,0,0) (cleared each frame) and early-out.
				g_pEnv->_graphicsDevice->SetTexture2D(14, _gbuffer.GetFeatures());
				//_currentShadowMapForComposition = shadowMap;
				//g_pEnv->_graphicsDevice->SetTexture2D(_shadowMapsAccumulator);

				SetupPerShadowCasterBuffer(light, false, 0, 0, r_shadowSamples._val.i32, 0.0f);

				

				guiRenderer->FullScreenTexturedQuad(nullptr, _compositionShader.get());

				_lightAccumulationBuffer->CopyTo(_beautyRT);
			}


			guiRenderer->EndFrame();
		}
#endif
	}

	void SceneRenderer::RenderPointLights()
	{
		std::vector<PointLight*> pointLights;
		if (_currentScene->GetComponents<PointLight>(pointLights) == false)
			return;

		g_pEnv->_graphicsDevice->SetRenderTarget(_lightAccumulationBuffer);

		// Material-features RT for PointLight.shader's GBUFFER_FEATURES_RESOURCE
		// at t5. Point lights only use t0-4 (gbuffer) and t13 (beauty), so t5 is
		// the first free slot. Set once for the pass since all point lights share it.
		g_pEnv->_graphicsDevice->SetTexture2D(5, _gbuffer.GetFeatures());

		const auto& cameraPos = _currentCamera->GetEntity()->GetPosition();

		auto renderer = _sphereEntity->GetComponent<StaticMeshComponent>();
		renderer->SetMaterial(_pointLightMaterial);
		auto mesh = renderer->GetMesh();
		auto instance = mesh->GetInstance();

		bool renderedSphere = false;

		int32_t numPointLightsRendered = 0;

		for (auto& comp : pointLights)
		{
			PointLight* light = (PointLight*)comp;

			const auto& diffuse = light->GetDiffuseColour();

			if (diffuse.w <= 0.0f)
				continue;

			auto lightEnt = light->GetEntity();
			const auto& lightPos = lightEnt->GetPosition();
			const float lightRad = light->GetRadius();

			//SetupPerShadowCasterBuffer(light, true, 0, numPointLightsRendered, 16, 0.0f);

			// if we're inside the sphere we should reverse culling
			/*if ((cameraPos - lightPos).Length() <= lightRad)
			{
				g_pEnv->_graphicsDevice->SetCullingMode(CullingMode::FrontFace);
			}
			else
			{
				g_pEnv->_graphicsDevice->SetCullingMode(CullingMode::BackFace);
			}*/

			if (renderedSphere == false)
			{
				instance->Start();

				renderer->RenderMesh(mesh.get(), MeshRenderFlags::MeshRenderNormal, numPointLightsRendered);

				renderedSphere = true;
			}

			_sphereEntity->SetPosition(lightPos);
			_sphereEntity->SetScale(math::Vector3(lightRad));

			//instance->Render(_sphereEntity->GetWorldTMTranspose(), _sphereEntity->GetWorldTMPrev().Transpose(), math::Color(1, 1, 1, 1));


			const auto& lightForward = lightEnt->GetComponent<Transform>()->GetForward();
			const math::Matrix lightMatrix = math::Matrix::CreateScale(lightRad) * math::Matrix::CreateWorld(lightPos, lightForward, math::Vector3::Up);
			instance->Render(
				_sphereEntity->GetWorldTM(),
				_sphereEntity->GetWorldTMTranspose()/*lightMatrix.Transpose()*/,
				_sphereEntity->GetWorldTMPrevTranspose(),
				_sphereEntity->GetWorldTMInvert(),
				diffuse,
				math::Vector2(lightRad, light->GetLightStrength()));

			numPointLightsRendered++;

			
		}

		if (numPointLightsRendered > 0)
		{
			instance->Finish();

			g_pEnv->_graphicsDevice->SetBlendState(BlendState::Additive);
			g_pEnv->_graphicsDevice->SetDepthBufferState(DepthBufferState::DepthNone);

			//g_pEnv->_graphicsDevice->SetTexture2D(_beautyRT);

			GFX_PERF_BEGIN(0xFFFFFFFF, L"Begin PointLight");

			g_pEnv->_graphicsDevice->DrawIndexedInstanced(_sphereMesh->GetNumIndices(), numPointLightsRendered);

			GFX_PERF_END();
		}

		//g_pEnv->_graphicsDevice->SetCullingMode(CullingMode::BackFace); // restore culling
	}

#if 1
	void SceneRenderer::RenderSpotLights()
	{
		std::vector<SpotLight*> spotLights;
		if (_currentScene->GetComponents<SpotLight>(spotLights) == false)
			return;

		const auto& cameraPos = _currentCamera->GetEntity()->GetPosition();

		std::vector<SpotLight*> spotLightsInsideVolume, spotLightOutsideVolume;

		// sort the spot lights into two vectors, one for volumes the camera is inside of, one for outside
		for (auto& comp : spotLights)
		{
			SpotLight* light = (SpotLight*)comp;

			auto lightEnt = light->GetEntity();
			const auto& lightPos = lightEnt->GetWorldTM().Translation();
			const float lightRad = light->GetRadius();

			if ((cameraPos - lightPos).Length() <= lightRad)
			{
				spotLightsInsideVolume.push_back(light);
			}
			else
			{
				spotLightOutsideVolume.push_back(light);
			}
		}

		g_pEnv->_graphicsDevice->SetRenderTarget(_lightAccumulationBuffer);

		

		auto renderer = _sphereEntity->GetComponent<StaticMeshComponent>();
		renderer->SetMaterial(_spotLightMaterial);
		auto mesh = renderer->GetMesh();
		auto instance = mesh->GetInstance();
		bool renderedSphere = false;

		auto renderLightList = [&](const std::vector<SpotLight*>& lights, CullingMode cullMode)
			{
				// One draw call per light. The previous version batched all spot lights
				// into a single DrawIndexedInstanced, but it also wrote per-light cone
				// state and shadow-map bindings into PER-PASS constants/SRVs before each
				// instance::Render - which the batched draw silently collapsed to the
				// LAST light's values. Per-light dispatching keeps the per-pass state in
				// sync with the instance being drawn; the cost is N small draw calls
				// instead of one batched call, but N is small (<= a few dozen lights
				// typically) and each draw is a low-poly sphere so the GPU work dominates
				// the per-draw fixed cost.
				//
				// Important: blend / depth / culling state MUST be set per-iteration AFTER
				// RenderMesh, because StaticMeshComponent::RenderMesh calls SetBlendState
				// / SetDepthBufferState / SetCullingMode from the spot-light material's
				// defaults (Opaque, DepthDefault, BackFace). Setting them once before the
				// loop is silently undone on the first RenderMesh call - we'd then render
				// with depth-test on (sphere clipped behind scene geometry, including
				// when the camera is inside the bounding sphere where the back faces are
				// behind the near plane) and opaque blending (replaces beauty instead of
				// adding to it).

				int32_t numSpotLightsRendered = 0;

				for (auto& comp : lights)
				{
					SpotLight* light = (SpotLight*)comp;

					const auto& diffuse = light->GetDiffuseColour();

					if (diffuse.w <= 0.0f)
						continue;

					auto lightEnt = light->GetEntity();
					const auto& lightPos = lightEnt->GetWorldTM().Translation();
					const float lightRad = light->GetRadius();

					// Per-pass state for THIS light: light dir, cone size + cosines, shadow
					// caster matrices. Reading the new physical cone constants out of the
					// shader without this would still leave the previous light's cosines in
					// the constant buffer.
					SetupPerShadowCasterBuffer(light, true, 0, numSpotLightsRendered, 2, light->GetConeSize());

					_gbuffer.BindAsShaderResource();

					// Bind this light's shadow maps. Earlier code only bound on the first
					// iteration, leaving subsequent lights sampling the first light's
					// depth - visible as the wrong shadows landing on lights past the
					// first.
					for (int32_t i = 0; i < 6; ++i)
					{
						auto shadowMap = light->GetShadowMap(i);
						if (shadowMap != nullptr)
							shadowMap->BindAsShaderResource();
						else
							g_pEnv->_graphicsDevice->SetTexture2D(nullptr);
					}

					// Material-features RT at slot 11 - SpotLight.shader's
					// GBUFFER_FEATURES_RESOURCE binds there (gbuffer = t0..4,
					// shadowmaps = t5..10, features = t11). Bound per-light because
					// the gbuffer rebind + shadowmap loop above advances the slot
					// state and would otherwise leave t11 stale.
					g_pEnv->_graphicsDevice->SetTexture2D(11, _gbuffer.GetFeatures());

					// One-instance buffer: Start/Render/Finish each iteration so the
					// instance vertex buffer has exactly this light's data when the draw
					// fires. Allocates a fresh upload each frame per light which is the
					// price we pay for per-light state correctness.
					instance->Start();

					_sphereEntity->SetPosition(lightPos);
					_sphereEntity->SetScale(math::Vector3(lightRad));

					instance->Render(
						_sphereEntity->GetWorldTM(),
						_sphereEntity->GetWorldTMTranspose(),
						_sphereEntity->GetWorldTMPrevTranspose(),
						_sphereEntity->GetWorldTMInvert(),
						diffuse,
						math::Vector2(lightRad, light->GetLightStrength()));

					instance->Finish();

					// RenderMesh sets up vertex/index/material bindings AND - critically -
					// resets blend/depth/cull to the material's defaults. We override
					// those AFTER the call so the actual draw runs with additive blending,
					// depth-test off, and the inside/outside-bounding-sphere culling mode.
					renderer->RenderMesh(mesh.get(), MeshRenderFlags::MeshRenderNormal, 0);

					g_pEnv->_graphicsDevice->SetCullingMode(cullMode);
					g_pEnv->_graphicsDevice->SetBlendState(BlendState::Additive);
					g_pEnv->_graphicsDevice->SetDepthBufferState(DepthBufferState::DepthNone);

					GFX_PERF_BEGIN(0xFFFFFFFF, L"Begin SpotLight");
					g_pEnv->_graphicsDevice->DrawIndexedInstanced(_sphereMesh->GetNumIndices(), 1);
					GFX_PERF_END();

					numSpotLightsRendered++;
				}

				// renderedSphere flag was used by the old batched path; left at the outer
				// scope as a no-op for any code below that might read it.
				renderedSphere = numSpotLightsRendered > 0;
			};

		renderLightList(spotLightsInsideVolume, CullingMode::FrontFace);
		renderLightList(spotLightOutsideVolume, CullingMode::BackFace);

		
		g_pEnv->_graphicsDevice->SetCullingMode(CullingMode::BackFace);
	}
#endif

	void SceneRenderer::SetupForwardLights()
	{
		if (_forwardLightsBuffer == nullptr || _currentScene == nullptr)
			return;

		ForwardLightConstants data = {};
		uint32_t pointCount = 0;
		uint32_t spotCount = 0;

		// Point lights: same selection rule the deferred path uses (skip empties / zero-alpha).
		// Capped at kMaxForwardPointLights — anything beyond is dropped silently.
		std::vector<PointLight*> pointLights;
		if (_currentScene->GetComponents<PointLight>(pointLights))
		{
			for (auto* light : pointLights)
			{
				if (light == nullptr || light->GetEntity() == nullptr || light->GetEntity()->IsPendingDeletion())
					continue;
				const auto diffuse = light->GetDiffuseColour();
				if (diffuse.w <= 0.0f)
					continue;
				if (pointCount >= kMaxForwardPointLights)
					break;

				const auto pos = light->GetEntity()->GetPosition();
				const float radius = std::max(0.05f, light->GetRadius());
				const float strength = std::max(0.0f, light->GetLightStrength() * light->GetLightMultiplier());

				data.pointPosRadius[pointCount] = math::Vector4(pos.x, pos.y, pos.z, radius);
				data.pointColorStrength[pointCount] = math::Vector4(diffuse.x, diffuse.y, diffuse.z, strength);
				++pointCount;
			}
		}

		std::vector<SpotLight*> spotLights;
		if (_currentScene->GetComponents<SpotLight>(spotLights))
		{
			for (auto* light : spotLights)
			{
				if (light == nullptr || light->GetEntity() == nullptr || light->GetEntity()->IsPendingDeletion())
					continue;
				const auto diffuse = light->GetDiffuseColour();
				if (diffuse.w <= 0.0f)
					continue;
				if (spotCount >= kMaxForwardSpotLights)
					break;

				auto* lightEnt = light->GetEntity();
				const auto pos = lightEnt->GetWorldTM().Translation();
				const auto fwd = lightEnt->GetWorldTM().Forward();
				const float radius = std::max(0.05f, light->GetRadius());
				const float strength = std::max(0.0f, light->GetLightStrength() * light->GetLightMultiplier());
				// cos(half-cone-angle) for both outer and inner. Shader does
				// smoothstep(cosOuter, cosInner, cosTheta) -> soft edge between the
				// two angles. cosInner > cosOuter (smaller angle = larger cosine).
				const float outerAngle = std::max(0.1f, light->GetOuterConeAngle());
				const float innerAngle = std::clamp(light->GetInnerConeAngle(), 0.0f, outerAngle);
				const float cosOuter = std::cos(ToRadian(outerAngle * 0.5f));
				const float cosInner = std::cos(ToRadian(innerAngle * 0.5f));

				data.spotPosRadius[spotCount] = math::Vector4(pos.x, pos.y, pos.z, radius);
				data.spotDirCone[spotCount] = math::Vector4(fwd.x, fwd.y, fwd.z, cosOuter);
				data.spotColorStrength[spotCount] = math::Vector4(diffuse.x, diffuse.y, diffuse.z, strength);
				data.spotInnerCone[spotCount] = math::Vector4(cosInner, 0.0f, 0.0f, 0.0f);
				++spotCount;
			}
		}

		data.countsAndParams = math::Vector4(static_cast<float>(pointCount), static_cast<float>(spotCount), 0.0f, 0.0f);
		_forwardLightsBuffer->Write(&data, sizeof(data));
	}

	void SceneRenderer::RenderTransparent()
	{
		PROFILE();

		GFX_PERF_BEGIN(0xFFFFFFFF, L"Begin Transparent");

		// Transparent shaders (notably water, but also any alpha-blended mesh) sample the
		// beauty texture for reflection / refraction. Render transparency into a separate RT
		// to avoid SRV/RTV hazards on _beautyRT, then copy back at the end.
		const bool haveSnapshotForReflection = (_waterRT != nullptr);
		if (haveSnapshotForReflection)
		{
			_beautyRT->CopyTo(_waterRT);
			g_pEnv->_graphicsDevice->SetRenderTarget(_waterRT, _gbuffer.GetDepthBuffer());
		}
		else
		{
			g_pEnv->_graphicsDevice->SetRenderTarget(_beautyRT, _gbuffer.GetDepthBuffer());
		}

		// Populate b7 with the current frame's dynamic point/spot lights so transparent meshes
		// can do forward PBR lighting (Default.shader transparency path samples this).
		SetupForwardLights();
		if (_forwardLightsBuffer != nullptr)
		{
			g_pEnv->_graphicsDevice->SetConstantBufferPS(7, _forwardLightsBuffer);
		}

		// Bind the opaque scene colour (snapshot from CopyTo above) and the G-buffer normal/
		// position so the transparency shader can do an inline SSR ray-march. Material
		// textures live at t0..t7; slot 11 (depth) is left empty because the depth buffer is
		// currently bound as DSV and the engine's hazard handler would unbind our render
		// target. The shader reads view-space depth from the normal texture's .w channel.
		if (haveSnapshotForReflection && _beautyRT != nullptr)
			g_pEnv->_graphicsDevice->SetTexture2D(10, _beautyRT);
		if (_gbuffer.GetNormal() != nullptr)
			g_pEnv->_graphicsDevice->SetTexture2D(12, _gbuffer.GetNormal());
		if (_gbuffer.GetPosition() != nullptr)
			g_pEnv->_graphicsDevice->SetTexture2D(13, _gbuffer.GetPosition());

		// CRITICAL: SetTexture2D(slot, ...) advances the device's "next implicit slot" counter.
		// Scene::RenderEntities reads that counter to decide where to bind each mesh's
		// material textures (albedo/normal/etc at t0..t7). If we leave the counter at 14,
		// materials end up at t14..t21 and the shader reads garbage from t0..t7 (whole frame
		// renders desaturated / black). Reset it now — the SRVs at slots 10/12/13 remain bound.
		g_pEnv->_graphicsDevice->SetBoundResourceIndex(0);

		_currentScene->RenderEntities(
			_currentCamera->GetPVS(),
			LAYERMASK(Layer::StaticGeometry) | LAYERMASK(Layer::DynamicGeometry) | LAYERMASK(Layer::Grass) | LAYERMASK(Layer::Water),
			MeshRenderFlags::MeshRenderTransparency);

		// Unbind so the next pass doesn't get a stale SRV (and to silence the D3D11 debug
		// layer when these textures are reused as render targets later in the frame). Then
		// reset the counter to whatever RenderEntities left it at — explicit-slot binds bump
		// the counter and we don't want the fog/post-process passes to inherit slot 14.
		const uint32_t postMaterialIndex = g_pEnv->_graphicsDevice->GetBoundResourceIndex();
		g_pEnv->_graphicsDevice->SetTexture2D(10, nullptr);
		g_pEnv->_graphicsDevice->SetTexture2D(12, nullptr);
		g_pEnv->_graphicsDevice->SetTexture2D(13, nullptr);
		g_pEnv->_graphicsDevice->SetBoundResourceIndex(postMaterialIndex);

		if (g_pEnv && g_pEnv->_particleWorldSystem)
		{
			g_pEnv->_particleWorldSystem->Render(_currentScene, _currentCamera, _waterRT ? _waterRT : _beautyRT, _gbuffer.GetDepthBuffer());
		}

		if (_waterRT)
		{
			_waterRT->CopyTo(_beautyRT);
		}

		GFX_PERF_END();

		//g_pEnv->_graphicsDevice->SetCullingMode(CullingMode::BackFace);
	}

	void SceneRenderer::RenderSubsurfaceScattering()
	{
		if (!r_sss._val.b || _subsurfaceShader == nullptr || _beautyRT == nullptr || _subsurfaceIntermediateRT == nullptr || _subsurfaceParamsBuffer == nullptr)
			return;

		PROFILE();

		auto* graphics = g_pEnv->_graphicsDevice;
		auto* guiRenderer = g_pEnv->GetUIManager().GetRenderer();
		if (guiRenderer == nullptr)
			return;

		// Shared bindings: features gbuffer at t1 (model id + per-pixel params) and
		// normal/depth at t2 (depth-aware kernel weighting). The shader's t0 is the
		// source colour - beauty for the horizontal pass, intermediate for the
		// vertical pass. We swap that explicitly between passes below.
		auto* featuresTex = _gbuffer.GetFeatures();
		auto* normalDepthTex = _gbuffer.GetNormal();
		if (featuresTex == nullptr || normalDepthTex == nullptr)
			return;

		// Cbuffer at b6 - matches the shader's `cbuffer SssParams : register(b6)`.
		// x = pass direction (0=H, 1=V), y = world-space radius, z = global intensity.
		auto writeParams = [&](float direction)
		{
			const math::Vector4 params(
				direction,
				r_sssRadius._val.f32,
				r_sssIntensity._val.f32,
				0.0f);
			math::Vector4 paramsCopy = params; // Write takes void* (non-const)
			_subsurfaceParamsBuffer->Write(&paramsCopy, sizeof(paramsCopy));
			graphics->SetConstantBufferPS(6, _subsurfaceParamsBuffer);
		};

		// --- Horizontal pass: beauty -> intermediate ---
		guiRenderer->StartFrame();
		graphics->SetRenderTarget(_subsurfaceIntermediateRT);
		graphics->SetTexture2D(0, _beautyRT);
		graphics->SetTexture2D(1, featuresTex);
		graphics->SetTexture2D(2, normalDepthTex);
		writeParams(0.0f);
		guiRenderer->FullScreenTexturedQuad(nullptr, _subsurfaceShader.get());

		// --- Vertical pass: intermediate -> beauty ---
		graphics->SetRenderTarget(_beautyRT);
		graphics->SetTexture2D(0, _subsurfaceIntermediateRT);
		graphics->SetTexture2D(1, featuresTex);
		graphics->SetTexture2D(2, normalDepthTex);
		writeParams(1.0f);
		guiRenderer->FullScreenTexturedQuad(nullptr, _subsurfaceShader.get());
		guiRenderer->EndFrame();

		// Unbind the SSS-specific SRVs so later passes don't inherit stale bindings.
		graphics->SetTexture2D(0, nullptr);
		graphics->SetTexture2D(1, nullptr);
		graphics->SetTexture2D(2, nullptr);
		graphics->SetConstantBufferPS(6, nullptr);
	}

	void SceneRenderer::RenderDecals()
	{
		// Required resources: shader + cube buffers + position-copy RT + cbuffer.
		// All allocated in CreateShaders / CreateRenderTargets. The GBuffer must
		// be fully populated by RenderOpaque before we run (we need the position
		// RT to snapshot).
		if (_decalShader == nullptr || _decalCubeVB == nullptr || _decalCubeIB == nullptr ||
			_decalConstantsBuffer == nullptr || _decalPositionCopy == nullptr ||
			_currentScene == nullptr)
			return;

		std::vector<DecalComponent*> decals;
		if (_currentScene->GetComponents<DecalComponent>(decals) == false)
			return;

		// Cheap early-out before paying the RT copy cost. We do work in this pass
		// if EITHER (a) there are renderable manual decals OR (b) the auto-puddle
		// system is enabled (it always paints a fullscreen quad once on, and
		// uses puddleAmount=0 as its own per-frame early-out inside the shader).
		bool anyManualDecal = false;
		for (auto* d : decals)
		{
			if (d != nullptr && d->IsRenderable())
			{
				anyManualDecal = true;
				break;
			}
		}
		const bool autoPuddlesOn = r_autoPuddles._val.b && _autoPuddlesShader != nullptr && _autoPuddlesConstantsBuffer != nullptr;
		if (!anyManualDecal && !autoPuddlesOn)
			return;

		PROFILE();

		auto* graphics = g_pEnv->_graphicsDevice;

		// Snapshot the GBuffer position into the decal pass's read RT. Position is
		// bound for write during the opaque pass and during the upcoming lighting
		// passes; the decal PS needs to sample it while diff/mat are bound for
		// write, which is only legal if we read from a copy.
		_gbuffer.GetPosition()->CopyTo(_decalPositionCopy);

		// Bind only the GBuffer RTs we intend to write. Position / velocity /
		// normal / features stay unbound so they aren't clobbered by zeroed writes
		// and so we can keep reading position from our snapshot.
		std::vector<ITexture2D*> decalRTs = { _gbuffer.GetDiffuse(), _gbuffer.GetSpecular() };
		graphics->SetRenderTargets(decalRTs, _gbuffer.GetDepthBuffer());

		// Set state: standard alpha blend (RT0 + RT1 both get src.a * src + (1-src.a) * dst),
		// no depth test (decal box itself is independent of scene depth - containment
		// is decided in the PS by sampling the position copy), and no face culling
		// because the camera might be inside the decal box for a low ceiling-projector.
		const BlendState        prevBlend = graphics->GetBlendState();
		const DepthBufferState  prevDepth = graphics->GetDepthBufferState();
		const CullingMode       prevCull  = graphics->GetCullingMode();
		graphics->SetBlendState(BlendState::Transparency);
		graphics->SetDepthBufferState(DepthBufferState::DepthNone);
		graphics->SetCullingMode(CullingMode::NoCulling);

		// Clear stale PS SRVs from prior passes BEFORE binding our own. The
		// previous (opaque / depth-pyramid / shadow) passes leave assorted SRVs
		// bound at higher slots - notably a position-format (R32G32B32A32_FLOAT)
		// SRV around t6 and the engine's comparison sampler at s1. D3D11's
		// validator pairs every bound SRV against every bound sampler regardless
		// of whether the shader actually references them, and an R32G32B32A32_FLOAT
		// SRV + a SamplerComparisonState binding is flagged as an invalid combo
		// (DEVICE_DRAW_RESOURCE_FORMAT_SAMPLE_C_UNSUPPORTED #372). Wiping all PS
		// SRVs first gives the decal pass a clean slot table and avoids the
		// false-positive validation error.
		graphics->UnbindAllPixelShaderResources();

		// Decal shader expects:
		//   t0 = position copy  (set once for the whole pass)
		//   t1 = decal albedo   (set per-decal)
		//   t2 = decal normal   (reserved v1)
		//   t3 = decal mat      (set per-decal)
		// VS + PS + cbuffer b4 also bound per-decal.
		graphics->SetTexture2D(0, _decalPositionCopy);

		IShaderStage* vs = _decalShader->GetShaderStage(ShaderStage::VertexShader);
		IShaderStage* ps = _decalShader->GetShaderStage(ShaderStage::PixelShader);
		graphics->SetVertexShader(vs);
		graphics->SetPixelShader(ps);
		graphics->SetInputLayout(_decalShader->GetInputLayout());
		graphics->SetTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		graphics->SetVertexBuffer(0, _decalCubeVB);
		graphics->SetIndexBuffer(_decalCubeIB);
		graphics->SetConstantBufferPS(4, _decalConstantsBuffer);

		// Per-decal constants payload. Must match Decal.shader's cbuffer DecalConstants.
		struct DecalGpuConstants
		{
			math::Matrix worldInverse;
			math::Vector4 weights;    // albedo, normal (rsvd), mat, opacity
			math::Vector4 overrides;  // roughness, metallic, normalCutoff, albedoBound
			math::Vector4 flags;      // normalBound (rsvd), matBound, _, _
		};

		auto* perObjectBuffer = graphics->GetEngineConstantBuffer(EngineConstantBuffer::PerObjectBuffer);

		for (auto* decal : decals)
		{
			if (decal == nullptr || !decal->IsRenderable())
				continue;

			auto* entity = decal->GetEntity();
			if (entity == nullptr)
				continue;

			// World matrix from the entity's transform. The unit cube vertices live
			// in [-0.5, 0.5]^3 so the entity's scale acts as the decal extents
			// directly - artists set "size" via the standard Transform scale gizmo,
			// no extra UI required.
			const math::Matrix world = entity->GetWorldTM();
			const math::Matrix worldInverse = world.Invert();

			// Update PerObjectBuffer with the decal's world matrix (the shader reads
			// g_worldMatrix for the VS transform AND for extracting world-space
			// basis axes in the PS). Material props left default - the decal shader
			// doesn't look at g_material.
			if (perObjectBuffer != nullptr)
			{
				PerObjectBuffer perObj = {};
				perObj._worldMatrix = world.Transpose(); // engine uploads matrices transposed
				perObj._flags = 0;
				perObj.entityId = -1;
				perObj.cullDistance = 0.0f;
				perObj.pad = 0;
				perObj._material = MaterialProperties{};
				perObjectBuffer->Write(&perObj, sizeof(perObj));
			}

			DecalGpuConstants consts;
			consts.worldInverse = worldInverse.Transpose();
			consts.weights = math::Vector4(
				decal->GetAlbedoWeight(),
				decal->GetNormalWeight(),
				decal->GetMatWeight(),
				decal->GetOpacity());
			const bool hasAlbedo = decal->GetAlbedoTexture() != nullptr;
			const bool hasNormal = decal->GetNormalTexture() != nullptr;
			const bool hasMat    = decal->GetMatTexture()    != nullptr;
			consts.overrides = math::Vector4(
				decal->GetRoughnessOverride(),
				decal->GetMetallicOverride(),
				decal->GetNormalCutoff(),
				hasAlbedo ? 1.0f : 0.0f);
			consts.flags = math::Vector4(
				hasNormal ? 1.0f : 0.0f,
				hasMat    ? 1.0f : 0.0f,
				decal->GetRespondsToWeather() ? 1.0f : 0.0f,
				0.0f);
			_decalConstantsBuffer->Write(&consts, sizeof(consts));

			// Decal textures. Slots match Decal.shader's t1/t2/t3. Null is fine -
			// the PS checks the bound flags before sampling.
			graphics->SetTexture2D(1, decal->GetAlbedoTexture());
			graphics->SetTexture2D(2, decal->GetNormalTexture());
			graphics->SetTexture2D(3, decal->GetMatTexture());

			graphics->DrawIndexed(36);
		}

		// Auto-puddles: one fullscreen draw after the manual decal loop. Reuses
		// the same RT binding (diff + mat) and the same position-copy SRV at t0.
		// The shader checks puddleAmount > 0 internally so we don't need a
		// per-frame "is it raining" gate in C++; the gate that DOES matter is
		// the HVar toggle (artists turn the whole system off without recompiling).
		if (autoPuddlesOn && _autoPuddlesQuadVB != nullptr && _autoPuddlesQuadIB != nullptr)
		{
			// Bind the live GBuffer normal RT as SRV at t1 (we don't write to it
			// during the decal/auto-puddle pass so reading is legal). The auto-
			// puddle PS samples it for the per-pixel flatness test.
			graphics->SetTexture2D(1, _gbuffer.GetNormal());

			// Pack the HVar-driven config into the auto-puddle cbuffer.
			struct AutoPuddleGpuConstants
			{
				math::Vector4 params;     // scale, threshold, normalCutoff, opacity
				math::Vector4 appearance; // darken, forceRain, _, _
			};
			AutoPuddleGpuConstants apc;
			apc.params = math::Vector4(
				r_autoPuddlesScale._val.f32,
				r_autoPuddlesThreshold._val.f32,
				r_autoPuddlesNormalCutoff._val.f32,
				r_autoPuddlesOpacity._val.f32);
			apc.appearance = math::Vector4(
				r_autoPuddlesDarken._val.f32,
				r_autoPuddlesForceRain._val.f32,
				0.0f, 0.0f);
			_autoPuddlesConstantsBuffer->Write(&apc, sizeof(apc));
			graphics->SetConstantBufferPS(4, _autoPuddlesConstantsBuffer);

			// Direct fullscreen quad draw - we deliberately bypass the GuiRenderer's
			// FullScreenTexturedQuad path because that one is built for single-RT
			// writes only (it doesn't preserve multi-RT setup on some paths). Our
			// own VB/IB is a clip-space (-1, -1, 0)..(1, 1, 0) quad and the
			// AutoPuddles VS takes positions through to SV_POSITION as-is. Uses
			// the same `Pos` input layout the decal cube uses.
			IShaderStage* apVS = _autoPuddlesShader->GetShaderStage(ShaderStage::VertexShader);
			IShaderStage* apPS = _autoPuddlesShader->GetShaderStage(ShaderStage::PixelShader);
			graphics->SetVertexShader(apVS);
			graphics->SetPixelShader(apPS);
			graphics->SetInputLayout(_autoPuddlesShader->GetInputLayout());
			graphics->SetTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			graphics->SetVertexBuffer(0, _autoPuddlesQuadVB);
			graphics->SetIndexBuffer(_autoPuddlesQuadIB);
			graphics->DrawIndexed(6);

			// Clear t1 so the cleanup below doesn't leave the normal RT bound as
			// SRV across into the next pass.
			graphics->SetTexture2D(1, nullptr);
		}

		// Restore state. Unbind decal SRVs so the next pass doesn't accidentally
		// sample stale decal textures, and put the GBuffer RTs back to the canonical
		// SetAsRenderTargets binding for downstream passes.
		graphics->SetTexture2D(0, nullptr);
		graphics->SetTexture2D(1, nullptr);
		graphics->SetTexture2D(2, nullptr);
		graphics->SetTexture2D(3, nullptr);
		graphics->SetConstantBufferPS(4, nullptr);

		graphics->SetBlendState(prevBlend);
		graphics->SetDepthBufferState(prevDepth);
		graphics->SetCullingMode(prevCull);

		// Reset the implicit "next-bind" PS SRV counter to 0. The engine's slot-
		// less SetTexture2D / SetTexture2DArray paths advance this counter and
		// subsequent passes (notably _gbuffer.BindAsShaderResource() in
		// RenderDirectionalLights) rely on it being 0 so their SetTexture2DArray
		// lands the GBuffer at t0..t4 - which is what every deferred shader
		// expects. Our explicit-slot SetTexture2D(3, nullptr) at the unbind step
		// above sets the counter to 4 instead, which would shift the GBuffer
		// position RT into slot 7 and cause D3D11 validation to flag the
		// R32G32B32A32_FLOAT SRV at slot 6 against the engine's comparison
		// sampler at s1 (DEVICE_DRAW_RESOURCE_FORMAT_SAMPLE_C_UNSUPPORTED).
		graphics->SetBoundResourceIndex(0);
	}

	void SceneRenderer::RenderBokehDoF()
	{
		if (!r_dof._val.b || _bokehDoFShader == nullptr || _beautyRT == nullptr ||
			_subsurfaceIntermediateRT == nullptr || _bokehDoFParamsBuffer == nullptr)
			return;

		PROFILE();

		auto* graphics = g_pEnv->_graphicsDevice;
		auto* guiRenderer = g_pEnv->GetUIManager().GetRenderer();
		if (guiRenderer == nullptr)
			return;

		auto* normalDepthTex = _gbuffer.GetNormal();
		if (normalDepthTex == nullptr)
			return;

		// Reuse the SSS intermediate as a scratch RT - SSS has already finished
		// for this frame and the format/size match exactly, so allocating a
		// dedicated DoF scratch is wasteful. The bokeh pass reads beauty, writes
		// scratch, then we copy scratch back into beauty so downstream effects
		// (colour grading, vignette, etc.) see the DoF'd image.
		auto* scratchRT = _subsurfaceIntermediateRT;

		// Cbuffer at b6: (focusDistance, focusRange, aperture, maxCocPixels).
		math::Vector4 params(
			r_dofFocusDistance._val.f32,
			r_dofFocusRange._val.f32,
			r_dofAperture._val.f32,
			r_dofMaxBlur._val.f32);
		math::Vector4 paramsCopy = params; // Write takes void* (non-const)
		_bokehDoFParamsBuffer->Write(&paramsCopy, sizeof(paramsCopy));
		graphics->SetConstantBufferPS(6, _bokehDoFParamsBuffer);

		// NOTE: do NOT wrap this in guiRenderer->StartFrame()/EndFrame() - this
		// path runs from inside RenderOverlays which has already begun a frame,
		// and a nested EndFrame would flush the outer draw list against our
		// scratch RT (visible as a grey screen because the queued UI draws land
		// in the wrong place and then get copied over the beauty buffer).
		graphics->SetRenderTarget(scratchRT);
		graphics->SetTexture2D(0, _beautyRT);
		graphics->SetTexture2D(1, normalDepthTex);
		guiRenderer->FullScreenTexturedQuad(nullptr, _bokehDoFShader.get());

		// Copy back so beauty carries the DoF'd image into the rest of the post
		// chain. Cheap on D3D11 (a single CopyResource on same-format RTs).
		scratchRT->CopyTo(_beautyRT);

		graphics->SetTexture2D(0, nullptr);
		graphics->SetTexture2D(1, nullptr);
		graphics->SetConstantBufferPS(6, nullptr);
	}

	void SceneRenderer::RenderFog()
	{
		if (!r_fog._val.i8 || r_debugBypassFog._val.b)
			return;

		if (!_fogEffect || !_fogBuffer || !_beautyRT)
		{
			if (r_debugLightingPass._val.b)
			{
				LOG_WARN("RenderFog skipped due to missing resources: fogEffect=%d fogBuffer=%d beauty=%d",
					_fogEffect ? 1 : 0,
					_fogBuffer ? 1 : 0,
					_beautyRT ? 1 : 0);
			}
			return;
		}

		PROFILE();

		// Switch back to the back buffer
		_fogBuffer->ClearRenderTargetView(math::Color(0, 0, 0, 0));
		g_pEnv->_graphicsDevice->SetRenderTarget(_fogBuffer);

		if (auto guiRenderer = g_pEnv->GetUIManager().GetRenderer(); guiRenderer != nullptr)
		{
			guiRenderer->StartFrame();

			// Render fog as a post process
			_gbuffer.BindAsShaderResource(_beautyRT);
			g_pEnv->_graphicsDevice->SetTexture2D(_atmosphereRT);
			g_pEnv->_graphicsDevice->SetTexture2D(_gbuffer.GetDepthBuffer());
			guiRenderer->FullScreenTexturedQuad(nullptr, _fogEffect.get());

			//_fogBuffer->CopyTo(_gbuffer.GetDiffuse());
			_fogBuffer->CopyTo(_beautyRT);

			guiRenderer->EndFrame();
		}
	}

	void SceneRenderer::RenderVolumetricLighting()
	{
		if (!env_volumetricLighting._val.b)
			return;

		PROFILE();

		_volumetricLightingBuffer->ClearRenderTargetView(math::Color(0, 0, 0, 1));
		g_pEnv->_graphicsDevice->SetBlendState(BlendState::Additive);

		// Switch back to the back buffer
		g_pEnv->_graphicsDevice->SetRenderTarget(_volumetricLightingBuffer);

		const auto& bbvp = _currentCamera->GetViewport();

		// set the shadow viewport
		//
		D3D11_VIEWPORT vp;
		vp.TopLeftX = 0;
		vp.TopLeftY = 0;
		vp.Width = bbvp.width / 2;
		vp.Height = bbvp.height / 2;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		g_pEnv->_graphicsDevice->SetViewport(vp);

		if (auto guiRenderer = g_pEnv->GetUIManager().GetRenderer(); guiRenderer != nullptr)
		{
			guiRenderer->StartFrame();

			for (auto& caster : _shadowCasters)
			{
				if (caster->GetIsVolumetric() == false)
					continue;

				// Render fog as a post process
				_gbuffer.BindAsShaderResource();

				// Bind the shadow mappers
				/*for (auto i = 0; i < 6; ++i)
				{
					if (i >= caster->GetMaxSupportedShadowCascades())
						g_pEnv->_graphicsDevice->SetTexture2D(nullptr);
					else
					{
						if (caster->GetShadowMap(i))
							caster->GetShadowMap(i)->BindAsShaderResource();
					}
				}*/

				for (auto i = 0; i < 6; ++i)
				{
					auto shadowMap = caster->GetShadowMap(i);

					if (!shadowMap)
					{
						g_pEnv->_graphicsDevice->SetTexture2D(nullptr);
						continue;
					}

					shadowMap->BindAsShaderResource();
				}

				g_pEnv->_graphicsDevice->SetTexture2D(_blueNoise.get());

				bool forceCascade = caster->CastAs<SpotLight>() != nullptr;

				SetupPerShadowCasterBuffer(caster, forceCascade, 0, 0, 0, 0.0f);

				guiRenderer->FullScreenTexturedQuad(nullptr, _volumetricLighting.get());
			}

			// the blur should have the gbuffer as it needs to sample depth
			
			//_volumetricBlur->Render(guiRenderer);

			g_pEnv->_graphicsDevice->SetViewport(*bbvp.Get11());			

#if 0 // use bilateral
			_gbuffer.BindAsShaderResource();
			_volumetricLightingBuffer->BlendTo_Additive(_compositionRT);
#else
			_volumetricLightingBuffer->BlendTo_Additive(_beautyRT, nullptr);
#endif


			guiRenderer->EndFrame();
		}

		g_pEnv->_graphicsDevice->SetBlendState(BlendState::Opaque);
	}

	void SceneRenderer::RenderSSR()
	{
		PROFILE();

		if (!r_ssr._val.b)
			return;

		GFX_PERF_BEGIN(0xFFFFFFFF, L"SSR Begin");

		_ssrDiffuseTexture->ClearRenderTargetView(math::Color(0, 0, 0, 0));
		_ssrDiffuseHitInfo->ClearRenderTargetView(math::Color(0, 0, 0, 0));
		_ssrTexture->ClearRenderTargetView(math::Color(0, 0, 0, 0));
		_ssrHitInfo->ClearRenderTargetView(math::Color(0, 0, 0, 0));

		g_pEnv->_graphicsDevice->SetRenderTargets({ _ssrDiffuseTexture, _ssrDiffuseHitInfo, _ssrTexture, _ssrHitInfo });
		
		const auto& bbvp = _currentCamera->GetViewport();
		// set the shadow viewport
		//
		D3D11_VIEWPORT vp;
		vp.TopLeftX = 0;
		vp.TopLeftY = 0;
		vp.Width = bbvp.width;// / 2;
		vp.Height = bbvp.height;// / 2;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		g_pEnv->_graphicsDevice->SetViewport(vp);

		if (auto guiRenderer = g_pEnv->GetUIManager().GetRenderer(); guiRenderer != nullptr)
		{
			guiRenderer->StartFrame();

			// Ensure the auto-slot SRV counter starts at zero so the bindings below land at
			// the registers the SSR shader declares (gbuffer=t0..t4, then t5..t8, then GI).
			g_pEnv->_graphicsDevice->UnbindAllPixelShaderResources();

			// Render fog as a post process
			_gbuffer.BindAsShaderResource();

			g_pEnv->_graphicsDevice->SetTexture2D(_beautyRT);
			g_pEnv->_graphicsDevice->SetTexture2D(_blueNoise.get());
			g_pEnv->_graphicsDevice->SetTexture2D(_ssrHistory);
			g_pEnv->_graphicsDevice->SetTexture2D(_gbuffer.GetVelocity());

			// Bind voxel GI clipmaps + GIConstants (b4) so SSR can fall back to GI on miss.
			// Continues from slot t9 (gbuffer=0-4, beauty=5, noise=6, history=7, velocity=8).
			//
			// Critically: only bind when GI is actually enabled. DiffuseGI::Update/Render both
			// early-return when r_giEnable is false, so the voxel textures freeze at whatever
			// state they had when GI was last on - including any bright/saturated patches that
			// were accumulating at that moment. If we kept binding them here, SSR's fallback
			// would keep sampling that stale data forever and produce a persistent coloured
			// blowout on terrain/walls even after the user disabled GI. By skipping the bind
			// the SSR shader sees unbound voxel SRVs which sample as black, so the GI fallback
			// path naturally returns zero contribution.
			HVar* giEnableVar = g_pEnv->_commandManager->FindHVar("r_giEnable");
			const bool giEnabled = (giEnableVar != nullptr) ? giEnableVar->_val.b : true;
			if (giEnabled)
			{
				_diffuseGi.BindVoxelsForReflection();
			}

			guiRenderer->FullScreenTexturedQuad(nullptr, _ssrShader.get());

			

            _ssrResolved->ClearRenderTargetView(math::Color(0, 0, 0, 0));

            _denoiseFD.camera = _currentCamera;
            _denoiseFD.jitter = _taa.GetJitterOffset(bbvp.width, bbvp.height);

			if (r_ssrDenoise._val.b)
			{
				// NRD-denoised path: pack diffuse + specular SSR signals + their hit distances,
				// run NRD's RELAX_DIFFUSE_SPECULAR, then composite the resolved signal additively.
				g_pEnv->_denoiserProvider->BuildFrameData(_denoiseFD, _ssrDiffuseTexture, _ssrDiffuseHitInfo, _ssrTexture, _ssrHitInfo, _gbuffer.GetNormal(), _gbuffer.GetSpecular(), _gbuffer.GetVelocity());
				g_pEnv->_denoiserProvider->FilterFrame(_denoiseFD, _ssrResolved);

				_ssrResolved->CopyTo(_ssrHistory);

				// NRD overwrites our per-frame constant buffer state; re-upload it.
				auto sunLight = _currentScene->GetSunLight();

				SetupPerFrameBuffer(
					_currentCamera->GetViewMatrix(),
					_currentCamera->GetProjectionMatrix(),
					_currentCamera->GetViewMatrixPrev(),
					_currentCamera->GetProjectionMatrixPrev(),
					r_shadowCascades._val.i32,
					sunLight ? sunLight->GetEntity()->GetComponent<Transform>()->GetForward() : math::Vector3::Forward,
					_currentCamera->GetViewport(),
					6,
					sunLight ? sunLight->GetLightMultiplier() : 1.0f
				);

				guiRenderer->StartFrame();
				g_pEnv->_graphicsDevice->SetViewport(*bbvp.Get11());
				g_pEnv->_graphicsDevice->SetRenderTarget(_beautyRT);
				GFX_PERF_BEGIN(0xFFFFFFFF, L"SSR Blit Resolve");
				g_pEnv->_graphicsDevice->SetBlendState(BlendState::Additive);
				guiRenderer->FullScreenTexturedQuad(_ssrResolved, _ssrResolve.get());
				g_pEnv->_graphicsDevice->SetBlendState(BlendState::Opaque);
			}
			else
			{
				auto sunLight = _currentScene->GetSunLight();

				SetupPerFrameBuffer(
					_currentCamera->GetViewMatrix(),
					_currentCamera->GetProjectionMatrix(),
					_currentCamera->GetViewMatrixPrev(),
					_currentCamera->GetProjectionMatrixPrev(),
					r_shadowCascades._val.i32,
					sunLight ? sunLight->GetEntity()->GetComponent<Transform>()->GetForward() : math::Vector3::Forward,
					_currentCamera->GetViewport(),
					6,
					sunLight ? sunLight->GetLightMultiplier() : 1.0f
				);

				// NRD-bypass path: composite the raw SSR diffuse + specular textures directly
				// onto beauty. Use this to verify whether artifacts originate from the SSR shader
				// or from NRD's denoising. If artifacts disappear here, the shader output is OK
				// and the problem lives in NRD's preprocess / matrices / hit-distance interpretation.
				//
				// Specular reflection (_ssrTexture) is the shiny / mirror channel that produces
				// the visible reflections on wet surfaces; without it, r_ssrDenoise=0 looked like
				// "reflections vanished entirely" and made the diagnostic useless. Composite both
				// diffuse and specular so the toggle isolates NRD vs the raw shader honestly.
				guiRenderer->StartFrame();
				g_pEnv->_graphicsDevice->SetViewport(*bbvp.Get11());
				g_pEnv->_graphicsDevice->SetRenderTarget(_beautyRT);
				GFX_PERF_BEGIN(0xFFFFFFFF, L"SSR Blit Resolve (no denoise)");
				g_pEnv->_graphicsDevice->SetBlendState(BlendState::Additive);
				guiRenderer->FullScreenTexturedQuad(_ssrDiffuseTexture, _ssrResolve.get());
				guiRenderer->FullScreenTexturedQuad(_ssrTexture, _ssrResolve.get());
				g_pEnv->_graphicsDevice->SetBlendState(BlendState::Opaque);
			}

			//guiRenderer->FullScreenTexturedQuad(_ssrResolved, _ssrResolve.get());
			//_ssrResolved->CopyTo(_beautyRT);
			GFX_PERF_END();

			//_ssrResolved->BlendTo_Additive(_compositionRT);
			
			//_ssrTexture->BlendTo_Additive(_compositionRT);

			//_compositionRT->CopyTo(_gbuffer.GetDiffuse());

			/*_ssrTexture->BlendTo_NonPremultiplied(_compositionRT);
			_ssrTexture->BlendTo_NonPremultiplied(_gbuffer.GetDiffuse());*/



			guiRenderer->EndFrame();
		}

		GFX_PERF_END();
	}

	void SceneRenderer::RenderVolumetricClouds()
	{
		if (!r_cloudEnable._val.b || !_currentCamera || !_cloudsBuffer || !_volumetricClouds || !_cloudConstantBuffer || !_cloudShapeNoise || !_cloudDetailNoise || !_fogBuffer)
			return;

		PROFILE();

		const auto& bbvp = _currentCamera->GetViewport();
		if (bbvp.width <= 1.0f || bbvp.height <= 1.0f)
			return;

		CloudConstants constants = {};
		if (!BuildCloudConstants(_currentCamera, constants))
			return;

		_cloudConstantBuffer->Write(&constants, sizeof(constants));
		g_pEnv->_graphicsDevice->SetConstantBufferPS(4, _cloudConstantBuffer);

		_cloudsBuffer->ClearRenderTargetView(math::Color(0, 0, 0, 0));
		g_pEnv->_graphicsDevice->SetRenderTarget(_cloudsBuffer);

		D3D11_VIEWPORT halfVp = {};
		halfVp.TopLeftX = 0.0f;
		halfVp.TopLeftY = 0.0f;
		halfVp.Width = std::max(1.0f, bbvp.width * 0.5f);
		halfVp.Height = std::max(1.0f, bbvp.height * 0.5f);
		halfVp.MinDepth = 0.0f;
		halfVp.MaxDepth = 1.0f;
		g_pEnv->_graphicsDevice->SetViewport(halfVp);

		if (auto guiRenderer = g_pEnv->GetUIManager().GetRenderer(); guiRenderer != nullptr)
		{
			guiRenderer->StartFrame();
			_gbuffer.BindAsShaderResource();
			g_pEnv->_graphicsDevice->SetTexture2D(_beautyRT);
			g_pEnv->_graphicsDevice->SetTexture2D(_blueNoise.get());
			g_pEnv->_graphicsDevice->SetTexture3D(_cloudShapeNoise);
			g_pEnv->_graphicsDevice->SetTexture3D(_cloudDetailNoise);
			guiRenderer->FullScreenTexturedQuad(nullptr, _volumetricClouds.get());
			guiRenderer->EndFrame();

			_fogBuffer->ClearRenderTargetView(math::Color(0, 0, 0, 0));
			g_pEnv->_graphicsDevice->SetRenderTarget(_fogBuffer);
			g_pEnv->_graphicsDevice->SetViewport(*bbvp.Get11());

			guiRenderer->StartFrame();
			_gbuffer.BindAsShaderResource();
			g_pEnv->_graphicsDevice->SetTexture2D(_cloudsBuffer);
			guiRenderer->FullScreenTexturedQuad(nullptr, _bilateralUpsample.get());
			guiRenderer->EndFrame();

			g_pEnv->_graphicsDevice->SetRenderTarget(_beautyRT);
			g_pEnv->_graphicsDevice->SetBlendState(BlendState::Transparency);
			g_pEnv->_graphicsDevice->SetDepthBufferState(DepthBufferState::DepthNone);

			guiRenderer->StartFrame();
			guiRenderer->FullScreenTexturedQuad(_fogBuffer, _fullScreenQuadShader.get());
			guiRenderer->EndFrame();
		}

		g_pEnv->_graphicsDevice->SetBlendState(BlendState::Opaque);
		g_pEnv->_graphicsDevice->SetDepthBufferState(DepthBufferState::DepthDefault);
		g_pEnv->_graphicsDevice->SetViewport(*bbvp.Get11());
	}

	const GBuffer* SceneRenderer::GetGBuffer()
	{
		return &_gbuffer;
	}

	const Light* SceneRenderer::GetCurrentShadowCaster()
	{
		return _currentShadowCasterForComposition;
	}

	const ShadowMap* SceneRenderer::GetCurrentShadowMap()
	{
		return _currentShadowMapForComposition;
	}

	ITexture2D* SceneRenderer::GetBeautyTexture() const
	{
		return _beautyRT;
	}
}


