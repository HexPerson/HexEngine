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

	float hash11(float p)
	{
		return frac(sin(p * 127.1f) * 43758.5453123f);
	}

	float noise1D(float x)
	{
		float i = floor(x);
		float f = frac(x);
		float a = hash11(i);
		float b = hash11(i + 1.0f);
		float u = f * f * (3.0f - 2.0f * f);
		return lerp(a, b, u);
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

		if (fullSkyPass)
		{
			float auroraIntensity = max(0.0f, g_weatherSurface.auroraParams.x);
			float auroraNight = saturate((-sunDir.y + 0.02f) / 0.22f);
			if (auroraIntensity > 0.001f && auroraNight > 0.0f)
			{
				float auroraSpeed = max(0.01f, g_weatherSurface.auroraParams.y);
				float auroraBanding = max(0.25f, g_weatherSurface.auroraParams.z);
				float auroraHeight = clamp(g_weatherSurface.auroraParams.w, 0.05f, 0.85f);
				float3 horizonDir = normalize(float3(viewDir.x, 0.0f, viewDir.z) + float3(0.0001f, 0.0f, 0.0001f));
				float wrapA = horizonDir.x * 0.83f + horizonDir.z * 1.27f;
				float wrapB = horizonDir.x * -1.41f + horizonDir.z * 0.62f;
				float wrapC = horizonDir.x * 1.12f + horizonDir.z * -0.94f;
				float sweep = wrapA * (3.8f + auroraBanding * 0.48f) + wrapB * 1.7f;
				float ribbonWaveA = sin(sweep * 1.15f + wrapC * 1.8f + g_time * auroraSpeed * 0.18f);
				float ribbonWaveB = sin(sweep * 1.95f - wrapB * 2.1f - g_time * auroraSpeed * 0.26f);
				float ribbonNoise = noise1D(wrapA * 5.4f + wrapB * 3.1f + g_time * auroraSpeed * 0.08f);

				float baseCenter = auroraHeight + ribbonWaveA * 0.08f + ribbonWaveB * 0.05f + (ribbonNoise - 0.5f) * 0.10f;
				float centerB = baseCenter + 0.10f + sin(sweep * 1.55f + 1.8f) * 0.04f;
				float centerC = baseCenter - 0.08f + sin(sweep * 0.92f - 0.9f) * 0.03f;

				float thicknessA = 0.18f;
				float thicknessB = 0.14f;
				float thicknessC = 0.12f;

				float ribbonA = smoothstep(baseCenter - thicknessA, baseCenter - thicknessA * 0.15f, viewDir.y) *
					(1.0f - smoothstep(baseCenter + thicknessA * 0.55f, baseCenter + thicknessA * 1.55f, viewDir.y));
				float ribbonB = smoothstep(centerB - thicknessB, centerB - thicknessB * 0.15f, viewDir.y) *
					(1.0f - smoothstep(centerB + thicknessB * 0.55f, centerB + thicknessB * 1.45f, viewDir.y));
				float ribbonC = smoothstep(centerC - thicknessC, centerC - thicknessC * 0.10f, viewDir.y) *
					(1.0f - smoothstep(centerC + thicknessC * 0.55f, centerC + thicknessC * 1.35f, viewDir.y));

				float ribbons = max(ribbonA, max(ribbonB * 0.72f, ribbonC * 0.58f));
				float horizonAnchor = smoothstep(0.01f, 0.18f, viewDir.y);
				float topFade = 1.0f - smoothstep(0.62f, 0.92f, viewDir.y);
				float broadPresence = saturate(ribbons * horizonAnchor * topFade);

				float foldNoiseA = noise1D(wrapA * 22.0f + wrapB * 11.0f + viewDir.y * 8.0f - g_time * auroraSpeed * 0.20f);
				float foldNoiseB = noise1D(wrapA * -17.0f + wrapB * 26.0f - viewDir.y * 11.0f + g_time * auroraSpeed * 0.32f);
				float foldMix = saturate(0.62f + (foldNoiseA - 0.5f) * 0.38f + (foldNoiseB - 0.5f) * 0.26f);
				float foldVeins = 0.82f + 0.18f * sin(wrapA * 28.0f + wrapB * 17.0f + viewDir.y * 6.0f + g_time * auroraSpeed * 0.15f);
				float curtainDetail = saturate(foldMix * foldVeins);

				float glowBias = saturate(0.62f + 0.16f * ribbonWaveA + 0.08f * ribbonWaveB);
				float auroraMask = broadPresence * lerp(0.80f, 1.12f, curtainDetail) * glowBias * auroraNight;

				float greenDominance = saturate(0.72f + ribbonA * 0.34f + broadPresence * 0.20f - ribbonC * 0.12f);
				float purplePlumeMask = saturate(
					(1.0f - ribbonA) * 0.22f +
					ribbonB * 0.78f +
					max(0.0f, ribbonWaveB) * 0.22f +
					(curtainDetail - 0.45f) * 0.28f);
				float magentaCore = saturate(pow(purplePlumeMask, 1.55f) * (0.55f + 0.45f * foldMix));
				float plumeVariation = saturate(0.5f + 0.5f * sin(wrapA * 4.4f + wrapC * 3.2f + g_time * auroraSpeed * 0.07f));

				float3 greenSheet = lerp(
					g_weatherSurface.auroraColorA.rgb,
					float3(0.55f, 1.00f, 0.52f),
					0.46f);
				float3 magentaPlume = lerp(
					float3(0.92f, 0.18f, 0.76f),
					float3(0.66f, 0.32f, 1.00f),
					plumeVariation);

				float3 auroraColour = greenSheet * greenDominance;
				auroraColour = lerp(auroraColour, magentaPlume, magentaCore * 0.82f);

				float horizonBloom = smoothstep(auroraHeight - 0.22f, auroraHeight + 0.02f, viewDir.y) * (1.0f - smoothstep(auroraHeight + 0.10f, auroraHeight + 0.28f, viewDir.y));
				float3 bloomColour = lerp(float3(0.38f, 0.95f, 0.44f), float3(0.74f, 0.24f, 0.82f), magentaCore * 0.35f);
				atmosphereColour += bloomColour * (horizonBloom * broadPresence * auroraIntensity * 0.32f);
				atmosphereColour += auroraColour * (auroraMask * auroraIntensity * 1.72f);
			}

			float boltIntensity = max(0.0f, g_weatherSurface.lightningBoltData.x);
			if (boltIntensity > 0.001f)
			{
				float3 boltDir = normalize(g_weatherSurface.lightningBoltDirection.xyz);
				float3 basisUp = abs(boltDir.y) > 0.92f ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f);
				float3 boltRight = normalize(cross(basisUp, boltDir));
				float3 boltDown = normalize(cross(boltDir, boltRight));
				float localX = dot(viewDir, boltRight);
				float localY = dot(viewDir, boltDown);
				float descend = saturate((-localY) / 0.42f);
				float strikeProgress = saturate(g_weatherSurface.lightningBoltData.z);
				float reveal = smoothstep(descend - 0.10f, descend + 0.015f, strikeProgress);
				float visualEnvelope = 1.0f - smoothstep(0.22f, 0.88f, strikeProgress);
				float width = max(0.0022f, g_weatherSurface.lightningBoltData.w);
				float seed = g_weatherSurface.lightningBoltData.y;
				float branching = saturate(g_weatherSurface.lightningBoltDirection.w);
				float freqA = lerp(15.0f, 24.0f, hash11(seed * 0.113f));
				float freqB = lerp(28.0f, 43.0f, hash11(seed * 0.197f));
				float freqC = lerp(18.0f, 33.0f, hash11(seed * 0.271f));
				float ampA = lerp(1.1f, 1.9f, hash11(seed * 0.347f));
				float ampB = lerp(0.35f, 0.85f, hash11(seed * 0.419f));
				float branchStrength = lerp(1.8f, 4.6f, hash11(seed * 0.563f)) * lerp(0.72f, 1.22f, branching);
				float branchBias = hash11(seed * 0.617f) * 6.28318f;
				float wiggle = (sin(descend * freqA + seed * 1.7f) * ampA + sin(descend * freqB + seed * 2.9f) * ampB) * width * (1.2f + descend * 2.1f);
				float core = 1.0f - smoothstep(width * 0.22f, width * 1.18f, abs(localX - wiggle));
				float branchGate = smoothstep(0.16f, 0.58f, descend) * (1.0f - smoothstep(0.62f, 0.92f, descend));
				float branchOffset = wiggle + sin(descend * freqC + seed * 4.7f + branchBias) * width * branchStrength;
				float branch = (1.0f - smoothstep(width * 0.18f, width * 0.92f, abs(localX - branchOffset))) * branchGate;
				float secondaryOffset = wiggle + sin(descend * (freqC * 0.74f) + seed * 6.1f + branchBias * 1.6f) * width * branchStrength * 0.62f;
				float secondaryBranchGate = smoothstep(0.28f, 0.74f, descend) * (1.0f - smoothstep(0.70f, 0.98f, descend));
				float secondaryBranch = (1.0f - smoothstep(width * 0.16f, width * 0.66f, abs(localX - secondaryOffset))) * secondaryBranchGate * branching;
				float horizonMask = smoothstep(0.00f, 0.07f, viewDir.y);
				float boltMask = (core + branch * lerp(0.28f, 0.52f, branching) + secondaryBranch * 0.18f) * reveal * visualEnvelope * smoothstep(0.08f, 0.98f, descend) * horizonMask;
				float halo = (1.0f - smoothstep(width * 1.8f, width * 8.0f, abs(localX - wiggle))) * reveal * visualEnvelope * smoothstep(0.02f, 0.96f, descend) * horizonMask;
				float branchHalo = (1.0f - smoothstep(width * 1.2f, width * 5.2f, abs(localX - branchOffset))) * branchGate * reveal * visualEnvelope * horizonMask;
				float originGlow = (1.0f - smoothstep(0.02f, 0.26f, length(float2(localX, localY + 0.02f)))) * visualEnvelope * horizonMask;
				float3 boltColour = lerp(float3(0.70f, 0.82f, 1.0f), float3(1.0f, 1.0f, 1.0f), 0.55f);
				float3 haloColour = lerp(float3(0.32f, 0.46f, 0.95f), float3(0.58f, 0.72f, 1.0f), 0.55f);
				atmosphereColour += haloColour * ((halo * 0.95f + branchHalo * 0.38f + originGlow * 0.42f) * boltIntensity * 1.85f);
				atmosphereColour += boltColour * (boltMask * boltIntensity * 5.2f);
			}
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
