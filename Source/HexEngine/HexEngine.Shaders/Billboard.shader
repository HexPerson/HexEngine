"InputLayout"
{
	PosNormTanBinTex_INSTANCED
}
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
	MeshPixelInput ShaderMain(MeshVertexInput input, MeshInstanceData instance)
	{
		MeshPixelInput output = (MeshPixelInput)0;

		input.position.w = 1.0f;

		output.position = mul(input.position, instance.world);
		output.positionWS = output.position;
		output.position = mul(output.position, g_viewProjectionMatrix);

		output.texcoord = input.texcoord * instance.uvScale + instance.worldPrev[3].xy;

		output.normal = mul(input.normal, (float3x3)instance.world);
		output.normal = normalize(output.normal);

		output.tangent = mul(input.tangent, (float3x3)instance.world);
		output.tangent = normalize(output.tangent);

		output.binormal = mul(input.binormal, (float3x3)instance.world);
		output.binormal = normalize(output.binormal);

		// Determine the viewing direction based on the position of the camera and the position of the vertex in the world.
		output.viewDirection.xyz = g_eyePos.xyz - output.positionWS.xyz;

		// Normalize the viewing direction vector.
		output.viewDirection.xyz = normalize(output.viewDirection.xyz);
		output.viewDirection.w = 0.0f;

		output.colour = instance.colour;

		return output;
	}
}
"PixelShader"
{
	Texture2D g_albedoMap : register(t0);

	SamplerState g_textureSampler : register(s0);

	float4 ShaderMain(MeshPixelInput input) : SV_Target
	{
		return g_albedoMap.Sample(g_textureSampler, input.texcoord) * input.colour;
	}
}
