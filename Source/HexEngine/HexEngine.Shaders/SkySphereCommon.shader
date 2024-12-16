"GlobalIncludes"
{
	Global
}
"Global"
{	
	// This is the structure sent to the vertex shader from the gpu
	struct MeshVertexInput
	{
		float4 position 	: POSITION;		
	};
	
	// This is the structure sent to the pixel shader from the vertex shader
	struct MeshPixelInput
	{
		float4 position : SV_POSITION;
		float4 gradientPosition : TEXCOORD0;
		float4 sunScreenPos : TEXCOORD1;
		float4 skyPixelPos : TEXCOORD2;
		float4 positionWS : TEXCOORD3;

		// taa stuff
		float4 previousPositionUnjittered : TEXCOORD4;
		float4 currentPositionUnjittered : TEXCOORD5;
	};

	struct MeshInstanceData
	{
		matrix world : WORLD;
		matrix worldInverseTranspose : WORLDIT;
		matrix worldPrev : WORLDPREV;
		float4 colour : INSTANCECOLOR;
		float2 uvScale : UVSCALE;
		
	};
}