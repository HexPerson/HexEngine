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

	static const uint MAX_BONES = 70;

#define MERGE(a, b) a##b
#define LIT(val) (val)

#define GBUFFER_RESOURCE(idx0, idx1, idx2, idx3, idx4) Texture2D GBUFFER_DIFFUSE : register(MERGE(t, idx0));\
	Texture2D GBUFFER_SPECULAR : register(MERGE(t, idx1));\
	Texture2D GBUFFER_NORMAL : register(MERGE(t, idx2));\
	Texture2D GBUFFER_POSITION : register(MERGE(t, idx3));\
	Texture2D GBUFFER_VELOCITY : register(MERGE(t, idx4));

#define SHADOWMAPS_RESOURCE(idx) Texture2D SHADOWMAPS[6] : register(t##idx);

	
	struct Atmosphere
	{
		float zenithExponent;
		float anisotropicIntensity;
		float density;
		float fogDensity;

		float volumetricScattering;
		float volumetricStrength;
		int volumetricStepsMax;
		float volumetricStepIncrement;

		float4 ambientLight;
	};

	struct Bloom
	{
		float luminosityThreshold;
		float viewportScale;
		float pad_1;
		float pad_2;
	};	

	struct OceanSettings
	{
		float4 shallowColour;
		float4 deepColour;
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

		float3 colourFilter;
		float colour_pad;
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

		float2 g_jitterOffsets;
		uint g_frame;
		float pad_4;
	};

	struct MaterialProps
	{
		float metallicFactor;
		float roughnessFactor;
		float smoothness;
		float specularProbability;

		float4 diffuseColour;
		float4 emissiveColour;

		int hasTransparency;
		int isWater;
		int isInTransparencyPhase;
		int pad5;
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
		int	  pad0;
		int	  pad1;
		int	  pad2;
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
		float PerShadowCasterBuffer_pad0;
		float PerShadowCasterBuffer_pad1;
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
	};

	struct SSROut
	{
		float4 diff : SV_TARGET0;
		float4 hitinfo : SV_TARGET1;
	};

	float3 GetForwardVectorFromWorldMatrix(matrix mat)
	{
		return float3(mat[0][0], mat[1][0], mat[2][0]);
	}

#endif
}