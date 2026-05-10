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
	AtmospherePhysical
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
	SamplerState g_pointSampler : register(s2);

	float hash12(float2 p)
	{
		float3 p3 = frac(float3(p.xyx) * 0.1031f);
		p3 += dot(p3, p3.yzx + 33.33f);
		return frac((p3.x + p3.y) * p3.z);
	}

	float starField(float3 dir, float3 cellSeed, float densityMul, float coreSize)
	{
		const float PI = 3.14159265359f;
		float2 uv = float2(atan2(dir.z, dir.x) / (2.0f * PI) + 0.5f, asin(clamp(dir.y, -1.0f, 1.0f)) / PI + 0.5f);
		float2 grid = float2(1400.0f, 700.0f) * densityMul;
		float2 p = uv * grid;
		float2 cell = floor(p);
		float2 f = frac(p) - 0.5f;

		float n = hash12(cell + cellSeed.xy);
		float starMask = smoothstep(0.9965f, 1.0f, n);
		float falloff = smoothstep(coreSize, 0.0f, length(f));
		return starMask * falloff;
	}

	GBufferOut ShaderMain(MeshPixelInput input)
	{
		// passIndex==0: base sky only (for fog sampling texture)
		// passIndex==1: full sky (visible frame)
		// default: full sky (backwards-compatible)
		bool sunVisible = input.sunScreenPos.w > 0.0f;
		float3 viewDir = normalize(input.positionWS.xyz - g_eyePos.xyz);
		float3 sunDir = normalize(-g_lightDirection.xyz);
		bool fullSkyPass = (g_shadowConfig.passIndex != 0);
		int steps = fullSkyPass ? 32 : 20;
		PhysicalAtmosphereSample skySample = IntegrateAtmospherePhysical(
			g_eyePos.xyz,
			viewDir,
			g_frustumDepths[3],
			sunDir,
			steps,
			fullSkyPass && sunVisible);

		float3 atmosphereColour = skySample.inscatter;
		float sunsetAmount = saturate((0.22f - sunDir.y) / 0.32f);
		float dayAmount = 1.0f - sunsetAmount;
		float sunsetWarmStrength = max(0.0f, g_atmosphere.sunsetWarmStrength);
		float sunsetCoolStrength = max(0.0f, g_atmosphere.sunsetCoolStrength);
		float sunsetGlowStrength = max(0.0f, g_atmosphere.sunsetGlowStrength);

		// Keep the daytime shaping light-touch: a slightly richer zenith and a brighter horizon, but no heavy recoloring.
		float horizonFactor = 1.0f - saturate(viewDir.y * 0.5f + 0.5f);
		float zenithFactor = saturate(viewDir.y * 0.5f + 0.5f);
		float sunFacing = saturate(dot(viewDir, sunDir) * 0.5f + 0.5f);
		float antiSunFacing = saturate(dot(viewDir, -sunDir) * 0.5f + 0.5f);
		float3 dayZenithTint = float3(0.97f, 0.99f, 1.02f);
		float3 dayHorizonTint = float3(1.05f, 1.03f, 1.00f);
		float3 daySunSideTint = float3(1.06f, 1.05f, 1.02f);

		float skyLuma = dot(atmosphereColour, float3(0.299f, 0.587f, 0.114f));
		float3 zenithTarget = atmosphereColour * dayZenithTint;
		float3 horizonTarget = max(atmosphereColour, dayHorizonTint * skyLuma * 1.03f);
		float3 sunSideTarget = max(atmosphereColour, daySunSideTint * skyLuma * 1.05f);

		atmosphereColour = lerp(atmosphereColour, zenithTarget, zenithFactor * dayAmount * 0.025f);
		atmosphereColour = lerp(atmosphereColour, horizonTarget, horizonFactor * dayAmount * 0.10f);
		atmosphereColour = lerp(atmosphereColour, sunSideTarget, sunFacing * horizonFactor * dayAmount * 0.06f);

		// Sunset shaping: warm near-sun horizon, cooler violet opposite the sun, and a soft dusk lift higher in the dome.
		float sunsetHorizon = sunsetAmount * horizonFactor;
		float sunsetZenith = sunsetAmount * saturate(1.0f - horizonFactor);
		float3 sunsetWarmTint = float3(1.28f, 0.58f, 0.30f);
		float3 sunsetOrangeTint = float3(1.18f, 0.46f, 0.24f);
		float3 sunsetPurpleTint = float3(0.70f, 0.42f, 0.86f);
		float3 sunsetVioletTint = float3(0.42f, 0.36f, 0.72f);
		float3 sunsetWarmTarget = max(atmosphereColour, sunsetWarmTint * skyLuma * 1.18f);
		float3 sunsetOrangeTarget = max(atmosphereColour, sunsetOrangeTint * skyLuma * 1.05f);
		float3 sunsetPurpleTarget = max(atmosphereColour, sunsetPurpleTint * skyLuma * 0.95f);
		float3 sunsetVioletTarget = max(atmosphereColour, sunsetVioletTint * skyLuma * 0.90f);

		atmosphereColour = lerp(atmosphereColour, sunsetWarmTarget, sunsetHorizon * sunFacing * (0.34f * sunsetWarmStrength));
		atmosphereColour = lerp(atmosphereColour, sunsetOrangeTarget, sunsetHorizon * sunFacing * (0.18f * sunsetWarmStrength));
		atmosphereColour = lerp(atmosphereColour, sunsetPurpleTarget, sunsetHorizon * antiSunFacing * (0.22f * sunsetCoolStrength));
		atmosphereColour = lerp(atmosphereColour, sunsetVioletTarget, sunsetZenith * antiSunFacing * (0.16f * sunsetCoolStrength));

		if (fullSkyPass && sunVisible)
		{
			float mu = dot(viewDir, sunDir);
			float3 sunColour = ComputePhysicalSunColour(g_eyePos.xyz, sunDir);

			float sunDisk = smoothstep(0.99984f, 0.999975f, mu);
			float sunAureole = pow(saturate((mu - 0.99835f) / (0.99984f - 0.99835f)), lerp(4.2f, 2.8f, sunsetAmount));
			float sunHalo = pow(saturate((mu - 0.9960f) / (0.99835f - 0.9960f)), lerp(7.5f, 4.8f, sunsetAmount));

			float3 diskColour = sunColour;
			float3 aureoleColour = lerp(sunColour, atmosphereColour, 0.36f);
			float3 haloColour = lerp(sunColour, atmosphereColour, 0.74f);

			atmosphereColour += diskColour * (sunDisk * lerp(0.56f, 0.92f, sunsetAmount));
			atmosphereColour += aureoleColour * (sunAureole * lerp(0.045f, 0.24f * sunsetGlowStrength, sunsetAmount));
			atmosphereColour += haloColour * (sunHalo * lerp(0.005f, 0.055f * sunsetGlowStrength, sunsetAmount));
		}

		atmosphereColour = jodieReinhardTonemap(atmosphereColour);
		atmosphereColour = pow(atmosphereColour, float3(2.2f, 2.2f, 2.2f));

		//atmosphereColour *= 1.5f.rrr;

		// Add procedural stars at night in visible sky pass only.
		if (g_shadowConfig.passIndex != 0)
		{
			float sunElevation = -g_lightDirection.y;
			float nightFactor = saturate((-sunElevation + 0.03f) / 0.25f);
			float horizonFade = smoothstep(0.02f, 0.20f, viewDir.y);

			float starsA = starField(viewDir, float3(17.13f, 53.91f, 11.0f), 1.0f, 0.85f);
			float starsB = starField(viewDir, float3(91.71f, 11.37f, 29.0f), 1.65f, 0.60f);
			float stars = saturate(starsA * 0.85f + starsB * 0.55f);

			float twinkle = 0.85f + 0.15f * sin(g_time * 2.2f + hash12(viewDir.xz * 137.0f) * 6.2831f);
			stars *= twinkle * nightFactor * horizonFade;

			float3 starColour = float3(0.82f, 0.87f, 1.0f);
			atmosphereColour += starColour * stars * 0.78f;
		}

		GBufferOut output;

		float2 velocity = CalcVelocity(input.currentPositionUnjittered, input.previousPositionUnjittered, float2(g_screenWidth, g_screenHeight));
		output.diff = float4(atmosphereColour, -1);

		output.mat = float4(0, 0, 0, 0);

		output.norm = float4(0, 0, 0, g_frustumDepths[3]);

		output.velocity = float2(0,0);//velocity;

		// project it out as far as possible to mimic far away sky
		float3 worldSpaceDir = normalize(input.positionWS.xyz - g_eyePos);
		float3 worldSpacePos = g_eyePos + worldSpaceDir * g_frustumDepths[3];

		output.pos = float4(worldSpacePos, -1.0f);

		return output;
	}
}
