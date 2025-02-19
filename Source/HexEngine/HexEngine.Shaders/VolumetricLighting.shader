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
	Atmosphere
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

	Texture2D g_noiseTexture : register(t11);

	SamplerState g_textureSampler : register(s0);
	SamplerComparisonState g_cmpSampler : register(s1);
	SamplerState g_pointSampler : register(s2);

	static const float G_SCATTERING = -0.40f;// -0.5f;
	static const float PI = 3.14159f;

	float ComputeScattering(float lightDotView)
	{
		float result = 1.0f - g_atmosphere.volumetricScattering * g_atmosphere.volumetricScattering;
		result /= (4.0f * PI * pow(1.0f + g_atmosphere.volumetricScattering * g_atmosphere.volumetricScattering - (2.0f * g_atmosphere.volumetricScattering) * lightDotView, 1.5f));
		return result;
	}

	

	/*float SampleDepth(Texture2D shadowMap, float2 projectTexCoord, float lightDepthValue)
	{
		float sum = 0;
		int num = 0;

		const float pcfFactor = 5.0f;
		for (float y = -pcfFactor; y <= pcfFactor; y += 1.0)
		{
			for (float x = -pcfFactor; x <= pcfFactor; x += 1.0)
			{
				sum += shadowMap.SampleCmpLevelZero(g_cmpSampler, projectTexCoord.xy + TexOffset(x, y), lightDepthValue).r;
				num = num + 1;
			}
		}

		return sum / (float)num;
	}*/

	float3 CalculateVolumetricLighting(float pixelDepth, float3 pixelPos, float2 screenPos, float2 screenPosUV, float3 noise)
	{
		float3 viewEyePosition = g_eyePos;

		// float3 ScreenToWorldPosition2(float depth, float maxDepth, float3 dirToPixelWS, float3 cameraPos)

		float3 eyeVec = normalize(pixelPos - g_eyePos);

		//return float3(eyeVec);

		float3 viewPixelPosition = ScreenToWorldPosition2(pixelDepth, g_frustumDepths[3], eyeVec, g_eyePos);

		//float3 viewPixelPosition = ScreenToWorldPosition(pixelDepth, g_frustumDepths[3], screenPos, float2(g_screenWidth, g_screenHeight), g_viewProjectionMatrixInverse);// VSPositionFromDepth(pixelDepth, screenPosUV);// pixelPos;

		//if (pixelDepth == -1)
			//return float3(0, 0, 0);
			//viewPixelPosition = VSPositionFromDepth(pixelDepth, screenPosUV);

		//pixelPos = pixelPos * 1.0f;

		float3 startPos = viewEyePosition;
		float3 endPos = viewPixelPosition;

		float3 direction = (endPos - startPos);

		//float2 noisePos = float2(screenPos.x / g_screenWidth, screenPos.y / g_screenHeight);

		//noisePos.xy += g_time * 0.01f;

		// sample the noise
		//float4 noise = g_noiseTexture.Sample(g_pointSampler, noisePos);

		//direction.x += cos(noise.x * 2 * PI) * 0.25f;
		//direction.y += sin(noise.x * 2 * PI) * 0.25f;

		float traceLen = length(direction);
		direction /= traceLen;

		int index = 0;

		const int numSteps = g_atmosphere.volumetricStepsMax;
		float stepLength = g_atmosphere.volumetricStepIncrement;//traceLen / (float)numSteps; // g_atmosphere.volumetricStepIncrement;//

		float3 tracePos = startPos;
		float totalTraceLen = 0.0f;

		float accumFog = 0.0f;

#if 0
		float4 noise = g_noiseTexture.Sample(g_textureSampler, screenPosUV * float2(g_screenWidth / 128.0f, g_screenHeight / 128.0f) /** 4.0f*/);

		direction *= (noise.xyz /** 0.5*/);
		direction = normalize(direction);
#endif

		/*const float ditherPattern[4][4] = {
			{ 0.0f, 0.5f, 0.125f, 0.625f},
			{ 0.75f, 0.22f, 0.875f, 0.375f},
			{ 0.1875f, 0.6875f, 0.0625f, 0.5625},
			{ 0.9375f, 0.4375f, 0.8125f, 0.3125}
		};*/

#if 0
		const float ditherPattern[4][4] = {
			{00.0 / 16.0, 12.0 / 16.0, 03.0 / 16.0, 15.0 / 16.0},
			{08.0 / 16.0, 04.0 / 16.0, 11.0 / 16.0, 07.0 / 16.0},
			{02.0 / 16.0, 14.0 / 16.0, 01.0 / 16.0, 13.0 / 16.0},
			{10.0 / 16.0, 06.0 / 16.0, 09.0 / 16.0, 05.0 / 16.0}
		};

		float ditherValue = ditherPattern[screenPos.x % 4][screenPos.y % 4];

		float goldenRatio = 1.61803398875;
		float invGoldenRatio = 1.0 / goldenRatio;

		float blue = ditherValue;
		float startOffset = blue * 1.0;
		startOffset = blue + frac(g_time) * 10.0f;// invGoldenRatio;

		tracePos += direction * startOffset;
#endif

		tracePos += direction + noise * .8f;// + frac(g_time * 50.0f));

		int numHits = 0;

		[loop]
		for (int i = 0; i < numSteps; i++)
		{
			if (g_shadowConfig.cascadeOverride != -1)
			{
				index = g_shadowConfig.passIndex;
			}
			else
			{
				if (totalTraceLen <= g_frustumDepths[0])
				{
					index = 0;
				}
				else if (totalTraceLen <= g_frustumDepths[1])
				{
					index = 1;
				}
				else if (totalTraceLen <= g_frustumDepths[2])
				{
					index = 2;
				}
				else
				{
					index = 3;
				}
			}

			float4 pixelPosShadow = float4(tracePos, 1.0f);

			pixelPosShadow = mul(pixelPosShadow, g_lightViewProjectionMatrix[index]);

			float depthFromLight = pixelPosShadow.z / pixelPosShadow.w;			

			float2 projectTexCoord;

			projectTexCoord.x = pixelPosShadow.x / pixelPosShadow.w / 2.0f + 0.5f;
			projectTexCoord.y = -pixelPosShadow.y / pixelPosShadow.w / 2.0f + 0.5f;

			// Determine if the projected coordinates are in the 0 to 1 range.  If so then this pixel is in the view of the light.
			if ((saturate(projectTexCoord.x) == projectTexCoord.x) && (saturate(projectTexCoord.y) == projectTexCoord.y))
			{
				float shadowDepth = 1.0f;

				if (index == 0)
				{
					//shadowDepth = SampleDepth(SHADOWMAPS[0], projectTexCoord.xy, depthFromLight);

					shadowDepth = SHADOWMAPS[0].Sample(g_pointSampler, projectTexCoord.xy).x;
				}
				else if (index == 1)
				{
					shadowDepth = SHADOWMAPS[1].Sample(g_pointSampler, projectTexCoord.xy).x;
				}
				else if (index == 2)
				{
					shadowDepth = SHADOWMAPS[2].Sample(g_pointSampler, projectTexCoord.xy).x;
				}
				else if (index == 3)
				{
					shadowDepth = SHADOWMAPS[3].Sample(g_pointSampler, projectTexCoord.xy).x;
				}

				if (shadowDepth > depthFromLight)
				{
					accumFog += ComputeScattering(dot(direction, normalize(g_shadowCasterLightDir.xyz))) * g_atmosphere.volumetricStrength;
					numHits++;
				}
			}

			tracePos += (direction * stepLength);

			if (length(tracePos - startPos) >= traceLen)
				break;

			//if(tracePos.y)
			totalTraceLen += stepLength;			
		}

		accumFog /= (float)numSteps;

		return saturate(getSunColour() * accumFog);
	}

	float4 ShaderMain(UIPixelInput input) : SV_TARGET
	{
		float2 texcoord = input.texcoord;

		float2 screenPos = float2(input.position.x / (float)g_screenWidth * 2, input.position.y / (float)g_screenHeight * 2);

		// Sample the gbuffer
		//
		//float4 pixelColour = GBUFFER_DIFFUSE.Sample(g_pointSampler, screenPos);
		float4 pixelNormal = GBUFFER_NORMAL.Sample(g_pointSampler, screenPos);
		float4 pixelPosWS = GBUFFER_POSITION.Sample(g_pointSampler, screenPos);
		//float4 pixelSpecular = GBUFFER_SPECULAR.Sample(g_pointSampler, screenPos);

		float2 noiseSamplePos = screenPos * 8.0f;

		//noiseSamplePos += frac(g_time) * 5.0f;

		float3 noise = g_noiseTexture.Sample(g_pointSampler, noiseSamplePos).rgb;

		float3 singleNoiseVal = noise;

		// move from 0-1 to -1 to +1
		singleNoiseVal = (singleNoiseVal * 2.0f) - 1.0f;

		// close the range
		singleNoiseVal /= 16.0f;

		//singleNoiseVal *= 5.0f;

		float3 volumetric = CalculateVolumetricLighting(pixelNormal.w, pixelPosWS.xyz, input.position.xy, screenPos, noise);

		//volumetric.xyz += singleNoiseVal.xyz;

		return saturate(float4(volumetric, 1.0f));
	}
}