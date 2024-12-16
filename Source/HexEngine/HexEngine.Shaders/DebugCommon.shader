"GlobalIncludes"
{
	Global
}
"Global"
{
	// This is the structure sent to the vertex shader from the gpu
	struct DebugVertexInput
	{
		float3 position 	: POSITION;
		float4 color		: COLOR;
	};

	// This is the structure sent to the pixel shader from the vertex shader
	struct DebugPixelInput
	{
		float4 position		: SV_POSITION;
		float4 color		: COLOR;
	};
}