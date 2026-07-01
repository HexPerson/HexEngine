"InputLayout"
{
	PosNormTanBinTex_INSTANCED
}
"VertexShaderIncludes"
{
	MeshCommon
	Utils
}
"PixelShaderIncludes"
{
	MeshCommon
	Utils
}
"VertexShader"
{
	// Minimal silhouette transform - we only need SV_Position. Mirrors the
	// standard mesh VS (instance.world * g_worldMatrix, then view-projection)
	// so the silhouette lines up exactly with the lit mesh.
	MeshPixelInput ShaderMain(MeshVertexInput input, MeshInstanceData instance, uint instanceID : SV_INSTANCEID)
	{
		MeshPixelInput output = (MeshPixelInput)0;

		input.position.w = 1.0f;
		matrix worldMatrix = mul(instance.world, g_worldMatrix);
		float4 worldPos = mul(input.position, worldMatrix);
		output.position = mul(worldPos, g_viewProjectionMatrix);

		return output;
	}
}
"PixelShader"
{
	// Jump-flood seed: every covered pixel writes its OWN absolute screen
	// coordinate (in pixels) as the seed. Uncovered pixels keep the cleared
	// sentinel (-1,-1). The RG32F target holds pixel coords exactly.
	float4 ShaderMain(MeshPixelInput input) : SV_Target
	{
		return float4(input.position.xy, 0.0f, 1.0f);
	}
}
