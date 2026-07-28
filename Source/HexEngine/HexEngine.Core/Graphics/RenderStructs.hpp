

#pragma once

#include "../Required.hpp"

namespace HexEngine
{
	class IVertexBuffer;
	class IIndexBuffer;
	class IShaderStage;
	class IConstantBuffer;

	/** @brief Blend-state presets used by the renderer. */
	enum class BlendState
	{
		Invalid = -1,
		Opaque,
		Additive,
		Subtractive,
		Transparency,
		TransparencyPreserveAlpha,
		// Multiplicative: target = target * src. Used by AO modulation passes
		// where the source colour is an AO factor in [0, 1] and we want to
		// darken the existing beauty by that factor. dst = src * dst (BlendOp
		// = ADD, SrcBlend = ZERO, DestBlend = SRC_COLOR).
		Multiplicative,
		// PremultipliedAlpha: target = src + target * (1 - src.a). The source
		// colour is already premultiplied, so alpha carries only how much of the
		// destination to REPLACE. Used by the SSR resolve to conserve energy: a
		// surface that reflects a fraction F of the light arriving at it must
		// lose that fraction from what it was already emitting, so the pass
		// writes the reflection in rgb and F in alpha and gets
		// (1 - F) * base + reflection in one draw. Alpha is preserved on the
		// destination like the other *PreserveAlpha states.
		PremultipliedAlpha,
		Count
	};

	/** @brief Depth-stencil presets used by the renderer. */
	enum class DepthBufferState
	{
		Invalid = -1,
		DepthNone,
		DepthDefault,
		DepthRead,
		DepthReverseZ,
		DepthReadReverseZ,
		Count
	};

	/** @brief Face-culling mode for rasterization state. */
	enum class CullingMode
	{
		Invalid = -1,
		NoCulling,
		BackFace,
		FrontFace
	};

	/** @brief Atmosphere and fog parameters uploaded in the per-frame constant buffer. */
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
		// 1.0 when the aerial-perspective volume is producing the
		// distance-haze pass (r_atmosphereLUTs on). PostFog uses this to
		// skip its own analytic distance atmosphere integration so the
		// two systems don't compound and over-tint. Height fog / sunset
		// shaping / lightning are unaffected.
		float fogUseAerialPerspective;

		float volumetricScattering;
		float volumetricStrength;
		int volumetricSteps;
		float volumetricStepIncrement;
		int volumetricQuality;
		// Maximum camera-to-light distance (metres) at which the per-pixel
		// volumetric scattering loop still runs for a punctual light. Beyond
		// this the light's volume mesh still rasterises (cheap) and the surface
		// lighting still computes, but the expensive 14..20-sample ray-march
		// loop in the PS is skipped. Driven from r_volumetricLightMaxDistance.
		float volumetricLightMaxDistance;
		int volumetric_pad1;
		int volumetric_pad2;
		float volumetricPointInsideMin;
		float volumetricPointInsideMax;
		float volumetricSpotInsideMin;
		float volumetricSpotInsideMax;

