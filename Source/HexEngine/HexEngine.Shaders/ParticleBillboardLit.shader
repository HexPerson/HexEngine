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

		output.texcoord = input.texcoord * instance.uvScale;

		output.normal = mul(input.normal, (float3x3)instance.world);
		output.normal = normalize(output.normal);

		output.tangent = mul(input.tangent, (float3x3)instance.world);
		output.tangent = normalize(output.tangent);

		output.binormal = mul(input.binormal, (float3x3)instance.world);
		output.binormal = normalize(output.binormal);

		output.viewDirection.xyz = g_eyePos.xyz - output.positionWS.xyz;
		output.viewDirection.xyz = normalize(output.viewDirection.xyz);
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
		float4 tex = g_albedoMap.Sample(g_textureSampler, input.texcoord);
		float alpha = tex.a * input.colour.a;
		if (alpha <= 0.001f)
			clip(-1.0f);

		float3 n = normalize(input.normal);
		float3 l = -normalize(g_lightDirection.xyz);
		float sunNdotL = saturate(dot(n, l));
		float sunStrength = max(0.0f, g_globalLight.x);
		float3 ambient = max(g_atmosphere.ambientLight.rgb * g_atmosphere.ambientLight.a, float3(0.08f, 0.08f, 0.08f));
		float3 lightAccum = ambient + sunNdotL * sunStrength.xxx;

		float3 baseColor = tex.rgb * input.colour.rgb;
		float3 litColor = baseColor * lightAccum;

		return float4(litColor, alpha);
	}
}
