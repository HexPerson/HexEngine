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
	LightingUtils
	Atmosphere
	PBRutils
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
	Texture2D g_beautyTex : register(t5);
	SHADOWMAPS_RESOURCE(6);
	

	SamplerState g_textureSampler : register(s0);
	SamplerComparisonState g_cmpSampler : register(s1);
	SamplerState g_pointSampler : register(s2);

	void CalculateDiffuseAndSpecularLighting(
		float shadowValue,
		float3 pixelNormal,
		float3 pixelSpecular,
		float3 pixelColour,
		float3 lightDir,
		float3 eyeDir,
		float shinyPower,
		float shininessStrength,		
		float lightMultiplier,
		inout float3 diffuse, 
		inout float3 specular)
	{
		float lightIntensity = saturate(dot(pixelNormal, lightDir));

		if (lightIntensity > 0.0f /*&& shadowValue > 0.0f*/)
		{
			diffuse += pixelColour * lightIntensity * shadowValue;

			diffuse = /*saturate*/(diffuse) * lightMultiplier;

			// Calculate the reflection vector based on the light intensity, normal vector, and light direction.
			float3 reflection = normalize(2 * lightIntensity * pixelNormal - lightDir);

			// Determine the amount of specular light based on the reflection vector, viewing direction, and7 specular power.
			specular = pow(saturate(dot(reflection, eyeDir)), shinyPower);// * shininessStrength;

			specular = specular * pixelSpecular * shadowValue;

			specular = /*saturate*/(specular) * lightMultiplier;
		}
	}

	float4 ShaderMain(UIPixelInput input) : SV_TARGET
	{
		float2 texcoord = input.texcoord;

		float2 screenPos = float2(input.position.x / (float)g_screenWidth, input.position.y / (float)g_screenHeight);

		// Sample the gbuffer
		//
		float4 pixelColour = g_beautyTex.Sample(g_pointSampler, screenPos);
		float4 pixelNormal = GBUFFER_NORMAL.Sample(g_pointSampler, screenPos);
		float4 pixelPosWS = GBUFFER_POSITION.Sample(g_pointSampler, screenPos);
		//float4 pixelMaterial = GBUFFER_SPECULAR.Sample(g_pointSampler, screenPos);
		
		//return float4(pixelColour.aaa, 1.0f);

		// sky
		if(pixelColour.a == -1 || pixelPosWS.a > 0.0f)
		{
			//return float4(1, 0, 0, 1.0f);
			return float4(pixelColour.rgb, 1.0f);
		}
		

		float3 lightDir = -normalize(g_lightDirection.xyz);
		float3 eyeVector = normalize(g_eyePos.xyz - pixelPosWS.xyz);

		ShadowInput shadow;
		shadow.pixelDepth = pixelNormal.w;
		shadow.positionWS = pixelPosWS;
		shadow.positionSS = input.position.xy;
		shadow.samples = g_shadowConfig.samples;

		float d = dot(normalize(pixelNormal.xyz), normalize(g_shadowCasterLightDir.xyz));
		float bias = g_shadowConfig.biasMultiplier* (1.0 - d);// max(0.000002 * (1.0 - d), 0.0000002); // seems good
			//float bias = 0.00011 * (1.0 - d);// max(0.000002 * (1.0 - d), 0.0000002);

		float depthValue = CalculateShadows(shadow, g_cmpSampler, g_pointSampler, SHADOWMAPS, bias);

		float4 pbr = CalculatePBR(
			GBUFFER_SPECULAR, 
			g_pointSampler,
			screenPos, 
			pixelNormal.xyz, 
			pixelPosWS.xyz, 
			lightDir, 
			getSunColour(), 
			pixelColour.rgb,
			depthValue,
			g_globalLight[0]);

		return pbr;

	#if 0
		float shinyPower = pixelSpecular.g;
		float shininessStrength = pixelSpecular.r;
		float emission = pixelPosWS.w;

		if(emission == -1.0f)
		{
			return float4(pixelColour.rgb, 1.0f);
		}
		else if (emission > 0.0f)
		{
			pixelColour.rgb = pixelColour.rgb * emission;
		}
		//else
		{
			ShadowInput shadow;
			shadow.pixelDepth = pixelNormal.w;
			shadow.positionWS = pixelPosWS;
			shadow.positionSS = input.position.xy;
			shadow.samples = g_shadowConfig.samples;

			float d = dot(normalize(pixelNormal.xyz), normalize(g_shadowCasterLightDir.xyz));
			float bias = g_shadowConfig.biasMultiplier* (1.0 - d);// max(0.000002 * (1.0 - d), 0.0000002); // seems good
			//float bias = 0.00011 * (1.0 - d);// max(0.000002 * (1.0 - d), 0.0000002);

			float depthValue = CalculateShadows(shadow, g_cmpSampler, g_pointSampler, SHADOWMAPS, bias);

			float3 ambient = pixelColour.rgb * g_atmosphere.ambientLight.rgb;
			float3 diffuse = float3(0, 0, 0);// pixelColour.rgb* depthValue;// float3(0, 0, 0);
			float3 specular = float3(0, 0, 0);

			CalculateDiffuseAndSpecularLighting(
				depthValue,
				pixelNormal.xyz,
				pixelSpecular.rrr,
				pixelColour.rgb * getSunColour(),
				lightDir,
				eyeVector, 
				shinyPower,
				shininessStrength,		
				g_globalLight[0],
				diffuse,
				specular);

			float3 finalColour = ambient + diffuse + specular;

			float4 result = float4(finalColour.rgb, 1.0f);
			return /*saturate*/(result);
		
		}
		#endif
	}
}