		math::Vector4 ambientLight;
	};

	/** @brief Bloom post-process parameters uploaded per frame. */
	struct BloomParams
	{
		float luminosityThreshold;
		float viewportScale;
		float bloomIntensity;
		float bloomClamp;
	};

	/** @brief Shadow filtering/cascade settings for shadow passes. */
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
		// Non-zero if the current shadow-casting light's depth map is bound and valid
		// (set by SetupPerShadowCasterBuffer from Light::GetDoesCastShadows()). The
		// per-pixel spot/point-light shaders check this before sampling SHADOWMAPS[0..N];
		// without the gate, non-shadow-casting lights would sample a nulled SRV via
		// SampleCmpLevelZero and read 0 = "fully occluded", turning every fragment inside
		// the cone/sphere black.
		int	  castsShadowsFlag;
		// Number of shadow cascades actually allocated and bound for this caster. The
		// shader's cascade loop and cascade-blend both need this: MAX_SHADOW_CASCADES is 6
		// but r_shadowCascades defaults to 4, and blending off the end of the real set reads
		// an unbound depthMaps[] slot (which samples as 0 = fully occluded) and produced a
		// dark band at the far edge of the last cascade. Occupies the old pad1 slot, so the
		// cbuffer layout is unchanged.
		int	  cascadeCount;
		int	  pad2;

		// Screen-space contact shadow settings, packed as a vec4 for cbuffer alignment.
		// Only the directional light populates these; other shadow casters leave them
		// zeroed so the shader's enabled-check naturally disables them.
		//   x = enabled (1 = on, 0 = off)
		//   y = step count
		//   z = max ray length in world units (metres)
		//   w = thickness window (metres) - blocker must be within this depth of the
		//       ray for the test to count, otherwise we'd shadow through walls.
		math::Vector4 contactShadowParams;
	};

	/** @brief Ocean rendering settings uploaded in the per-frame constant buffer. */
	struct OceanSettings
	{
		OceanSettings() :
			shallowColour(HEX_RGBA_TO_FLOAT4(53.0f, 58.0f, 69.0f, 255.0f)),
			deepColour(HEX_RGBA_TO_FLOAT4(77.0f, 63.0f, 73.0f, 255.0f)),
			fresnelPow(3.2f),
			shoreFadeStrength(12.0f),
			fadeFactor(15.0f),
			reflectionStrength(0.4f),
			reflectionNearDistance(150.0f),
			reflectionFarDistance(450.0f)
		{
		}

		math::Vector4 shallowColour;
		math::Vector4 deepColour;
		float fresnelPow;
		float shoreFadeStrength;
		float fadeFactor;
		float reflectionStrength;
		float reflectionNearDistance;
		float reflectionFarDistance;
		float reflection_pad0;
		float reflection_pad1;
	};

	/** @brief Colour-grading parameters uploaded in the per-frame constant buffer. */
	struct ColourGradeSettings
	{
		float contrast;
		float exposure;
		float hueShift;
		float saturation;

		math::Vector3 colourFilter;
		float colour_pad;
	};

	/** @brief Weather surface/material parameters uploaded per frame for weather-aware shaders. */
	struct WeatherSurfaceParams
	{
		float wetness = 0.0f;
		float puddleAmount = 0.0f;
		float snowCoverage = 0.0f;
		float snowMelt = 0.0f;
		float dirtAmount = 0.0f;
		float temperatureBias = 0.0f;
		float precipitationIntensity = 0.0f;
		float lightningFlash = 0.0f;
		math::Vector4 windDirectionAndSpeed = math::Vector4(0.0f, 0.0f, 1.0f, 0.0f);
		math::Vector4 lightningBoltData = math::Vector4::Zero; // x=intensity y=seed z=progress w=width
		math::Vector4 lightningBoltDirection = math::Vector4(0.0f, 1.0f, 0.0f, 0.0f); // xyz=sky direction w=branching
		math::Vector4 auroraParams = math::Vector4::Zero; // x=intensity y=speed z=banding w=height
		math::Vector4 auroraColorA = math::Vector4(0.10f, 0.90f, 0.65f, 1.0f);
		math::Vector4 auroraColorB = math::Vector4(0.32f, 0.38f, 1.00f, 1.0f);
	};

	/** @brief Cached graphics state snapshot used to reduce redundant state changes. */
	struct RenderState
	{
		void Reset()
		{
			_vbuffer = nullptr;
			_ibuffer = nullptr;
			_vertexShader = nullptr;
			_pixelShader = nullptr;
			_geometryShader = nullptr;
			_vsConstant = nullptr;
			_psConstant = nullptr;
			_gsConstant = nullptr;
			_topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
			_depthState = DepthBufferState::Invalid;
			_blendState = BlendState::Invalid;
			_cullMode = CullingMode::Invalid;
		}
		IVertexBuffer* _vbuffer = nullptr;
		IIndexBuffer* _ibuffer = nullptr;
		IShaderStage* _vertexShader = nullptr;
		IShaderStage* _pixelShader = nullptr;
		IShaderStage* _geometryShader = nullptr;
		IConstantBuffer* _vsConstant = nullptr;
		IConstantBuffer* _psConstant = nullptr;
		IConstantBuffer* _gsConstant = nullptr;
		D3D_PRIMITIVE_TOPOLOGY _topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
		DepthBufferState _depthState = DepthBufferState::Invalid;
		BlendState _blendState = BlendState::Invalid;
		CullingMode _cullMode = CullingMode::Invalid;
	};

	/** @brief Per-frame constants shared by most shaders. */
	struct PerFrameConstantBuffer
	{
		// Current frame
		math::Matrix _viewMatrix;
		math::Matrix _projectionMatrix;
		math::Matrix _viewProjectionMatrix;
		math::Matrix _viewMatrixInverse;
		math::Matrix _projectionMatrixInverse;
		math::Matrix _viewProjectionMatrixInverse;

		// Previous frame
		math::Matrix _viewMatrixPrev;
		math::Matrix _projectionMatrixPrev;
		math::Matrix _viewProjectionMatrixPrev;
		math::Matrix _viewMatrixInversePrev;
		math::Matrix _projectionMatrixInversePrev;
		math::Matrix _viewProjectionMatrixInversePrev;

		math::Vector4 _eyePos;
		math::Vector4 _eyeDir;
		math::Vector4 _lightPosition;
		math::Vector4 _lightDirection;
		math::Vector4 _frustumSplits;
		float _globalLight[4];
		int _screenWidth;
		int _screenHeight;
		float _time;
		// 1 = apply the physically-correct PBR energy terms (currently the diffuse 1/PI),
		// 0 = legacy behaviour. Driven by r_pbrEnergyFix. Occupies the slot that used to
		// hold _gamma, which was uploaded every frame from r_gamma and read by no shader -
		// the SDR tonemap hardcodes 1/2.2 - so the layout is unchanged.
		float _pbrEnergyFix;

		Atmosphere _atmosphere;
		BloomParams _bloom;
		OceanSettings _oceanConfig;
		ColourGradeSettings _colourGrading;
		WeatherSurfaceParams _weatherSurface;

		math::Vector2 _jitterOffsets;
		uint32_t _frame;
		float _chromaticAbberationAmmount;

		// HDR display calibration. Driven by r_hdrPaperWhiteNits /
		// r_hdrPeakNits HVars, consumed by TonemapHDR.shader to map
		// post-tonemap output into absolute nits so the same scene reads
		// the same brightness regardless of whether the swap chain is
		// going through DWM composition (editor windowed) or Independent
		// Flip (launcher fullscreen-borderless).
		//
		// IMPORTANT: keep these at the END of the cbuffer. The shader
		// build system only recompiles .shader files whose own source
		// changed - if we add fields mid-buffer, any shader that wasn't
		// touched still has the old offsets and reads garbage for every
		// field past the insertion point. Appending at the end is safe
		// because old shaders ignore the trailing bytes.
		float _hdrPaperWhiteNits;
		float _hdrPeakNits;
		// Tonemap operator id - matches the TonemapOperator enum in
		// SceneRenderer.cpp (0=Reinhard, 2=ACES Fitted default, etc.).
		// Stored as float for HLSL alignment uniformity; shader casts to int.
		float _tonemapOperator;
		// Debug visualization flag for the rain-drip system. 0 = normal output,
		// > 0.5 = ApplyRainDroplets writes its cell-grid (cellFrac as R/G channels)
		// straight into the surface colour so we can see the basis + scroll
		// direction visually. Plumbed via PerFrameBuffer because it has to reach
		// every material shader path (DefaultPixel + DefaultAnimated + graph-
		// compiled materials) without per-shader cbuffer rewiring.
		float _rainDripDebug;

		// TAA tuning, appended at the end per the note above.
		//   x = neighbourhood variance-clip gamma (mean +/- x*sigma). Lower is tighter and
		//       rejects more history: safer against ghosting, costs some detail.
		//   y = sign applied to the velocity buffer's Y when reprojecting history.
		//       CalcVelocity emits a clip-space delta (+y up) while texcoords are y-down, so
		//       -1 is the mathematically correct value; +1 is the engine's long-standing
		//       behaviour. Exposed because the two interact - correcting the sign makes
		//       history land on the right pixel and therefore be ACCEPTED far more often,
		//       which changes how much the clip gamma above is doing.
		math::Vector4 _taaParams;

		// Reflection / IBL parameters, appended at the end per the note above.
		//   x = SSR sky-fallback strength (r_ssrSkyFallbackStrength). 0 restores the old
		//       behaviour where a specular ray finding nothing returned black.
		//   yzw reserved for the rest of Phase 1 (probe counts, IBL intensity).
		math::Vector4 _reflectionParams;

		// Image-based lighting, appended at the end per the note above.
		//   x = sky specular IBL strength (r_iblSkySpecular)
		//   y = sky diffuse IBL strength  (r_iblSkyDiffuse)
		//   z = reflection probe strength (r_iblProbeStrength)
		//   w reserved.
		math::Vector4 _iblParams;

		// Active reflection probe (nearest captured probe to the camera; one per
		// frame in v1). Appended at the end per the note above.
		//   _probeCenter:  xyz = world-space box centre, w = 1 when a probe atlas
		//                  is bound at t16 this frame, else 0.
		//   _probeExtents: xyz = box half-extents (metres), w = 1 when box
		//                  projection is enabled for this probe.
		math::Vector4 _probeCenter;
		math::Vector4 _probeExtents;

		// Second-nearest probe, for cross-fading between adjacent probe volumes.
		// Without this a pixel snaps from one probe to the next the instant the
		// selection changes, which pops when walking between rooms. Same packing
		// as the primary; center.w = 1 when a second atlas is bound at t17.
		math::Vector4 _probeCenter2;
		math::Vector4 _probeExtents2;

		// Environment-specular ownership, appended at the end per the note above.
		//   x = 1 when the SSR resolve owns the environment specular term, 0 when
		//       the deferred lighting pass does.
		//
		// The two systems have to be COMPOSED, not stacked. Deferred runs before
		// SSR and the SSR resolve blends additively onto beauty, so with both
		// enabled a pixel whose ray hit got environment + screen reflection
		// double-counted, while a pixel whose ray missed (a floor ray aimed at a
		// window pane finds nothing - transparent glass never writes the opaque
		// gbuffer) got neither and rendered black. Handing the whole term to the
		// resolve puts both estimates in the same shader at the same time, where
		// lerp(environment, screen, confidence) is expressible.
		//
		// Set per frame from r_iblComposeSSR AND the exact predicate that decides
		// whether RenderSSR runs at all - a probe capture face, a secondary
		// camera, r_ssr 0 or a scene with nothing reflective all keep the term in
		// the deferred pass, because no resolve will run to supply it.
		//   y = 1 when the SSR resolve takes the reflected fraction off the base
		//       layer (energy conservation).
		//   z = fraction of albedo removed at full rain wetness.
		//   w reserved.
		math::Vector4 _iblComposeParams;

		// Weather overcast tint, appended at the end per the note above.
		//   xyz = the colour the sky is lerped toward, w = how far.
		//
		// The sky sphere already applies this (Hillaire is a clear-sky model and
		// cannot produce overcast, so this lerp IS how rain gets its grey). The
		// prefiltered environment atlas needs the identical tint or reflections
		// disagree with the sky above them.
		math::Vector4 _skyOvercast;

		// SSR behaviour toggles, appended at the end per the note above.
		//   x = 1 to use water's last-in-screen sample when the specular march
		//       gives up. It makes a dark reflection bright, but the bright
		//       thing it draws sits where the RAY gave up rather than where the
		//       mirror image is - so reflections stop lining up with what they
		//       reflect. yzw reserved.
		math::Vector4 _ssrParams;
	};

	/** @brief Per-light shadow-caster constants used by shadow rendering shaders. */
	struct PerShadowCasterBuffer
	{
		math::Matrix _lightViewMatrix[6];
		math::Matrix _lightProjectionMatrix[6];
		math::Matrix _lightViewProjectionMatrix[6];

		math::Vector3 _shadowCasterLightDir;
		float _lightRadius;

		// Legacy Phong-exponent cone size kept here so shaders that still expect this
		// field bind without a rewrite. The new soft-cone model uses _spotLightCosInner
		// and _spotLightCosOuter (cosines of the full inner and outer cone angles, in
		// radians). When both are populated the shader does smoothstep(cosOuter,
		// cosInner, cosTheta); when they're 0 (e.g. pre-update shadow setup with no
		// active spot) the shader falls back to the legacy formula.
		float _spotLightConeSize;
		float _spotLightCosInner;
		float _spotLightCosOuter;
		float pad2;

		ShadowSettings _shadowConfig;
	};

	/** @brief Material texture slot indices expected by engine shaders. */
	enum MaterialTexture
	{
		Albedo,
		Normal,
		Roughness,
		Metallic,
		Height,
		Emission,
		Opacity,
		AmbientOcclusion,
		Count
	};

	/** @brief Packing format for material texture workflows. */
	enum class MaterialFormat
	{
		None,
		ORM,
		RMA
	};

	/** @brief Material scalar/vector properties uploaded per draw. */
	struct MaterialProperties
	{
		MaterialProperties() :
			metallicFactor(0.0f),
			roughnessFactor(0.5f),
			smoothness(0.0f),
			rainDripIntensity(0.0f),
			diffuseColour(1.0f),
			hasTransparency(0),
			isWater(0),
			emissiveColour(0.0f),
			isInTransparencyPhase(0),
			materialModel(0),
			modelParams(0.0f, 0.0f, 0.0f, 0.0f)
		{
		}

		bool operator ==(const MaterialProperties& other)
		{
			return (
				metallicFactor == other.metallicFactor &&
				roughnessFactor == other.roughnessFactor &&
				diffuseColour == other.diffuseColour &&
				emissiveColour == other.emissiveColour &&
				hasTransparency == other.hasTransparency &&
				isWater == other.isWater
				);
		}

		float metallicFactor;
		float roughnessFactor;
		float smoothness;
		// Per-material rain-drip intensity (0..1). When non-zero AND the global
		// g_weatherSurface.wetness > 0, DefaultPixel.shader perturbs the surface
		// normal + drops roughness via procedural droplet noise so the material
		// reads as "wet with rain droplets". 0 = ignore weather entirely; 1 =
		// full droplet displacement. Previously this 4-byte slot was a
		// specularProbability pad that nothing consumed - repurposed without an
		// ABI rev because the cbuffer offset of diffuseColour is unchanged.
		float rainDripIntensity;
		math::Vector4 diffuseColour;
		math::Vector4 emissiveColour;

		int hasTransparency;
		int isWater;
		int isInTransparencyPhase;
		// Shading-model selector for the new material-features RT path. 0 = standard
		// PBR (existing behaviour). Non-zero ids unlock the extended shading models
		// (subsurface scattering, clearcoat, anisotropic, sheen, ...) at the cost of
		// occupying the .r channel of the features gbuffer plus a model-specific
		// post-process pass. See MATERIAL_MODEL_* in Global.shader.
		int materialModel;

		// Per-model parameter vec4. Interpretation depends on materialModel:
		//   SSS:        .x = mask, .yzw = scatter color tint (linear)
		//   Clearcoat:  .x = strength, .y = roughness, .z = ior tweak, .w = unused
		//   Anisotropic:.x = anisotropy [-1..1], .yz = tangent direction.xy, .w = unused
		//   Sheen:      .x = strength, .yzw = sheen tint
		// Zero default = "feature off".
		math::Vector4 modelParams;
	};

	/** @brief Per-object constants uploaded for each draw call. */
	struct PerObjectBuffer
	{
		math::Matrix _worldMatrix;
		uint32_t _flags;
		int entityId;
		float cullDistance;
		int pad;

		MaterialProperties _material;
	};

	/** @brief Skeletal animation matrices uploaded for skinned mesh rendering. */
	struct PerAnimationBuffer
	{
		math::Matrix _boneTransforms[70];
		// Previous frame's pose. Needed so skinned meshes emit real motion vectors: the
		// animated vertex shader used to reproject with the CURRENT pose, so deformation
		// contributed nothing and animated characters ghosted under TAA/DLSS.
		math::Matrix _boneTransformsPrev[70];
	};

	/** @brief Engine-managed constant buffer binding slots. */
	enum class EngineConstantBuffer
	{
		PerFrameBuffer,
		PerObjectBuffer,
		PerShadowCasterBuffer,
		PerAnimationBuffer,
		NumEngineConstantBuffers
	};

	/** @brief Display mode descriptor returned by graphics device output queries. */
	struct ScreenDisplayMode
	{
		struct RefreshRate
		{
			int32_t numerator;
			int32_t denominator;
		};

		int32_t width;
		int32_t height;
		RefreshRate refresh;
	};
}
