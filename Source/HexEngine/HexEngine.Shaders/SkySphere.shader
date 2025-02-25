"InputLayout"
{
	Pos_INSTANCED
}
"VertexShaderIncludes"
{
	SkySphereCommon
	Utils
}
"PixelShaderIncludes"
{
	SkySphereCommon
	Atmosphere
	Utils
}
"VertexShader"
{
	static matrix Identity =
	{
		{ 1, 0, 0, 0 },
		{ 0, 1, 0, 0 },
		{ 0, 0, 1, 0 },
		{ 0, 0, 0, 1 }
	};

	MeshPixelInput ShaderMain(MeshVertexInput input, MeshInstanceData instance)
	{
		MeshPixelInput output;
		
		input.position.w = 1.0f;

		float4 sunPosition = -g_lightDirection * 500.0f;// g_lightPosition;
		sunPosition.w = 1.0f;

		output.position = mul(input.position, instance.world);

		output.positionWS = output.position;

		output.position = mul(output.position, g_viewProjectionMatrix);

		// Calculate velocity
		float4x4 prevFrame_modelMatrix = instance.worldPrev;
		float4 prevFrame_worldPos = mul(input.position, prevFrame_modelMatrix);
		float4 prevFrame_clipPos = mul(prevFrame_worldPos, g_viewProjectionMatrixPrev);

		output.previousPositionUnjittered = prevFrame_clipPos;
		output.currentPositionUnjittered = output.position;

		// Apply TAA jitter
		output.position.xy += g_jitterOffsets * output.position.w;

		output.skyPixelPos = output.position;

		output.sunScreenPos = mul(sunPosition, instance.world);
		output.sunScreenPos = mul(output.sunScreenPos, g_viewProjectionMatrix);

		// Calculate the light dir
		output.gradientPosition = input.position;
		
		return output;
	}
}
"PixelShader"
{	
	Texture2D g_noiseTexture : register(t1);

	SamplerState g_pointSampler : register(s2);

	static float4 topColour = float4(0.322f, 0.467f, 0.757f, 1.0f);
	static float4 bottomColour = float4(0.576f, 0.824f, 1.0f, 1.0f);

	GBufferOut ShaderMain(MeshPixelInput input)
	{
		float4 colour = topColour;
		float height = input.gradientPosition.y;

		// clamp to horizon
		//if (height < 0.0f)
		//	height = 0.0f;	

		float aspect = (float)g_screenWidth / (float)g_screenHeight;

		float2 projectedSun, projectedPixel;

		projectedSun = input.sunScreenPos.xy / input.sunScreenPos.w / 2.f + 0.5f;
		projectedPixel = input.skyPixelPos.xy / input.skyPixelPos.w / 2.f + 0.5f;

		float3 atmosphereColour;

		if (input.sunScreenPos.w > 0.0f)
		{
			atmosphereColour = getAtmosphericScattering(
				-g_lightDirection.y * 2.0f,
				height * 2.0f,
				projectedPixel.xy * 2.0f,
				projectedSun.xy * 2.0f,
				aspect,
				true);
		}
		else
		{

			atmosphereColour = getAtmosphericScattering(
				-g_lightDirection.y * 2.0f,
				height * 2.0f,
				projectedPixel.xy * 2.0f,
				projectedSun.xy * 2.0f,
				aspect,
				false);

		}

		atmosphereColour = jodieReinhardTonemap(atmosphereColour);
		atmosphereColour = pow(atmosphereColour, float3(2.2, 2.2, 2.2));

		colour = float4(atmosphereColour, 1.0f);
		

		GBufferOut output;

		bool isCameraUnderWater = false;// g_eyePos.y <= 0.0f;

		if (isCameraUnderWater && input.positionWS.y <= 0.0f)
			colour.rgb = g_oceanConfig.fogColour.rgb;

		float2 noiseSamplePos = projectedPixel.xy * 4.0f;

		noiseSamplePos += frac(g_time) * 2.0f;

		float3 noise = g_noiseTexture.Sample(g_pointSampler, noiseSamplePos).rgb;

		float3 singleNoiseVal = noise;

		// move from 0-1 to -1 to +1
		//singleNoiseVal = (singleNoiseVal * 2.0f) - 1.0f;

		// close the range
		singleNoiseVal /= 8.0f;

		float2 velocity = CalcVelocity(input.currentPositionUnjittered, input.previousPositionUnjittered, float2(g_screenWidth, g_screenHeight));
		//velocity /= float2(g_screenWidth, g_screenHeight);

		output.diff = float4(colour.rgb + singleNoiseVal, -1);// lerp(output.diff, fogColour, fogLerp);

		output.mat = float4(0, 0, 0, 0);

		output.norm = float4(0, 0, 0, g_frustumDepths[3]);

		output.velocity = velocity;

		// project it out as far as possible to mimic far away sky
		float3 worldSpaceDir = normalize(input.positionWS.xyz - g_eyePos);
		float3 worldSpacePos = g_eyePos + worldSpaceDir * g_frustumDepths[3];

		output.pos = float4(worldSpacePos, -1.0f);

		return output;
	}
}