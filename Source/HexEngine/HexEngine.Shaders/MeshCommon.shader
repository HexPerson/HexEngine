"GlobalIncludes"
{
	Global
}
"Global"
{	
#ifndef MESHCOMMON_SHADER
#define MESHCOMMON_SHADER
	// This is the structure sent to the vertex shader from the gpu
	struct MeshVertexInput
	{
		float4 position 	: POSITION;
		float3 normal 		: NORMAL;
		float3 tangent 		: TANGENT;
		float3 binormal 	: BINORMAL;
		float2 texcoord		: TEXCOORD0;	

		// Bone stuff
		//float4 boneIds		: BLENDINDICES;
		//float4 boneWeights	: BLENDWEIGHT;
	};

	struct AnimatedMeshVertexInput
	{
		float4 position 	: POSITION;
		float3 normal 		: NORMAL;
		float3 tangent 		: TANGENT;
		float3 binormal 	: BINORMAL;
		float2 texcoord		: TEXCOORD0;

		// Bone stuff
		float4 boneIds		: BLENDINDICES;
		float4 boneWeights	: BLENDWEIGHT;
	};


	struct MeshInstanceData
	{
		matrix world : WORLD;
		matrix worldInverseTranspose : WORLDIT;
		matrix worldPrev : WORLDPREV;
		float4 colour : INSTANCECOLOR;
		float2 uvScale : UVSCALE;
		//uint instanceID : SV_INSTANCEID;
	};
	
	// This is the structure sent to the pixel shader from the vertex shader
	struct MeshPixelInput
	{
		float4 position : SV_POSITION;
		float2 texcoord : TEXCOORD0;
		float3 normal : NORMAL;
		float3 tangent : TANGENT;
		float3 binormal : BINORMAL;
		float4 lightViewPosition1 : TEXCOORD1;
		float4 lightViewPosition2 : TEXCOORD2;
		float4 lightViewPosition3 : TEXCOORD3;
		float4 lightViewPosition4 : TEXCOORD4;
		float4 positionWS : TEXCOORD5;
		float4 viewDirection : TEXCOORD6;
		float4 colour : TEXCOORD7;

		// taa stuff
		float4 previousPositionUnjittered : TEXCOORD8;
		float4 currentPositionUnjittered : TEXCOORD9;

		uint instanceID : SV_INSTANCEID;
	};

#endif
}