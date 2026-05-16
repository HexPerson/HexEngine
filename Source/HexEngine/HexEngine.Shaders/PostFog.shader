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
	AtmospherePhysical
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
	Texture2D g_depthTexture : register(t6);

	SamplerState g_textureSampler : register(s0);
	SamplerComparisonState g_cmpSampler : register(s1);
	SamplerState g_pointSampler : register(s2);

	float4 ShaderMain(UIPixelInput input) : SV_TARGET
	{
		//return float4(1,0,0, 1.0f);

		float2 texcoord = input.texcoord;

		float2 screenPos = float2(input.position.x / (float)g_screenWidth, input.position.y / (float)g_screenHeight);

		// Sample the gbuffer
		//
		float4 pixelColour = GBUFFER_DIFFUSE.Sample(g_pointSampler, screenPos);
		float depthSample = g_depthTexture.Sample(g_pointSampler, screenPos).r;

		// don't fog over emissive pixels
		if (depthSample >= 0.999999f)
		{
			return float4(pixelColour.rgb, 1.0f);
		}

		float2 ndcXY = float2(screenPos.x * 2.0f - 1.0f, (1.0f - screenPos.y) * 2.0f - 1.0f);
		float4 clipPos = float4(ndcXY, depthSample, 1.0f);
		float4 worldPosH = mul(clipPos, g_viewProjectionMatrixInverse);
		float3 worldPos = worldPosH.xyz / max(worldPosH.w, 1e-5f);

		float fogDist = length(worldPos - g_eyePos.xyz);
		float startDistance = max(0.0f, g_atmosphere.fogStartDistance);
		float distanceDensity = max(0.0f, g_atmosphere.fogDensity);
		float heightDensity = max(0.0f, g_atmosphere.fogHeightDensity);
		float heightFalloff = max(0.0001f, g_atmosphere.fogHeightFalloff);
		float heightPivot = g_atmosphere.fogHeightPivot;
		float skyTintInfluence = saturate(g_atmosphere.fogSkyTintInfluence);
		float farDesatStrength = saturate(g_atmosphere.fogFarDesaturate);
		float atmosphereBlendStart = max(startDistance, g_atmosphere.fogAtmosphereBlendStart);
		float atmosphereBlendRange = max(1.0f, g_atmosphere.fogAtmosphereBlendRange);
		float sunsetRange = max(0.02f, g_atmosphere.fogSunsetRange);
		float sunsetWarmthStrength = saturate(g_atmosphere.fogSunsetWarmthStrength);
		float farAtmosphereMatchStrength = saturate(g_atmosphere.fogFarAtmosphereMatchStrength);

		float effectiveDist = max(0.0f, fogDist - startDistance);
		if (effectiveDist <= 0.0001f)
			return float4(pixelColour.rgb, 1.0f);

		// Integrate height fog along the ray so density placement does not depend on camera height alone.
		float distExtinction = effectiveDist * distanceDensity;
		float rayHeightDelta = worldPos.y - g_eyePos.y;
		float heightRayMid = g_eyePos.y + rayHeightDelta * 0.5f;
		float heightRaySpan = abs(rayHeightDelta) * 0.5f + 1.0f;
		float heightBandBase = exp2(-(heightRayMid - heightPivot) * heightFalloff);
		float heightVariation = saturate(rayHeightDelta * heightFalloff / heightRaySpan);
		float heightExtinction = heightBandBase * heightDensity * effectiveDist;
		heightExtinction *= lerp(1.0f, 0.82f, heightVariation);
		float extinction = distExtinction + heightExtinction;
		float fogFactor = saturate(1.0f - exp2(-extinction));

		float3 rayDir = normalize(worldPos - g_eyePos.xyz);
		float3 sunDir = normalize(-g_lightDirection.xyz);

		// Use the physical model for spectral attenuation along the view ray.
		const int fogSteps = 12;
		PhysicalAtmosphereSample raySample = IntegrateAtmospherePhysical(
			g_eyePos.xyz + rayDir * startDistance,
			rayDir,
			effectiveDist,
			sunDir,
			fogSteps,
			false);

		// Probe the atmosphere from a stable reference origin so fog colour is independent of fog band placement.
		const float3 fogProbeOrigin = float3(0.0f, 0.0f, 0.0f);

		// Probe the atmosphere with stable, camera-independent directions for fog colour.
		float3 zenithDir = float3(0.0f, 1.0f, 0.0f);
		float3 antiSunDir = normalize(float3(-sunDir.x, 0.18f, -sunDir.z));
		float3 nearSunDir = normalize(float3(sunDir.x, 0.16f, sunDir.z));

		PhysicalAtmosphereSample zenithProbe = IntegrateAtmospherePhysical(fogProbeOrigin, zenithDir, g_frustumDepths[3], sunDir, 18, false);
		PhysicalAtmosphereSample horizonProbe = IntegrateAtmospherePhysical(fogProbeOrigin, antiSunDir, g_frustumDepths[3], sunDir, 18, false);
		PhysicalAtmosphereSample sunsetProbe = IntegrateAtmospherePhysical(fogProbeOrigin, nearSunDir, g_frustumDepths[3], sunDir, 18, false);
		float3 farSkyColour = g_atmosphereTexture.Sample(g_textureSampler, screenPos).rgb;

		float3 ambientFogBase = max(g_atmosphere.ambientLight.rgb, float3(0.001f, 0.001f, 0.001f));

		float heightFogWeight = saturate(heightExtinction / max(extinction, 0.001f));
		float raySampleLuma = max(dot(raySample.inscatter, float3(0.299f, 0.587f, 0.114f)), 0.001f);
		float3 viewRayFog = raySample.inscatter / raySampleLuma;

		float sunElevation = -g_lightDirection.y;
		float sunsetWeight = saturate((sunsetRange - sunElevation) / max(sunsetRange, 0.02f)) * (1.0f - saturate((-0.02f - sunElevation) / 0.10f));
		float nightWeight = saturate((-sunElevation + 0.02f) / 0.20f);
		float distanceAtmosphereBlend = saturate((fogDist - atmosphereBlendStart) / atmosphereBlendRange);
		float horizonColourMix = lerp(0.80f, 0.54f, nightWeight);
		float3 directionalFog = lerp(horizonProbe.inscatter, raySample.inscatter, saturate(0.28f + distanceAtmosphereBlend * 0.34f));
		float3 farMatchedFog = lerp(directionalFog, farSkyColour, farAtmosphereMatchStrength * distanceAtmosphereBlend);
		float3 heightDominantFog = lerp(farMatchedFog, horizonProbe.inscatter, heightFogWeight * 0.72f);
		float3 baseAtmosphereFog = lerp(heightDominantFog, zenithProbe.inscatter, nightWeight * 0.18f);
		baseAtmosphereFog = lerp(baseAtmosphereFog, horizonProbe.inscatter, horizonColourMix * 0.22f);
		float sunsetBlend = sunsetWeight * distanceAtmosphereBlend * sunsetWarmthStrength * 0.18f;
		float3 physicalFogColour = lerp(baseAtmosphereFog, sunsetProbe.inscatter, sunsetBlend);

		// At night, pull back toward cooler zenith/ambient haze so the fog does not keep the horizon's warm tint.
		float3 coolNightFog = lerp(zenithProbe.inscatter, ambientFogBase, 0.10f);
		physicalFogColour = lerp(physicalFogColour, coolNightFog, nightWeight * 0.65f);

		// Preserve atmospheric hue while only using ambient light as an energy floor, not as an authored tint.
		float physicalLuma = max(dot(physicalFogColour, float3(0.299f, 0.587f, 0.114f)), 0.001f);
		float baseLuma = max(dot(ambientFogBase, float3(0.299f, 0.587f, 0.114f)), 0.001f);
		float3 physicalHue = physicalFogColour / physicalLuma;
		float hueStrength = saturate((physicalHue.b - max(physicalHue.r, physicalHue.g)) * 0.45f);
		physicalHue = lerp(physicalHue, viewRayFog, 0.18f + heightFogWeight * 0.22f);
		physicalHue = lerp(physicalHue, normalize(max(physicalFogColour, 0.0001f)) * 1.732f, hueStrength * 0.18f);
		physicalFogColour = saturate(physicalHue * max(physicalLuma, baseLuma * 0.35f));

		float fogSkyAmount = saturate(skyTintInfluence * 0.62f + distanceAtmosphereBlend * (0.20f + farAtmosphereMatchStrength * 0.26f));
		fogSkyAmount = lerp(fogSkyAmount, fogSkyAmount * 0.70f, nightWeight);
		float3 fogColour = lerp(ambientFogBase, physicalFogColour, fogSkyAmount);
		fogColour = lerp(fogColour, farSkyColour, farAtmosphereMatchStrength * distanceAtmosphereBlend * saturate(fogFactor + heightFogWeight * 0.35f));
		float fogLuma = dot(fogColour, float3(0.299f, 0.587f, 0.114f));
		float dayWeight = 1.0f - nightWeight;
		float distanceDesat = farDesatStrength * distanceAtmosphereBlend * lerp(0.30f, 0.10f, dayWeight);
		fogColour = lerp(fogColour, fogLuma.xxx, distanceDesat);

		float lightningFlash = saturate(g_weatherSurface.lightningFlash);
		if (lightningFlash > 0.0001f)
		{
			float3 lightningDir = normalize(g_weatherSurface.lightningBoltDirection.xyz + float3(1e-5f, 1e-5f, 1e-5f));
			float forwardScatter = saturate(dot(rayDir, lightningDir) * 0.5f + 0.5f);
			float3 lightningFog = float3(0.62f, 0.76f, 1.0f) * lightningFlash;
			fogColour += lightningFog * (0.10f + 0.24f * fogFactor + 0.22f * heightFogWeight + 0.16f * forwardScatter);
		}

		// Use spectral transmittance from the physical ray march, but keep the old height fog amount.
		float3 surfaceAttenuation = lerp(float3(1.0f, 1.0f, 1.0f), raySample.transmittance, fogFactor);
		float3 foggedAlbedo = pixelColour.rgb * surfaceAttenuation;
		foggedAlbedo = lerp(foggedAlbedo, fogColour, fogFactor);

		return float4(saturate(foggedAlbedo), 1.0f);
	}
}
