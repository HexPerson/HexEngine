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
	Water
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
	Texture2D g_atmosphereTexture : register(t5);

	//Texture2D g_inputTex : register(t0);

	SamplerState g_textureSampler : register(s0);
	SamplerComparisonState g_cmpSampler : register(s1);
	SamplerState g_pointSampler : register(s2);

	static matrix Identity =
	{
		{ 1, 0, 0, 0 },
		{ 0, 1, 0, 0 },
		{ 0, 0, 1, 0 },
		{ 0, 0, 0, 1 }
	};

	float4 ShaderMain(UIPixelInput input) : SV_TARGET
	{
		//return float4(1,0,0, 1.0f);

		float2 texcoord = input.texcoord;

		float2 screenPos = float2(input.position.x / (float)g_screenWidth, input.position.y / (float)g_screenHeight);

		// Sample the gbuffer
		//
		float4 pixelColour = GBUFFER_DIFFUSE.Sample(g_pointSampler, screenPos);
		float4 pixelNormal = GBUFFER_NORMAL.Sample(g_pointSampler, screenPos);
		float4 worldPos = GBUFFER_POSITION.Sample(g_pointSampler, screenPos);

		// don't fog over emissive pixels
		if (pixelNormal.w == g_frustumDepths[3])
		{
			return float4(pixelColour.rgb, 1.0f);
		}

		float pixelDepth = pixelNormal.w;

		float4 fogColour;// = float4(g_globalLight[1], g_globalLight[2], g_globalLight[3], 1);

		/// --------- CALCULATE FOG ----------- //
		

#if 1

		float4 pixelPos = mul(worldPos, g_viewMatrix);
		pixelPos = mul(pixelPos, g_projectionMatrix);

		float4 sunPosition = g_lightPosition;
		sunPosition.w = 1.0f;
		sunPosition = mul(sunPosition, Identity);
		sunPosition = mul(sunPosition, g_viewMatrix);
		sunPosition = mul(sunPosition, g_projectionMatrix);

		float2 projectedPixel = pixelPos.xy / pixelPos.w / 2.f + 0.5f;
		float2 projectedSun = sunPosition.xy / sunPosition.w / 2.f + 0.5f;

		float aspect = (float)g_screenWidth / (float)g_screenHeight;

		float3 atmosphericFogColour = getAtmosphereColour(
			(worldPos.y / 12000.0f),
			projectedPixel.xy * 1.0f,
			projectedSun.xy * 1.0f,
			aspect,
			false/*sunPosition.w > 0.0f*/);

		atmosphericFogColour = jodieReinhardTonemap(atmosphericFogColour);
		atmosphericFogColour = pow(atmosphericFogColour, float3(2.2, 2.2, 2.2));
		fogColour = float4(atmosphericFogColour.rgb, 1.0f);
#else

		// use ATMOSPHERE COLOUR?
		fogColour = float4(getSunColour() * 0.8, 1.0f);//g_atmosphereTexture.Sample(g_pointSampler, screenPos);
#endif


		float fogStart = 140.0f;
		float fogEnd = 1050.0f;
		float fogRange = fogEnd - fogStart;

		float fogStrength = 1.0f;

		

		//if (pixelDepth >= 0.0f)
		{
			float fogDepth = g_frustumDepths[3] - pixelDepth;
			float fogDist = length(worldPos.xyz - g_eyePos.xyz);

			float waveHeightAtWorldPos = 0.0f;
			float startPos = 0.0f;
				
			waveHeightAtWorldPos += GerstnerWaveHeight(_WaveA, startPos);
			waveHeightAtWorldPos += GerstnerWaveHeight(_WaveB, startPos);
			waveHeightAtWorldPos += GerstnerWaveHeight(_WaveC, startPos);
			waveHeightAtWorldPos += GerstnerWaveHeight(_WaveD, startPos);

			//waveHeightAtWorldPos -= 1.5f;

			bool isPixelUnderWater = waveHeightAtWorldPos <= 0.0f;
			bool isCameraUnderWater = g_eyePos.y <= waveHeightAtWorldPos;

			if(isCameraUnderWater)
			{
				if(pixelDepth >= g_frustumDepths[3] /* || pixelDepth == -1 */)
					fogDist = 10.0f;
			}

			const float waterFogOffset = 10.0f;
			const float waterFogFadeDistance = 30.0f;
			
			if(false && worldPos.y <= waveHeightAtWorldPos /* - waterFogOffset */ /* && isCameraUnderWater */)
			{
				// exponential
				float fogFactor = saturate(exp2(-( g_frustumDepths[3] - (fogDist * 5.1f) ) * (g_atmosphere.fogDensity / 2.8)  /*0.0030f*/));

				// linear
				//float fogFactor = saturate((1500 - (g_frustumDepths[3] - pixelDepth)) / 300);

				const float3 underWaterFogColour = float3(27.0f / 255.0f, 45.0f / 255.0f, 51.0f / 255.0f) * g_globalLight[0];

				float heightFactor = 1.0f - saturate(exp2(-(/* waveHeightAtWorldPos */0.0f - worldPos.y - waterFogOffset) / waterFogFadeDistance));

				//if(isCameraUnderWater == false)
				{
					//float heightFadeFactorFromSurface = saturate((waveHeightAtWorldPos - g_eyePos.y) / 50.0f);
					
					//fogFactor = lerp(0.0f, 1.0f, heightFadeFactorFromSurface);

					//heightFactor =min(heightFactor, 0.66f);
				}

				float3 foggedAlbedo = lerp(pixelColour.rgb, underWaterFogColour, fogFactor * heightFactor);

				

				return float4(foggedAlbedo, 1.0f);
			}
			else
			{
				// exponential
				float fogFactor = saturate(exp2(-(g_frustumDepths[3]-fogDist) /** fogDepth*/ * g_atmosphere.fogDensity /*0.0030f*/));

				// linear
				//float fogFactor = saturate((fogEnd - (g_frustumDepths[3] - pixelDepth)) / fogRange);

				float3 foggedAlbedo = lerp(pixelColour.rgb, fogColour, fogFactor);

				return float4(foggedAlbedo, 1.0f);
			}
			
			// handle fog when under water
			/*if (isCameraUnderWater)
			{
				// exponential
				//float fogFactor = saturate(exp2(-fogDepth * fogDepth * 0.00000007));

				//float fogFactor = saturate(exp2(-fogDepth * 0.0002));

				// linear
				//float fogFactor = saturate(((g_frustumDepths[3] - fogEnd) - (g_frustumDepths[3] - pixelDepth)) / fogRange);

				

				float fogFactor = saturate((fogStart + fogDist) / fogRange);

				float3 foggedAlbedo = lerp(pixelColour.rgb, g_oceanConfig.fogColour.rgb, fogFactor);

				return float4(foggedAlbedo, 1.0f);
			}*/
		}
		return float4(pixelColour.rgb, 1.0f);
	}
}