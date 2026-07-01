"Global"
{
#ifndef __GLOBAL_HLSL__
#define __GLOBAL_HLSL__

	static const uint OBJECT_FLAGS_HAS_BUMP					= (1 << 0);
	static const uint OBJECT_FLAGS_HAS_ROUGHNESS			= (1 << 1);
	static const uint OBJECT_FLAGS_HAS_METALLIC				= (1 << 2);
	static const uint OBJECT_FLAGS_HAS_HEIGHT				= (1 << 3);
	static const uint OBJECT_FLAGS_HAS_EMISSION				= (1 << 4);
	static const uint OBJECT_FLAGS_HAS_OPACITY				= (1 << 5);
	static const uint OBJECT_FLAGS_HAS_AMBIENT_OCCLUSION	= (1 << 6);
	static const uint OBJECT_FLAGS_HAS_ANIMATION			= (1 << 7);
	static const uint OBJECT_FLAGS_ORM_FORMAT				= (1 << 8);
	static const uint OBJECT_FLAGS_RMA_FORMAT				= (1 << 9);

	static const uint MAX_BONES = 70;

#define MERGE(a, b) a##b
#define LIT(val) (val)

#define GBUFFER_RESOURCE(idx0, idx1, idx2, idx3, idx4) Texture2D GBUFFER_DIFFUSE : register(MERGE(t, idx0));\
	Texture2D GBUFFER_SPECULAR : register(MERGE(t, idx1));\
	Texture2D GBUFFER_NORMAL : register(MERGE(t, idx2));\
	Texture2D GBUFFER_POSITION : register(MERGE(t, idx3));\
	Texture2D GBUFFER_VELOCITY : register(MERGE(t, idx4));

// Material-features GBuffer SRV. Bound at a per-shader slot since t5 collides with
// beauty / shadowmap bindings in various places. Layout:
//   .r = material model id (0=standard, 1=SSS, 2=clearcoat, 3=anisotropic, 4=sheen)
//   .g = primary parameter
//   .b = secondary parameter
//   .a = tertiary parameter
// See GBuffer::GetFeatures() in C++ for the full convention table.
#define GBUFFER_FEATURES_RESOURCE(idx) Texture2D GBUFFER_FEATURES : register(MERGE(t, idx));

// Material model ids - keep in sync with the C++ side and any code that branches
// on the gbuffer .r channel. Encoded as a 0..255 value in the unorm RT, so the
// shader does ids reads via uint(gbuffer.r * 255.0 + 0.5).
static const uint MATERIAL_MODEL_STANDARD     = 0;
static const uint MATERIAL_MODEL_SSS          = 1;
static const uint MATERIAL_MODEL_CLEARCOAT    = 2;
static const uint MATERIAL_MODEL_ANISOTROPIC  = 3;
static const uint MATERIAL_MODEL_SHEEN        = 4;

#define SHADOWMAPS_RESOURCE(idx) Texture2D SHADOWMAPS[6] : register(t##idx);

	static const float WaveSizeMultiplier = 4.9f;

	static const float4 _WaveA = float4(0.6, 0.12, 0.10, 140);
	static const float4 _WaveB = float4(0.7, -1, 0.051, 125);
	static const float4 _WaveC = float4(0.4564, 0.348, 0.05, 20);
	static const float4 _WaveD = float4(-0.1, 0.12, 0.067, 175);
	
	struct Atmosphere
	{
		float zenithExponent;
		float anisotropicIntensity;
		float density;
		float rayleighStrength;
		float mieStrength;
		float ambientStrength;
		float sunHazeStrength;
		float sunsetWarmStrength;
		float sunsetCoolStrength;
		float sunsetGlowStrength;
		float atmospherePad0;
		float fogDensity;
		float fogStartDistance;
		float fogHeightDensity;
		float fogHeightFalloff;
		float fogHeightPivot;
		float fogSkyTintInfluence;
		float fogFarDesaturate;
		float fogAtmosphereBlendStart;
		float fogAtmosphereBlendRange;
		float fogSunsetRange;
		float fogSunsetWarmthStrength;
		float fogFarAtmosphereMatchStrength;
		// 1.0 when the aerial-perspective volume is doing distance haze
		// (see g_pEnv->_atmosphereLUTs). PostFog gates its analytic
		// atmosphere integration on this so the two passes don't compound.
		float fogUseAerialPerspective;

		float volumetricScattering;
		float volumetricStrength;
		int volumetricStepsMax;
		float volumetricStepIncrement;
		int volumetricQuality;
		// Max camera-to-light distance (metres) at which the per-pixel volumetric
		// scattering loop still runs. Beyond this the light still shades the
		// surface but the expensive ray-march is skipped.
		float volumetricLightMaxDistance;
		int volumetric_pad1;
		int volumetric_pad2;
		float volumetricPointInsideMin;
		float volumetricPointInsideMax;
		float volumetricSpotInsideMin;
		float volumetricSpotInsideMax;

		float4 ambientLight;
	};

	struct Bloom
	{
		float luminosityThreshold;
		float viewportScale;
		float bloomIntensity;
		float bloomClamp;
	};	

	struct OceanSettings
	{
		float4 shallowColour;
		float4 deepColour;
		float fresnelPow;
		float shoreFadeStrength;
		float fadeFactor;
		float reflectionStrength;
		float reflectionNearDistance;
		float reflectionFarDistance;
		float reflection_pad0;
		float reflection_pad1;
	};

	struct ColourGradeSettings
	{
		float contrast;
		float exposure;
		float hueShift;
		float saturation;

		float3 colourFilter;
		float colour_pad;
	};

	struct WeatherSurfaceParams
	{
		float wetness;
		float puddleAmount;
		float snowCoverage;
		float snowMelt;
		float dirtAmount;
		float temperatureBias;
		float precipitationIntensity;
		float lightningFlash;
		float4 windDirectionAndSpeed;
		float4 lightningBoltData;
		float4 lightningBoltDirection;
		float4 auroraParams;
		float4 auroraColorA;
		float4 auroraColorB;
	};

	// This is the constant buffer that is updated once per frame
	cbuffer PerFrameBuffer : register(b0)
	{
		// Current
		matrix g_viewMatrix;
		//
		matrix g_projectionMatrix;
		//
		matrix g_viewProjectionMatrix;
		//
		matrix g_viewMatrixInverse;
		//
		matrix g_projectionMatrixInverse;
		//
		matrix g_viewProjectionMatrixInverse;

		// Previous
		 matrix g_viewMatrixPrev;
		//
		matrix g_projectionMatrixPrev;
		//
		matrix g_viewProjectionMatrixPrev;
		//
		matrix g_viewMatrixInversePrev;
		//
		matrix g_projectionMatrixInversePrev;
		//
		matrix g_viewProjectionMatrixInversePrev;


		//
		float4 g_eyePos;
		//
		float4 g_eyeDir;
		//
		float4 g_lightPosition;
		//
		float4 g_lightDirection;
		//
		float4 g_frustumDepths;
		//	
		float4 g_globalLight; // x = sun strength, yzw = fog colour

		uint g_screenWidth;
		uint g_screenHeight;
		float g_time;
		float g_gamma;

		//float zenithExponent;
		//float anisotropicIntensity;

		Atmosphere g_atmosphere;
		Bloom g_bloom;
		OceanSettings g_oceanConfig;
		ColourGradeSettings g_colourGrading;
		WeatherSurfaceParams g_weatherSurface;

		float2 g_jitterOffsets;
		uint g_frame;
		float g_chromaticAbberationAmmount;

		// HDR display calibration. Kept at the END of the cbuffer so
		// shaders not recompiled when these were added still have
		// correct offsets for the rest of the buffer (old shaders just
		// don't read these last 16 bytes). See RenderStructs.hpp for
		// the full reasoning.
		float g_hdrPaperWhiteNits;
		float g_hdrPeakNits;
		// Tonemap operator id - cast to int in the tonemap shaders to
		// pick a curve from TonemapOperators.shader::ApplyTonemap.
		float g_tonemapOperator;
		// Diagnostic flag for rain-drip visualization. 0 = normal,
		// > 0.5 = paint cell grid (cellFrac.x in R, cellFrac.y in G) instead of
		// running the normal-perturbation path. Driven from r_rainDripDebug.
		float g_rainDripDebug;
	};

	struct MaterialProps
	{
		float metallicFactor;
		float roughnessFactor;
		float smoothness;
		// Per-material rain-drip intensity (0..1). When non-zero AND
		// g_weatherSurface.wetness > 0, DefaultPixel.shader's rain-droplet
		// perturbation activates - normal pushed toward random tangent-space
		// offsets to read as drops, roughness drops to read as wet. Previously
		// this slot held a dead specularProbability pad.
		float rainDripIntensity;

		float4 diffuseColour;
		float4 emissiveColour;

		int hasTransparency;
		int isWater;
		int isInTransparencyPhase;
		// Shading-model selector (MATERIAL_MODEL_* constants). 0 = standard PBR;
		// non-zero values branch into the feature-gbuffer-driven extended shading
		// models (SSS, clearcoat, anisotropic, sheen, ...).
		int materialModel;

		// Per-model param vec4. See RenderStructs.hpp for the per-model layout
		// table (SSS = mask + scatter colour, clearcoat = strength + roughness, etc.)
		float4 modelParams;
	};
	
	cbuffer PerObjectBuffer : register(b1)
	{
		matrix g_worldMatrix;

		uint g_objectFlags;
		int entityId;
		float g_cullDistance;
		int pad3;

		MaterialProps g_material;		
	}

	cbuffer PerAnimationBuffer : register(b3)
	{
		matrix g_boneTransforms[MAX_BONES];
	}

	struct ShadowSettings
	{
		float shadowFilterMaxSize;
		float penumbraFilterMaxSize;
		float biasMultiplier;
		float samples;

		float cascadeBlendRange;
		int	  passIndex;
		int	  cascadeOverride;
		float shadowMapSize;

		int   lightIndex;
		// Non-zero when the per-light shadow map(s) bound at SHADOWMAPS[*] are valid for
		// this draw. Per-pixel spot/point-light shaders gate their SampleCmpLevelZero on
		// this flag: sampling a null SRV with a comparison sampler reads 0 = fully
		// occluded, which would black out every fragment inside the cone/sphere of any
		// non-shadow-casting light. Populated by SetupPerShadowCasterBuffer from
		// Light::GetDoesCastShadows().
		int	  castsShadowsFlag;
		int	  pad1;
		int	  pad2;

		// Screen-space contact shadow settings:
		//   x = enabled (1/0), y = step count, z = max length (m), w = thickness (m)
		// Only the directional light populates this; other shadow casters leave it
		// zeroed so the enabled check disables contact shadows automatically.
		float4 contactShadowParams;
	};

	cbuffer PerShadowCasterBuffer : register(b2)
	{
		matrix g_lightViewMatrix[6];
		//
		matrix g_lightProjectionMatrix[6];
		//
		matrix g_lightViewProjectionMatrix[6];

		float3 g_shadowCasterLightDir;
		float g_lightRadius;

		float g_spotLightConeSize;
		// Cosines of the full inner / outer cone angles (i.e. cos(innerAngle) and
		// cos(outerAngle)). When the shader sees both non-zero, it uses
		// smoothstep(g_spotLightCosOuter, g_spotLightCosInner, dot(-L, lightDir)) for
		// physically-shaped soft-edge cone falloff. Zero means "no spot configured" -
		// the volumetric path and any caller that didn't update the per-pass buffer
		// (e.g. directional-shadow rendering) gets a clean disable instead of garbage.
		float g_spotLightCosInner;
		float g_spotLightCosOuter;
		float PerShadowCasterBuffer_pad2;

		ShadowSettings g_shadowConfig;
	};

	struct GBufferOut
	{
		float4 diff : SV_TARGET0;
		float4 mat : SV_TARGET1;
		float4 norm : SV_TARGET2;
		float4 pos :  SV_TARGET3;
		float2 velocity : SV_TARGET4;
		// Material-features RT. Layout: see GBUFFER_FEATURES_RESOURCE / MATERIAL_MODEL_* above.
		// Pixels that don't write this leave the cleared (0,0,0,0) value = standard PBR.
		float4 feat : SV_TARGET5;
	};

	struct SSROut
	{
		float4 diff : SV_TARGET0;
		float4 diffHitInfo : SV_TARGET1;
		float4 spec : SV_TARGET2;
		float4 specHitInfo : SV_TARGET3;
	};

	float3 GetForwardVectorFromWorldMatrix(matrix mat)
	{
		return float3(mat[0][0], mat[1][0], mat[2][0]);
	}

#endif
}
