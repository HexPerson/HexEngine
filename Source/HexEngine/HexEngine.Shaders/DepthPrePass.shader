"VertexShaderIncludes"
{
	MeshCommon
}
"PixelShaderIncludes"
{
	MeshCommon
}
"VertexShader"
{
	MeshPixelInput ShaderMain(MeshVertexInput input)
	{
		MeshPixelInput output;
		
		input.position.w = 1.0f;

		// We only need position for a depth pass
		output.position = mul(input.position, g_worldMatrix);
		output.position = mul(output.position, g_viewMatrix);
		output.position = mul(output.position, g_projectionMatrix);
		
		return output;
	}
}