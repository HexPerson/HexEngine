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

		// Distance-based fog extinction. Suppressed when AP is active -
		// the aerial-perspective volume is doing the distance haze
		// physically, so adding fog density on top compounds the tinting
		// and over-blues nearby geometry (200m-1km objects get 50%+
		// replaced by fog colour at default r_fogDensity=0.003).
		const bool apActive = g_atmosphere.fogUseAerialPerspective >= 0.5f;
		float distExtinction = apActive ? 0.0f : (effectiveDist * distanceDensity);
		// Height fog is preserved when AP is active but dampened. The
		// current height-fog formula multiplies density by effectiveDist,
		// which means ground-level horizontal views (eye and pixel both
		// near ground, default heightPivot=0) accumulate the full ~1km of
		// "low altitude air" along the ray and hit fogFactor=0.9 even at
		// 1 km of view distance - looks identical to old distance fog.
		// The 0.20 scale keeps the artistic effect (visible mist in
		// valleys when looking down into them from above) while removing
		// the spurious horizontal-view-through-ground haze.
		const float heightFogApScale = apActive ? 0.20f : 1.0f;
		float rayHeightDelta = worldPos.y - g_eyePos.y;
		float heightRayMid = g_eyePos.y + rayHeightDelta * 0.5f;
		float heightRaySpan = abs(rayHeightDelta) * 0.5f + 1.0f;
		float heightBandBase = exp2(-(heightRayMid - heightPivot) * heightFalloff);
		float heightVariation = saturate(rayHeightDelta * heightFalloff / heightRaySpan);
		float heightExtinction = heightBandBase * heightDensity * effectiveDist * heightFogApScale;
		heightExtinction *= lerp(1.0f, 0.82f, heightVariation);
		float extinction = distExtinction + heightExtinction;
		float fogFactor = saturate(1.0f - exp2(-extinction));

		float3 rayDir = normalize(worldPos - g_eyePos.xyz);
		float3 sunDir = normalize(-g_lightDirection.xyz);

		// Sky-colour sample (LUT-driven when r_atmosphereLUTs is on) - reused
		// by both paths below as the colour the fog tints toward.
		float3 farSkyColour = g_atmosphereTexture.Sample(g_textureSampler, screenPos).rgb;
		float3 ambientFogBase = max(g_atmosphere.ambientLight.rgb, float3(0.001f, 0.001f, 0.001f));

		float heightFogWeight = saturate(heightExtinction / max(extinction, 0.001f));
		float sunElevation = -g_lightDirection.y;
		float nightWeight = saturate((-sunElevation + 0.02f) / 0.20f);
		float distanceAtmosphereBlend = saturate((fogDist - atmosphereBlendStart) / atmosphereBlendRange);

		float3 fogColour;
		float3 surfaceAttenuation;

		if (apActive)
		{
			// AP-active path. The aerial-perspective volume is producing
			// physically-correct distance haze for opaque geometry after
			// this pass runs. To avoid compounding, we skip the analytic
			// atmosphere integration entirely and pick a fog colour
			// driven only by the sky texture (which already reflects the
			// LUT-based sky shading) and the ambient floor. Height fog,
			// lightning, and the final fogFactor composite still apply,
			// so authored low-altitude haze and storm effects survive.
			float fogSkyAmount = saturate(skyTintInfluence * 0.62f + distanceAtmosphereBlend * 0.20f);
			fogSkyAmount = lerp(fogSkyAmount, fogSkyAmount * 0.70f, nightWeight);
			fogColour = lerp(ambientFogBase, farSkyColour, fogSkyAmount);
			// No spectral transmittance available without the analytic
			// march - use a scalar attenuation. AP will refine the
			// per-channel transmittance for distant pixels itself.
			surfaceAttenuation = float3(1.0f, 1.0f, 1.0f);
		}
		else
		{
			// Original analytic path - kept for backward compat when the
			// LUT system is disabled (r_atmosphereLUTs 0 or LUT init
			// failed). The full Hillaire-style integration runs here.
			const int fogSteps = 12;
			PhysicalAtmosphereSample raySample = IntegrateAtmospherePhysical(
				g_eyePos.xyz + rayDir * startDistance,
				rayDir,
				effectiveDist,
				sunDir,
				fogSteps,
				false);

			const float3 fogProbeOrigin = float3(0.0f, 0.0f, 0.0f);
			float3 zenithDir = float3(0.0f, 1.0f, 0.0f);
			float3 antiSunDir = normalize(float3(-sunDir.x, 0.18f, -sunDir.z));
			float3 nearSunDir = normalize(float3(sunDir.x, 0.16f, sunDir.z));

			PhysicalAtmosphereSample zenithProbe = IntegrateAtmospherePhysical(fogProbeOrigin, zenithDir, g_frustumDepths[3], sunDir, 18, false);
			PhysicalAtmosphereSample horizonProbe = IntegrateAtmospherePhysical(fogProbeOrigin, antiSunDir, g_frustumDepths[3], sunDir, 18, false);
			PhysicalAtmosphereSample sunsetProbe = IntegrateAtmospherePhysical(fogProbeOrigin, nearSunDir, g_frustumDepths[3], sunDir, 18, false);

			float raySampleLuma = max(dot(raySample.inscatter, float3(0.299f, 0.587f, 0.114f)), 0.001f);
			float3 viewRayFog = raySample.inscatter / raySampleLuma;

			float sunsetWeight = saturate((sunsetRange - sunElevation) / max(sunsetRange, 0.02f)) * (1.0f - saturate((-0.02f - sunElevation) / 0.10f));
			float horizonColourMix = lerp(0.80f, 0.54f, nightWeight);
			float3 directionalFog = lerp(horizonProbe.inscatter, raySample.inscatter, saturate(0.28f + distanceAtmosphereBlend * 0.34f));
			float3 farMatchedFog = lerp(directionalFog, farSkyColour, farAtmosphereMatchStrength * distanceAtmosphereBlend);
			float3 heightDominantFog = lerp(farMatchedFog, horizonProbe.inscatter, heightFogWeight * 0.72f);
			float3 baseAtmosphereFog = lerp(heightDominantFog, zenithProbe.inscatter, nightWeight * 0.18f);
			baseAtmosphereFog = lerp(baseAtmosphereFog, horizonProbe.inscatter, horizonColourMix * 0.22f);
			float sunsetBlend = sunsetWeight * distanceAtmosphereBlend * sunsetWarmthStrength * 0.18f;
			float3 physicalFogColour = lerp(baseAtmosphereFog, sunsetProbe.inscatter, sunsetBlend);

			float3 coolNightFog = lerp(zenithProbe.inscatter, ambientFogBase, 0.10f);
			physicalFogColour = lerp(physicalFogColour, coolNightFog, nightWeight * 0.65f);

			float physicalLuma = max(dot(physicalFogColour, float3(0.299f, 0.587f, 0.114f)), 0.001f);
			float baseLuma = max(dot(ambientFogBase, float3(0.299f, 0.587f, 0.114f)), 0.001f);
			float3 physicalHue = physicalFogColour / physicalLuma;
			float hueStrength = saturate((physicalHue.b - max(physicalHue.r, physicalHue.g)) * 0.45f);
			physicalHue = lerp(physicalHue, viewRayFog, 0.18f + heightFogWeight * 0.22f);
			physicalHue = lerp(physicalHue, normalize(max(physicalFogColour, 0.0001f)) * 1.732f, hueStrength * 0.18f);
			physicalFogColour = saturate(physicalHue * max(physicalLuma, baseLuma * 0.35f));

			float fogSkyAmount = saturate(skyTintInfluence * 0.62f + distanceAtmosphereBlend * (0.20f + farAtmosphereMatchStrength * 0.26f));
			fogSkyAmount = lerp(fogSkyAmount, fogSkyAmount * 0.70f, nightWeight);
			fogColour = lerp(ambientFogBase, physicalFogColour, fogSkyAmount);
			fogColour = lerp(fogColour, farSkyColour, farAtmosphereMatchStrength * distanceAtmosphereBlend * saturate(fogFactor + heightFogWeight * 0.35f));
			float fogLuma = dot(fogColour, float3(0.299f, 0.587f, 0.114f));
			float dayWeight = 1.0f - nightWeight;
			float distanceDesat = farDesatStrength * distanceAtmosphereBlend * lerp(0.30f, 0.10f, dayWeight);
			fogColour = lerp(fogColour, fogLuma.xxx, distanceDesat);

			surfaceAttenuation = lerp(float3(1.0f, 1.0f, 1.0f), raySample.transmittance, fogFactor);
		}

		// Lightning flash adds a forward-scattering bright tint to the fog
		// colour regardless of which path computed the base colour above.
		float lightningFlash = saturate(g_weatherSurface.lightningFlash);
		if (lightningFlash > 0.0001f)
		{
			float3 lightningDir = normalize(g_weatherSurface.lightningBoltDirection.xyz + float3(1e-5f, 1e-5f, 1e-5f));
			float forwardScatter = saturate(dot(rayDir, lightningDir) * 0.5f + 0.5f);
			float3 lightningFog = float3(0.62f, 0.76f, 1.0f) * lightningFlash;
			fogColour += lightningFog * (0.10f + 0.24f * fogFactor + 0.22f * heightFogWeight + 0.16f * forwardScatter);
		}

		float3 foggedAlbedo = pixelColour.rgb * surfaceAttenuation;
		foggedAlbedo = lerp(foggedAlbedo, fogColour, fogFactor);

		return float4(saturate(foggedAlbedo), 1.0f);
	}
}
