
#include "SceneRenderer.hpp"
#include "../HexEngine.hpp"
#include "../Entity/Component/SpotLight.hpp"
#include "../Entity/Component/PointLight.hpp"
#include "../Entity/Component/DecalComponent.hpp"
#include "../Entity/Component/ReflectionProbeComponent.hpp"
#include "../Entity/Component/InteractionComponent.hpp"
#include "../Graphics/IVertexBuffer.hpp"
#include "../Graphics/IIndexBuffer.hpp"
#include "../Graphics/ISSAOProvider.hpp"
#include "../Graphics/DiffuseGIAOProvider.hpp"
#include "../Graphics/AtmosphereLUTs.hpp"
#include "../Graphics/VolumetricScattering.hpp"
#include "../Graphics/ShadowMap.hpp"
#include "../Math/FloatMath.hpp"
// After project headers (see the dxgidebug include-order note in the D3D11
// plugin): d3d11_1 for ID3D11DeviceContext1::ClearView - the only way to
// clear a single atlas tile without wiping the cached neighbours.
#include <d3d11_1.h>
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

	// Point / spot light render caps. Sort by camera distance ascending and
	// keep the closest N. Lights beyond the cap are dropped entirely (no
	// surface lighting + no volumetric). Defaults tuned for a typical mid-range
	// GPU; bump up if you have headroom, drop down for low-end / debug.
	HVar r_maxPointLights("r_maxPointLights", "Max point lights rendered per frame (closest-N by camera distance)", 16, 1, 256);
	HVar r_maxSpotLights("r_maxSpotLights",  "Max spot lights rendered per frame (closest-N by camera distance)",  16, 1, 256);
	// Per-light volumetric scattering distance cutoff. Lights closer than this
	// run the full per-pixel ray-march loop. Further ones still shade the
	// surface but skip the volumetric loop entirely - typically the dominant
	// cost of dense scenes with many volumetric lights.
	HVar r_volumetricLightMaxDistance("r_volumetricLightMaxDistance", "Camera-to-light distance (m) beyond which volumetric scattering is skipped", 50.0f, 0.0f, 500.0f);
	// Point-light volumetric shadow bias (metres, linear-depth comparison).
	// Negative values switch the scatter shader into debug-visualisation
	// modes (-1 force dark, -2 force half, -3 raw cube ndcZ, -4 amplified,
	// -5 direction check, -6 ndcZ*1000, -7 face-index, -8 ndcZ histogram,
	// -9 low-value clamp test) and -10 additionally enables a CPU readback
	// in VolumetricScattering that logs per-face min/max/avg of both the
	// cube array and its source RTs. An HVar so debug modes are switchable
	// from the console without a rebuild.
	HVar r_pointShadowBias("r_pointShadowBias", "Point volumetric shadow bias (m); negative = debug modes (-1..-10)", 0.1f, -10.0f, 1.0f);
	// Screen-space emissive -> froxel injection. Surfaces with emissive
	// materials (neon signs, lit windows, screens) inject glow into the fog
	// volume around them. Strength 0 disables the gbuffer sampling entirely.
	// Range is the world-space falloff radius of the glow around the surface.
	HVar r_volumetricEmissive("r_volumetricEmissive", "Emissive surface glow strength in volumetric fog (0 = off)", 1.0f, 0.0f, 50.0f);
	HVar r_volumetricEmissiveRange("r_volumetricEmissiveRange", "World-space falloff radius (m) of emissive glow in fog", 6.0f, 0.1f, 100.0f);
	// Ambient inscatter of the froxel fog medium. Dense weather fog (storm,
	// blizzard, sandstorm) reads as a coloured soup because ambient skylight
	// multiple-scatters within it - without this term the froxel fog only
	// darkens (extinction) and brightens where direct light shafts hit,
	// leaving shadowed fog unnaturally black. Scales the scene ambient
	// colour (weather presets author this per-condition: grey for storms,
	// orange for sandstorm) by the medium's density per metre.
	HVar r_volumetricAmbient("r_volumetricAmbient", "Ambient inscatter strength of froxel fog (x scene ambient colour)", 1.6f, 0.0f, 10.0f);
	HVar r_drawNavMesh("r_drawNavMesh", "Render the navmesh debug overlay", false, false, true);
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
	// NOTE: r_gamma is vestigial - it was uploaded to the per-frame cbuffer every frame and
	// read by no shader (the SDR tonemap hardcodes 1/2.2). Kept declared so scenes that
	// serialised it still load; its cbuffer slot now carries r_pbrEnergyFix.
	HVar r_gamma("r_gamma", "Unused. Gamma is fixed at 2.2 in the tonemap shaders", 2.2f, 0.1f, 5.0f);

	// The reference glTF diffuse term is albedo/PI, but the divide is commented out in
	// PBRutils.shader, leaving diffuse response ~3.14x hot relative to the specular lobe on
	// every lit surface in the engine. Correcting it is a large, deliberate change in
	// appearance: everything gets substantially darker and existing light intensities and
	// exposure need rebalancing to match. Default OFF so nothing changes until that
	// rebalance happens; turn on to see (and then retune toward) correct energy.
	HVar r_pbrEnergyFix("r_pbrEnergyFix", "Apply the physically-correct diffuse 1/PI term (needs light/exposure rebalance)", false, false, true);
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

	// TAA neighbourhood variance-clip width, in standard deviations. 0.5 is what the shader
	// shipped with; the textbook value is ~1.25 but that ghosts noticeably here.
	HVar r_taaVarianceGamma("r_taaVarianceGamma", "TAA variance clip width in sigma (lower = tighter, less ghosting, less detail)", 0.5f, 0.1f, 3.0f);

	// Environment fallback for specular rays that find no screen-space hit and no voxel
	// coverage. Those rays used to return black, which is why a wet road reflected the sky
	// as black rather than a sky gradient. 0 restores that behaviour.
	// DISABLED BY DEFAULT (was 1.0). Every screen-space test for "this ray reached the sky" is
	// unsound indoors: the opaque gbuffer behind transparent glass holds the skysphere, so a wall
	// pixel's horizontal reflected ray crosses a window in screen space, "sees sky", and gets
	// handed horizon radiance - painting the horizon straight through the wall. Marching further,
	// distance-gating, direction-sampling the LUT instead of the beauty buffer, and gating on
	// "the march ended on a sky pixel" were all tried; each moved the artifact rather than
	// removing it, because they all rest on the same false premise. Needs real occlusion data
	// (reflection probes, or a cone trace with coverage that actually reaches the far wall) - not
	// another screen-space heuristic. Left in place, off, for that work.
	HVar r_ssrSkyFallbackStrength("r_ssrSkyFallbackStrength", "Strength of the sky-LUT fallback for SSR rays that hit nothing", 0.0f, 0.0f, 4.0f);

	// Screen-space sky hits: a reflection ray that passes over sky (e.g. a glossy floor
	// reflecting sky through a window) should return that sky. An earlier version of this
	// returned sky the moment a screen sample landed on it, which streaked badly because a
	// floor ray sees the window in screen space long before it reaches it in 3D. Requiring a
	// minimum travelled distance - and letting real geometry hits still win - fixes that.
	// Raise if streaks reappear; 0 disables sky hits.
	// DISABLED BY DEFAULT (was 2.0) - see r_ssrSkyFallbackStrength above for why the whole
	// screen-space sky-detection approach is unsound. 0 disables the sky-pixel branch entirely,
	// which is main's behaviour.
	HVar r_ssrSkyHitMinDistance("r_ssrSkyHitMinDistance", "Min world distance before an SSR ray may accept a sky pixel as a hit (0 = disable)", 0.0f, 0.0f, 64.0f);
	// Forward transparency environment reflection. Defaults ON: glass that finds
	// no screen-space hit used to reflect literal black, which is why window panes
	// read as dark holes. Unlike the sky terms for OPAQUE surfaces, this is not an
	// occlusion hazard - a window reflecting the sky is what a window does.
	HVar r_glassEnvReflection("r_glassEnvReflection", "Environment reflection strength for transparent surfaces when SSR misses", 1.0f, 0.0f, 4.0f);

	// Diagnostic for "why is my glossy floor not reflecting the sky". Paints SSR sky hits
	// MAGENTA where accepted and GREEN where a sky pixel was seen but rejected by
	// r_ssrSkyHitMinDistance. No colour at all means the sky test never matched, which points
	// at the gbuffer behind the window carrying real geometry depth rather than the sky's
	// frustum-far marker.
	// MUST be an int HVar, not a bool: a bool clamps every value to 0/1, so modes 2 and 3
	// silently became mode 1 and their branches never ran.
	//   1 = sky markers   : magenta accepted / green distance-rejected / blue alpha-marker-only
	//   2 = path classify : red depth-hit / yellow last-in-screen fallback / cyan miss
	//   3 = cbuffer test  : flood the specular output unconditionally
	HVar r_ssrDebugSkyHits("r_ssrDebugSkyHits", "Debug SSR: 1=sky markers, 2=path classify, 3=cbuffer flood test", (int32_t)0, (int32_t)0, (int32_t)3);

	// IBL: prefiltered sky environment atlas (see RenderSkyEnvMap /
	// EnvMapCommon.shader). Inert until the deferred IBL resolve consumes it,
	// so defaulting on only costs the small prefilter draw.
	HVar r_iblSkyEnv("r_iblSkyEnv", "Generate the prefiltered sky environment atlas each frame", true, false, true);
	// 1 = sky atlas, 2 = active probe atlas, 3 = the active probe's RAW capture
	// face 0 (before prefiltering - answers "is the rig even framing the room?").
	// Int, not bool: a bool HVar clamps every value to 0/1, so modes 2 and 3
	// would silently become 1.
	// 1 = sky atlas, 2 = active probe atlas, 3..8 = the active probe's RAW capture
	// faces 0..5 (+X,-X,+Y,-Y,+Z,-Z) before prefiltering. Face 2 (+Y, straight up)
	// is the useful one for "is the rig inside the building?" - a ceiling and a sky
	// are impossible to confuse, whereas a wall and the ground both read as flat.
	HVar r_iblSkyEnvDebug("r_iblSkyEnvDebug", "Env atlas overlay: 1=sky, 2=probe atlas, 3..8=probe raw face 0..5", (int32_t)0, (int32_t)0, (int32_t)8);

	// Atlas geometry. Must match ENVMAP_FACE_SIZE / ENVMAP_ROUGHNESS_ROWS in
	// EnvMapCommon.shader - the shader derives everything from uv, so the only
	// contract is the overall texture aspect (width : height = 1 : rows).
	static constexpr int32_t kIblEnvMapFaceSize = 128;
	static constexpr int32_t kIblEnvMapRows = 5;
	// L2 spherical harmonics: 9 coefficients, one per row of a 1x9 texture.
	// Must match ENVMAP_SH_COEFFS in EnvMapCommon.shader.
	static constexpr int32_t kIblEnvShCoeffs = 9;
	// DFG table resolution. 128x128 is the usual choice: the function is smooth
	// in both axes, so more resolution buys nothing measurable.
	static constexpr int32_t kDfgLutSize = 128;

	// P1-B. Real DFG lookup in place of EnvBRDFApprox's analytic fit, plus
	// multi-scatter energy compensation. Single-scatter GGX drops the light that
	// would have bounced a second time off the microfacets, so rough metals come
	// out too dark; the compensation adds that energy back.
	HVar r_iblDfgLut("r_iblDfgLut", "Use the precomputed DFG LUT instead of the analytic EnvBRDF fit", true, false, true);
	HVar r_iblMultiScatter("r_iblMultiScatter", "Multi-scatter energy compensation for environment specular", true, false, true);

	// Sky image-based lighting. The engine had no IBL at all - ambient was a flat
	// diffuse-only constant - so no surface had any environment response. Specular defaults
	// ON because that is the term that restores missing reflections; diffuse defaults OFF
	// because the sky term is unoccluded, so indoors it floods a room. Reflection probes
	// (P1-D) add the occlusion/locality this lacks.
	// DEFAULT 0 until reflection probes provide occlusion. This term is unoccluded sky:
	// with it on, an interior wall's reflection vector sweeps through zero elevation as
	// the view position moves, which paints the sky's horizon line straight across the
	// wall, and upward-facing interior floors take a blue sky tint under a solid roof.
	// That was shipped at 1.0 (unverified) and is exactly the "horizon through walls" +
	// "weird blue reflections inside" report - it survived r_ssr 0 because it never was
	// SSR. Enable per-scene outdoors, or wait for probes to gate it indoors.
	// NOW DEFAULTS ON. This was disabled when it sampled the RAW sky-view LUT,
	// whose sharp horizon line got painted across interior walls. It now samples
	// the prefiltered atlas (verified: soft, roughness-correct, no horizon band),
	// and probes replace it entirely inside their volumes. Leaving it off meant an
	// outward-facing window pane whose SSR ray left the screen reflected literal
	// black - the "black window panes" report.
	HVar r_iblSkySpecular("r_iblSkySpecular", "Sky specular IBL strength (split-sum environment specular)", 1.0f, 0.0f, 4.0f);
	HVar r_iblSkyDiffuse("r_iblSkyDiffuse", "Sky diffuse IBL strength (unoccluded - floods interiors, prefer probes)", 0.0f, 0.0f, 4.0f);
	// Probes are captured radiance with real occlusion baked in, so unlike the sky
	// terms above they are safe to default ON: a scene with no probes is unaffected
	// (g_probeCenter.w stays 0), and a scene with probes gets correct local
	// reflections inside each probe's box.
	HVar r_iblProbeStrength("r_iblProbeStrength", "Reflection probe IBL strength (box-projected local environment)", 1.0f, 0.0f, 4.0f);
	// Probe DIFFUSE, separate from probe specular and defaulting to 0 for the same
	// reason r_iblSkyDiffuse does. The diffuse lookup is the atlas's roughest row -
	// a near-uniform average of the captured room - so applying it as an irradiance
	// term floods the whole interior with flat colour instead of lighting it. That
	// is what "probe on" looked like at first: the room washed uniform cream, all
	// contrast gone. Specular is the term that actually restores reflections.
	// A real fix needs a cosine-convolved irradiance probe (P1-C's SH), not a GGX
	// roughness-1 row standing in for one.
	// NOW DEFAULTS ON. Probe diffuse used the atlas's roughest row - a flat room
	// average that washed interiors out - so it had to be off. It now uses the
	// probe's own SH irradiance, integrated from what the probe actually sees, so
	// it carries its own occlusion and is the correct indoor diffuse term.
	HVar r_iblProbeDiffuse("r_iblProbeDiffuse", "Reflection probe diffuse strength (per-probe SH irradiance)", 1.0f, 0.0f, 4.0f);

	// Compose environment specular against SSR in the resolve, instead of adding it
	// in the deferred pass and letting the SSR blit stack on top.
	//
	// Stacking was never a composition: RenderLights runs first and adds the
	// environment term, then RenderSSR blends its reflection additively onto
	// beauty. Where a ray hit, the pixel got both and was double-bright. Where it
	// missed it got environment but no screen data - and the specular miss path
	// returned the voxel-GI cone trace, which is black whenever GI is off, so a
	// floor ray aimed at a window pane (transparent glass never writes the opaque
	// gbuffer, so the ray finds nothing) left the pane black in the reflection.
	//
	// With this on, the deferred pass drops the specular term entirely and the
	// resolve writes lerp(environment, screen, confidence). Off restores the old
	// stacked behaviour byte for byte, which is what makes the A/B measurable.
	HVar r_iblComposeSSR("r_iblComposeSSR", "Compose environment specular with SSR in the resolve instead of stacking the two", true, false, true);

	// Energy split at the SSR composite.
	//
	// The reflection is light the surface sends to the eye INSTEAD of the light
	// it already emitted, not on top of it. The resolve blended additively onto a
	// beauty buffer holding the full diffuse, so the result was
	// diffuse + F*reflection where it should be (1-F)*diffuse + F*reflection -
	// nothing anywhere scaled the base down. Measured on a wet floor: +19% near
	// the camera rising to +30% toward the horizon, tending to 2x at grazing,
	// because the error tracks Fresnel and so peaks exactly where the reflection
	// is most visible.
	//
	// This is a deliberate global change to how every reflective surface reads;
	// the cvar is here so it can be A/B'd against the old look.
	HVar r_ssrEnergyConserve("r_ssrEnergyConserve", "Take the reflected fraction off the base layer at the SSR composite (energy conservation)", true, false, true);

	// How much diffuse albedo a surface loses when fully wet.
	//
	// The rain response perturbed normals and dropped roughness but never
	// touched albedo, so a wet surface kept its full dry diffuse AND gained a
	// mirror on top - which reads as a washed-out, too-bright wet floor. Real
	// wet surfaces darken substantially: the water film lets light refract in
	// and get trapped by total internal reflection instead of scattering back
	// out. 0.35 (albedo x 0.65 at full wetness) is a conventional approximation;
	// 0 restores the previous behaviour.
	//
	// Note the separate auto-puddle system already had its own darkening
	// (r_autoPuddlesDarken); this covers the per-material rain path, which did
	// not.
	HVar r_wetnessDarkening("r_wetnessDarkening", "Fraction of albedo removed at full rain wetness (0 = none)", 0.35f, 0.0f, 1.0f);

	// Longest single SSR march step. This is the reach control:
	//
	//     reach ~= 28 * (0.3 + (maxStep - 0.3) / 3)   world units
	//
	// At the old hardcoded 3.0 that is ~34 units, against water.shader's ~96.
	// The Whereabouts hall is far bigger than 34 units across, so rays aimed
	// high on the far wall ran out partway and returned whatever they were
	// crossing - the lower window row reflected, the upper row did not, and the
	// run-out point banded visibly across the floor. 10.0 puts the reach at ~99,
	// matching the value water has always used successfully.
	//
	// Raise for larger spaces, lower to buy back performance; the cost is linear
	// in neither steps nor length but in how far each step can skip past thin
	// geometry, which the binary refinement then has to recover.
	HVar r_ssrMaxStepLength("r_ssrMaxStepLength", "Longest single SSR march step in world units (controls how far reflections reach)", 10.0f, 0.5f, 32.0f);

	// Self-clearing: writes the beauty buffer as SSR sees it, which is BEFORE
	// transparent geometry is drawn. A reflection that is dark because the ray
	// hit something dark and one that is dark because the thing it hit had not
	// been drawn yet look identical in the final frame and completely different
	// here.
	// Self-clearing: writes the sky env atlas, the active probe's atlas, and the
	// probe's raw face 0 straight to PNG via SaveToFile. Unlike the on-screen
	// overlay (r_iblSkyEnvDebug) these files bypass the tonemapper, so texel
	// values compare in LINEAR units - which is the only way to answer "is the
	// probe dimmer than the sky because of a units bug or because the room is
	// genuinely dimmer".
	HVar r_iblDumpAtlases("r_iblDumpAtlases", "Dump sky/probe env atlases to ibl_*.png in linear units", false, false, true);

	HVar r_ssrDumpBeauty("r_ssrDumpBeauty", "Dump the beauty buffer as the SSR pass sees it to ssr_beauty.png", false, false, true);

	// The water.shader last-in-screen fallback, on the specular path.
	//
	// It is worth being explicit about what this is: when the march gives up, it
	// returns the beauty at wherever the ray HAPPENED TO BE, which is not the
	// mirror image position. It makes a dark reflection bright, but the bright
	// thing it draws is in the wrong PLACE. Water gets away with it because a
	// large flat water plane mostly reflects distant sky, where a positional
	// error is invisible; an interior floor reflecting nearby windows shows it
	// immediately as reflections that do not line up with what they reflect.
	//
	// Exposed so the trade can be measured rather than argued about.
	// Which ray marcher SSR uses.
	//
	//   0 - legacy: steps in WORLD space and projects each step to screen. The
	//       screen footprint of a step is then unpredictable - near the camera
	//       one step spans many pixels and hits get skipped, far away many steps
	//       land in the same pixel and do nothing. The acceptance thickness has
	//       to absorb that error, which couples two knobs that should be
	//       independent and is why tuning either one trades one artifact for
	//       another.
	//   1 - screen-space DDA (McGuire & Mara 2014): project the ray's endpoints
	//       once, walk the 2D line in pixel steps, interpolate 1/w linearly so
	//       depth stays perspective-correct. Every pixel on the ray is visited
	//       exactly once, so thickness only has to model real geometric
	//       thickness rather than marching error.
	HVar r_ssrMarchMode("r_ssrMarchMode", "SSR ray march: 0 = legacy world-space stepping, 1 = screen-space DDA", (int32_t)1, (int32_t)0, (int32_t)1);

	// Rotate the SSR cone sample per frame WHEN THE DENOISER IS ON. The
	// frame-stable seed converged NRD to the per-pixel noise instead of the lobe:
	// reflections looked clean right after camera movement (history rejected ->
	// wide spatial filter) and degraded to static speckle over the next seconds
	// (history accumulated -> spatial support narrowed -> output = the one
	// unchanging sample). A temporal accumulator can only integrate a signal
	// that varies. Off, plus r_ssrDenoise off, restores the fully frame-stable
	// diagnostic behaviour.
	// Phase 2: clustered light culling. Off by default until the deferred pass
	// consumes the lists; with it on, the cull runs and the heatmap
	// (r_clusterDebug) verifies the binning against the world.
	HVar r_clusterLights("r_clusterLights", "Build clustered light lists (Phase 2)", true, false, true);
	HVar r_clusterDebug("r_clusterDebug", "Overlay the cluster occupancy heatmap (needs r_clusterLights)", false, false, true);
	// Slice 2: the fullscreen apply. Unshadowed point/spot lights shade from
	// the cluster lists in one draw; shadowed lights keep the per-light path.
	// Needs r_clusterLights for the lists to exist.
	HVar r_clusterApply("r_clusterApply", "Shade unshadowed local lights from the cluster lists in one fullscreen pass", true, false, true);
	// INT, not bool - bool HVars clamp to 0/1 and the staged modes vanish.
	// 1 = magenta flood (draw path), 2 = per-pixel cluster light count,
	// 3 = shaded result of the first light in the list, skips ignored.
	// Slice 3: the froxel volumetric CS reads the cluster lists so fog glow
	// stops being capped at the closest-16 while surface lighting is not.
	// Slice 4: glass / alpha-blend surfaces read uncapped local lights from
	// the cluster lists instead of the closest-16 forward arrays.
	HVar r_clusterForward("r_clusterForward", "Forward-lit surfaces consume the cluster lists (uncapped local lights)", true, false, true);
	// Slice 5: the transparency phase samples the sun cascades instead of the
	// hardcoded depthValue=1 - glass in a shadowed interior stops sun-lighting
	// as if it stood outdoors.
	HVar r_transparentShadows("r_transparentShadows", "Transparent surfaces receive sun cascade shadows", true, false, true);
	// Slice 7: local-light shadow faces render into one shared atlas with
	// LRU-cached tiles instead of per-light dedicated maps. A face whose
	// content hash is unchanged keeps last frame's depth for free, so the
	// budget bounds RE-RENDERS, not shadowed-light count.
	HVar r_shadowAtlas("r_shadowAtlas", "Local-light shadows via the shared LRU atlas (sun keeps its cascades)", true, false, true);
	// Units slice part 3: when on, authored light strengths are LUMENS
	// (spot cd = lm/(2pi(1-cos outerHalf)), point cd = lm/4pi), the sun is
	// LUX, emissive is NITS. Off = legacy arbitrary units. DEFAULT ON since
	// part 4 landed (calibration + EV comp curve, user-verified day+night
	// 2026-07-31) together with r_exposureMode.
	HVar r_physicalLightUnits("r_physicalLightUnits", "Interpret light strengths as physical units (lumens/lux/nits)", true, false, true);
	// Units slice part 4: the calibration bridge between legacy content and
	// physical units, measured live 2026-07-31 (day street meanLuma 0.298 ->
	// 4500 cd/m^2 sunny-street => factor ~15000). It declares BOTH:
	//   - legacy _strength * scale = the value's meaning in physical units
	//     (a lamp authored at 3 is ~45k lm - plausible street sodium), and
	//   - preExposure = 1/scale: rendered units = physical cd/m^2 * preExposure,
	//     which keeps FP16 buffers at today's proven magnitudes.
	// Because both factors ride every light, they cancel in the packed
	// strengths - so the conversion sites keep their part-3 form and the scale
	// only surfaces where units must be ABSOLUTE: the EV100 meter (AutoExposure
	// multiplies metered luma by scale) and g_exposureParams. Pre-exposure
	// stays STATIC (not previous-frame exposure a la Frostbite) until sky/GI/
	// probes are physical too - scaling only the analytic lights by a moving
	// exposure would make sky brightness swim against surfaces during
	// adaptation. End state: a one-time content migration bakes the factor
	// into saved _strength values, this returns to 1, and inspector lumens
	// become honest.
	HVar r_legacyLightScale("r_legacyLightScale", "Calibration factor: legacy light strength -> physical units (and 1/x = pre-exposure)", 15000.0f, 1.0f, 200000.0f);
	HVar r_shadowAtlasBudget("r_shadowAtlasBudget", "Max atlas shadow faces re-rendered per frame", (int32_t)4, (int32_t)0, (int32_t)16);
	HVar r_shadowAtlasDebug("r_shadowAtlasDebug", "Draw the shadow atlas as an overlay", false, false, true);
	// Phase 3 slice 2: shelter/rain occlusion. A top-down ortho depth map
	// (static geometry only) around the camera; wet response, snow and
	// puddles are masked where cover sits above a surface. Cached: only
	// re-rendered on recentre (camera moved a quarter extent) or the
	// refresh timer, and skipped entirely in dry weather.
	HVar r_rainOcclusion("r_rainOcclusion", "Shelter occlusion: surfaces under static cover stay dry in rain/snow", true, false, true);
	HVar r_rainOcclusionExtent("r_rainOcclusionExtent", "Half-extent in metres of the rain occlusion map around the camera", 96.0f, 16.0f, 512.0f);
	HVar r_rainOcclusionRefresh("r_rainOcclusionRefresh", "Seconds between rain occlusion map refreshes (recentre also refreshes)", 2.0f, 0.1f, 30.0f);
	HVar r_weatherSurfaceDebug("r_weatherSurfaceDebug", "Log the uploaded weather surface params once a second", false, false, true);

	// Slice 5: the sun whose cascades RenderTransparent binds at t15..t20. One
	// function shared by the g_taaParams.z packing and the bind block so the
	// flag and the binds can never disagree - a set flag with nothing bound
	// reads all-zero cascade maps as "fully shadowed" and would black out the
	// sun on every transparent surface. Caller enforces the main-camera check
	// (the cascades are fit to the main view, same reasoning as
	// clusterForwardActive).
	static DirectionalLight* FindTransparentShadowSun(Scene* scene)
	{
		if (!r_transparentShadows._val.b || scene == nullptr)
			return nullptr;

		std::vector<DirectionalLight*> suns;
		if (scene->GetComponents<DirectionalLight>(suns) == false)
			return nullptr;

		for (auto* sun : suns)
		{
			if (sun->GetShadowMap(0) != nullptr)
				return sun;
		}

		return nullptr;
	}

		HVar r_clusterFog("r_clusterFog", "Froxel volumetrics consume the cluster lists for unshadowed lights", true, false, true);
		HVar r_clusterApplyDebug("r_clusterApplyDebug", "Clustered apply debug: 1=flood 2=count 3=first light", (int32_t)0, (int32_t)0, (int32_t)3);

	HVar r_ssrTemporalJitter("r_ssrTemporalJitter", "Rotate SSR cone samples per frame so NRD's temporal accumulation integrates the lobe", true, false, true);

	HVar r_ssrInScreenFallback("r_ssrInScreenFallback", "Use water's last-in-screen sample when the specular march gives up (bright but positionally wrong)", true, false, true);

	// Declared in ReflectionProbeComponent.cpp - the probe dumps the rig camera's
	// render target and each downsampled face; this file dumps the beauty/gbuffer
	// they came from, so one run covers the whole chain.
	extern HVar r_iblProbeDumpCapture;

	// Re-bake every probe in the scene. A probe only requests a capture on
	// deserialize, so without this the sole way to retry one is to reload the
	// project - which is a minute per iteration when you're bisecting a capture
	// bug. Self-clearing: set it, the next frame requests the captures.
	HVar r_iblProbeRecapture("r_iblProbeRecapture", "Request a fresh 6-face capture for every reflection probe in the scene", false, false, true);

	// Sign applied to the velocity buffer's Y when TAA reprojects history. -1 is
	// mathematically correct (CalcVelocity emits a clip-space +y-up delta, texcoords are
	// y-down) and matches what Streamline and NRD do with the same buffer, but it ghosts
	// vertically in practice unless r_taaVarianceGamma is retuned with it. Defaults to the
	// engine's long-standing +1.
	HVar r_taaVelocityYSign("r_taaVelocityYSign", "Y sign for TAA history reprojection: 1 = legacy, -1 = clip-space-correct", 1.0f, -1.0f, 1.0f);
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
	// Default ON since Phase 3 slice 4: the puddle mask now composes with
	// universal wetness, shelter occlusion, and rain ripples - the full
	// wet-street stack - and costs nothing in dry weather (the shader
	// early-outs on puddleAmount 0).
	HVar r_autoPuddles("r_autoPuddles", "Enable procedural puddles driven by weather + surface normal + noise", true, false, true);
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
	// Diagnostic: when ON, the auto-puddle shader bypasses ALL checks and outputs
	// solid red on diff + bright-blue on mat at full alpha. If the screen turns
	// red with r_autoPuddles 1 + this, the pass + RT binding are correct and the
	// problem is in the puddle decision logic. If the screen does NOT turn red,
	// the pass isn't drawing - either RenderDecals is early-outing, the shader
	// failed to load, or the RT binding got clobbered.
	HVar r_autoPuddlesDebugSolid("r_autoPuddlesDebugSolid", "Diagnostic: paint solid red regardless of all checks", false, false, true);
	// Rain-drip cell-grid visualizer. Paints cellFrac.x in R, cellFrac.y in G,
	// and a thin grid line in B straight into the surface colour - lets us see
	// the rain-drip basis (square cells = good 2D basis; horizontal stripes =
	// degenerate basis) and the streak direction (cells should slide DOWN over
	// time on vertical surfaces). Bypasses every other rain-drip check so it
	// works on dry materials too.
	HVar r_rainDripDebug("r_rainDripDebug", "Diagnostic: paint rain-drip cell grid on every material", false, false, true);

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

		// Builds a cloud noise volume. Plain fractal simplex (the previous approach)
		// only yields smooth fog blobs; real cumulus structure comes from
		// Perlin-Worley - low-frequency Perlin FBM remapped by inverted-Worley
		// (cellular) FBM, which carves the billowy cauliflower lumps (Guerrilla
		// "Nubis" / Schneider). `billowShape` picks the base-shape build; otherwise
		// an inverted-Worley FBM is emitted for high-frequency edge erosion. Single
		// R32 channel - the cloud shader samples .r as before.
		static ITexture3D* CreateCloudNoiseVolume(int32_t resolution, int32_t seed, float frequency, int32_t octaves, float gain, float lacunarity, bool billowShape)
		{
			// Low-frequency Perlin/Simplex FBM base (large-scale coverage).
			FastNoiseLite perlin;
			perlin.SetSeed(seed);
			perlin.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
			perlin.SetFrequency(frequency);
			perlin.SetFractalType(FastNoiseLite::FractalType_FBm);
			perlin.SetFractalOctaves(octaves);
			perlin.SetFractalGain(gain);
			perlin.SetFractalLacunarity(lacunarity);

			// Worley/cellular FBM. Inverting the F1 distance makes cell centres
			// bright, which is what forms billows in the shape and wisps in detail.
			FastNoiseLite worley;
			worley.SetSeed(seed + 501);
			worley.SetNoiseType(FastNoiseLite::NoiseType_Cellular);
			worley.SetCellularDistanceFunction(FastNoiseLite::CellularDistanceFunction_Euclidean);
			worley.SetCellularReturnType(FastNoiseLite::CellularReturnType_Distance);
			worley.SetCellularJitter(1.0f);
			worley.SetFractalType(FastNoiseLite::FractalType_FBm);
			worley.SetFractalOctaves(std::max(2, octaves - 1));
			worley.SetFractalGain(0.5f);
			worley.SetFractalLacunarity(2.0f);
			worley.SetFrequency(frequency * (billowShape ? 1.7f : 2.3f));

			std::vector<float> noiseData(static_cast<size_t>(resolution) * static_cast<size_t>(resolution) * static_cast<size_t>(resolution), 0.0f);

			for (int32_t z = 0; z < resolution; ++z)
			{
				for (int32_t y = 0; y < resolution; ++y)
				{
					for (int32_t x = 0; x < resolution; ++x)
					{
						const float fx = static_cast<float>(x);
						const float fy = static_cast<float>(y);
						const float fz = static_cast<float>(z);

						// Inverted Worley FBM: 1 = billow core, 0 = cellular edge.
						const float worleyBillow = std::clamp(1.0f - (worley.GetNoise(fx, fy, fz) * 0.5f + 0.5f), 0.0f, 1.0f);

						float value;
						if (billowShape)
						{
							const float p = std::clamp(perlin.GetNoise(fx, fy, fz) * 0.5f + 0.5f, 0.0f, 1.0f);
							// Perlin-Worley remap (Schneider): dilate the Perlin base by
							// the inverted Worley so puffs merge in the cores and erode
							// to individual billows at the edges.
							const float low = worleyBillow - 1.0f;
							value = std::clamp((p - low) / std::max(1e-3f, 1.0f - low), 0.0f, 1.0f);
						}
						else
						{
							// Detail volume: pure inverted-Worley FBM for edge erosion.
							value = worleyBillow;
						}

						const int32_t idx = (z * resolution * resolution) + (y * resolution) + x;
						noiseData[idx] = value;
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
	// SSR + NRD were ~14 ms of a ~30 ms frame at 4K (user-measured on the
	// street view, 2026-07-30) - the single largest pass. The MARCH runs at
	// half resolution (4x fewer rays); the DENOISER stays at full resolution
	// behind a depth-aware upsample of the march outputs (SSRUpsample.shader).
	// NRD at half res made reflections swim under camera panning - four
	// fixes (deterministic guide decimation, boundary-snapped gbuffer reads,
	// resolution-scaled RELAX radii, depth-aware resolve upsample) improved
	// half-res correctness but only moving the denoiser back to full res
	// resolved it (bisect: raw half-res SSR never swam). Measured: 31 fps
	// full chain -> 51 all-half (swimming) -> 43 half-march/full-denoise,
	// motion quality indistinguishable from full res. USER-VERIFIED.
	HVar r_ssrHalfRes("r_ssrHalfRes", "March SSR at half resolution (denoise + resolve stay full res)", true, false, true);
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

			_cloudShapeNoise = CreateCloudNoiseVolume(shapeResolution, randomSeed, 0.025f, 5, 0.52f, 2.0f, /*billowShape*/ true);
			_cloudDetailNoise = CreateCloudNoiseVolume(detailResolution, randomSeed + 137, 0.09f, 4, 0.45f, 2.1f, /*billowShape*/ false);
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

		// The history texture was just destroyed and recreated at the new size, so whatever
		// it now contains is uninitialised. Blend from the current frame for one frame.
		_taa.ResetHistory();
	}

	void SceneRenderer::Destroy()
	{
		std::unique_lock lock(_lock);

		_diffuseGi.Destroy();
		_clusteredLights.Destroy();
		_shadowAtlas.Destroy();
		SAFE_DELETE(_shadowAtlasDebugTex);
		if (_rainOcclusionMap != nullptr)
		{
			_rainOcclusionMap->Destroy();
			delete _rainOcclusionMap;
			_rainOcclusionMap = nullptr;
		}
		_rainOcclusionPVS.reset();
		_rainOcclusionValid = false;
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
		SAFE_DELETE(_outlineJfaA);
		SAFE_DELETE(_outlineJfaB);
		SAFE_DELETE(_outlineGlowRT);
		SAFE_DELETE(_outlineParamsBuffer);
		SAFE_DELETE(_iblSkyEnvMap);
		SAFE_DELETE(_iblSkySH);
		SAFE_DELETE(_dfgLut);
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
		_ssrUpsampleShader			= IShader::Create("EngineData.Shaders/SSRUpsample.hcs");

		_clusteredLights.Create();
		if (!_shadowAtlas.Create())
			LOG_CRIT("ShadowAtlas::Create failed - r_shadowAtlas will render no local shadows");
		_clusterApplyShader = IShader::Create("EngineData.Shaders/ClusterLightApply.hcs");
		_iblSkyEnvShader			= IShader::Create("EngineData.Shaders/SkyEnvMap.hcs");
		_probeEnvShader				= IShader::Create("EngineData.Shaders/ProbeEnvMap.hcs");
		_envSHShader				= IShader::Create("EngineData.Shaders/EnvMapSH.hcs");
		_dfgLutShader				= IShader::Create("EngineData.Shaders/DFGLut.hcs");
		_tonemapShader				= IShader::Create("EngineData.Shaders/Tonemap.hcs");
		_hdrOutputShader			= IShader::Create("EngineData.Shaders/TonemapHDR.hcs");
		_basicDenoise				= IShader::Create("EngineData.Shaders/BasicDenoise.hcs");
		_waterBlitEffect			= IShader::Create("EngineData.Shaders/WaterBlit.hcs");
		_fullScreenQuadShader		= IShader::Create("EngineData.Shaders/FullScreenQuad.hcs");
		_subsurfaceShader			= IShader::Create("EngineData.Shaders/SubsurfaceScattering.hcs");
		_bokehDoFShader				= IShader::Create("EngineData.Shaders/BokehDoF.hcs");
		_aerialPerspectiveApplyShader = IShader::Create("EngineData.Shaders/AtmosphereAerialPerspectiveApply.hcs");
		_volumetricScatterApplyShader = IShader::Create("EngineData.Shaders/VolumetricScatterApply.hcs");
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

		// Interaction outline glow (jump-flood SDF) shaders + params cbuffer at
		// b5. Layout: float4 colour + (thickness, jumpStep, pad, pad) = 32 bytes.
		_outlineSeedShader      = IShader::Create("EngineData.Shaders/OutlineSeed.hcs");
		_outlineJfaShader       = IShader::Create("EngineData.Shaders/OutlineJFA.hcs");
		_outlineCompositeShader = IShader::Create("EngineData.Shaders/OutlineComposite.hcs");
		if (_outlineParamsBuffer == nullptr)
		{
			_outlineParamsBuffer = g_pEnv->_graphicsDevice->CreateConstantBuffer(sizeof(math::Vector4) * 2);
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

		const DXGI_FORMAT BEAUTY_FORMAT = HexEngine::detail::ShimToDxgiFormat(g_pEnv->_graphicsDevice->GetDesiredBackBufferFormat());

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

		// Interaction outline glow targets. The two JFA buffers store nearest-
		// seed pixel coordinates (RG32F, exact integer coords up to 4K); the
		// glow buffer holds the composited HDR ring. All single-sampled - the
		// outline is a screen-space post effect, not part of the MSAA scene.
		SAFE_DELETE(_outlineJfaA);
		_outlineJfaA = g_pEnv->_graphicsDevice->CreateTexture2D(
			width, height, DXGI_FORMAT_R32G32_FLOAT, 1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, 1, 0, nullptr, (D3D11_CPU_ACCESS_FLAG)0,
			D3D11_RTV_DIMENSION_TEXTURE2D, D3D11_UAV_DIMENSION_UNKNOWN, D3D11_SRV_DIMENSION_TEXTURE2D);
		_outlineJfaA->SetDebugName("_outlineJfaA");

		SAFE_DELETE(_outlineJfaB);
		_outlineJfaB = g_pEnv->_graphicsDevice->CreateTexture2D(
			width, height, DXGI_FORMAT_R32G32_FLOAT, 1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, 1, 0, nullptr, (D3D11_CPU_ACCESS_FLAG)0,
			D3D11_RTV_DIMENSION_TEXTURE2D, D3D11_UAV_DIMENSION_UNKNOWN, D3D11_SRV_DIMENSION_TEXTURE2D);
		_outlineJfaB->SetDebugName("_outlineJfaB");

		SAFE_DELETE(_outlineGlowRT);
		_outlineGlowRT = g_pEnv->_graphicsDevice->CreateTexture2D(
			width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, 1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, 1, 0, nullptr, (D3D11_CPU_ACCESS_FLAG)0,
			D3D11_RTV_DIMENSION_TEXTURE2D, D3D11_UAV_DIMENSION_UNKNOWN, D3D11_SRV_DIMENSION_TEXTURE2D);
		_outlineGlowRT->SetDebugName("_outlineGlowRT");

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

		CreateSsrTargets(width, height);

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

		// _denoiserProvider may be null if NRD's plugin self-rejected (e.g. running under a
		// non-D3D11 backend, or the plugin DLL just isn't installed). The denoise pass below
		// is also guarded; both guards together let the engine boot + render fine without
		// NRD. This call is what triggers the DLSS-render-extent rebuild of NRD's pool when
		// SceneRenderer::Resize fires from Camera::EnableDLSS.
		if (g_pEnv->_denoiserProvider != nullptr)
		{
			g_pEnv->_denoiserProvider->CreateBuffers(width, height, _ssrDiffuseTexture, _ssrDiffuseHitInfo, _ssrTexture, _ssrHitInfo, _gbuffer.GetNormal(), _gbuffer.GetSpecular(), _gbuffer.GetVelocity());
		}
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

		// Probe capture tracing: is the rig camera actually reaching the renderer?
		if (camera->IsEnvironmentCapture())
		{
			const auto& cvp = camera->GetViewport();
			// Where the rig actually IS and is LOOKING, not just that it ran. A
			// capture that renders a flat wash is either framing nothing or
			// framing it from the wrong place, and those need different fixes.
			const math::Vector3 entPos = camera->GetEntity() != nullptr
				? camera->GetEntity()->GetWorldTM().Translation() : math::Vector3::Zero;
			const math::Vector3 look = camera->GetLookDir();
			LOG_INFO("RenderScene: ENV CAPTURE camera, viewport %.0fx%.0f, rt=%p, eye=(%.2f, %.2f, %.2f), look=(%.2f, %.2f, %.2f), near=%.2f far=%.2f",
				cvp.width, cvp.height, (void*)camera->GetRenderTarget(),
				entPos.x, entPos.y, entPos.z, look.x, look.y, look.z,
				camera->GetNearZ(), camera->GetFarZ());
		}

		if (!_sphereEntity)
		{
			_sphereEntity = g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity("LightSphere");
			_sphereEntity->SetLayer(Layer::Invisible);
			_sphereEntity->SetFlag(EntityFlags::DoNotSave | EntityFlags::ExcludeFromHLOD);
			auto sphereMeshRenderer = _sphereEntity->AddComponent<StaticMeshComponent>();
			// This sphere is a transient deferred-light-volume helper: it is
			// repositioned, rescaled and re-materialed every frame (its material
			// is swapped between the point and spot passes). It must never enter
			// GI - both for correctness and because those per-frame mutations
			// would otherwise bump the GI revisions and thrash the voxel triangle
			// cache. Flag it before SetMesh/SetMaterial so neither notifies.
			sphereMeshRenderer->SetExcludeFromGI(true);
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

		// Discard TAA history across a cut. Reprojection cannot recover from a camera
		// switch or a teleport - the motion vectors describe a continuous frame-to-frame
		// delta that simply didn't happen - so the stale history smears across the new view
		// for however long the 0.9 feedback takes to wash out. TAA::ResetHistory() existed
		// but had no callers.
		{
			const math::Vector3 cameraPos = _cameraEntity->GetPosition();
			const bool cameraChanged = (_taaHistoryCamera != _currentCamera);
			const bool cameraScene = (_taaHistoryScene != _currentScene);
			// 5 m in a single frame is a teleport, not travel (300 m/s at 60fps).
			const bool cameraTeleported = !cameraChanged && !cameraScene &&
				(cameraPos - _taaHistoryCameraPos).LengthSquared() > (5.0f * 5.0f);

			if (cameraChanged || cameraScene || cameraTeleported)
				_taa.ResetHistory();

			_taaHistoryCamera = _currentCamera;
			_taaHistoryScene = _currentScene;
			_taaHistoryCameraPos = cameraPos;
		}

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

		// Slice 7 split: the sun always renders its cascade chain; local
		// lights either take the legacy dedicated-map path (r_shadowAtlas off,
		// exactly as before) or allocate tiles in the shared atlas, where a
		// face whose content hash is unchanged re-uses last frame's depth and
		// costs nothing.
		{
			std::vector<Light*> localCasters;
			for (auto& caster : _shadowCasters)
			{
				const bool isLocal = caster->CastAs<DirectionalLight>() == nullptr;
				if (isLocal && r_shadowAtlas._val.b)
					localCasters.push_back(caster);
				else
					RenderShadowMaps(caster);
			}

			if (!localCasters.empty())
			{
				const auto& assignments = _shadowAtlas.AssignTiles(
					localCasters, _currentCamera, r_shadowAtlasBudget._val.i32,
					_currentScene->GetShadowGeometryRevision());
				for (const auto& a : assignments)
				{
					if (a.needsRender)
						RenderShadowFaceToAtlas(a);
				}
			}
		}

		// Shelter/rain occlusion map (Phase 3 slice 2) - rides with the
		// shadow renders so the main-view SetupPerFrameBuffer below sees
		// this frame's validity + matrices.
		UpdateRainOcclusionMap();

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

		// Atmosphere LUTs. Phase B dispatches transmittance (on param
		// change), multi-scattering and sky-view (per-frame) compute
		// passes. Inputs are the camera's world Y altitude (lifted into
		// atmospheric coordinates inside Update()) and the sun's world-
		// space direction (pointing FROM the surface TO the sun, i.e.
		// the negation of the light's forward vector). Sits after the
		// per-frame buffer setup so any LUT pass that reads from the
		// per-frame cbuffer has the latest values.
		if (g_pEnv->_atmosphereLUTs != nullptr)
		{
			const math::Vector3 sunForward = sunLight
				? sunLight->GetEntity()->GetComponent<Transform>()->GetForward()
				: math::Vector3::Down;
			const math::Vector3 sunDir = -sunForward; // surface -> sun
			const float lightMult = sunLight ? sunLight->GetLightMultiplier() : 1.0f;

			// Solar-energy scaling. The Hillaire LUTs are generated with
			// normalised solar radiance (1.0), so their outputs need to be
			// multiplied up by an "energy" factor to match the engine's
			// HDR scale. We mirror the same lerp the old analytic sky used
			// in ComputePhysicalSunColour: 18 at horizon → 30 at zenith,
			// scaled by the sun light's multiplier. Without this the sky
			// rendered ~20x too dim once the LUT path took over.
			const float sunY = std::clamp(sunDir.y, -1.0f, 1.0f);
			const float sunEnergy = std::lerp(18.0f, 30.0f, std::clamp(sunY * 0.5f + 0.5f, 0.0f, 1.0f));
			const float sunIntensity = lightMult * sunEnergy;

			// Push the env_* atmosphere HVars into the LUT params so the
			// weather plugin (which drives env_density / env_rayleighStrength /
			// env_mieStrength to model day-night, storms, smog, etc.)
			// actually affects the LUT-driven sky. Without this the LUTs
			// ran with hardcoded Hillaire reference values regardless of
			// what the weather state asked for.
			//
			// env_density rescaling: the HVar's default and weather-plugin
			// values (~0.11-0.24) were tuned for the analytic Atmosphere
			// shader, where the base Rayleigh coefficient was already
			// engine-scaled (0.0046 etc) and env_density multiplied that
			// directly. Hillaire's reference coefficients we use here are
			// calibrated for density=1.0 (real-Earth atmosphere), so
			// multiplying directly by env_density=0.11 produces a 9x-too-
			// thin atmosphere - the sky goes dark navy / black at zenith.
			// Renormalising by the HVar default (0.11) makes env_density
			// at default reproduce Hillaire's reference clear sky, with
			// the weather plugin's 0.13-0.24 range giving the modest
			// density bumps it was authored for.
			AtmosphereLUTs::Params params = g_pEnv->_atmosphereLUTs->GetParams();
			constexpr float kDensityHVarDefault = 0.11f;
			const float densityScale     = std::max(0.02f, env_density._val.f32) / kDensityHVarDefault;
			const float rayleighStrength = std::max(0.0f,  env_rayleighStrength._val.f32);
			const float mieStrength      = std::max(0.0f,  env_mieStrength._val.f32);
			// Hillaire reference coefficients (per Mm). Re-applied here each
			// frame to a fresh Params struct so the env_* multipliers
			// compose correctly even after a previous frame scaled them.
			const math::Vector3 baseRayleighScatter(5.802f, 13.558f, 33.1f);
			const float         baseMieScatter     = 3.996f;
			const float         baseMieExtinction  = 4.40f;
			params.rayleighScatteringPerMM = baseRayleighScatter * (densityScale * rayleighStrength);
			params.mieScatteringPerMM      = baseMieScatter     * densityScale * mieStrength;
			params.mieExtinctionPerMM      = baseMieExtinction  * densityScale * mieStrength;
			g_pEnv->_atmosphereLUTs->SetParams(params);

			const float cameraWorldY = _currentCamera != nullptr
				? _currentCamera->GetEntity()->GetWorldTM().Translation().y
				: 0.0f;
			g_pEnv->_atmosphereLUTs->Update(cameraWorldY, sunDir, sunIntensity);

			// Post-LUT overcast tint. The Hillaire model only produces
			// clear-sky / sunset gradients - it physically can't make a
			// grey storm or dismal overcast sky (those come from cloud
			// cover reflecting diffuse sunlight in reality). We fake the
			// look here by lerping the LUT result toward a weather-driven
			// "cloud bottom" colour in the sky shader.
			//
			//   amount = max(precipitationIntensity, wetness * 0.7)
			//   colour = lerp(dismal grey, storm white, precipitation)
			//          * sun-dimming (low sun = darker storm)
			//          * sun light multiplier (night = dark)
			//
			// At amount=0 the sky is pure Hillaire. At amount=1 it reads
			// as flat overcast/storm grey, dimmed by sun height/intensity
			// so night storms look dark and overcast at noon looks bright
			// dismal-white. Brighter storm-white for heavy precipitation
			// (lit thunderhead) versus dismal grey for light overcast.
			if (_currentScene != nullptr)
			{
				const auto& wp = _currentScene->GetWeatherSurfaceParams();
				const float precipIntensity = std::clamp(wp.precipitationIntensity, 0.0f, 1.0f);
				const float wetness         = std::clamp(wp.wetness, 0.0f, 1.0f);
				const float overcastAmount  = std::clamp(std::max(precipIntensity, wetness * 0.7f), 0.0f, 1.0f);

				const float sunDimming = std::clamp(0.10f + 0.90f * std::clamp(sunDir.y * 0.5f + 0.5f, 0.0f, 1.0f), 0.10f, 1.0f);
				const math::Vector3 dismalGrey(0.55f, 0.57f, 0.62f);
				const math::Vector3 stormWhite(0.78f, 0.78f, 0.80f);
				const math::Vector3 cloudColour = dismalGrey + (stormWhite - dismalGrey) * precipIntensity;
				const math::Vector3 overcastColour = cloudColour * (sunDimming * lightMult);

				g_pEnv->_atmosphereLUTs->SetSkyRenderParams(overcastColour, overcastAmount);

				// The prefiltered sky ENVIRONMENT atlas has to receive the same
				// tint, or reflections disagree with the sky above them. Hillaire
				// is a clear-sky model and physically cannot produce overcast, so
				// this lerp IS how rain gets its grey - and the atlas prefilters
				// the raw sky-view LUT, which never sees it. The result was a wet
				// road mirroring clear blue under a grey-brown storm.
				_skyOvercastColour = overcastColour;
				_skyOvercastAmount = overcastAmount;
			}
			else
			{
				g_pEnv->_atmosphereLUTs->SetSkyRenderParams(math::Vector3(1.0f, 1.0f, 1.0f), 0.0f);
				_skyOvercastColour = math::Vector3(1.0f, 1.0f, 1.0f);
				_skyOvercastAmount = 0.0f;
			}
		}

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

		// MOVED: this block used to run BEFORE RenderOpaque, which meant the
		// scatter pass could not read the current frame's GBuffer (cleared at
		// frame start, filled by RenderOpaque). It now runs after the opaque +
		// decal passes so the emissive screen-space injection samples the final
		// surface data. Still well before the volumetric APPLY pass in post.
		// Phase D volumetric scattering update. Independent of AtmosphereLUTs
		// so it runs even when LUTs are disabled - the new system is the
		// SUCCESSOR to the legacy per-pixel VolumetricLighting march, not
		// a sibling of the atmosphere LUTs. Skipped only when there's no
		// sun light to drive god rays.
		if (g_pEnv->_volumetricScattering != nullptr && sunLight != nullptr)
		{
			const math::Vector3 vsSunForward = sunLight->GetEntity()->GetComponent<Transform>()->GetForward();
			const math::Vector3 vsSunDir = -vsSunForward;
			const float vsLightMult = sunLight->GetLightMultiplier();
			const math::Color sunCol = sunLight->GetDiffuseColour();
			const math::Vector3 sunColV(sunCol.R(), sunCol.G(), sunCol.B());
			// Phase G: forward-peaked Mie (positive) is what produces the
			// "rays brighter when looking at the sun" effect. The legacy
			// env_volumetricScattering HVar defaults to a negative value
			// because the old shader had different sign conventions; clamp
			// to a sensible forward-scatter range for the new system.
			const float phaseG = std::clamp(std::abs(env_volumetricScattering._val.f32), 0.0f, 0.95f);
			// Scattering strength multiplier - the legacy system applied
			// env_volumetricStrength (default 1.0, range 0.1-5.0) on top
			// of density. We do the same so authoring stays compatible.
			const float strength = std::max(0.0f, env_volumetricStrength._val.f32);
			// Sun intensity scaling. Unlike the atmosphere LUTs (which
			// need the 18-30 lerp because their scattering coefficients
			// are in per-megametre units and need bringing back to per-
			// metre HDR), the volumetric scatter math is already in
			// per-metre units and integrates over actual metres. Using
			// the LUT lerp here over-brights rays by ~25x. Just use the
			// sun light's natural multiplier (matches the engine's
			// existing direct-lighting magnitude) so rays sit in the
			// same HDR range as direct sun on a surface.
			const float vsSunIntensity = vsLightMult;
			// Uniform fog extinction comes from r_fogDensity - the same knob
			// the weather plugin's presets drive. This used to be hardcoded
			// 0, which made weather fogDensity a dead parameter: PostFog also
			// ignores it whenever the aerial-perspective volume is active
			// (distExtinction zeroed), so blizzard / storm / overcast all
			// rendered with identical near-field visibility. With it wired
			// here, weather fog is a real lit+shadowed participating medium.
			//
			// Night-dimmed by the SAME factor SetupPerFrameBuffer applied to
			// the cbuffer copy (cached in _volumetricFogNightDim): the apply
			// shader's beyond-128m analytic continuation reads the cbuffer
			// values, and the two media MUST have identical density or the
			// extinction slope steps at the volume far plane - the user saw
			// it as a literal ring around the camera at 128m radius.
			const float baseExt = r_fogDensity._val.f32 * _volumetricFogNightDim;
			// Same night-dim as baseExt + the cbuffer copy - see above.
			const float heightDensity = r_fogHeightDensity._val.f32 * _volumetricFogNightDim;
			const float heightPivot   = r_fogHeightPivot._val.f32;
			const float heightFalloff = r_fogHeightFalloff._val.f32;
			// Gather all sun shadow cascades. The volumetric scatter pass
			// walks 0..N-1 per froxel and uses the first cascade whose
			// NDC bounds contain the position - so god rays appear out
			// to whatever distance the furthest cascade covers, not just
			// the ~50m of cascade 0. r_shadowCascades caps the count.
			math::Matrix cascadeVPs[VolumetricScattering::kMaxCascades];
			ITexture2D*  cascadeMaps[VolumetricScattering::kMaxCascades] = {};
			math::Vector2 shadowMapSize(0.0f, 0.0f);
			const uint32_t requestedCascades = std::min(
				(uint32_t)std::max(0, r_shadowCascades._val.i32),
				VolumetricScattering::kMaxCascades);
			uint32_t numCascades = 0u;
			for (uint32_t ci = 0u; ci < requestedCascades; ++ci)
			{
				auto* shadowMapC = sunLight->GetShadowMap((int32_t)ci);
				if (shadowMapC == nullptr)
					break;
				cascadeVPs[ci] = sunLight->GetViewMatrix((int32_t)ci) * sunLight->GetProjectionMatrix((int32_t)ci);
				cascadeMaps[ci] = shadowMapC->GetDepthMap();
				if (ci == 0u)
				{
					const math::Viewport& sv = shadowMapC->GetViewport();
					shadowMapSize = math::Vector2(sv.width, sv.height);
				}
				numCascades = ci + 1u;
			}
			for (uint32_t ci = numCascades; ci < VolumetricScattering::kMaxCascades; ++ci)
				cascadeVPs[ci] = math::Matrix::Identity;
			const float shadowBias = 0.001f;
			// Current frame view-projection + eye position for next-frame
			// history reprojection. The volumetric integrate pass stores
			// these and uses them on the next call to find where each
			// froxel WAS in the previous frame's volume (eye pos needed
			// for distance-along-ray W mapping; off-axis froxels would
			// reproject to the wrong W slice without it).
			const math::Matrix currentVP =
				_currentCamera->GetViewMatrix() * _currentCamera->GetProjectionMatrix();
			const math::Vector3 currentEye =
				_currentCamera->GetEntity()->GetPosition();
			// Atmospheric transmittance LUT for sunset/sunrise reddening
			// of god rays. nullptr-safe in Update - falls back to raw
			// sunColour without atmospheric tint when LUTs are off.
			ITexture2D* atmTransLUT = nullptr;
			if (g_pEnv->_atmosphereLUTs != nullptr)
				atmTransLUT = g_pEnv->_atmosphereLUTs->GetTransmittanceLUT();
			// Forward lights cbuffer for volumetric contribution from
			// point/spot lights. SetupForwardLights() runs again later
			// for the transparency pass (idempotent); calling it here
			// ensures the buffer is populated before the volumetric
			// scatter dispatch reads it. The buffer is the same
			// instance both passes use.
			SetupForwardLights();

			// Gather shadow-casting spot lights for the volumetric pass.
			// MUST replicate the same selection + sort SetupForwardLights
			// uses (closest-N by camera distance, capped at r_maxSpotLights
			// & kMaxForwardSpotLights) so that the forward-spot index in
			// the cbuffer matches the index used here for shadowSlotForFwd.
			// Without matching the sort, the shader's per-forward-spot
			// lookup g_spotShadowSlotPerForward[i] would get shadow data
			// for the wrong spot (or none) - the user previously saw "some
			// spots scatter, others don't" because of this mismatch.
			math::Matrix spotShadowVPs[VolumetricScattering::kMaxShadowedSpots];
			ITexture2D*  spotShadowMaps[VolumetricScattering::kMaxShadowedSpots] = {};
			int          shadowSlotForFwd[VolumetricScattering::kMaxForwardSpots];
			for (auto& s : shadowSlotForFwd) s = -1;
			uint32_t numShadowedSpots = 0;
			math::Vector2 spotShadowMapSize(0.0f, 0.0f);
			{
				std::vector<SpotLight*> spotLights;
				if (_currentScene->GetComponents<SpotLight>(spotLights))
				{
					// Same filter+sort as SetupForwardLights.
					spotLights.erase(std::remove_if(spotLights.begin(), spotLights.end(),
						[](SpotLight* l)
						{
							if (l == nullptr || l->GetEntity() == nullptr || l->GetEntity()->IsPendingDeletion())
								return true;
							return l->GetDiffuseColour().w <= 0.0f;
						}), spotLights.end());
					const math::Vector3 camPos = _currentCamera->GetEntity()->GetPosition();
					std::sort(spotLights.begin(), spotLights.end(),
						[&camPos](SpotLight* a, SpotLight* b)
						{
							const float da = (a->GetEntity()->GetWorldTM().Translation() - camPos).LengthSquared();
							const float db = (b->GetEntity()->GetWorldTM().Translation() - camPos).LengthSquared();
							return da < db;
						});
					const size_t spotCap = std::min<size_t>(
						static_cast<size_t>(std::max(1, r_maxSpotLights._val.i32)),
						static_cast<size_t>(VolumetricScattering::kMaxForwardSpots));

					uint32_t fwdIdx = 0u;
					for (auto* light : spotLights)
					{
						if (fwdIdx >= spotCap)
							break;
						if (light->GetDoesCastShadows() &&
							numShadowedSpots < VolumetricScattering::kMaxShadowedSpots)
						{
							if (auto* sm = light->GetShadowMap(0); sm != nullptr)
							{
								spotShadowVPs[numShadowedSpots] =
									light->GetViewMatrix(0) * light->GetProjectionMatrix(0);
								spotShadowMaps[numShadowedSpots] = sm->GetDepthMap();
								shadowSlotForFwd[fwdIdx] = (int)numShadowedSpots;
								if (numShadowedSpots == 0u)
								{
									const math::Viewport& sv = sm->GetViewport();
									spotShadowMapSize = math::Vector2(sv.width, sv.height);
								}
								++numShadowedSpots;
							}
						}
						++fwdIdx;
					}
				}
			}
			for (uint32_t i = numShadowedSpots; i < VolumetricScattering::kMaxShadowedSpots; ++i)
				spotShadowVPs[i] = math::Matrix::Identity;

			// Gather shadow-casting point lights for the volumetric pass.
			// Same selection rule as spots: closest-N sorted by camera
			// distance, capped at r_maxPointLights & kMaxForwardPoints,
			// then take the first kMaxShadowedPoints shadow-casters.
			ITexture2D* pointShadowFaceMaps[VolumetricScattering::kMaxShadowedPoints][6] = {};
			float       pointShadowFarMetres[VolumetricScattering::kMaxShadowedPoints] = {};
			int         shadowSlotForFwdPoint[VolumetricScattering::kMaxForwardPoints];
			for (auto& s : shadowSlotForFwdPoint) s = -1;
			uint32_t numShadowedPoints = 0;
			{
				std::vector<PointLight*> pointLights;
				if (_currentScene->GetComponents<PointLight>(pointLights))
				{
					pointLights.erase(std::remove_if(pointLights.begin(), pointLights.end(),
						[](PointLight* l)
						{
							if (l == nullptr || l->GetEntity() == nullptr || l->GetEntity()->IsPendingDeletion())
								return true;
							return l->GetDiffuseColour().w <= 0.0f;
						}), pointLights.end());
					const math::Vector3 camPos = _currentCamera->GetEntity()->GetPosition();
					std::sort(pointLights.begin(), pointLights.end(),
						[&camPos](PointLight* a, PointLight* b)
						{
							const float da = (a->GetEntity()->GetWorldTM().Translation() - camPos).LengthSquared();
							const float db = (b->GetEntity()->GetWorldTM().Translation() - camPos).LengthSquared();
							return da < db;
						});
					const size_t pointCap = std::min<size_t>(
						static_cast<size_t>(std::max(1, r_maxPointLights._val.i32)),
						static_cast<size_t>(VolumetricScattering::kMaxForwardPoints));

					uint32_t fwdIdx = 0u;
					for (auto* light : pointLights)
					{
						if (fwdIdx >= pointCap)
							break;
						if (light->GetDoesCastShadows() &&
							numShadowedPoints < VolumetricScattering::kMaxShadowedPoints)
						{
							// PointLight has 6 ShadowMap entries (one per cube face). All 6 must
							// exist for the cubemap array slot to be valid. We feed the colour
							// RENDER TARGET (R32_FLOAT) into the volumetric, NOT the DSV-bound
							// depth texture - ShadowMapGeometry.shader writes NDC.z into the
							// colour RT specifically so the volumetric can CopySubresourceRegion
							// from a plain R32_FLOAT source without crossing any
							// DEPTH_STENCIL-bind state boundary. The DSV depth map (still
							// available via GetDepthMap()) continues to back the directional /
							// spot per-pixel shadow paths unchanged.
							bool allFacesValid = true;
							for (int32_t f = 0; f < 6; ++f)
							{
								auto* sm = light->GetShadowMap(f);
								if (sm == nullptr || sm->GetRenderTarget() == nullptr)
								{
									allFacesValid = false;
									break;
								}
							}
							if (allFacesValid)
							{
								for (int32_t f = 0; f < 6; ++f)
									pointShadowFaceMaps[numShadowedPoints][f] = light->GetShadowMap(f)->GetRenderTarget();
								pointShadowFarMetres[numShadowedPoints] = std::max(0.05f, light->GetRadius());
								shadowSlotForFwdPoint[fwdIdx] = (int)numShadowedPoints;
								++numShadowedPoints;
							}
						}
						++fwdIdx;
					}
				}
			}
			const float pointShadowBiasMetres = r_pointShadowBias._val.f32;
			// Spot shadow bias in NDC.z units. Spot perspective puts
			// most useful depth in the upper-end of [0,1] - e.g. a 30m-
			// radius spot puts a 5m floor at ndc.z ~0.998 and a 10m
			// occluder at ~0.9993. A bias of 0.0005 catches genuine
			// occluders without leaking, and matches the precision the
			// shadow map's R32 depth can resolve at these distances.
			// (Earlier 0.005 was 10x too generous and let everything
			// pass; this was masked by parallel engine bugs - bounding
			// sphere uninitialised, local-space view matrix - that
			// also produced an empty shadow map.)
			const float spotShadowBias = 0.0005f;

			g_pEnv->_volumetricScattering->SetClusteredLights(
				_clusteredLights.GetLightsSrv(),
				_clusteredLights.GetCountsSrv(),
				_clusteredLights.GetListsSrv(),
				r_clusterFog._val.b && r_clusterLights._val.b,
				r_shadowAtlas._val.b ? _shadowAtlas.GetAtlasSrv() : nullptr,
				r_shadowAtlas._val.b ? _clusteredLights.GetTileVpSrv() : nullptr);
			g_pEnv->_volumetricScattering->Update(
				vsSunDir, sunColV, vsSunIntensity, phaseG, strength,
				baseExt, heightDensity, heightPivot, heightFalloff,
				cascadeVPs, cascadeMaps, numCascades,
				shadowBias, shadowMapSize, currentVP, currentEye,
				atmTransLUT,
				_forwardLightsBuffer,
				spotShadowVPs, spotShadowMaps, shadowSlotForFwd, numShadowedSpots,
				spotShadowMapSize, spotShadowBias,
				pointShadowFaceMaps, pointShadowFarMetres, shadowSlotForFwdPoint,
				numShadowedPoints, pointShadowBiasMetres,
				// Screen-space emissive injection sources. Valid here because
				// this block runs AFTER RenderOpaque + RenderDecals filled the
				// gbuffer (the position RT's alpha carries the surface's
				// emissive intensity, the diffuse RT its tint).
				_gbuffer.GetDiffuse(), _gbuffer.GetPosition(),
				r_volumetricEmissive._val.f32, r_volumetricEmissiveRange._val.f32,
				// Ambient inscatter colour for the fog medium. Uses the
				// sunset/night-TINTED ambient cached by SetupPerFrameBuffer
				// (not the raw scene ambient) so the froxel fog colour
				// matches the cbuffer ambientLight that the apply shader's
				// beyond-range continuation converges to - a raw/tinted
				// mismatch shows as a colour ring at the 128m far plane at
				// sunset and night. Weather authors the base colour (grey
				// for storms, orange for sandstorm); the cache only adds
				// the time-of-day tint on top.
				_volumetricAmbientNight,
				r_volumetricAmbient._val.f32);
		}

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

			if (r_drawNavMesh._val.b && g_pEnv->_navMeshProvider != nullptr)
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
			_currentScene->GetEntities(((HexEngine::ComponentSignature)1 << componentId), entitiesWithComponent);

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

		// Directional lights are global - their entity position is arbitrary, so ranking
		// them by distance-to-camera is meaningless. They must never lose a shadow-map slot
		// to a local light, so hoist them to the front before the distance sort. (This used
		// to sort ALL casters, sun included, by distance DESCENDING and then keep the first
		// MaxShadowCasters - i.e. it deliberately kept the FURTHEST casters, and could drop
		// the sun entirely depending on where the sun entity happened to sit.)
		const auto firstLocal = std::stable_partition(pvs.begin(), pvs.end(), [](Light* light) {
			return dynamic_cast<DirectionalLight*>(light) != nullptr;
			});

		// Local lights: nearest first, since near lights dominate what the viewer sees.
		const math::Vector3 cameraPos = _currentCamera->GetEntity()->GetPosition();
		std::sort(firstLocal, pvs.end(), [&cameraPos](Light* left, Light* right) {

			auto leftDist = (left->GetEntity()->GetPosition() - cameraPos).LengthSquared();
			auto rightDist = (right->GetEntity()->GetPosition() - cameraPos).LengthSquared();

			return leftDist < rightDist;
			});

		// MaxShadowCasters bounds the number of shadow maps rendered this frame, across all
		// light types. With the atlas on, capacity is the atlas tile count
		// instead - the per-frame render budget lives in AssignTiles, not
		// here, so listing more casters costs nothing until they win tiles.
		const size_t cap = r_shadowAtlas._val.b
			? (size_t)ShadowAtlas::kTileCount
			: (size_t)MaxShadowCasters;
		const size_t casterCount = (pvs.size() < cap) ? pvs.size() : cap;
		_shadowCasters.insert(_shadowCasters.end(), pvs.begin(), pvs.begin() + casterCount);
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

			// TEMPORARY: hard numbers for the cbuffer layout. Hand-computing the HLSL side
			// gives total 1360 with _reflectionParams at 1328; if C++ disagrees, that is the
			// divergence making the appended tail unreadable from the shader.
			// Layout verified 2026-07-26: sizeof 1360, _taaParams@1312, _reflectionParams@1328,
			// _iblParams@1344 - which matches HLSL's packing of the same declaration exactly,
			// so appending float4s at the end of this struct is sound.
			// (Appended since that audit: _iblComposeParams, _skyOvercast, _ssrParams,
			// _exposureParams - the 1360 figure above is historical, the invariant that
			// C++ and HLSL append in LOCKSTEP is what matters.)
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
			bufferData._rainDripDebug = r_rainDripDebug._val.b ? 1.0f : 0.0f;
			bufferData._weatherSurface = _currentScene->GetWeatherSurfaceParams();
			// Diagnostic for the surface-matrix fields: logs what the GPU
			// actually receives ~once a second, so "slider does nothing"
			// reports can be split into CPU-chain vs shader problems.
			if (r_weatherSurfaceDebug._val.b)
			{
				static uint64_t sLastLogFrame = 0ull;
				const uint64_t frame = g_pEnv->_timeManager ? g_pEnv->_timeManager->_frameCount : 0ull;
				if (frame - sLastLogFrame >= 60ull)
				{
					sLastLogFrame = frame;
					const auto& ws = bufferData._weatherSurface;
					LOG_INFO("WeatherSurface upload: wet=%.3f puddle=%.3f snow=%.3f melt=%.3f dirt=%.3f precip=%.3f",
						ws.wetness, ws.puddleAmount, ws.snowCoverage, ws.snowMelt, ws.dirtAmount, ws.precipitationIntensity);
				}
			}

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
			// Signal to PostFog: aerial-perspective volume is doing the
			// distance-haze pass, so PostFog should skip its own
			// analytic atmosphere integration. Tied to the AP volume's
			// actual availability rather than the cvar so a failed LUT
			// init doesn't leave fog unintentionally broken.
			const bool apActive = g_pEnv->_atmosphereLUTs != nullptr
				&& g_pEnv->_atmosphereLUTs->GetAerialPerspectiveVolume() != nullptr;
			bufferData._atmosphere.fogUseAerialPerspective = apActive ? 1.0f : 0.0f;

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

				// Cache the night-dim factor and the tinted ambient for the
				// froxel volumetric update (which runs later this frame,
				// after the gbuffer fill). The froxel medium's extinction
				// and ambient-inscatter colour MUST match what the apply
				// shader's beyond-range analytic continuation reads from
				// this cbuffer (fogDensity / fogHeightDensity / ambientLight,
				// all modulated right here) - any mismatch steps the fog
				// density or colour at the volume's far plane and draws a
				// visible ring around the camera at that radius.
				_volumetricFogNightDim  = fogNightDim;
				_volumetricAmbientNight = ambientRgb;
			}

			bufferData._atmosphere.volumetricScattering = env_volumetricScattering._val.f32;
			bufferData._atmosphere.volumetricStrength = env_volumetricStrength._val.f32;
			// When the froxel-grid volumetric system is active, suppress the
			// legacy per-pixel volumetric ray-march in SpotLight.shader and
			// PointLight.shader. Both shaders' CalculateVolumetricScattering()
			// short-circuits on volumetricStrength <= 0, so zeroing it kills
			// the redundant per-pixel work without touching the shader code.
			// The froxel system reads its own strength from a private cbuffer
			// (VolumetricScattering passes env_volumetricStrength through
			// directly), so this doesn't affect the new system's brightness.
			const bool froxelActiveLegacyGate =
				g_pEnv->_volumetricScattering != nullptr &&
				g_pEnv->_volumetricScattering->GetIntegrationVolume() != nullptr;
			if (froxelActiveLegacyGate)
				bufferData._atmosphere.volumetricStrength = 0.0f;
			bufferData._atmosphere.volumetricSteps = GetVolumetricEffectiveSteps();
			bufferData._atmosphere.volumetricStepIncrement = env_volumetricStepIncrement._val.f32;
			bufferData._atmosphere.volumetricQuality = r_volumetricQuality._val.i32;
			bufferData._atmosphere.volumetricLightMaxDistance = r_volumetricLightMaxDistance._val.f32;
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
			bufferData._pbrEnergyFix = r_pbrEnergyFix._val.b ? 1.0f : 0.0f;

			bufferData._reflectionParams = math::Vector4(
				r_ssrSkyFallbackStrength._val.f32,
				r_ssrSkyHitMinDistance._val.f32,
				r_glassEnvReflection._val.f32,
				(float)r_ssrDebugSkyHits._val.i32);

			bufferData._iblParams = math::Vector4(
				r_iblSkySpecular._val.f32, r_iblSkyDiffuse._val.f32,
				r_iblProbeStrength._val.f32, r_iblProbeDiffuse._val.f32);

			// P1-B toggles ride in the reflection params' spare lanes (x and y are
			// dead since SSR.shader reverted to main and no longer reads them).
			bufferData._reflectionParams.x = r_iblDfgLut._val.b ? 1.0f : 0.0f;
			bufferData._reflectionParams.y = r_iblMultiScatter._val.b ? 1.0f : 0.0f;

			// Who owns environment specular this frame. Deferred.shader reads this
			// to decide whether to add the term; SSR.shader reads it to decide
			// whether a specular miss returns nothing (so SSRResolve can fill it
			// with environment) or falls back to the voxel-GI cone trace.
			bufferData._iblComposeParams = math::Vector4(
				ShouldComposeEnvSpecularInResolve() ? 1.0f : 0.0f,
				r_ssrEnergyConserve._val.b ? 1.0f : 0.0f,
				r_wetnessDarkening._val.f32,
				r_ssrMaxStepLength._val.f32);

			// Lane map (see Global.shader defines): x = in-screen fallback,
			// y = temporal jitter (gated on the denoiser - a dead toggle until
			// the marchMode lane collision was found), z = forward clustering
			// (main camera only - the grid is built for one view), w = marcher.
			const bool clusterForwardActive =
				r_clusterForward._val.b && r_clusterLights._val.b &&
				_currentScene != nullptr && _currentCamera == _currentScene->GetMainCamera();
			bufferData._ssrParams = math::Vector4(
				r_ssrInScreenFallback._val.b ? 1.0f : 0.0f,
				(r_ssrTemporalJitter._val.b && r_ssrDenoise._val.b) ? 1.0f : 0.0f,
				clusterForwardActive ? 1.0f : 0.0f,
				(float)r_ssrMarchMode._val.i32);

			// Physical units part 4: the declared convention is
			//   rendered units = physical cd/m^2 * g_preExposure
			// with the static pre-exposure 1/r_legacyLightScale (see the cvar
			// comment for why static, and why no shader multiplies by it yet -
			// the scale and pre-exposure cancel inside every packed light, so
			// rendered magnitudes are unchanged). Shaders that need ABSOLUTE
			// luminance (future physical sky, lens effects) multiply by
			// g_invPreExposure.
			{
				const float unitScale = std::max(r_legacyLightScale._val.f32, 1.0f);
				bufferData._exposureParams = math::Vector4(1.0f / unitScale, unitScale, unitScale, 0.0f);
			}

			// Shelter/rain occlusion (slice 2). Numbers mirror
			// UpdateRainOcclusionMap's constants: 2048 map, 160 m depth
			// range; bias = 0.5 m expressed in depth units.
			bufferData._rainOcclusionVP = (_rainOcclusionValid
				? (_rainOcclusionView * _rainOcclusionProj)
				: math::Matrix::Identity).Transpose();
			bufferData._rainOcclusionParams = math::Vector4(
				_rainOcclusionValid ? 1.0f : 0.0f,
				0.5f / 160.0f,
				1.0f / 2048.0f,
				// .w = snow-shell textures bound (t22/t23) - the shell PS reads
				// this to pick real snow textures vs its procedural fallback.
				_snowShellReady ? 1.0f : 0.0f);

			bufferData._skyOvercast = math::Vector4(
				_skyOvercastColour.x, _skyOvercastColour.y, _skyOvercastColour.z,
				_skyOvercastAmount);

			// Select this frame's reflection probe: the nearest atlas-ready probe
			// whose box contains the camera, falling back to the nearest ready
			// probe overall. One probe per frame in v1 - rooms don't overlap
			// often, and per-pixel probe arrays are a later step. The chosen
			// probe's atlas is bound at t16 by the deferred pass.
			_activeProbe = nullptr;
			_activeProbe2 = nullptr;
			bufferData._probeCenter = math::Vector4(0.0f, 0.0f, 0.0f, 0.0f);
			bufferData._probeExtents = math::Vector4(0.0f, 0.0f, 0.0f, 0.0f);
			bufferData._probeCenter2 = math::Vector4(0.0f, 0.0f, 0.0f, 0.0f);
			bufferData._probeExtents2 = math::Vector4(0.0f, 0.0f, 0.0f, 0.0f);
			// Never sample probes while capturing one: the capture camera sits
			// inside its own probe's box, so it would bake the previous capture's
			// reflections into the new one and compound them on every recapture.
			// Probe faces see sky IBL only.
			const bool capturingEnvironment =
				_currentCamera != nullptr && _currentCamera->IsEnvironmentCapture();
			if (!capturingEnvironment && _currentScene != nullptr && _cameraEntity != nullptr)
			{
				std::vector<ReflectionProbeComponent*> probes;
				if (_currentScene->GetComponents<ReflectionProbeComponent>(probes))
				{
					const math::Vector3 camPos = _cameraEntity->GetWorldTM().Translation();
					float bestDistSq = FLT_MAX;
					bool bestInside = false;
					// Keep the TWO best probes so the shader can cross-fade between
					// them. With only one, a pixel snaps to a different environment
					// the instant the selection flips, which pops when walking
					// between rooms.
					float secondDistSq = FLT_MAX;
					bool secondInside = false;

					for (auto* probe : probes)
					{
						if (probe == nullptr || !probe->IsAtlasReady())
							continue;
						const math::Vector3 centre = probe->GetWorldCentre();
						const math::Vector3 ext = probe->GetExtents();
						const math::Vector3 d = camPos - centre;
						const bool inside =
							fabsf(d.x) <= ext.x && fabsf(d.y) <= ext.y && fabsf(d.z) <= ext.z;
						const float distSq = d.LengthSquared();
						// Inside-probes always beat outside-probes; ties by distance.
						const bool beatsBest =
							(inside && !bestInside) || (inside == bestInside && distSq < bestDistSq);
						if (beatsBest)
						{
							// Demote the old winner to second place.
							secondInside = bestInside;
							secondDistSq = bestDistSq;
							_activeProbe2 = _activeProbe;

							bestInside = inside;
							bestDistSq = distSq;
							_activeProbe = probe;
						}
						else if ((inside && !secondInside) ||
								 (inside == secondInside && distSq < secondDistSq))
						{
							secondInside = inside;
							secondDistSq = distSq;
							_activeProbe2 = probe;
						}
					}
					if (_activeProbe != nullptr)
					{
						const math::Vector3 c = _activeProbe->GetWorldCentre();
						const math::Vector3 e = _activeProbe->GetExtents();
						bufferData._probeCenter = math::Vector4(c.x, c.y, c.z, 1.0f);
						bufferData._probeExtents = math::Vector4(
							e.x, e.y, e.z, _activeProbe->GetBoxProjection() ? 1.0f : 0.0f);
					}
					if (_activeProbe2 != nullptr)
					{
						const math::Vector3 c = _activeProbe2->GetWorldCentre();
						const math::Vector3 e = _activeProbe2->GetExtents();
						bufferData._probeCenter2 = math::Vector4(c.x, c.y, c.z, 1.0f);
						bufferData._probeExtents2 = math::Vector4(
							e.x, e.y, e.z, _activeProbe2->GetBoxProjection() ? 1.0f : 0.0f);
					}
				}
			}

			// z: transparent sun shadows (slice 5) - set only when RenderTransparent
			// will actually bind the cascades this frame (same sun lookup, main
			// camera only), because the shader gate trusts this flag completely.
			const bool transparentShadowsActive =
				_currentScene != nullptr && _currentCamera == _currentScene->GetMainCamera() &&
				FindTransparentShadowSun(_currentScene) != nullptr;
			bufferData._taaParams = math::Vector4(
				r_taaVarianceGamma._val.f32,
				// Snap to exactly +/-1 so an intermediate cvar value can't scale the
				// reprojection distance as well as its direction.
				(r_taaVelocityYSign._val.f32 < 0.0f) ? -1.0f : 1.0f,
				transparentShadowsActive ? 1.0f : 0.0f,
				// w: EXACTLY the condition SetClusteredLights hands the froxel CS -
				// if these ever diverge, per-light fog either double-counts or
				// vanishes.
				(r_clusterFog._val.b && r_clusterLights._val.b) ? 1.0f : 0.0f);

			// ocean
			bufferData._oceanConfig = _currentScene->GetOcean();

			// No TAA jitter for environment captures: a probe face is rendered
			// once with no history to accumulate into, so jitter would only
			// offset the capture by a sub-pixel - and a non-zero jitter from a
			// small capture viewport is what pushes NRD's cameraJitter out of
			// range (see RenderSSR's early-out).
			bufferData._jitterOffsets =
				(_currentCamera != nullptr && _currentCamera->IsEnvironmentCapture())
					? math::Vector2(0.0f, 0.0f)
					: _taa.GetJitterOffset(viewport.width, viewport.height);
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
			// Tell the per-pixel light shaders whether to sample the bound shadow map(s)
			// or skip and return lit. The spot-light render loop binds a null SRV at
			// SHADOWMAPS[0] for non-casters, and SampleCmpLevelZero against null reads 0
			// (fully occluded) - which would black out every fragment inside the cone.
			bufferData._shadowConfig.castsShadowsFlag =
				(shadowCaster != nullptr && shadowCaster->GetDoesCastShadows()) ? 1 : 0;

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

				// r_shadowFilterMaxSize / r_penumbraFilterMaxSize are expressed in TEXELS,
				// and PCSS multiplies them by texelSize (= 1/shadowMapSize) to get a UV
				// radius - so holding the world-space penumbra constant means scaling the
				// texel count PROPORTIONALLY to resolution. This was (8192 / size), i.e.
				// inverted, and then doubled again above 1.0: at 4096 that produced an 8x
				// larger penumbra instead of the same one. It only ever came out correct at
				// exactly 8192, where it evaluates to 1.0.
				const float kFilterReferenceResolution = 8192.0f;
				float shadowVarsMultiplier = (shadowMapSize > 0.0f)
					? (shadowMapSize / kFilterReferenceResolution)
					: 1.0f;

				// shadow
				bufferData._shadowConfig.penumbraFilterMaxSize = r_penumbraFilterMaxSize._val.f32 * shadowVarsMultiplier;
				bufferData._shadowConfig.shadowFilterMaxSize = r_shadowFilterMaxSize._val.f32 * shadowVarsMultiplier;
				bufferData._shadowConfig.biasMultiplier = r_shadowBiasMultiplier._val.f32;
				bufferData._shadowConfig.samples = (float)numSamples;
				bufferData._shadowConfig.cascadeBlendRange = r_shadowCascadeBlendRange._val.f32;

				// How many cascades are actually rendered and bound. RenderShadowMaps drives
				// the sun's cascade loop from r_shadowCascades, so a directional light can
				// have fewer live cascades than the 4 it allocates; point lights use all 6
				// faces and spots a single map. The shader must not iterate or blend past
				// this, or it samples an unbound depth map as "fully occluded".
				bufferData._shadowConfig.cascadeCount =
					(dynamic_cast<DirectionalLight*>(shadowCaster) != nullptr)
					? std::min(maxShadowCascades, r_shadowCascades._val.i32)
					: maxShadowCascades;

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

		// Bind the Hillaire 2020 sky LUTs at PS t0/t1 so SkySphere.shader's
		// LUT-driven path can sample them. PS sampler slot 4 is already a
		// linear sampler by engine convention (see GraphicsDeviceD3D11::
		// BeginFrame). Null binds when the LUT subsystem isn't ready - the
		// shader's static USE_SKY_VIEW_LUT branch should be flipped off in
		// that case, but null SRVs read as 0 so the worst case is a black
		// sky pass for a single frame during startup.
		if (g_pEnv->_atmosphereLUTs != nullptr)
		{
			g_pEnv->_graphicsDevice->SetTexture2D(0, g_pEnv->_atmosphereLUTs->GetSkyViewLUT());
			g_pEnv->_graphicsDevice->SetTexture2D(1, g_pEnv->_atmosphereLUTs->GetTransmittanceLUT());
			// PS b6 = sky-render cbuffer (overcast tint). Bound here
			// because the sky entity draw is upcoming and the engine's
			// material pipeline doesn't know about this cbuffer.
			g_pEnv->_graphicsDevice->SetConstantBufferPS(6, g_pEnv->_atmosphereLUTs->GetSkyRenderCBuffer());
		}

		// Pass 0: base sky only (no direct sun/mie). This is what fog samples from _atmosphereRT.
		SetupPerShadowCasterBuffer(nullptr, false, 0, 0, 0, 0.0f);
		_currentScene->RenderEntities(
			_currentCamera->GetPVS(),
			LAYERMASK(Layer::Sky),
			MeshRenderFlags::MeshRenderNormal);

		_gbuffer.GetDiffuse()->CopyTo(_atmosphereRT);

		// Re-bind for pass 1 (other entity draws between passes may have
		// stomped t0/t1 via slotless SRV binds).
		if (g_pEnv->_atmosphereLUTs != nullptr)
		{
			g_pEnv->_graphicsDevice->SetTexture2D(0, g_pEnv->_atmosphereLUTs->GetSkyViewLUT());
			g_pEnv->_graphicsDevice->SetTexture2D(1, g_pEnv->_atmosphereLUTs->GetTransmittanceLUT());
			// PS b6 = sky-render cbuffer (overcast tint). Bound here
			// because the sky entity draw is upcoming and the engine's
			// material pipeline doesn't know about this cbuffer.
			g_pEnv->_graphicsDevice->SetConstantBufferPS(6, g_pEnv->_atmosphereLUTs->GetSkyRenderCBuffer());
		}

		// Pass 1: full sky for the visible frame (includes sun/sunset/mie).
		SetupPerShadowCasterBuffer(nullptr, false, 1, 0, 0, 0.0f);
		_currentScene->RenderEntities(
			_currentCamera->GetPVS(),
			LAYERMASK(Layer::Sky),
			MeshRenderFlags::MeshRenderNormal);

		// Clear the LUT slots so subsequent entity draws don't pick up
		// stale atmosphere bindings (they don't read t0/t1 today, but
		// keeping the slot state clean avoids surprises for future code).
		// Reset the implicit-slot counter too: the slotted SetTexture2D
		// calls above left it at 2, which would cause any downstream
		// slotless PS SRV binds to start at slot 2 instead of slot 0 -
		// the same pattern that DiffuseGIAOProvider explicitly handles.
		if (g_pEnv->_atmosphereLUTs != nullptr)
		{
			g_pEnv->_graphicsDevice->SetTexture2D(0, nullptr);
			g_pEnv->_graphicsDevice->SetTexture2D(1, nullptr);
			g_pEnv->_graphicsDevice->SetConstantBufferPS(6, nullptr);
			g_pEnv->_graphicsDevice->SetBoundResourceIndex(0);
		}

		// Shelter/rain occlusion map at t26 for the opaque material shaders
		// (SampleRainShelter in PBRutils). Explicit slot, then reset the
		// implicit-slot counter so material auto-binds still start at 0 -
		// same pattern as the atmosphere block above.
		g_pEnv->_graphicsDevice->SetTexture2D(26,
			(_rainOcclusionValid && _rainOcclusionMap != nullptr)
				? _rainOcclusionMap->GetDepthMap() : nullptr);

		// Snow-shell material textures at t22/t23 (albedo/normal) for the snow
		// shell pixel shader. Lazy-loaded once; if missing, the shell keeps
		// its procedural white (the .w flag in SetupPerFrameBuffer gates it).
		if (!_snowShellTried)
		{
			_snowShellTried = true;
			_snowShellMaterial = Material::Create("EngineData.Materials/M_SnowShell.hmat");
			_snowShellReady = _snowShellMaterial != nullptr &&
				_snowShellMaterial->GetTexture(MaterialTexture::Albedo) != nullptr &&
				_snowShellMaterial->GetTexture(MaterialTexture::Normal) != nullptr;
			if (!_snowShellReady)
				LOG_WARN("Snow shell material/textures not found - shell uses procedural white");
		}
		if (_snowShellReady)
		{
			g_pEnv->_graphicsDevice->SetTexture2D(22, _snowShellMaterial->GetTexture(MaterialTexture::Albedo).get());
			g_pEnv->_graphicsDevice->SetTexture2D(23, _snowShellMaterial->GetTexture(MaterialTexture::Normal).get());
		}
		g_pEnv->_graphicsDevice->SetBoundResourceIndex(0);

		//g_pEnv->_graphicsDevice->SetCullingMode(CullingMode::FrontFace);

		_currentScene->RenderEntities(
			_currentCamera->GetPVS(),
			LAYERMASK(Layer::StaticGeometry) | LAYERMASK(Layer::DynamicGeometry) | LAYERMASK(Layer::Grass),
			MeshRenderFlags::MeshRenderNormal);
		_currentScene->RenderCustom(_currentScene, _currentCamera, MeshRenderFlags::MeshRenderNormal);

		// Tessellation state MUST NOT leak past the opaque pass. A hull/domain
		// pair left bound while any later pass (decals, deferred lighting,
		// transparents, post) issues a TRIANGLE-topology draw is an invalid
		// D3D11 pipeline (HS/DS require patch topology), which corrupts
		// intermittently depending on whether a tessellated mesh happened to
		// be the last opaque draw - exactly the "wrong winding + flashing at
		// certain angles" the snow road showed. Clear the stages and restore
		// triangle topology once, here, at the pass boundary.
		g_pEnv->_graphicsDevice->SetHullShader(nullptr);
		g_pEnv->_graphicsDevice->SetDomainShader(nullptr);
		g_pEnv->_graphicsDevice->SetTopology(HexEngine::PrimitiveTopology::TriangleList);

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

	void SceneRenderer::RenderShadowFaceToAtlas(const ShadowAtlas::FaceAssignment& assignment)
	{
		PROFILE();

		Light* light = const_cast<Light*>(assignment.key.light);
		auto* dsv = _shadowAtlas.GetTileDsv(assignment.tileIndex);
		auto* ctx = reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext());
		if (light == nullptr || dsv == nullptr || ctx == nullptr)
			return;

		int32_t tx = 0, ty = 0, tw = 0, th = 0;
		_shadowAtlas.GetTileViewport(assignment.tileIndex, tx, ty, tw, th);

		// Clear ONLY this tile. A whole-DSV clear would wipe every cached
		// neighbour, which is the entire point of the atlas - so the clear
		// goes through ID3D11DeviceContext1::ClearView with a rect (legal on
		// a depth-only DSV). If the interface is somehow missing, skip the
		// clear rather than nuke the atlas: the far-plane depth from the
		// previous owner only risks over-shadowing at the tile edge for one
		// frame.
		{
			ID3D11DeviceContext1* ctx1 = nullptr;
			if (SUCCEEDED(ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&ctx1)) && ctx1 != nullptr)
			{
				const FLOAT depthOne[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
				D3D11_RECT rect = {};
				rect.left = tx;
				rect.top = ty;
				rect.right = tx + tw;
				rect.bottom = ty + th;
				ctx1->ClearView(dsv, depthOne, &rect, 1);
				ctx1->Release();
			}
			else
			{
				static bool sWarned = false;
				if (!sWarned)
				{
					sWarned = true;
					LOG_WARN("ShadowAtlas: ID3D11DeviceContext1 unavailable - tile clears skipped");
				}
			}
		}

		// Depth-only bind, tile carved by viewport + scissor. Raw, like the
		// cluster SRV binds - the engine wrapper has no sub-rect DSV concept.
		ctx->OMSetRenderTargets(0, nullptr, dsv);
		D3D11_VIEWPORT vp = {};
		vp.TopLeftX = (FLOAT)tx;
		vp.TopLeftY = (FLOAT)ty;
		vp.Width = (FLOAT)tw;
		vp.Height = (FLOAT)th;
		vp.MaxDepth = 1.0f;
		ctx->RSSetViewports(1, &vp);
		D3D11_RECT sc = { tx, ty, tx + tw, ty + th };
		ctx->RSSetScissorRects(1, &sc);

		const int32_t face = (int32_t)assignment.key.face;
		const bool isPoint = light->CastAs<PointLight>() != nullptr;

		// Local lights have no camera-fit cascades: the face matrices are
		// fully determined by the light itself (cube face orientation for
		// points, the cone for spots). 0..1 mirrors what the legacy path
		// passes for its non-cascade lights.
		light->ConstructMatrices(_currentCamera, 0.0f, 1.0f, face);

		// Capture what this tile's depth is about to be rendered with - the
		// consumers sample with the CAPTURED matrices, never the light's
		// current ones (a later move that misses the re-render budget must
		// not re-project cached depth).
		_shadowAtlas.SetTileViewProj(assignment.tileIndex,
			light->GetViewMatrix(face) * light->GetProjectionMatrix(face));

		PVSParams params;
		params.lodPartition = r_lodPartition._val.f32;
		params.shapeType = PVSParams::ShapeType::Sphere;
		params.shape.sphere = light->GetLightBoundingSphere(face);
		params.isShadow = true;
		params.camera = _currentCamera;
		light->GetPVS(face)->CalculateVisibility(_currentScene, params);
		// Local lights don't get the per-entity-move instance-cache refresh
		// the camera and sun enjoy (Entity::ClearTransformCache), so a moved
		// prop would re-render with its OLD shadowInstanceData. Refresh the
		// whole face PVS - it only covers this light's radius, and this runs
		// solely for budgeted dirty faces.
		light->GetPVS(face)->RefreshAllInstanceCaches();

		math::Viewport shadowVp;
		shadowVp.width = (float)tw;
		shadowVp.height = (float)th;

		SetupPerFrameBuffer(
			light->GetViewMatrix(face),
			light->GetProjectionMatrix(face),
			light->GetViewMatrixPrev(face),
			light->GetProjectionMatrixPrev(face),
			light->GetMaxSupportedShadowCascades(),
			light->GetEntity()->GetComponent<Transform>()->GetForward(),
			shadowVp,
			face,
			1.0f,
			isPoint);

		_currentScene->RenderEntities(
			light->GetPVS(face),
			LAYERMASK(Layer::StaticGeometry) | LAYERMASK(Layer::DynamicGeometry),
			MeshRenderFlags::MeshRenderShadowMap);

		g_pEnv->_graphicsDevice->ClearScissorRect();
	}

	void SceneRenderer::UpdateRainOcclusionMap()
	{
		// Size + depth range are compile-time; the cbuffer pack in
		// SetupPerFrameBuffer repeats the two numbers - keep in sync.
		constexpr uint32_t kRainMapSize = 2048u;
		constexpr float kUpRange = 80.0f;   // metres above the map centre
		constexpr float kDownRange = 80.0f; // metres below

		const auto& wsp = _currentScene->GetWeatherSurfaceParams();
		const bool needed = r_rainOcclusion._val.b && _cameraEntity != nullptr &&
			(wsp.wetness > 0.001f || wsp.snowCoverage > 0.001f || wsp.puddleAmount > 0.001f);
		if (!needed)
		{
			// Dry weather: the map keeps its texels but shaders treat the
			// world as exposed, and no render cost is paid.
			_rainOcclusionValid = false;
			return;
		}

		if (_rainOcclusionMap == nullptr)
		{
			_rainOcclusionMap = new ShadowMap(kRainMapSize, kRainMapSize);
			_rainOcclusionMap->Create();
		}
		if (_rainOcclusionPVS == nullptr)
			_rainOcclusionPVS = std::make_unique<PVS>();

		const math::Vector3 camPos = _cameraEntity->GetPosition();
		const float extent = r_rainOcclusionExtent._val.f32;
		// Texel-snap the centre so cached depth doesn't crawl under camera
		// drift - same trick the sun cascades use.
		const float texelWorld = (2.0f * extent) / (float)kRainMapSize;
		const math::Vector3 centre(
			std::floor(camPos.x / texelWorld) * texelWorld,
			camPos.y,
			std::floor(camPos.z / texelWorld) * texelWorld);

		const double now = g_pEnv->_timeManager->GetTime();
		const float dx = centre.x - _rainOcclusionCentre.x;
		const float dz = centre.z - _rainOcclusionCentre.z;
		const float dy = std::fabs(centre.y - _rainOcclusionCentre.y);
		const float recentreDist = extent * 0.25f;
		const bool recentre = (dx * dx + dz * dz) > recentreDist * recentreDist
			|| dy > kUpRange * 0.5f;
		if (_rainOcclusionValid && !recentre && now < _rainOcclusionNextRefresh)
			return;

		// The map may still be bound as a PS SRV (t26) from last frame's
		// opaque pass - release it before rebinding as the depth target or
		// D3D11's hazard resolution nulls the DSV bind.
		g_pEnv->_graphicsDevice->SetTexture2D(26, nullptr);

		_rainOcclusionView = math::Matrix::CreateLookAt(
			centre + math::Vector3(0.0f, kUpRange, 0.0f), centre, math::Vector3(0.0f, 0.0f, 1.0f));
		_rainOcclusionProj = math::Matrix::CreateOrthographicOffCenter(
			-extent, extent, -extent, extent, 0.1f, kUpRange + kDownRange);

		PVSParams params;
		params.lodPartition = r_lodPartition._val.f32;
		params.shapeType = PVSParams::ShapeType::Sphere;
		params.shape.sphere = dx::BoundingSphere(
			dx::XMFLOAT3(centre.x, centre.y, centre.z),
			std::sqrt(2.0f * extent * extent + kUpRange * kUpRange));
		params.isShadow = true;
		params.camera = _currentCamera;
		_rainOcclusionPVS->CalculateVisibility(_currentScene, params);
		_rainOcclusionPVS->RefreshAllInstanceCaches();

		_rainOcclusionMap->SetRenderTarget();

		math::Viewport rainVp;
		rainVp.width = (float)kRainMapSize;
		rainVp.height = (float)kRainMapSize;
		SetupPerFrameBuffer(
			_rainOcclusionView, _rainOcclusionProj,
			_rainOcclusionView, _rainOcclusionProj,
			1, math::Vector3(0.0f, -1.0f, 0.0f), rainVp, 0, 1.0f, false);

		// Static geometry only: dynamic props sheltering the ground would
		// flicker wet/dry as they moved AND dirty the cache every frame.
		_currentScene->RenderEntities(
			_rainOcclusionPVS.get(),
			LAYERMASK(Layer::StaticGeometry),
			MeshRenderFlags::MeshRenderShadowMap);

		_rainOcclusionCentre = centre;
		_rainOcclusionNextRefresh = now + (double)r_rainOcclusionRefresh._val.f32;
		_rainOcclusionValid = true;
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
			// Sibling GI-AO pass: only runs when a plugin SSAO is loaded
			// AND r_useGIAO is set. Reads DiffuseGI's bilateral-blurred AO
			// target (produced by RenderDiffuseGI() below, so we're sampling
			// the previous frame's result - acceptable given GI's own
			// temporal accumulation; avoids a costly restructure of the
			// render order just for one frame of latency).
			if (r_useGIAO._val.b && g_pEnv->_giAOProvider != nullptr)
			{
				g_pEnv->_giAOProvider->ApplyAmbientOcclusion(
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
			// Volumetric lighting: prefer the Phase D froxel-grid path when
			// VolumetricScattering is up. The new path is depth-correct,
			// works on transparents naturally (sample per-fragment - though
			// that lands in D-4), and produces smoother results than the
			// half-res per-pixel ray-march in the legacy shader. Fall back
			// to the legacy path when the subsystem isn't initialised, so
			// scenes with broken compute support keep their sun shafts.
			const bool useFroxelVolumetrics =
				g_pEnv->_volumetricScattering != nullptr &&
				g_pEnv->_volumetricScattering->GetIntegrationVolume() != nullptr &&
				_volumetricScatterApplyShader != nullptr;
			if (useFroxelVolumetrics)
				RenderVolumetricScattering();
			else
				RenderVolumetricLighting();
			RenderVolumetricClouds();
			// GPU particles render AFTER the volumetric apply (moved out of
			// RenderTransparent): the apply attenuates beauty pixels by the
			// fog transmittance at the OPAQUE depth, so particles drawn
			// before it were fogged as if they sat on the surface behind
			// them - blizzard-density fog erased nearly every snowflake.
			// ParticleBillboardLit now samples the integration volume at
			// each particle's own depth for self-consistent fogging. After
			// the clouds pass so near-camera particles composite over the
			// cloud layer rather than under it.
			if (g_pEnv && g_pEnv->_particleWorldSystem)
			{
				g_pEnv->_particleWorldSystem->Render(_currentScene, _currentCamera, _beautyRT, _gbuffer.GetDepthBuffer());
			}
			// Aerial perspective runs LAST in the atmospheric chain so it
			// has the final say on distance haze. The existing PostFog
			// shader does its own analytic atmosphere integration that
			// only blends 60% toward the LUT sky colour by default, which
			// leaves a visible mismatch at the horizon. AP fixes that by
			// modulating beauty with the same Hillaire LUT data the sky
			// is rendered from, so opaque geometry fades smoothly into
			// the sky at the silhouette. Transparents have already
			// composited above by this point - they don't participate in
			// AP for now (correct per-layer AP would need MRT depth
			// peeling); acceptable v1 limit.
			RenderAerialPerspective();

			// IBL atlases are per-scene, not per-camera: generating them again
			// for each of a probe's six capture faces is pure waste, and running
			// the probe prefilter while a capture is in flight risks binding a
			// probe atlas as a render target mid-capture.
			if (_currentCamera == nullptr || !_currentCamera->IsEnvironmentCapture())
			{
				// Prefiltered sky environment atlas. Runs regardless of SSR: the
				// deferred IBL term consumes it even when nothing screen-space
				// reflects.
				RenderSkyEnvMap();

				// Prefilter any reflection probe whose 6-face capture just finished.
				RenderProbeEnvMaps();

				// Linear-unit atlas dump - see the HVar comment. Placed after both
				// prefilters so the files reflect this frame's content.
				if (r_iblDumpAtlases._val.b)
				{
					r_iblDumpAtlases._val.b = false;
					if (_iblSkyEnvMap != nullptr)
					{
						try { _iblSkyEnvMap->SaveToFile(fs::path("ibl_sky_atlas.png")); }
						catch (const std::exception& e) { LOG_WARN("ibl dump: sky atlas failed: %s", e.what()); }
					}
					if (_activeProbe != nullptr && _activeProbe->GetEnvAtlas() != nullptr)
					{
						try { _activeProbe->GetEnvAtlas()->SaveToFile(fs::path("ibl_probe_atlas.png")); }
						catch (const std::exception& e) { LOG_WARN("ibl dump: probe atlas failed: %s", e.what()); }
					}
					if (_activeProbe != nullptr && _activeProbe->GetFace(0) != nullptr)
					{
						try { _activeProbe->GetFace(0)->SaveToFile(fs::path("ibl_probe_face0.png")); }
						catch (const std::exception& e) { LOG_WARN("ibl dump: probe face failed: %s", e.what()); }
					}
					LOG_INFO("r_iblDumpAtlases: wrote ibl_sky_atlas.png / ibl_probe_atlas.png / ibl_probe_face0.png");
				}
			}

			// don't bother doing this if we don't need to, its expensive!
			// WillRenderSSR() carries the DidAnyDrawnItemReflect check along with
			// the camera/cvar ones, and is the same predicate the deferred pass
			// consults when deciding whether to keep its environment specular.
			if (WillRenderSSR())
				RenderSSR();

			// DLSS is itself a temporal resolver and consumes the jittered, un-resolved
			// frame. Running our TAA as well meant two accumulators fighting over the same
			// history, which costs sharpness for nothing.
			const bool dlssActive =
				_currentCamera->IsDLSSEnabled() &&
				g_pEnv->_streamlineProvider != nullptr &&
				g_pEnv->_streamlineProvider->IsEnabled();

			// Environment captures skip TAA for the same reason they skip SSR:
			// one-shot render, no history of their own, and resolving would
			// blend the capture against the MAIN camera's history buffer.
			const bool isEnvCapture = _currentCamera != nullptr && _currentCamera->IsEnvironmentCapture();

			if (r_taa._val.b && !dlssActive && !isEnvCapture)
			{
				_taa.Resolve(_beautyRT, _beautyRT, _gbuffer.GetVelocity(), _gbuffer.GetNormal(), g_pEnv->GetUIManager().GetRenderer());
				_temporalAaAppliedThisFrame = true;
			}
			else
			{
				_temporalAaAppliedThisFrame = dlssActive;
			}

			// Interaction look-at outline glow. Runs before bloom so the SDF ring
			// picks up a soft bloom halo. No-op when nothing is focused.
			GFX_PERF_BEGIN(0xFFFFFFFF, L"Outline glow");
			{
				RenderOutlineGlow();
			}
			GFX_PERF_END();

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
			// Mirror the other call site's r_useGIAO sibling pass.
			if (r_useGIAO._val.b && g_pEnv->_giAOProvider != nullptr)
			{
				g_pEnv->_giAOProvider->ApplyAmbientOcclusion(
					_currentCamera,
					_gbuffer.GetDepthBuffer(),
					nullptr/*_gbuffer.GetNormal()*/,
					_beautyRT);
			}

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

					// Environment captures take a tonemapped SCALING blit of beauty
					// into their own (smaller) render target instead of the overlay
					// chain. RenderOverlays ping-pongs with `renderTarget->CopyTo(beauty)`
					// between the camera RT and the shared full-res beauty buffer,
					// and CopyResource requires identical dimensions - a 256px
					// capture RT against 3840x2071 buffers is an immediate D3D11
					// RESOURCE_MANIPULATION error. A fullscreen quad rescales, so
					// the capture gets the finished frame at its own resolution.
					// Post effects that only exist inside the overlay chain
					// (vignette, colour grading, DoF) are intentionally absent from
					// probe captures - reflections shouldn't carry lens effects.
					if (_currentCamera->IsEnvironmentCapture())
					{
						// Region copy, not a fullscreen blit. A capture camera has a
						// small viewport but renders into the SHARED full-resolution
						// beauty buffer, so it only fills the top-left corner of it -
						// a fullscreen quad would sample the whole buffer and hand the
						// probe mostly stale main-camera content (first attempt did
						// exactly that: every roughness row of the probe atlas came
						// out a uniform cream, structureless even in the mirror row).
						//
						// Copying the rendered rect keeps source and destination the
						// same size, which is what CopyResource requires, and takes
						// the LINEAR HDR beauty before tonemapping - better probe
						// radiance than a tonemapped LDR frame would be.
						GFX_PERF_BEGIN(0xFFFFFFFF, L"EnvCapture Copy");
						const auto& capVp = _currentCamera->GetViewport();
						// The capture rasterizes at the FULL buffer size (it has to -
						// the fullscreen passes key their gbuffer UVs off the
						// viewport size), but a probe face is the centred SQUARE of
						// that view: with a 90-degree vertical FOV, the central
						// height x height crop is exactly the 90x90 face the
						// prefilter reconstructs. Take that square out of the middle
						// and land it at the origin of the camera's square target.
						const LONG capW = (LONG)capVp.width;
						const LONG capH = (LONG)capVp.height;
						const LONG side = std::min(capW, capH);
						const LONG originX = (capW - side) / 2;
						const LONG originY = (capH - side) / 2;

						RECT region{ originX, originY, originX + side, originY + side };
						RECT destRegion{ 0, 0, side, side };

						// Capture-chain dump, source side. The probe's own dump
						// shows what the rig camera's target ended up with; this
						// shows what beauty and the gbuffer held at the moment we
						// copied, which separates "the capture never rendered the
						// room" from "the copy took the wrong pixels".
						if (r_iblProbeDumpCapture._val.b)
						{
							static int32_t sDumpIdx = 0;
							const std::string tag = std::to_string(sDumpIdx++);
							LOG_INFO("probe dump %s: beauty %dx%d, gbufDiffuse %dx%d, capture viewport %.0fx%.0f",
								tag.c_str(),
								(int32_t)_beautyRT->GetWidth(), (int32_t)_beautyRT->GetHeight(),
								_gbuffer.GetDiffuse() != nullptr ? (int32_t)_gbuffer.GetDiffuse()->GetWidth() : -1,
								_gbuffer.GetDiffuse() != nullptr ? (int32_t)_gbuffer.GetDiffuse()->GetHeight() : -1,
								capVp.width, capVp.height);
							try { _beautyRT->SaveToFile(fs::path("probe_dump_beauty_" + tag + ".png")); }
							catch (const std::exception& e) { LOG_WARN("probe dump: beauty %s failed: %s", tag.c_str(), e.what()); }
							if (_gbuffer.GetDiffuse() != nullptr)
							{
								try { _gbuffer.GetDiffuse()->SaveToFile(fs::path("probe_dump_gbuf_" + tag + ".png")); }
								catch (const std::exception& e) { LOG_WARN("probe dump: gbuf %s failed: %s", tag.c_str(), e.what()); }
							}
						}

						_beautyRT->CopyTo(_currentCamera->GetRenderTarget(), region, destRegion);
						GFX_PERF_END();
					}
					else
					{
						GFX_PERF_BEGIN(0xFFFFFFFF, L"RenderOverlays");
						{
							g_pEnv->_graphicsDevice->SetRenderTarget(_currentCamera->GetRenderTarget());
							RenderOverlays(flags, _beautyRT, _currentCamera->GetRenderTarget());
						}
						GFX_PERF_END();
					}
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

			// Skip FXAA when a temporal resolver already ran this frame. TAA defaults on, so
			// the two were stacking every frame: FXAA then blurs edges TAA had already
			// resolved, and it does so pre-tonemap on unbounded linear HDR where its fixed
			// relative thresholds behave inconsistently across the exposure range. Turning
			// r_taa off still gives you FXAA as the fallback AA.
			if (r_fxaa._val.i32 && canPostProcess && !_temporalAaAppliedThisFrame)
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
			}

			// Sky environment atlas overlay - native 1:1 so the roughness rows
			// (mirror at the top, rough at the bottom) can be inspected without
			// filtering. Deliberately outside r_debugScene: it's the one image
			// needed to verify the IBL prefilter.
			ITexture2D* debugAtlas = nullptr;
			bool debugIsFace = false;
			if (r_iblSkyEnvDebug._val.i32 == 1)
				debugAtlas = _iblSkyEnvMap;
			else if (r_iblSkyEnvDebug._val.i32 == 2 && _activeProbe != nullptr)
				debugAtlas = _activeProbe->GetEnvAtlas();
			else if (r_iblSkyEnvDebug._val.i32 >= 3 && _activeProbe != nullptr)
			{
				// Raw capture face, unprefiltered. If this doesn't look like the room
				// the probe sits in, the problem is the capture (rig framing /
				// position), not the prefilter.
				debugAtlas = _activeProbe->GetFace(
					std::clamp(r_iblSkyEnvDebug._val.i32 - 3, 0, 5));
				debugIsFace = true;
			}

			// Cluster occupancy heatmap (Phase 2). Drawn from the same debug
			// overlay block as the env atlases; RenderDebug dispatches the
			// heatmap compute against this frame's cull results first.
			if (r_clusterDebug._val.b && r_clusterLights._val.b && canPostProcess &&
				_clusteredLights.GetDebugTexture() != nullptr)
			{
				_clusteredLights.RenderDebug(_gbuffer.GetNormal());
				guiRenderer->FillTexturedQuad(_clusteredLights.GetDebugTexture(),
					10, 170, 960, 540, math::Color(1, 1, 1, 1));
			}

			// Shadow-atlas overlay (slice 7): the raw depth atlas, scaled down.
			// Occupied tiles show their depth silhouettes; free tiles stay
			// flat. The copy target is lazy - 64 MB nobody pays for until the
			// cvar flips - and CopyResource is legal because R32_TYPELESS and
			// R32_FLOAT are copy-compatible at identical dimensions.
			if (r_shadowAtlasDebug._val.b && r_shadowAtlas._val.b && canPostProcess &&
				_shadowAtlas.GetAtlasSrv() != nullptr)
			{
				if (_shadowAtlasDebugTex == nullptr)
				{
					_shadowAtlasDebugTex = g_pEnv->_graphicsDevice->CreateTexture2D(
						ShadowAtlas::kAtlasSize, ShadowAtlas::kAtlasSize,
						DXGI_FORMAT_R32_FLOAT, 1,
						D3D11_BIND_SHADER_RESOURCE,
						0, 1, 0, nullptr, (D3D11_CPU_ACCESS_FLAG)0,
						D3D11_RTV_DIMENSION_UNKNOWN,
						D3D11_UAV_DIMENSION_UNKNOWN,
						D3D11_SRV_DIMENSION_TEXTURE2D);
					if (_shadowAtlasDebugTex != nullptr)
						_shadowAtlasDebugTex->SetDebugName("_shadowAtlasDebugTex");
				}
				if (_shadowAtlasDebugTex != nullptr)
				{
					if (auto* rawCtx = reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext()))
					{
						ID3D11Resource* atlasRes = nullptr;
						_shadowAtlas.GetAtlasSrv()->GetResource(&atlasRes);
						if (atlasRes != nullptr)
						{
							rawCtx->CopyResource(
								reinterpret_cast<ID3D11Resource*>(_shadowAtlasDebugTex->GetNativePtr()),
								atlasRes);
							atlasRes->Release();
						}
					}
					guiRenderer->FillTexturedQuad(_shadowAtlasDebugTex,
						10, 170, 540, 540, math::Color(1, 1, 1, 1));
				}
			}

			if (debugAtlas != nullptr && canPostProcess)
			{
				guiRenderer->FillTexturedQuad(
					debugAtlas, 10, 10,
					debugIsFace ? 512 : kIblEnvMapFaceSize,
					debugIsFace ? 512 : kIblEnvMapFaceSize * kIblEnvMapRows,
					math::Color(1, 1, 1, 1));

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
		// Clustered light list build. Runs before any lighting so a consumer -
		// this pass, the froxel volume, forward transparents - can read the
		// lists; today the heatmap is the only reader. Main camera only: the
		// grid is sized to one view and probe-capture faces don't need it.
		if (r_clusterLights._val.b &&
			_currentScene != nullptr && _currentCamera == _currentScene->GetMainCamera())
		{
			_clusteredLights.UpdateAndCull(_currentScene, _currentCamera, _shadowCasters,
				r_shadowAtlas._val.b ? &_shadowAtlas : nullptr);
		}

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
		// Clustered apply: every unshadowed local light in one fullscreen draw,
		// additively into the accumulation buffer the per-light passes also
		// target. Runs before them so the frame composes identically whichever
		// path a light takes.
		if (r_clusterApply._val.b && r_clusterLights._val.b && _clusterApplyShader != nullptr &&
			_currentScene != nullptr && _currentCamera == _currentScene->GetMainCamera())
		{
			if (auto guiRenderer = g_pEnv->GetUIManager().GetRenderer(); guiRenderer != nullptr)
			{
				GFX_PERF_BEGIN(0xFFFFFFFF, L"Clustered Light Apply");
				guiRenderer->StartFrame();
				g_pEnv->_graphicsDevice->SetRenderTarget(_lightAccumulationBuffer);
				g_pEnv->_graphicsDevice->SetViewport(*_currentCamera->GetViewport().Get11());
				g_pEnv->_graphicsDevice->SetBlendState(BlendState::Additive);

				g_pEnv->_graphicsDevice->UnbindAllPixelShaderResources();
				_gbuffer.BindAsShaderResource();      // t0..t4 via the auto counter
				_clusteredLights.BindApply();         // t21..t23 + b5, raw

				// Slice 7: atlas depth + per-tile matrices at t24/t25, raw.
				// Null when the atlas is off - the shader gates on params.w.
				if (auto* rawCtx = reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext()))
				{
					ID3D11ShaderResourceView* atlasSrvs[2] = {
						r_shadowAtlas._val.b ? _shadowAtlas.GetAtlasSrv() : nullptr,
						r_shadowAtlas._val.b ? _clusteredLights.GetTileVpSrv() : nullptr };
					rawCtx->PSSetShaderResources(24, 2, atlasSrvs);
				}

				guiRenderer->FullScreenTexturedQuad(nullptr, _clusterApplyShader.get());

				_clusteredLights.UnbindApply();
				if (auto* rawCtx = reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext()))
				{
					ID3D11ShaderResourceView* atlasNulls[2] = { nullptr, nullptr };
					rawCtx->PSSetShaderResources(24, 2, atlasNulls);
				}
				guiRenderer->EndFrame();
				GFX_PERF_END();
			}
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

				// t15 = prefiltered sky environment atlas for image-based lighting
				// (RenderSkyEnvMap's output - octahedral roughness rows, see
				// EnvMapCommon.shader). Explicit slot, like the features RT above,
				// because the cloud-noise binds before this are conditional and the
				// auto-slot counter would land this somewhere else. Null reads as
				// black = no IBL, never a crash.
				g_pEnv->_graphicsDevice->SetTexture2D(15, _iblSkyEnvMap);

				// t16 = the frame's selected reflection probe atlas (see
				// SetupPerFrameBuffer's selection). Null when no captured probe
				// exists - g_probeCenter.w is 0 then, so the shader never reads it.
				g_pEnv->_graphicsDevice->SetTexture2D(16,
					_activeProbe != nullptr ? _activeProbe->GetEnvAtlas() : nullptr);
				// t17 = second-nearest probe, for the cross-fade.
				g_pEnv->_graphicsDevice->SetTexture2D(17,
					_activeProbe2 != nullptr ? _activeProbe2->GetEnvAtlas() : nullptr);
				// t18 = sky SH irradiance coefficients (P1-C).
				g_pEnv->_graphicsDevice->SetTexture2D(18, _iblSkySH);
				// t19/t20 = per-probe SH irradiance for the two selected probes.
				g_pEnv->_graphicsDevice->SetTexture2D(19,
					_activeProbe != nullptr ? _activeProbe->GetShTex() : nullptr);
				g_pEnv->_graphicsDevice->SetTexture2D(20,
					_activeProbe2 != nullptr ? _activeProbe2->GetShTex() : nullptr);
				// t21 = DFG table (P1-B). Null until the first frame generates it,
				// which the shader detects and falls back to the analytic fit for.
				g_pEnv->_graphicsDevice->SetTexture2D(21, _dfgLut);
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

		const auto& cameraPos = _currentCamera->GetEntity()->GetPosition();

		// Sort the light list by camera distance ascending and truncate to the
		// configured cap. Distant lights are cheap-to-skip here (one
		// LengthSquared per light) and expensive-to-keep further down (each
		// rendered light pays for a full screen-space sphere pass + a per-pixel
		// volumetric ray-march). Cull instead of clamping the GPU cost.
		std::sort(pointLights.begin(), pointLights.end(),
			[&cameraPos](PointLight* a, PointLight* b)
			{
				const float da = (a->GetEntity()->GetPosition() - cameraPos).LengthSquared();
				const float db = (b->GetEntity()->GetPosition() - cameraPos).LengthSquared();
				return da < db;
			});
		const size_t maxLights = static_cast<size_t>(std::max(1, r_maxPointLights._val.i32));
		if (pointLights.size() > maxLights)
			pointLights.resize(maxLights);

		// Split into inside-volume / outside-volume lists so each can run with
		// the right culling mode (inside the bounding sphere the front faces
		// are behind the near plane, so we rasterise the back faces instead).
		// Same pattern as RenderSpotLights.
		std::vector<PointLight*> insideVolume, outsideVolume;
		for (auto* light : pointLights)
		{
			const auto& lightPos = light->GetEntity()->GetWorldTM().Translation();
			if ((cameraPos - lightPos).Length() <= light->GetRadius())
				insideVolume.push_back(light);
			else
				outsideVolume.push_back(light);
		}

		g_pEnv->_graphicsDevice->SetRenderTarget(_lightAccumulationBuffer);

		auto renderer = _sphereEntity->GetComponent<StaticMeshComponent>();
		renderer->SetMaterial(_pointLightMaterial);
		auto mesh = renderer->GetMesh();
		auto instance = mesh->GetInstance();

		// One draw per light, mirroring RenderSpotLights. The previous batched
		// DrawIndexedInstanced couldn't bind per-light shadow maps or per-light
		// PerShadowCasterBuffer state (all instances shared whatever was bound
		// last) - which is why the per-pixel path never had point shadows. Per-
		// light dispatch costs N small draws of a low-poly sphere; N is capped
		// at r_maxPointLights so the fixed overhead stays bounded.
		int32_t numPointLightsRendered = 0;

		auto renderLightList = [&](const std::vector<PointLight*>& lights, CullingMode cullMode)
		{
			for (auto* light : lights)
			{
				// Clustered apply owns unshadowed lights when active.
				if (r_clusterApply._val.b && r_clusterLights._val.b &&
					std::find(_shadowCasters.begin(), _shadowCasters.end(),
						static_cast<Light*>(light)) == _shadowCasters.end())
					continue;

				const auto& diffuse = light->GetDiffuseColour();
				if (diffuse.w <= 0.0f)
					continue;

				auto lightEnt = light->GetEntity();
				const auto& lightPos = lightEnt->GetWorldTM().Translation();
				const float lightRad = light->GetRadius();

				// Per-light pass constants. For a PointLight this writes all 6
				// face view-projection matrices into g_lightViewProjectionMatrix
				// (GetMaxSupportedShadowCascades() == 6) plus castsShadowsFlag -
				// PointLight.shader's CalculatePointShadowSample picks the face
				// from the dominant axis of (pixel - light) and projects through
				// the matching matrix.
				SetupPerShadowCasterBuffer(light, true, 0, numPointLightsRendered, 2, 0.0f);

				_gbuffer.BindAsShaderResource();

				// Bind this light's 6 cube-face depth maps at t5..t10 in engine
				// face order (PointLight::gLightDirs = Forward,Backward,Up,Down,
				// Left,Right = world -Z,+Z,+Y,-Y,-X,+X; SimpleMath is RH so
				// Forward is NEGATIVE Z) - same order as the VP matrix array.
				// Null-bind missing faces so a non-shadow-casting light doesn't
				// sample a stale previous light's depth.
				for (int32_t f = 0; f < 6; ++f)
				{
					auto* shadowMap = light->GetShadowMap(f);
					if (shadowMap != nullptr)
						shadowMap->BindAsShaderResource();
					else
						g_pEnv->_graphicsDevice->SetTexture2D(nullptr);
				}

				// Material-features RT at t11 (gbuffer t0..4, shadowmaps t5..10).
				// Matches SpotLight.shader's layout so both punctual shaders share
				// slot conventions.
				g_pEnv->_graphicsDevice->SetTexture2D(11, _gbuffer.GetFeatures());

				instance->Start();

				_sphereEntity->SetPosition(lightPos);
				_sphereEntity->SetScale(math::Vector3(lightRad));

				// Units slice 3c: lumens -> candela on the per-light path,
				// matching the clustered gather exactly (a shadowed light
				// moving between paths must not change brightness).
				float pointPassStrength = light->GetLightStrength();
				if (r_physicalLightUnits._val.b)
					pointPassStrength /= 4.0f * 3.14159265f;

				instance->Render(
					_sphereEntity->GetWorldTM(),
					_sphereEntity->GetWorldTMTranspose(),
					_sphereEntity->GetWorldTMPrevTranspose(),
					_sphereEntity->GetWorldTMInvert(),
					diffuse,
					math::Vector2(lightRad, pointPassStrength));

				instance->Finish();

				// RenderMesh resets blend/depth/cull to the material defaults, so
				// the actual draw state is applied AFTER it (same ordering note as
				// RenderSpotLights).
				renderer->RenderMesh(mesh.get(), MeshRenderFlags::MeshRenderNormal, 0);

				g_pEnv->_graphicsDevice->SetCullingMode(cullMode);
				g_pEnv->_graphicsDevice->SetBlendState(BlendState::Additive);
				g_pEnv->_graphicsDevice->SetDepthBufferState(DepthBufferState::DepthNone);

				GFX_PERF_BEGIN(0xFFFFFFFF, L"Begin PointLight");
				g_pEnv->_graphicsDevice->DrawIndexedInstanced(_sphereMesh->GetNumIndices(), 1);
				GFX_PERF_END();

				numPointLightsRendered++;
			}
		};

		renderLightList(insideVolume, CullingMode::FrontFace);
		renderLightList(outsideVolume, CullingMode::BackFace);

		g_pEnv->_graphicsDevice->SetCullingMode(CullingMode::BackFace); // restore culling
	}

#if 1
	void SceneRenderer::RenderSpotLights()
	{
		std::vector<SpotLight*> spotLights;
		if (_currentScene->GetComponents<SpotLight>(spotLights) == false)
			return;

		const auto& cameraPos = _currentCamera->GetEntity()->GetPosition();

		// Closest-N cap (same rationale as RenderPointLights): sort by camera
		// distance ascending and truncate. Spot lights each pay the additional
		// cost of a shadow-map render + per-pass constant upload, so capping
		// here is even more valuable than for point lights.
		std::sort(spotLights.begin(), spotLights.end(),
			[&cameraPos](SpotLight* a, SpotLight* b)
			{
				const float da = (a->GetEntity()->GetWorldTM().Translation() - cameraPos).LengthSquared();
				const float db = (b->GetEntity()->GetWorldTM().Translation() - cameraPos).LengthSquared();
				return da < db;
			});
		const size_t maxLights = static_cast<size_t>(std::max(1, r_maxSpotLights._val.i32));
		if (spotLights.size() > maxLights)
			spotLights.resize(maxLights);

		std::vector<SpotLight*> spotLightsInsideVolume, spotLightOutsideVolume;

		// sort the spot lights into two vectors, one for volumes the camera is inside of, one for outside
		for (auto& comp : spotLights)
		{
			SpotLight* light = (SpotLight*)comp;

			// Clustered apply owns unshadowed lights when active - and, with
			// the atlas on, shadowed spots whose tile holds valid content
			// (those shade in the apply with an atlas term; drawing them here
			// too would double-light).
			if (r_clusterApply._val.b && r_clusterLights._val.b)
			{
				const bool isShadowCaster = std::find(_shadowCasters.begin(), _shadowCasters.end(),
					static_cast<Light*>(light)) != _shadowCasters.end();
				if (!isShadowCaster)
					continue;
				if (r_shadowAtlas._val.b &&
					_shadowAtlas.FindContentTile(static_cast<Light*>(light), 0) >= 0)
					continue;
			}


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

					// Units slice 3c: cone-coupled lumens -> candela, matching
					// the clustered gather.
					float spotPassStrength = light->GetLightStrength();
					if (r_physicalLightUnits._val.b)
					{
						const float cosOuterPass = std::cos(ToRadian(light->GetOuterConeAngle() * 0.5f));
						spotPassStrength /= std::max(2.0f * 3.14159265f * (1.0f - cosOuterPass), 1e-4f);
					}

					instance->Render(
						_sphereEntity->GetWorldTM(),
						_sphereEntity->GetWorldTMTranspose(),
						_sphereEntity->GetWorldTMPrevTranspose(),
						_sphereEntity->GetWorldTMInvert(),
						diffuse,
						math::Vector2(lightRad, spotPassStrength));

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

		// Selection rule matches the deferred path: closest-N by camera
		// distance, capped at min(r_max*Lights cvar, kMaxForward*Lights).
		// Previously this picked scene-iteration order which meant some
		// lights would get direct lighting (deferred) but not volumetric
		// scattering (forward cbuffer), or vice versa - the user saw
		// "some spots scatter, others don't" inconsistency. Sorting
		// once here ensures both consumers see the same closest-N.
		const math::Vector3 cameraPos = (_currentCamera != nullptr && _currentCamera->GetEntity() != nullptr)
			? _currentCamera->GetEntity()->GetPosition()
			: math::Vector3::Zero;

		auto sortByCameraDist = [&cameraPos](auto* a, auto* b) -> bool
		{
			const float da = (a->GetEntity()->GetWorldTM().Translation() - cameraPos).LengthSquared();
			const float db = (b->GetEntity()->GetWorldTM().Translation() - cameraPos).LengthSquared();
			return da < db;
		};

		// Point lights: gather, filter, sort, cap at min(r_maxPointLights, 16).
		std::vector<PointLight*> pointLights;
		if (_currentScene->GetComponents<PointLight>(pointLights))
		{
			// Filter out invalid / zero-alpha lights first so we don't waste
			// slots in the sorted closest-N on lights that wouldn't have
			// rendered anyway.
			pointLights.erase(std::remove_if(pointLights.begin(), pointLights.end(),
				[](PointLight* l)
				{
					if (l == nullptr || l->GetEntity() == nullptr || l->GetEntity()->IsPendingDeletion())
						return true;
					return l->GetDiffuseColour().w <= 0.0f;
				}), pointLights.end());
			std::sort(pointLights.begin(), pointLights.end(), sortByCameraDist);
			const size_t pointCap = std::min<size_t>(
				static_cast<size_t>(std::max(1, r_maxPointLights._val.i32)),
				static_cast<size_t>(kMaxForwardPointLights));

			for (auto* light : pointLights)
			{
				if (pointCount >= pointCap)
					break;
				const auto diffuse = light->GetDiffuseColour();
				const auto pos = light->GetEntity()->GetWorldTM().Translation();
				const float radius = std::max(0.05f, light->GetRadius());
				float strength = std::max(0.0f, light->GetLightStrength() * light->GetLightMultiplier());
				// Units slice 3b: lumens -> candela, matching the clustered gather.
				if (r_physicalLightUnits._val.b)
					strength /= 4.0f * 3.14159265f;

				data.pointPosRadius[pointCount] = math::Vector4(pos.x, pos.y, pos.z, radius);
				data.pointColorStrength[pointCount] = math::Vector4(diffuse.x, diffuse.y, diffuse.z, strength);
				++pointCount;
			}
		}

		// Spot lights: same pattern.
		std::vector<SpotLight*> spotLights;
		if (_currentScene->GetComponents<SpotLight>(spotLights))
		{
			spotLights.erase(std::remove_if(spotLights.begin(), spotLights.end(),
				[](SpotLight* l)
				{
					if (l == nullptr || l->GetEntity() == nullptr || l->GetEntity()->IsPendingDeletion())
						return true;
					return l->GetDiffuseColour().w <= 0.0f;
				}), spotLights.end());
			std::sort(spotLights.begin(), spotLights.end(), sortByCameraDist);
			const size_t spotCap = std::min<size_t>(
				static_cast<size_t>(std::max(1, r_maxSpotLights._val.i32)),
				static_cast<size_t>(kMaxForwardSpotLights));

			for (auto* light : spotLights)
			{
				if (spotCount >= spotCap)
					break;
				auto* lightEnt = light->GetEntity();
				const auto diffuse = light->GetDiffuseColour();
				const auto pos = lightEnt->GetWorldTM().Translation();
				const auto fwd = lightEnt->GetWorldTM().Forward();
				const float radius = std::max(0.05f, light->GetRadius());
				float strength = std::max(0.0f, light->GetLightStrength() * light->GetLightMultiplier());
				const float outerAngle = std::max(0.1f, light->GetOuterConeAngle());
				const float innerAngle = std::clamp(light->GetInnerConeAngle(), 0.0f, outerAngle);
				const float cosOuter = std::cos(ToRadian(outerAngle * 0.5f));
				const float cosInner = std::cos(ToRadian(innerAngle * 0.5f));
				// Units slice 3b: lumens -> candela, cone-coupled like the clustered gather.
				if (r_physicalLightUnits._val.b)
					strength /= std::max(2.0f * 3.14159265f * (1.0f - cosOuter), 1e-4f);

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

		// Cluster list SRVs for the forward material shaders (t27..t29, raw -
		// no engine API for PS structured buffers). Bound for the whole
		// transparent pass and nulled at the end; the shaders gate on
		// g_clusterForwardActive so a null bind is also safe.
		const bool clusterForwardBind =
			r_clusterForward._val.b && r_clusterLights._val.b &&
			_currentScene != nullptr && _currentCamera == _currentScene->GetMainCamera();
		if (clusterForwardBind)
		{
			if (auto* ctx = reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext()))
			{
				ID3D11ShaderResourceView* clSrvs[3] = {
					_clusteredLights.GetLightsSrv(),
					_clusteredLights.GetCountsSrv(),
					_clusteredLights.GetListsSrv() };
				ctx->PSSetShaderResources(27, 3, clSrvs);
			}
		}

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
		// t14 = prefiltered sky environment atlas. The transparency path reflects
		// this wherever its screen-space march misses; a null bind reads black,
		// which is the old (broken) behaviour rather than a crash.
		g_pEnv->_graphicsDevice->SetTexture2D(14, _iblSkyEnvMap);

		// t15..t20 = sun shadow cascades (slice 5), plus the b2 caster constants
		// re-uploaded for the sun: the deferred per-light passes left b2 holding
		// the LAST point/spot light's data, so without this re-setup the shader
		// would project sun cascades through a spot light's matrices. The
		// shaders gate on g_taaParams.z, which SetupPerFrameBuffer set from the
		// same FindTransparentShadowSun + main-camera condition.
		DirectionalLight* transparentSun =
			(_currentScene != nullptr && _currentCamera == _currentScene->GetMainCamera())
				? FindTransparentShadowSun(_currentScene)
				: nullptr;
		if (transparentSun != nullptr)
		{
			for (int32_t i = 0; i < 6; ++i)
			{
				auto shadowMap = i < transparentSun->GetMaxSupportedShadowCascades()
					? transparentSun->GetShadowMap(i)
					: nullptr;
				g_pEnv->_graphicsDevice->SetTexture2D(15 + i,
					shadowMap != nullptr ? shadowMap->GetDepthMap() : nullptr);
			}
			SetupPerShadowCasterBuffer(transparentSun, false, 0, 0, r_shadowSamples._val.i32, 0.0f);
		}

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
		g_pEnv->_graphicsDevice->SetTexture2D(14, nullptr);
		if (transparentSun != nullptr)
		{
			// The cascade depth maps become DSVs again next frame; a stale SRV
			// bind here is a guaranteed debug-layer hazard warning.
			for (int32_t i = 0; i < 6; ++i)
				g_pEnv->_graphicsDevice->SetTexture2D(15 + i, nullptr);
		}
		g_pEnv->_graphicsDevice->SetBoundResourceIndex(postMaterialIndex);

		// NOTE: GPU particles no longer render here. They moved AFTER the
		// volumetric apply pass (see the call following
		// RenderVolumetricScattering in the composite flow): the fullscreen
		// apply attenuates every beauty pixel by the fog transmittance at
		// the OPAQUE gbuffer depth, so particles rendered before it were
		// fogged as if they sat on whatever surface was BEHIND them - a
		// snowflake 5m away in front of a 60m-distant building inherited
		// exp(-sigma*60). In blizzard-density fog that erased nearly every
		// flake on screen. Particles now fog themselves at their own depth
		// in ParticleBillboardLit.shader instead.

		if (_waterRT)
		{
			_waterRT->CopyTo(_beautyRT);
		}

		if (clusterForwardBind)
		{
			if (auto* ctx = reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext()))
			{
				ID3D11ShaderResourceView* clNulls[3] = { nullptr, nullptr, nullptr };
				ctx->PSSetShaderResources(27, 3, clNulls);
			}
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

	void SceneRenderer::RenderAerialPerspective()
	{
		// Skip if any required resource is missing. Atmosphere LUT
		// subsystem failure (no compute support, shader compile fail
		// etc.) cleanly disables AP - the scene just renders without
		// distance haze, no error.
		if (_aerialPerspectiveApplyShader == nullptr ||
			_beautyRT == nullptr ||
			_subsurfaceIntermediateRT == nullptr ||
			g_pEnv->_atmosphereLUTs == nullptr)
		{
			return;
		}
		auto* apVolume = g_pEnv->_atmosphereLUTs->GetAerialPerspectiveVolume();
		if (apVolume == nullptr)
			return;

		PROFILE();

		auto* graphics = g_pEnv->_graphicsDevice;
		auto* guiRenderer = g_pEnv->GetUIManager().GetRenderer();
		if (guiRenderer == nullptr)
			return;

		// Two-RT pattern (same as SSS / Bokeh DoF): read beauty + AP
		// volume, write to intermediate, copy intermediate back to beauty.
		// _subsurfaceIntermediateRT is matched in format to _beautyRT and
		// has finished its use by the time we run.
		guiRenderer->StartFrame();
		graphics->SetRenderTarget(_subsurfaceIntermediateRT);

		// t0..t4: gbuffer (matches the GBUFFER_RESOURCE macro the apply
		// shader uses; needed for the sky-pixel guard). BindAsShaderResource
		// uses the slotless path and leaves the implicit-slot counter at 5.
		_gbuffer.BindAsShaderResource();
		// t5: beauty as source. Slotted bind advances counter to 6.
		graphics->SetTexture2D(5, _beautyRT);
		// t6: AP 3D volume. Engine has no PS-specific SetTexture3D(slot,...)
		// today, so we rely on the slotless variant landing at the
		// implicit-slot counter (now 6) - same trick the cloud noise
		// volumes use during the directional-light pass.
		graphics->SetTexture3D(apVolume);
		// t7: SkyView LUT. The apply shader samples this and lerps the
		// AP composite toward it for distant pixels so geometry silhouettes
		// dissolve cleanly into the sky behind them.
		graphics->SetTexture2D(7, g_pEnv->_atmosphereLUTs->GetSkyViewLUT());

		guiRenderer->FullScreenTexturedQuad(nullptr, _aerialPerspectiveApplyShader.get());
		guiRenderer->EndFrame();

		// Copy result back into beauty.
		_subsurfaceIntermediateRT->CopyTo(_beautyRT);

		// Unbind to keep state clean for the next pass. SetBoundResourceIndex
		// reset so downstream slotless binds start at 0 again - matches the
		// same hygiene as the sky pass.
		graphics->SetTexture2D(5, nullptr);
		graphics->SetTexture2D(7, nullptr);
		graphics->SetTexture3D(nullptr);
		graphics->SetBoundResourceIndex(0);
	}

	void SceneRenderer::RenderVolumetricScattering()
	{
		// Bail out cleanly when any required resource is missing. Callers
		// gate the legacy RenderVolumetricLighting on this method's success
		// via the same null-checks, so a partial setup naturally falls back
		// to the old per-pixel march.
		if (_volumetricScatterApplyShader == nullptr ||
			_beautyRT == nullptr ||
			_subsurfaceIntermediateRT == nullptr ||
			g_pEnv->_volumetricScattering == nullptr)
		{
			return;
		}
		auto* integrationVolume = g_pEnv->_volumetricScattering->GetIntegrationVolume();
		if (integrationVolume == nullptr)
			return;

		PROFILE();

		auto* graphics = g_pEnv->_graphicsDevice;
		auto* guiRenderer = g_pEnv->GetUIManager().GetRenderer();
		if (guiRenderer == nullptr)
			return;

		// Same two-RT swap pattern as AP apply / SSS / Bokeh DoF. Read
		// beauty + gbuffer + volume, write to the SSS intermediate, copy
		// back to beauty.
		guiRenderer->StartFrame();
		graphics->SetRenderTarget(_subsurfaceIntermediateRT);

		// t0..t4 = gbuffer (via slotless BindAsShaderResource, leaves
		// implicit-slot counter at 5).
		_gbuffer.BindAsShaderResource();
		// t5 = beauty as the read source. Slotted bind advances counter to 6.
		graphics->SetTexture2D(5, _beautyRT);
		// t6 = volumetric integration 3D volume. Engine's slotless SetTexture3D
		// uses the implicit counter (now 6) - same trick as the AP apply pass.
		graphics->SetTexture3D(integrationVolume);

		guiRenderer->FullScreenTexturedQuad(nullptr, _volumetricScatterApplyShader.get());
		guiRenderer->EndFrame();

		_subsurfaceIntermediateRT->CopyTo(_beautyRT);

		// Cleanup - same hygiene pattern as AP apply.
		graphics->SetTexture2D(5, nullptr);
		graphics->SetTexture3D(nullptr);
		graphics->SetBoundResourceIndex(0);
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

		// We do work in this pass if EITHER (a) there are renderable manual decals
		// OR (b) the auto-puddle system is enabled. CRITICAL: do NOT early-out on
		// "scene has zero DecalComponents" before evaluating the auto-puddle branch -
		// auto-puddles is meant to work in scenes that have never had a single
		// manual decal placed, and bailing out on an empty decal list here would
		// make the feature appear broken whenever no manual decal happens to exist.
		std::vector<DecalComponent*> decals;
		_currentScene->GetComponents<DecalComponent>(decals); // may return false; either way `decals` is the list (possibly empty)

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
			// Rate-limited "I am running" log so the user can sanity-check whether
			// the auto-puddle pass is even being entered without needing PIX. Hits
			// the log roughly every 2 seconds at 60 fps.
			{
				static int sAutoPuddleLogCounter = 0;
				if ((sAutoPuddleLogCounter++ % 120) == 0)
				{
					LOG_INFO("AutoPuddles pass running (forceRain=%.2f, debugSolid=%d)",
						r_autoPuddlesForceRain._val.f32,
						r_autoPuddlesDebugSolid._val.b ? 1 : 0);
				}
			}

			// Bind the live GBuffer normal RT as SRV at t1 (we don't write to it
			// during the decal/auto-puddle pass so reading is legal). The auto-
			// puddle PS samples it for the per-pixel flatness test.
			graphics->SetTexture2D(1, _gbuffer.GetNormal());
			// Shelter occlusion map at t2 - sheltered floors collect no
			// puddles (see PuddleShelter in AutoPuddles.shader).
			graphics->SetTexture2D(2,
				(_rainOcclusionValid && _rainOcclusionMap != nullptr)
					? _rainOcclusionMap->GetDepthMap() : nullptr);

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
				r_autoPuddlesDebugSolid._val.b ? 1.0f : 0.0f,
				0.0f);
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

		// Unbind only the DoF params cbuffer. We deliberately do NOT unbind the
		// source SRVs here: DrawIndexed already calls UnbindAllPixelShaderResources
		// (clearing them AND resetting the auto-slot counter to 0). The ORIGINAL
		// bug was calling SetTexture2D(0,nullptr)/SetTexture2D(1,nullptr) AFTER the
		// draw - those are explicit-slot binds, so they bumped the auto-slot
		// counter from the clean 0 back up to 2. The next overlay pass's
		// single-arg SetTexture2D(beauty) then landed at t2 while its shader reads
		// t0 -> it sampled nothing, output grey, and that grey was copied over
		// beauty for every remaining pass (the grey screen). Removing them is the fix.
		graphics->SetConstantBufferPS(6, nullptr);

		// Restore the overlay chain's render target. DrawIndexed resets SRVs but
		// does NOT touch the render target, so our scratch RT is still bound here.
		// RenderOverlays renders the rest of the chain (grading, vignette, tonemap)
		// into the camera RT and propagates back to beauty via CopyTo; leaving the
		// scratch RT bound corrupts that.
		graphics->SetRenderTarget(_currentCamera->GetRenderTarget());
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

	void SceneRenderer::RenderOutlineGlow()
	{
		auto* focused = InteractionComponent::GetFocused();
		if (focused == nullptr || focused->GetEntity() == nullptr)
			return;
		if (_outlineSeedShader == nullptr || _outlineJfaShader == nullptr || _outlineCompositeShader == nullptr ||
			_outlineJfaA == nullptr || _outlineJfaB == nullptr || _outlineGlowRT == nullptr ||
			_outlineParamsBuffer == nullptr || _currentCamera == nullptr)
			return;

		Entity* ent = focused->GetEntity();
		auto* smc = ent->GetComponent<StaticMeshComponent>();
		if (smc == nullptr)
			return;
		auto mesh = smc->GetMesh();
		if (mesh == nullptr || mesh->GetInstance() == nullptr)
			return;

		auto* graphics = g_pEnv->_graphicsDevice;

		// Viewport sized to the (render-resolution) outline targets.
		const auto& camvp = _currentCamera->GetViewport();
		D3D11_VIEWPORT vp;
		vp.TopLeftX = 0; vp.TopLeftY = 0;
		vp.Width = camvp.width; vp.Height = camvp.height;
		vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;

		const float thickness = std::max(1.0f, focused->GetOutlineThickness());
		const math::Color glow = focused->GetHighlightColour();

		// b5 params: float4 colour + (thickness, jumpStep, pad, pad) = 32 bytes.
		struct OutlineParams { math::Vector4 colour; float thickness; float jumpStep; float pad0; float pad1; } params;
		params.colour    = math::Vector4(glow.x, glow.y, glow.z, 1.0f);
		params.thickness = thickness;
		params.jumpStep  = 0.0f;
		params.pad0 = 0.0f; params.pad1 = 0.0f;

		// 1. Seed pass: silhouette of the focused mesh into JFA-A. Every covered
		// pixel writes its own absolute screen coordinate; the rest stay at the
		// cleared (-1,-1) sentinel. No depth -> full, unoccluded silhouette.
		graphics->SetRenderTarget(_outlineJfaA);
		_outlineJfaA->ClearRenderTargetView(math::Color(-1.0f, -1.0f, 0.0f, 0.0f));
		graphics->SetViewport(vp);

		auto instance = mesh->GetInstance();
		instance->Start();
		instance->Render(
			ent->GetWorldTM(), ent->GetWorldTMTranspose(), ent->GetWorldTMPrevTranspose(),
			ent->GetWorldTMInvert(), math::Vector4(1, 1, 1, 1), math::Vector2(1, 1));
		instance->Finish();

		// RenderMesh binds the mesh VB/IB + per-object cbuffer (correct world);
		// we then swap in the silhouette/seed shader and draw (same override
		// pattern as RenderPointLights).
		smc->RenderMesh(mesh.get(), MeshRenderFlags::MeshRenderNormal, 0);
		graphics->SetVertexShader(_outlineSeedShader->GetShaderStage(ShaderStage::VertexShader));
		graphics->SetPixelShader(_outlineSeedShader->GetShaderStage(ShaderStage::PixelShader));
		graphics->SetInputLayout(_outlineSeedShader->GetInputLayout());
		graphics->SetBlendState(BlendState::Opaque);
		graphics->SetDepthBufferState(DepthBufferState::DepthNone);
		graphics->SetCullingMode(CullingMode::BackFace);
		graphics->DrawIndexedInstanced(mesh->GetNumIndices(), 1);

		// 2 + 3: jump-flood propagation then composite, via the GUI fullscreen quad.
		if (auto guiRenderer = g_pEnv->GetUIManager().GetRenderer(); guiRenderer != nullptr)
		{
			guiRenderer->StartFrame();

			ITexture2D* src = _outlineJfaA;
			ITexture2D* dst = _outlineJfaB;

			// Propagate only as far as the outline is wide: start at the smallest
			// power-of-two >= thickness, halve down to 1.
			int32_t startStep = 1;
			while ((float)startStep < thickness) startStep <<= 1;

			for (int32_t step = startStep; step >= 1; step >>= 1)
			{
				params.jumpStep = (float)step;
				_outlineParamsBuffer->Write(&params, sizeof(params));
				graphics->SetConstantBufferPS(5, _outlineParamsBuffer);

				graphics->SetRenderTarget(dst);
				graphics->SetViewport(vp);
				guiRenderer->FullScreenTexturedQuad(src, _outlineJfaShader.get());

				ITexture2D* tmp = src; src = dst; dst = tmp;
			}

			// Composite the distance field into the glow ring.
			params.jumpStep = 0.0f;
			_outlineParamsBuffer->Write(&params, sizeof(params));
			graphics->SetConstantBufferPS(5, _outlineParamsBuffer);

			graphics->SetRenderTarget(_outlineGlowRT);
			_outlineGlowRT->ClearRenderTargetView(math::Color(0, 0, 0, 0));
			graphics->SetViewport(vp);
			guiRenderer->FullScreenTexturedQuad(src, _outlineCompositeShader.get());

			guiRenderer->EndFrame();
		}

		// 4. Additively blend the glow ring into the beauty buffer (before bloom).
		_outlineGlowRT->BlendTo_Additive(_beautyRT);
	}

	void SceneRenderer::RenderSkyEnvMap()
	{
		PROFILE();

		if (!r_iblSkyEnv._val.b)
			return;
		if (_iblSkyEnvShader == nullptr)
			return;
		if (g_pEnv->_atmosphereLUTs == nullptr || g_pEnv->_atmosphereLUTs->GetSkyViewLUT() == nullptr)
			return;

		auto* graphics = g_pEnv->_graphicsDevice;

		// Fixed-size atlas, created lazily and deliberately not part of the
		// resize path.
		if (_iblSkyEnvMap == nullptr)
		{
			_iblSkyEnvMap = graphics->CreateTexture2D(
				kIblEnvMapFaceSize,
				kIblEnvMapFaceSize * kIblEnvMapRows,
				DXGI_FORMAT_R16G16B16A16_FLOAT,
				1,
				D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
				1);
			if (_iblSkyEnvMap == nullptr)
			{
				LOG_WARN("SceneRenderer::RenderSkyEnvMap failed to create the sky environment atlas");
				return;
			}
			_iblSkyEnvMap->SetDebugName("_iblSkyEnvMap");
		}

		// Regenerated every frame by design: the day/night cycle moves the sun
		// continuously and the weather system retints the sky-view LUT, so a
		// sun-direction cache would be invalid most frames anyway and buys a
		// set of invalidation bugs. The draw is 128x640 texels sampling a
		// 192x108 LUT - far below the cost of a single shadow cascade.
		auto guiRenderer = g_pEnv->GetUIManager().GetRenderer();
		if (guiRenderer == nullptr)
			return;

		GFX_PERF_BEGIN(0xFFFFFFFF, L"SkyEnvMap");
		guiRenderer->StartFrame();

		// P1-B: generate the DFG table once. It depends only on NdotV and
		// roughness - no scene, no lighting, no time of day - so regenerating it
		// per frame would be pure waste.
		if (!_dfgLutGenerated && _dfgLutShader != nullptr)
		{
			if (_dfgLut == nullptr)
			{
				_dfgLut = graphics->CreateTexture2D(
					kDfgLutSize, kDfgLutSize,
					DXGI_FORMAT_R16G16B16A16_FLOAT,
					1,
					D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
					1);
				if (_dfgLut != nullptr)
					_dfgLut->SetDebugName("_dfgLut");
			}

			if (_dfgLut != nullptr)
			{
				graphics->SetRenderTarget(_dfgLut);

				D3D11_VIEWPORT dvp;
				dvp.TopLeftX = 0.0f;
				dvp.TopLeftY = 0.0f;
				dvp.Width = (float)kDfgLutSize;
				dvp.Height = (float)kDfgLutSize;
				dvp.MinDepth = 0.0f;
				dvp.MaxDepth = 1.0f;
				graphics->SetViewport(dvp);

				// The source texture is unused by the shader (it integrates
				// analytically); pass the atlas purely to satisfy the quad helper.
				guiRenderer->FullScreenTexturedQuad(_iblSkyEnvMap, _dfgLutShader.get());

				_dfgLutGenerated = true;
				LOG_INFO("Generated %dx%d DFG LUT (split-sum BRDF + single-scatter energy)",
					kDfgLutSize, kDfgLutSize);
			}
		}

		graphics->SetRenderTarget(_iblSkyEnvMap);

		D3D11_VIEWPORT vp;
		vp.TopLeftX = 0.0f;
		vp.TopLeftY = 0.0f;
		vp.Width = (float)kIblEnvMapFaceSize;
		vp.Height = (float)(kIblEnvMapFaceSize * kIblEnvMapRows);
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		graphics->SetViewport(vp);

		// The sky-view LUT rides in as the quad's source texture (t0).
		guiRenderer->FullScreenTexturedQuad(g_pEnv->_atmosphereLUTs->GetSkyViewLUT(), _iblSkyEnvShader.get());

		// P1-C: project the atlas we just built into SH irradiance coefficients.
		// Nine texels, each integrating the atlas's mirror row against one basis
		// function - the cosine-convolved diffuse term the flat ambient constant
		// has been standing in for.
		if (_envSHShader != nullptr)
		{
			if (_iblSkySH == nullptr)
			{
				_iblSkySH = graphics->CreateTexture2D(
					1, kIblEnvShCoeffs,
					DXGI_FORMAT_R16G16B16A16_FLOAT,
					1,
					D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
					1);
				if (_iblSkySH != nullptr)
					_iblSkySH->SetDebugName("_iblSkySH");
			}

			if (_iblSkySH != nullptr)
			{
				graphics->SetRenderTarget(_iblSkySH);

				D3D11_VIEWPORT shvp;
				shvp.TopLeftX = 0.0f;
				shvp.TopLeftY = 0.0f;
				shvp.Width = 1.0f;
				shvp.Height = (float)kIblEnvShCoeffs;
				shvp.MinDepth = 0.0f;
				shvp.MaxDepth = 1.0f;
				graphics->SetViewport(shvp);

				guiRenderer->FullScreenTexturedQuad(_iblSkyEnvMap, _envSHShader.get());
			}
		}

		guiRenderer->EndFrame();

		// Restore the beauty target and camera viewport before returning -
		// leaving a 128-wide viewport bound corrupts whatever pass runs next
		// (same class of bug as the SRV auto-slot counter trap).
		graphics->SetRenderTarget(_beautyRT);
		graphics->SetViewport(*_currentCamera->GetViewport().Get11());
		GFX_PERF_END();
	}

	void SceneRenderer::RenderProbeEnvMaps()
	{
		PROFILE();

		if (_probeEnvShader == nullptr || _currentScene == nullptr)
			return;

		std::vector<ReflectionProbeComponent*> probes;
		if (!_currentScene->GetComponents<ReflectionProbeComponent>(probes))
			return;

		// Manual re-bake trigger. Self-clearing so it reads as a verb rather than
		// a mode - setting it once queues one capture per probe.
		if (r_iblProbeRecapture._val.b)
		{
			r_iblProbeRecapture._val.b = false;
			for (auto* probe : probes)
			{
				if (probe != nullptr)
					probe->RequestCapture();
			}
			LOG_INFO("r_iblProbeRecapture: queued a fresh capture for %d probe(s)", (int32_t)probes.size());
		}

		// One prefilter per frame: it's a bake step triggered by a capture
		// completing, and spreading multiple probes across frames keeps a
		// scene-load recapture burst from hitching.
		ReflectionProbeComponent* dirty = nullptr;
		for (auto* probe : probes)
		{
			if (probe != nullptr && probe->IsAtlasDirty())
			{
				dirty = probe;
				break;
			}
		}
		if (dirty == nullptr)
			return;

		ITexture2D* atlas = dirty->EnsureEnvAtlas();
		if (atlas == nullptr)
			return;

		auto* graphics = g_pEnv->_graphicsDevice;
		auto guiRenderer = g_pEnv->GetUIManager().GetRenderer();
		if (guiRenderer == nullptr)
			return;

		GFX_PERF_BEGIN(0xFFFFFFFF, L"ProbeEnvMap");
		guiRenderer->StartFrame();

		// Explicit face binds at t0..t5 (ProbeEnvMap.shader's register layout).
		for (int32_t i = 0; i < 6; ++i)
			graphics->SetTexture2D(i, dirty->GetFace(i));

		graphics->SetRenderTarget(atlas);

		D3D11_VIEWPORT vp;
		vp.TopLeftX = 0.0f;
		vp.TopLeftY = 0.0f;
		vp.Width = (float)kIblEnvMapFaceSize;
		vp.Height = (float)(kIblEnvMapFaceSize * kIblEnvMapRows);
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		graphics->SetViewport(vp);

		guiRenderer->FullScreenTexturedQuad(dirty->GetFace(0), _probeEnvShader.get());

		guiRenderer->EndFrame();

		// Project this probe's atlas into its own SH irradiance (P1-C, locally).
		// Sky SH is unoccluded and floods interiors; a probe's SH is integrated
		// from what the probe actually sees, so an indoor probe already knows the
		// roof is solid. This is what makes diffuse IBL usable inside a building.
		if (_envSHShader != nullptr)
		{
			if (ITexture2D* probeSH = dirty->EnsureShTex(); probeSH != nullptr)
			{
				graphics->SetRenderTarget(probeSH);

				D3D11_VIEWPORT shvp;
				shvp.TopLeftX = 0.0f;
				shvp.TopLeftY = 0.0f;
				shvp.Width = 1.0f;
				shvp.Height = (float)kIblEnvShCoeffs;
				shvp.MinDepth = 0.0f;
				shvp.MaxDepth = 1.0f;
				graphics->SetViewport(shvp);

				guiRenderer->FullScreenTexturedQuad(atlas, _envSHShader.get());
			}
		}

		dirty->MarkAtlasPrefiltered();
		LOG_INFO("ReflectionProbe: prefiltered atlas + SH for '%s' - probe is now selectable",
			dirty->GetEntity()->GetName().c_str());

		// Restore beauty target + camera viewport (same discipline as
		// RenderSkyEnvMap - a stale 128-wide viewport corrupts the next pass).
		graphics->SetRenderTarget(_beautyRT);
		graphics->SetViewport(*_currentCamera->GetViewport().Get11());
		GFX_PERF_END();
	}

	bool SceneRenderer::WillRenderSSR() const
	{
		if (!r_ssr._val.b)
			return false;

		// Environment-capture cameras (reflection probe faces) never run SSR - see
		// the NRD jitter/buffer-size note in RenderSSR.
		if (_currentCamera == nullptr || _currentCamera->IsEnvironmentCapture())
			return false;

		// Main camera only. SSR + NRD are temporal systems keyed to one camera's
		// history and viewport.
		if (_currentScene == nullptr || _currentCamera != _currentScene->GetMainCamera())
			return false;

		// RenderPostProcessing skips the whole pass when nothing drawn this frame
		// was reflective. Mirrored here so the deferred pass doesn't drop its
		// environment specular for a frame whose resolve never runs.
		if (!_currentScene->DidAnyDrawnItemReflect())
			return false;

		return g_pEnv->GetUIManager().GetRenderer() != nullptr;
	}

	bool SceneRenderer::ShouldComposeEnvSpecularInResolve() const
	{
		return r_iblComposeSSR._val.b && WillRenderSSR();
	}

	void SceneRenderer::CreateSsrTargets(int32_t width, int32_t height)
	{
		// Same format/MSAA derivation as CreateRenderTargets - these were
		// function-locals there, re-derived here so this can also run from
		// RenderSSR when r_ssrHalfRes flips at runtime.
		const auto MsaaLevel = g_pEnv->_graphicsDevice->GetCurrentMSAALevel();
		const DXGI_FORMAT BEAUTY_FORMAT = HexEngine::detail::ShimToDxgiFormat(g_pEnv->_graphicsDevice->GetDesiredBackBufferFormat());

		SAFE_DELETE(_ssrDiffuseTexture);
		SAFE_DELETE(_ssrDiffuseHitInfo);
		SAFE_DELETE(_ssrTexture);
		SAFE_DELETE(_ssrHitInfo);
		SAFE_DELETE(_ssrHistory);
		SAFE_DELETE(_ssrResolved);
		SAFE_DELETE(_ssrUpDiffuse);
		SAFE_DELETE(_ssrUpDiffuseHit);
		SAFE_DELETE(_ssrUpSpecular);
		SAFE_DELETE(_ssrUpSpecularHit);

		_ssrBaseWidth = width;
		_ssrBaseHeight = height;
		_ssrHalfResActive = r_ssrHalfRes._val.b;

		const int32_t halfW = std::max(1, width / 2);
		const int32_t halfH = std::max(1, height / 2);

		auto makeSsrRT = [&](int32_t w, int32_t h, const char* name) -> ITexture2D*
		{
			ITexture2D* tex = g_pEnv->_graphicsDevice->CreateTexture2D(
				w,
				h,
				BEAUTY_FORMAT,
				1,
				D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
				0, MsaaLevel, 0,
				nullptr,
				(D3D11_CPU_ACCESS_FLAG)0,
				MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
				D3D11_UAV_DIMENSION_UNKNOWN,
				MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D);
			tex->SetDebugName(name);
			return tex;
		};

		// March MRTs: half res when r_ssrHalfRes, full otherwise.
		const int32_t mw = _ssrHalfResActive ? halfW : width;
		const int32_t mh = _ssrHalfResActive ? halfH : height;
		_ssrDiffuseTexture = makeSsrRT(mw, mh, "_ssrDiffuseTexture");
		_ssrDiffuseHitInfo = makeSsrRT(mw, mh, "_ssrDiffuseHitInfo");
		_ssrTexture        = makeSsrRT(mw, mh, "_ssrSpecularTexture");
		_ssrHitInfo        = makeSsrRT(mw, mh, "_ssrSpecularHitInfo");

		// NRD input/output/history are ALWAYS full res - the denoiser's
		// half-res temporal accumulation is what made reflections swim, so
		// half-res mode bridges through the upsampled set below instead.
		_ssrHistory = makeSsrRT(width, height, "_ssrHistory");
		_ssrResolved = makeSsrRT(width, height, "_ssrResolved");

		if (_ssrHalfResActive)
		{
			_ssrUpDiffuse     = makeSsrRT(width, height, "_ssrUpDiffuse");
			_ssrUpDiffuseHit  = makeSsrRT(width, height, "_ssrUpDiffuseHit");
			_ssrUpSpecular    = makeSsrRT(width, height, "_ssrUpSpecular");
			_ssrUpSpecularHit = makeSsrRT(width, height, "_ssrUpSpecularHit");
		}
	}

	void SceneRenderer::RenderSSR()
	{
		PROFILE();

		if (!r_ssr._val.b)
			return;

		// Environment-capture cameras (reflection probe faces) never run SSR. The
		// NRD denoiser's buffers are sized for the main camera, but this camera
		// renders at its own much smaller resolution, and jitter reaches NRD in
		// NDC and is converted back to pixels with the DENOISER's width - so a
		// 256px capture against 3840px buffers turns a +/-0.5px jitter into
		// +/-7.5 and trips NRD's internal range assert. A one-shot capture face
		// also has no temporal history for a denoiser to work with.
		if (_currentCamera != nullptr && _currentCamera->IsEnvironmentCapture())
			return;

		// Main camera only. SSR + NRD are temporal systems keyed to a single
		// camera's history and viewport: running them for a secondary camera
		// (reflection-probe capture rig, the in-game map view) hands NRD jitter
		// computed for that camera's viewport against buffers sized for the main
		// one - which trips NRD's 'cameraJitter must be in [-0.5, 0.5]' assert
		// (crash call stack: RenderSSR -> FilterFrame -> SetCommonSettings) -
		// and would corrupt the main camera's reflection history even where it
		// didn't crash. Secondary views get their reflections from the IBL
		// terms instead.
		if (_currentScene != nullptr && _currentCamera != _currentScene->GetMainCamera())
			return;

		GFX_PERF_BEGIN(0xFFFFFFFF, L"SSR Begin");

		const auto& bbvp = _currentCamera->GetViewport();

		// Recreate the SSR chain when r_ssrHalfRes flips at runtime (or the
		// viewport changed under us - Resize handles the normal path, this is
		// the cvar toggle).
		if (_ssrHalfResActive != r_ssrHalfRes._val.b ||
			_ssrBaseWidth != (int32_t)bbvp.width || _ssrBaseHeight != (int32_t)bbvp.height)
		{
			CreateSsrTargets((int32_t)bbvp.width, (int32_t)bbvp.height);
		}

		_ssrDiffuseTexture->ClearRenderTargetView(math::Color(0, 0, 0, 0));
		_ssrDiffuseHitInfo->ClearRenderTargetView(math::Color(0, 0, 0, 0));
		_ssrTexture->ClearRenderTargetView(math::Color(0, 0, 0, 0));
		_ssrHitInfo->ClearRenderTargetView(math::Color(0, 0, 0, 0));

		g_pEnv->_graphicsDevice->SetRenderTargets({ _ssrDiffuseTexture, _ssrDiffuseHitInfo, _ssrTexture, _ssrHitInfo });

		// March at the SSR target resolution (half the viewport when
		// r_ssrHalfRes). The shader derives its UVs from the fullscreen quad's
		// texcoord, so the smaller viewport just means fewer rays - each ray
		// still marches the full-res gbuffer.
		D3D11_VIEWPORT vp;
		vp.TopLeftX = 0;
		vp.TopLeftY = 0;
		vp.Width = (float)_ssrTexture->GetWidth();
		vp.Height = (float)_ssrTexture->GetHeight();
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		g_pEnv->_graphicsDevice->SetViewport(vp);

		if (auto guiRenderer = g_pEnv->GetUIManager().GetRenderer(); guiRenderer != nullptr)
		{
			guiRenderer->StartFrame();

			// One-shot dump of exactly what SSR is about to sample.
			//
			// A reflection can be dark for two completely different reasons: the
			// ray landed somewhere dark, or the ray landed on something that IS
			// bright in the final frame but was not yet drawn when SSR read the
			// buffer. Transparent geometry is drawn after this pass, so anything
			// seen through glass falls in the second category. Comparing this
			// dump against the presented frame separates them; nothing else in
			// the pipeline can.
			if (r_ssrDumpBeauty._val.b)
			{
				r_ssrDumpBeauty._val.b = false;
				try { _beautyRT->SaveToFile(fs::path("ssr_beauty.png")); }
				catch (const std::exception& e) { LOG_WARN("ssr beauty dump failed: %s", e.what()); }
				LOG_INFO("r_ssrDumpBeauty: wrote ssr_beauty.png - beauty as SSR sees it");
			}

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

			// Sky-view LUT at t21 = the environment fallback for specular rays that find
			// nothing. Bound with an EXPLICIT slot rather than the auto-slot counter,
			// because the GI bind above is conditional: on the auto path this would land at
			// t9 whenever GI is off and get read as voxel radiance. The explicit setter also
			// leaves the counter at slot+1, and nothing binds after it in this pass.
			// A null bind reads as black, which degrades to the old no-fallback behaviour.
			g_pEnv->_graphicsDevice->SetTexture2D(21,
				g_pEnv->_atmosphereLUTs != nullptr ? g_pEnv->_atmosphereLUTs->GetSkyViewLUT() : nullptr);

			// t22..t24 = the environment atlases, so a specular ray that finds
			// nothing can return the environment along its own direction instead
			// of leaving a hole for the resolve to patch. Patching it afterwards
			// meant gating a DENOISED radiance on an UNFILTERED confidence mask,
			// and the two disagreed at every hit/miss boundary - see the miss
			// path in SSR.shader. Null binds read as black, degrading to the old
			// "miss contributes nothing".
			g_pEnv->_graphicsDevice->SetTexture2D(22, _iblSkyEnvMap);
			g_pEnv->_graphicsDevice->SetTexture2D(23,
				_activeProbe != nullptr ? _activeProbe->GetEnvAtlas() : nullptr);
			g_pEnv->_graphicsDevice->SetTexture2D(24,
				_activeProbe2 != nullptr ? _activeProbe2->GetEnvAtlas() : nullptr);

			guiRenderer->FullScreenTexturedQuad(nullptr, _ssrShader.get());

			// The march ran at the SSR resolution; everything from here on runs
			// at the full viewport.
			vp.Width = bbvp.width;
			vp.Height = bbvp.height;
			g_pEnv->_graphicsDevice->SetViewport(vp);

			// Half-res bridge: depth-aware upsample of the four march outputs
			// to full res BEFORE the denoiser. NRD's half-res temporal
			// accumulation made reflections swim under camera motion (bisect:
			// raw half-res SSR does not swim), so the denoiser always runs at
			// full resolution against the full-res gbuffer guides.
			const bool ssrHalfRes = _ssrHalfResActive &&
				_ssrUpDiffuse != nullptr && _ssrUpSpecular != nullptr;
			if (ssrHalfRes && r_ssrDenoise._val.b && _ssrUpsampleShader != nullptr)
			{
				g_pEnv->_graphicsDevice->UnbindAllPixelShaderResources();
				g_pEnv->_graphicsDevice->SetRenderTargets({ _ssrUpDiffuse, _ssrUpDiffuseHit, _ssrUpSpecular, _ssrUpSpecularHit });
				g_pEnv->_graphicsDevice->SetTexture2D(0, _ssrDiffuseTexture);
				g_pEnv->_graphicsDevice->SetTexture2D(1, _ssrDiffuseHitInfo);
				g_pEnv->_graphicsDevice->SetTexture2D(2, _ssrTexture);
				g_pEnv->_graphicsDevice->SetTexture2D(3, _ssrHitInfo);
				g_pEnv->_graphicsDevice->SetTexture2D(4, _gbuffer.GetNormal());
				g_pEnv->_graphicsDevice->SetBoundResourceIndex(0);
				guiRenderer->FullScreenTexturedQuad(nullptr, _ssrUpsampleShader.get());
				for (int32_t i = 0; i < 5; ++i)
					g_pEnv->_graphicsDevice->SetTexture2D(i, nullptr);
				g_pEnv->_graphicsDevice->SetBoundResourceIndex(0);
			}

            _ssrResolved->ClearRenderTargetView(math::Color(0, 0, 0, 0));

            _denoiseFD.camera = _currentCamera;
            _denoiseFD.jitter = _taa.GetJitterOffset(bbvp.width, bbvp.height);

			// The SSR signal handed to the resolve. Two sources so both the denoised
			// and the raw path go through ONE draw: the resolve adds the
			// environment term, and running it twice would add it twice.
			//   denoised     - A = NRD's resolved diffuse+specular, B = nothing
			//   not denoised - A = raw SSR diffuse, B = raw SSR specular
			// A null bind reads as black, so the unused source contributes nothing.
			ITexture2D* ssrSourceA = nullptr;
			ITexture2D* ssrSourceB = nullptr;

			if (r_ssrDenoise._val.b && g_pEnv->_denoiserProvider != nullptr)
			{
				// NRD-denoised path: pack diffuse + specular SSR signals + their hit distances,
				// run NRD's RELAX_DIFFUSE_SPECULAR, then hand the resolved signal to the
				// composition below.
				// FilterFrame compares the input texture size against NRD's last-bound size and
				// rebuilds the pool internally when they differ - that's the DLSS-toggle safety
				// net in case SceneRenderer::Resize's explicit CreateBuffers call is missed.
				// NRD always denoises at full resolution with the full-res
				// gbuffer guides; half-res mode feeds it the upsampled bridge
				// set instead of the raw half-res march outputs.
				g_pEnv->_denoiserProvider->BuildFrameData(_denoiseFD,
					ssrHalfRes ? _ssrUpDiffuse     : _ssrDiffuseTexture,
					ssrHalfRes ? _ssrUpDiffuseHit  : _ssrDiffuseHitInfo,
					ssrHalfRes ? _ssrUpSpecular    : _ssrTexture,
					ssrHalfRes ? _ssrUpSpecularHit : _ssrHitInfo,
					_gbuffer.GetNormal(),
					_gbuffer.GetSpecular(),
					_gbuffer.GetVelocity());
				g_pEnv->_denoiserProvider->FilterFrame(_denoiseFD, _ssrResolved);

				_ssrResolved->CopyTo(_ssrHistory);

				ssrSourceA = _ssrResolved;
			}
			else
			{
				// NRD-bypass path: composite the raw SSR diffuse + specular textures directly
				// onto beauty. Use this to verify whether artifacts originate from the SSR shader
				// or from NRD's denoising. If artifacts disappear here, the shader output is OK
				// and the problem lives in NRD's preprocess / matrices / hit-distance interpretation.
				//
				// Specular reflection (_ssrTexture) is the shiny / mirror channel that produces
				// the visible reflections on wet surfaces; without it, r_ssrDenoise=0 looked like
				// "reflections vanished entirely" and made the diagnostic useless. Composite both
				// diffuse and specular so the toggle isolates NRD vs the raw shader honestly.
				ssrSourceA = _ssrDiffuseTexture;
				ssrSourceB = _ssrTexture;
			}

			// NRD overwrites our per-frame constant buffer state; re-upload it. The
			// resolve reads the IBL params and probe placement out of it, so this
			// is not optional on the denoised path.
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
			}

			guiRenderer->StartFrame();
			g_pEnv->_graphicsDevice->SetViewport(*bbvp.Get11());
			g_pEnv->_graphicsDevice->SetRenderTarget(_beautyRT);
			GFX_PERF_BEGIN(0xFFFFFFFF, L"SSR Resolve + Env Compose");

			// Everything the resolve reads is bound EXPLICITLY, and the quad is
			// drawn with a null texture so nothing lands on the auto-slot counter
			// after these. The IBL atlases keep the same registers the deferred
			// pass uses (t15/t16/t17/t21) because both passes call the same
			// EvaluateEnvSpecular and must not drift.
			g_pEnv->_graphicsDevice->UnbindAllPixelShaderResources();
			_gbuffer.BindAsShaderResource();                                  // t0..t4
			g_pEnv->_graphicsDevice->SetTexture2D(5, ssrSourceA);
			g_pEnv->_graphicsDevice->SetTexture2D(6, ssrSourceB);
			// Raw, un-denoised specular hit info: .r is the screen-space
			// confidence the composition blends against.
			g_pEnv->_graphicsDevice->SetTexture2D(7, _ssrHitInfo);
			g_pEnv->_graphicsDevice->SetTexture2D(15, _iblSkyEnvMap);
			g_pEnv->_graphicsDevice->SetTexture2D(16,
				_activeProbe != nullptr ? _activeProbe->GetEnvAtlas() : nullptr);
			g_pEnv->_graphicsDevice->SetTexture2D(17,
				_activeProbe2 != nullptr ? _activeProbe2->GetEnvAtlas() : nullptr);
			g_pEnv->_graphicsDevice->SetTexture2D(21, _dfgLut);

			// PremultipliedAlpha (src + dst * (1 - src.a)), not Additive: the
			// resolve writes the reflection in rgb and the surface's specular
			// reflectance in alpha, so the same draw adds the reflection AND takes
			// that fraction back off the base layer. The shader returns alpha 0 on
			// every path that shouldn't attenuate (sky, legacy stacking,
			// r_ssrEnergyConserve off), where this degrades exactly to the
			// additive blend it replaced.
			g_pEnv->_graphicsDevice->SetBlendState(BlendState::PremultipliedAlpha);
			guiRenderer->FullScreenTexturedQuad(nullptr, _ssrResolve.get());
			g_pEnv->_graphicsDevice->SetBlendState(BlendState::Opaque);

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


