

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
		float fog_pad0;

		float volumetricScattering;
		float volumetricStrength;
		int volumetricSteps;
		float volumetricStepIncrement;
		int volumetricQuality;
		int volumetric_pad0;
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
		int	  pad0;
		int	  pad1;
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
		float _gamma;

		Atmosphere _atmosphere;
		BloomParams _bloom;
		OceanSettings _oceanConfig;
		ColourGradeSettings _colourGrading;
		WeatherSurfaceParams _weatherSurface;

		math::Vector2 _jitterOffsets;
		uint32_t _frame;
		float _chromaticAbberationAmmount;
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
			_pad0(0.0f),
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
		// Pads the (metallic, roughness, smoothness) triplet up to a 16-byte
		// boundary so the following diffuseColour Vector4 lands at offset 16 -
		// matches the HLSL cbuffer's implicit padding rules. Previously this
		// slot held specularProbability, which was dead weight: nothing read
		// pixelSpecular.a from the gbuffer and the BRDF lobes only consume
		// .r (metallic) and .g (roughness). Removed to free the slot; named
		// explicitly so future fields can claim it without an ABI rev.
		float _pad0;
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
