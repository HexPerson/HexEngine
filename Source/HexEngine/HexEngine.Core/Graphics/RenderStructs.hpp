

#pragma once

#include "../Required.hpp"

namespace HexEngine
{
	class IVertexBuffer;
	class IIndexBuffer;
	class IShaderStage;
	class IConstantBuffer;

	enum class BlendState
	{
		Invalid = -1,
		Opaque,
		Additive,
		Subtractive,
		Transparency,
		Count
	};

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

	enum class CullingMode
	{
		Invalid = -1,
		NoCulling,
		BackFace,
		FrontFace
	};

	struct Atmosphere
	{
		float zenithExponent;
		float anisotropicIntensity;
		float density;
		float fogDensity;

		float volumetricScattering;
		float volumetricStrength;
		int volumetricSteps;
		float volumetricStepIncrement;

		math::Vector4 ambientLight;
	};

	struct BloomParams
	{
		float luminosityThreshold;
		float viewportScale;
		float pad_1;
		float pad_2;
	};

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
	};

	struct OceanSettings
	{
		OceanSettings() :
			shallowColour(HEX_RGBA_TO_FLOAT4(53.0f, 58.0f, 69.0f, 255.0f)),
			deepColour(HEX_RGBA_TO_FLOAT4(77.0f, 63.0f, 73.0f, 255.0f)),
			fresnelPow(3.2f),
			shoreFadeStrength(12.0f),
			fadeFactor(15.0f),
			reflectionStrength(0.4f)
		{
		}

		math::Vector4 shallowColour;
		math::Vector4 deepColour;
		float fresnelPow;
		float shoreFadeStrength;
		float fadeFactor;
		float reflectionStrength;
	};

	struct ColourGradeSettings
	{
		float contrast;
		float exposure;
		float hueShift;
		float saturation;

		math::Vector3 colourFilter;
		float colour_pad;
	};

	struct RenderState
	{
		void Reset()
		{
			_vbuffer = nullptr;
			_ibuffer = nullptr;
			_vertexShader = nullptr;
			_pixelShader = nullptr;
			_vsConstant = nullptr;
			_psConstant = nullptr;
			_topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
			_depthState = DepthBufferState::Invalid;
			_blendState = BlendState::Invalid;
			_cullMode = CullingMode::Invalid;
		}
		IVertexBuffer* _vbuffer = nullptr;
		IIndexBuffer* _ibuffer = nullptr;
		IShaderStage* _vertexShader = nullptr;
		IShaderStage* _pixelShader = nullptr;
		IConstantBuffer* _vsConstant = nullptr;
		IConstantBuffer* _psConstant = nullptr;
		D3D_PRIMITIVE_TOPOLOGY _topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
		DepthBufferState _depthState = DepthBufferState::Invalid;
		BlendState _blendState = BlendState::Invalid;
		CullingMode _cullMode = CullingMode::Invalid;
	};

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

		math::Vector2 _jitterOffsets;
		uint32_t _frame;
		float _chromaticAbberationAmmount;
	};

	struct PerShadowCasterBuffer
	{
		math::Matrix _lightViewMatrix[6];
		math::Matrix _lightProjectionMatrix[6];
		math::Matrix _lightViewProjectionMatrix[6];

		math::Vector3 _shadowCasterLightDir;
		float _lightRadius;

		float _spotLightConeSize;
		float pad0;
		float pad1;
		float pad2;

		ShadowSettings _shadowConfig;
	};

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

	enum class MaterialFormat
	{
		None,
		ORM
	};

	struct MaterialProperties
	{
		MaterialProperties() :
			metallicFactor(0.0f),
			roughnessFactor(0.5f),
			diffuseColour(1.0f),
			hasTransparency(0),
			isWater(0),
			emissiveColour(0.0f),
			isInTransparencyPhase(0)
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
		float specularProbability;
		math::Vector4 diffuseColour;
		math::Vector4 emissiveColour;

		int hasTransparency;
		int isWater;
		int isInTransparencyPhase;
		int pad2;
	};

	struct PerObjectBuffer
	{
		math::Matrix _worldMatrix;
		uint32_t _flags;
		int entityId;
		float cullDistance;
		int pad;

		MaterialProperties _material;
	};

	struct PerAnimationBuffer
	{
		math::Matrix _boneTransforms[70];
	};

	enum class EngineConstantBuffer
	{
		PerFrameBuffer,
		PerObjectBuffer,
		PerShadowCasterBuffer,
		PerAnimationBuffer,
		NumEngineConstantBuffers
	};

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
