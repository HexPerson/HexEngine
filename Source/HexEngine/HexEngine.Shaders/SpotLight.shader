"Requirements"
{
}
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
	ShadowUtils
	PBRutils
}
"VertexShader"
{
	MeshPixelInput ShaderMain(MeshVertexInput input, MeshInstanceData instance)
	{
		MeshPixelInput output = (MeshPixelInput)0;

		input.position.w = 1.0f;

		output.position = mul(input.position, instance.world);
		output.position = mul(output.position, g_viewProjectionMatrix);

		output.positionWS = mul(input.position, instance.world);
		//output.positionWS = float4(instance.world[3].xyz, instance.world[0].x);// mul(input.position, instance.instanceWorld);

		output.colour = instance.colour;

		// we'll use this for radius and strength
		output.texcoord = instance.uvScale;

		// store the world pos of the light in tangent
		output.tangent = instance.world[3].xyz;

		output.normal = mul(input.normal, (float3x3)instance.worldInverseTranspose);
		output.normal = normalize(output.normal);

		// use viewDirection to store forwards
		//output.viewDirection = instance.customData;

		return output;
	}
}
"PixelShader"
{
	GBUFFER_RESOURCE(0, 1, 2, 3, 4);
	SHADOWMAPS_RESOURCE(5);

	SamplerState g_textureSampler : register(s0);
	SamplerComparisonState g_cmpSampler : register(s1);
	SamplerState g_pointSampler : register(s2);

	//static const float G_SCATTERING = -0.40f;// -0.5f;
	//static const float PI = 3.14159f;

	float ComputeScattering(float lightDotView)
	{
		float result = 1.0f - g_atmosphere.volumetricScattering * g_atmosphere.volumetricScattering;
		result /= (4.0f * PI * pow(1.0f + g_atmosphere.volumetricScattering * g_atmosphere.volumetricScattering - (2.0f * g_atmosphere.volumetricScattering) * lightDotView, 1.5f));
		return result;
	}

	float CalculateVolumetricScattering(float3 worldPos, float3 direction, float3 lightPos, float3 lightDir, float radius, float lightStrength)
	{
		float3 startPos = worldPos;
		float3 currentPos = startPos;
		const float stepDistance = (radius / 16);

		float accumFog = 0.0f;

		float samples = 0.0f;
		//float3 lastPos = currentPos;

		[loop]
		for(float i = 0; i < 16; i = i + 1.0f)
		{
			currentPos += direction * stepDistance;

			float3 lightToPixelVec = (lightPos - currentPos);
			float d = length(lightToPixelVec);
			lightToPixelVec /= d;

#if 0 // make it depth aware
			float4 fragScr = float4(currentPos.xyz, 1.0f);
			float4 fragView = mul(fragScr, g_viewMatrix);
			float4 fragClip = mul(fragView, g_projectionMatrix);

			fragClip.xyz /= fragClip.w;
			float fragDepth = -fragView.z;

			fragClip.xy = fragClip.xy * 0.5 + 0.5; // is this needed?
			float2 fragTex = float2(fragClip.x, 1.0f - fragClip.y);

			float pixelDepth = GBUFFER_NORMAL.Sample(g_pointSampler, fragTex).w;

			if(pixelDepth < fragDepth)
				break;
#endif

			float attenuation = saturate(1.0f - saturate(d / radius));
			attenuation = pow(attenuation, 2);

			float coneDot = dot(-lightToPixelVec, lightDir);
			float coneAtten = pow(max(coneDot, 0.0f), g_spotLightConeSize);

			//if(coneDot == 0.0f || coneAtten == 0.0f)
			//	break;

			//float dist = length(currentPos - lastPos);

			//if(length(currentPos - lightPos) >= radius)
			//	break;
			//	accumFog += 1.0f;

			//accumFog += coneDot;

			//if(coneDot > 0)
				//accumFog += dist / radius;

			accumFog += ComputeScattering(dot(direction, normalize(lightDir))) * coneAtten * attenuation;

			samples += 1.0f;

			//lastPos = currentPos;
		}

		if(samples == 0.0f)
			return 0.0f;

		accumFog /= samples;//(radius * 2);
		accumFog *= lightStrength;

		return accumFog;
	}

	float4 ShaderMain(MeshPixelInput input) : SV_Target
	{
		float2 projectTexCoord;

	//return float4(0, 0, 0, 0);

		float2 screenPos = float2(input.position.x / (float)g_screenWidth, input.position.y / (float)g_screenHeight);

		// get the world pos of the pixel being rendered
		//float4 worldPos = mul(input.position, g_viewProjectionMatrixInverse);
		// Sample the gbuffer
		//
		float4 pixelColour = GBUFFER_DIFFUSE.Sample(g_pointSampler, screenPos);
		float4 pixelNormal = GBUFFER_NORMAL.Sample(g_pointSampler, screenPos);
		float4 pixelPosWS = GBUFFER_POSITION.Sample(g_pointSampler, screenPos);
		//float4 pixelSpecular = GBUFFER_SPECULAR.Sample(g_pointSampler, screenPos);

		// Skip non-geometry pixels (sky/background markers in the GBuffer).
		if (pixelPosWS.a > 0.0f || pixelColour.a == -1.0f || pixelNormal.w <= 0.0f)
			return float4(0.0f, 0.0f, 0.0f, 0.0f);

		float3 normalWS = pixelNormal.xyz;
		const float normalLenSq = dot(normalWS, normalWS);
		if (normalLenSq <= 0.000001f)
			return float4(0.0f, 0.0f, 0.0f, 0.0f);
		normalWS *= rsqrt(normalLenSq);

		float3 eyeVector = normalize(g_eyePos.xyz - pixelPosWS.xyz);
		float3 sphereNormal = normalize(input.normal.xyz);
		float3 eyeToSphereDir = normalize(input.positionWS.xyz - g_eyePos.xyz);
		float pixelDepth = pixelNormal.w;

		float3 lightPos = input.tangent;
		float lightRange = input.texcoord.x;//g_lightRadius;// input.positionWS.w;
		float4 lightDiffuse = float4(input.colour.rgb, 1.0f);
		float lightIntensity = input.colour.a;

		float3 lightToPixelVec = lightPos.xyz - pixelPosWS.xyz;
		float d = length(lightToPixelVec);
		if (d <= 0.0001f)
			return float4(0.0f, 0.0f, 0.0f, 0.0f);
		if (d > lightRange)
			return float4(0.0f, 0.0f, 0.0f, 0.0f);
		lightToPixelVec /= d;

		float attenuation = saturate(1.0f - saturate(d / lightRange));
		attenuation = pow(attenuation, 2);

		float3 lightDir = g_shadowCasterLightDir;// input.viewDirection.xyz;
		const float cone = g_spotLightConeSize;// input.viewDirection.w;

		float coneDot = dot(-lightToPixelVec, lightDir);
		float coneAtten = pow(max(coneDot, 0.0f), cone);

		float dir = 1.0f;

		if(length(lightPos.xyz - g_eyePos.xyz) <= lightRange)
		{
			//eyeToSphereDir *= -1.0f;
			//lightToPixelVec *= -1.0f;

			dir = -1.0f;
		}
		if(dot(sphereNormal, eyeToSphereDir) < 0)
		{
			//clip(-1);
		}
		else
		{
			//eyeVector *= -1.0f;
		}
			
		// depth test
		//if(pixelDepth < input.position.w)
		//	clip(-1);//return float4(pixelColour.rgb, 1);//float4(1,0,0,1);
		
		

		//float len = length(input.positionWS.xyz - g_eyePos.xyz) / lightRange;

		//return float4(input.positionWS.xyz, 1.0f);

		float volumetricScattering = 1.0f;

		// float volumetricScattering = CalculateVolumetricScattering(
		// 	input.positionWS.xyz, 
		// 	eyeToSphereDir/*normalize(lightPos.xyz - g_eyePos.xyz)*/,
		// 	lightPos,
		// 	lightDir,
		// 	lightRange,
		// 	lightIntensity
		// );

		//if(volumetricScattering <= 0.0f)
		//	return float4(0,0,0,1);

		//if (d > lightRange)
		//	return float4(coneDot,0,0,1);
		
#if 0
		ShadowInput shadow;
		shadow.pixelDepth = pixelNormal.w;
		shadow.positionWS = pixelPosWS;
		shadow.positionSS = input.position.xy;
		shadow.samples = g_shadowConfig.samples;

		float d2 = dot(normalize(pixelNormal.xyz), normalize(g_shadowCasterLightDir.xyz));
		float bias = g_shadowConfig.biasMultiplier * (1.0 - d2);// max(0.000002 * (1.0 - d), 0.0000002); // seems good
		//float bias = 0.00011 * (1.0 - d);// max(0.000002 * (1.0 - d), 0.0000002);

		float depthValue = CalculateShadows(shadow, g_cmpSampler, g_pointSampler, SHADOWMAPS, bias);

		//if (depthValue == 0.0f)
		//	return float4(0.0f, 0.0f, 0.0f, 0.0f);
#else
		float depthValue = 1.0f;
#endif

		

		//float4 finalColor = float4(0.0f, 0.0f, 0.0f, 0.0f);

		
		//float shinyPower = pixelSpecular.w;
		//float shininessStrength = pixelColour.w;

		//float3 HalfWay = normalize(eyeVector + lightToPixelVec);
		//float NDotH = saturate(dot(HalfWay, pixelNormal.xyz));

		float howMuchLight = dot(lightToPixelVec.xyz, pixelNormal.xyz) * depthValue;
		float3 lightAtt = float3(1.00f, 0.1f, 0.00f);

		

		float4 pbr = CalculatePBRSpotLighting(			
			GBUFFER_SPECULAR,
			g_pointSampler,
			screenPos,
			normalWS,
			pixelPosWS.xyz,
			lightToPixelVec,
			input.colour.rgb,
			pixelColour.rgb,
			depthValue,
			attenuation * coneAtten
			);

		//return float4((input.colour.rgb * coneAtten * attenuation) + (input.colour.rgb * volumetricScattering), 1.0f);
		//const float volumetricScatteringContribution = 3.2f;

		float3 lightContribution = max((pbr.rgb * lightIntensity * attenuation * coneAtten), 0.0f);
		if (!all(isfinite(lightContribution)))
			lightContribution = 0.0f.xxx;
		return float4(lightContribution /*+ (input.colour.rgb *  volumetricScattering * volumetricScatteringContribution )*/, 1.0f);
	}
}
