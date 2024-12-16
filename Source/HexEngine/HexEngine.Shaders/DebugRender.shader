"InputLayout"
{
	PosColour
}
"VertexShaderIncludes"
{
	DebugCommon
}
"PixelShaderIncludes"
{
	DebugCommon
}
"VertexShader"
{
	DebugPixelInput ShaderMain(DebugVertexInput input)
	{
		DebugPixelInput output;

		output.position = mul(float4(input.position, 1.0f), g_worldMatrix);
		output.position = mul(output.position, g_viewMatrix);
		output.position = mul(output.position, g_projectionMatrix);

		output.color = input.color;

		return output;
	}
}
"PixelShader"
{
	float4 ShaderMain(DebugPixelInput input) : SV_Target
	{
		return input.color;// *g_material.diffuseColour; // simply return the colour of the object from the per object buffer
	}
}