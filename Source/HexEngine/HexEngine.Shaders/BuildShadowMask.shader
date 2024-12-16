"Requirements"
{
	ShadowMaps
}
"InputLayout"
{
	PosTexColour
}
"VertexShaderIncludes"
{
	UICommon
}
"PixelShaderIncludes"
{
	UICommon
	ShadowUtils
	Utils
}
"VertexShader"
{
	UIPixelInput ShaderMain(UIVertexInput input)
	{
		UIPixelInput output;

		output.position = input.position;
		output.texcoord = input.texcoord;
		output.positionSS = output.position;

		return output;
	}
}
"PixelShader"
{
	GBUFFER_RESOURCE(0, 1, 2, 3, 4);
	SHADOWMAPS_RESOURCE(5);

	Texture2D g_shadowAccumulatorTex : register(t11);

	//SamplerState g_textureSampler : register(s0);
	SamplerComparisonState g_cmpSampler : register(s1);
	SamplerState g_pointSampler : register(s2);

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		float2 texcoord = input.texcoord;

		float2 screenPos = float2(input.position.x / (float)g_screenWidth, input.position.y / (float)g_screenHeight);

		// Sample the gbuffer
		//
		//float4 pixelColour = GBUFFER_DIFFUSE.Sample(g_pointSampler, screenPos);
		float4 pixelNormal = GBUFFER_NORMAL.Sample(g_pointSampler, screenPos);
		float4 pixelPosWS = GBUFFER_POSITION.Sample(g_pointSampler, screenPos);
		//float4 pixelSpecular = GBUFFER_SPECULAR.Sample(g_pointSampler, screenPos);
		float4 accumulator = g_shadowAccumulatorTex.Sample(g_pointSampler, screenPos);

		ShadowInput shadow;
		shadow.pixelDepth = pixelNormal.w;
		shadow.positionWS = pixelPosWS;
		shadow.positionSS = input.position.xy;
		shadow.samples = g_shadowConfig.samples;

		float d = dot(normalize(pixelNormal.xyz), normalize(g_shadowCasterLightDir.xyz));
		float bias = g_shadowConfig.biasMultiplier* (1.0 - d);// max(0.000002 * (1.0 - d), 0.0000002); // seems good
		//float bias = 0.00011 * (1.0 - d);// max(0.000002 * (1.0 - d), 0.0000002);

		float depthValue = CalculateShadows(shadow, g_cmpSampler, g_pointSampler, SHADOWMAPS, bias);

		if (depthValue < 1.0f)
			depthValue = 0.0f;

		int iDepth = (int)depthValue;

		//int lightIndex = accumulator.g;

		//if(lightIndex == g_shadowConfig.lightIndex)
		//	return float4(depthValue, g_shadowConfig.lightIndex, 1, 1);

		return float4(depthValue/*(float)(iDepth ^ (int)accumulator.r)*/, g_shadowConfig.lightIndex, 1, 1);
	}
}