"GlobalIncludes"
{
	Global
}
"Global"
{	
	// This is the structure sent to the vertex shader from the gpu
	struct UIVertexInput
	{
		float4 position 	: POSITION;
		float2 texcoord		: TEXCOORD0;	
		float4 colour		: COLOR;
	};

	struct FontVertexInput
	{
		float2 position 	: POSITION;
	};

	struct UIInstance
	{
		float2 center		: CENTER;
		float2 scale 		: SCALE;
		float2 texcoord0	: TEXCOORDTL;
		float2 texcoord1	: TEXCOORDBR;		
		float4 colourt		: COLORT;
		float4 colourb		: COLORB;
		matrix rotation		: ROTATION;
	};
	
	// This is the structure sent to the pixel shader from the vertex shader
	struct UIPixelInput
	{
		float4 position		: SV_POSITION;
		float4 positionSS	: TEXCOORD0;
		float2 texcoord		: TEXCOORD1;
		float4 colour		: COLOR;
	};
}