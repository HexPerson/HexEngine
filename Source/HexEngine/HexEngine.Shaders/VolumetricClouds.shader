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
	AtmospherePhysical
	Utils
	CloudCommon
}
"VertexShader"
{
	UIPixelInput ShaderMain(UIVertexInput input)
	{
		UIPixelInput output;

		output.position = input.position;
		output.texcoord = input.texcoord;
		output.positionSS = output.position;
		output.colour = input.colour;

		return output;
	}
}
"PixelShader"
{
	GBUFFER_RESOURCE(0, 1, 2, 3, 4);

	Texture2D g_sceneColour : register(t5);
	Texture2D g_noiseTexture : register(t6);
	Texture3D g_shapeNoise : register(t7);
	Texture3D g_detailNoise : register(t8);

	SamplerState g_pointSampler : register(s2);
	SamplerState g_mirrorSampler : register(s3);
	SamplerState g_linearSampler : register(s4);


	static const float PI = 3.14159265f;

	float Hash12(float2 p)
	{
		const float h = dot(p, float2(127.1f, 311.7f));
		return frac(sin(h) * 43758.5453123f);
	}

	// Thin wrappers over the shared CloudCommon implementations (the density
	// function lives there so the shadow-map pass can't drift from the render).
	float2 RayBoxDist(float3 boundsMin, float3 boundsMax, float3 rayOrigin, float3 rayDir)
	{
		return CloudRayBoxDist(boundsMin, boundsMax, rayOrigin, rayDir);
	}

	float SampleCloudDensity(float3 worldPos, float3 boundsMin, float3 boundsMax, float3 windOffset)
	{
		return SampleCloudDensityTex(g_shapeNoise, g_detailNoise, g_mirrorSampler, worldPos, boundsMin, boundsMax, windOffset);
	}




	float HenyeyGreenstein(float cosTheta, float anisotropy)
	{
		const float g = clamp(anisotropy, -0.95f, 0.95f);
		const float denom = max(1e-4f, 1.0f + g * g - 2.0f * g * cosTheta);
		return (1.0f - g * g) / (4.0f * PI * pow(denom, 1.5f));
	}

	float DualLobeHG(float cosTheta, float anisotropy)
	{
		// A strong forward lobe (silver lining) blended with a gentle back-scatter
		// lobe (soft ambient rim) - real clouds exhibit both at once.
		const float forward = HenyeyGreenstein(cosTheta, anisotropy);
		const float backward = HenyeyGreenstein(cosTheta, -0.25f);
		return lerp(forward, backward, 0.35f);
	}

	float3 GetWorldRay(float2 uv)
	{
		float4 clipPos = float4(uv * 2.0f - 1.0f, 1.0f, 1.0f);
		clipPos.y *= -1.0f;
		float4 worldPos = mul(clipPos, g_viewProjectionMatrixInverse);
		worldPos.xyz /= max(1e-5f, worldPos.w);
		return normalize(worldPos.xyz - g_eyePos.xyz);
	}


	float MarchToLight(float3 samplePos, float3 boundsMin, float3 boundsMax, float3 windOffset, float3 sunDir, int lightSteps)
	{
		const float2 lightHit = RayBoxDist(boundsMin, boundsMax, samplePos, sunDir);
		if (lightHit.y <= 0.0f)
			return 1.0f;

		// Horizon-scale bounds can put kilometres of slab above a sample;
		// light beyond ~900m of cloud is fully extinct anyway, and without
		// this clamp the light steps stretch into uselessness.
		const float lightSpan = min(lightHit.y, 900.0f);
		const float stepLen = max(1.0f, lightSpan / max(1, lightSteps));
		// Physical per-metre extinction (see ShaderMain) - the old
		// height-normalized scale capped every vertical path at the same
		// optical depth regardless of thickness or density.
		const float invCloudHeight = 0.012f;
		float travelled = 0.0f;
		float opticalDepth = 0.0f;

		[loop]
		for (int i = 0; i < lightSteps; ++i)
		{
			if (travelled >= lightSpan)
				break;

			const float3 p = samplePos + sunDir * travelled;
			const float density = SampleCloudDensity(p, boundsMin, boundsMax, windOffset);
			opticalDepth += density * stepLen * invCloudHeight;
			travelled += stepLen;
		}

		return max(g_cloudParams3.z, exp(-opticalDepth * g_cloudParams1.x));
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float2 uv = input.texcoord;
		float pixelDepth = GBUFFER_NORMAL.Sample(g_pointSampler, uv).w;
		// SKY detection must match the GI trace: the SKYDOME IS GEOMETRY and
		// writes a real gbuffer depth (its radius), so a depth-only test
		// never fires for sky - the clouds were being depth-clipped against
		// the dome sphere, which reads as a circular cap overhead that never
		// reaches the horizon. Sky is flagged by diffuse.a == -1.
		const float skyFlag = GBUFFER_DIFFUSE.Sample(g_pointSampler, uv).a;
		if (skyFlag == -1.0f || pixelDepth <= 0.0f || pixelDepth == -1.0f)
		{
			// No scene occluder: march to the cloud system's own distance
			// limit (maxTraceDistance is still clamped by g_cloudParams0.w).
			pixelDepth = 1e9f;
		}

		const float3 boundsMin = g_cloudBoundsMin.xyz;
		const float3 boundsMax = g_cloudBoundsMax.xyz;
		const float3 eyePos = g_eyePos.xyz;
		const float3 rayDir = GetWorldRay(uv);
		const float2 cloudHit = RayBoxDist(boundsMin, boundsMax, eyePos, rayDir);

		if (cloudHit.y <= 0.0f)
			return float4(0.0f, 0.0f, 0.0f, 0.0f);

		const float entryDist = cloudHit.x;
		float maxTraceDistance = min(cloudHit.y, max(0.0f, pixelDepth - entryDist));
		maxTraceDistance = min(maxTraceDistance, g_cloudParams0.w);
		if (maxTraceDistance <= 0.0f)
			return float4(0.0f, 0.0f, 0.0f, 0.0f);

		int viewSteps = max(8, (int)g_cloudMarch.x);
		int lightSteps = max(2, (int)g_cloudMarch.y);
		const float qualityPreset = g_cloudWindDirection.w;
		if (qualityPreset <= 0.5f)
		{
			viewSteps = max(8, (int)(viewSteps * 0.80f));
			lightSteps = max(2, (int)(lightSteps * 0.70f));
		}
		else if (qualityPreset >= 1.5f)
		{
			viewSteps = (int)(viewSteps * 1.15f);
			lightSteps = (int)(lightSteps * 1.20f);
		}

		float baseStep = maxTraceDistance / max(1, viewSteps);
		baseStep = max(1.0f, baseStep * g_cloudParams1.w);
		// PHYSICAL per-metre extinction, replacing the old 1/slabHeight
		// normalization. That normalization made a full vertical path through
		// the deck total the SAME optical depth (~0.5 with default cvars)
		// regardless of thickness or density - an overcast could never get
		// past ~40% alpha and rendered as a milky veil. Real stratus runs
		// ~0.02-0.1/m extinction; 0.012 at density 1 leaves the density and
		// absorption cvars honest headroom in both directions.
		const float invCloudHeight = 0.012f;

		const float2 noiseUv = uv * float2(max(1.0f, g_screenWidth / 128.0f), max(1.0f, g_screenHeight / 128.0f));
		const float noise = g_noiseTexture.Sample(g_linearSampler, noiseUv).r;
		const float jitter = frac(noise + Hash12(input.position.xy) + frac(g_time * 0.1337f));

		const float3 windOffset = g_cloudWindOffset.xyz;
		const float3 sunDir = normalize(-g_lightDirection.xyz + float3(1e-5f, 1e-5f, 1e-5f));
		const float3 cloudProbeOrigin = float3(0.0f, lerp(boundsMin.y, boundsMax.y, 0.58f), 0.0f);
		const float3 cloudSunRadiance = ComputePhysicalSunColour(cloudProbeOrigin, sunDir);
		const float cloudSunLuma = max(dot(cloudSunRadiance, float3(0.299f, 0.587f, 0.114f)), 0.001f);
		const float3 cloudSunHue = cloudSunRadiance / cloudSunLuma;
		const float sunElevation = -g_lightDirection.y;
		// Sun visibility: the direct term must switch OFF once the sun is below
		// the horizon. It used to carry a hard max(lightMult, 0.35) floor, so a
		// 35%-strength sun lit the clouds all night - and the sunset warmth
		// below kept RISING past the horizon instead of fading back out, so
		// that night-time floor was pulled 55% toward orange: brown-orange
		// clouds in every cloudy preset after dark.
		const float sunUp = smoothstep(-0.12f, 0.02f, sunElevation);
		// Sunset warmth is a BAND around the horizon: ramps in as the sun
		// drops toward it, ramps out again once it is below (same shape as
		// the CPU-side ambient tint in SetupPerFrameBuffer).
		const float sunsetAmount = saturate((0.22f - sunElevation) / 0.32f)
		                         * (1.0f - saturate((-0.02f - sunElevation) / 0.10f));
		const float3 cloudSunBalancedHue = lerp(cloudSunHue, float3(1.0f, 0.78f, 0.58f), sunsetAmount * 0.55f);
		const float3 cloudSunColour = cloudSunBalancedHue * max(g_globalLight[0], 0.35f) * 0.55f * sunUp;
		const float3 horizonProbeDir = normalize(float3(-sunDir.x, 0.10f, -sunDir.z));
		const float3 zenithProbeDir = float3(0.0f, 1.0f, 0.0f);
		const PhysicalAtmosphereSample cloudHorizonProbe = IntegrateAtmospherePhysical(cloudProbeOrigin, horizonProbeDir, g_frustumDepths[3], sunDir, 12, false);
		const PhysicalAtmosphereSample cloudZenithProbe = IntegrateAtmospherePhysical(cloudProbeOrigin, zenithProbeDir, g_frustumDepths[3], sunDir, 12, false);
		const float lightningFlash = saturate(g_weatherSurface.lightningFlash);
		const float3 lightningDir = normalize(g_weatherSurface.lightningBoltDirection.xyz + float3(1e-5f, 1e-5f, 1e-5f));
		const float3 lightningColour = float3(0.64f, 0.78f, 1.0f) * lightningFlash;
		const float cosSun = dot(rayDir, sunDir);
		const float isotropicPhase = 1.0f / (4.0f * PI);
		const float phase = lerp(isotropicPhase, DualLobeHG(cosSun, g_cloudParams1.z), 0.85f) * g_cloudParams3.w;
		// x1.6 inherent lift: clouds are the brightest ambient scatterers in
		// a daytime scene, and the probe inscatter alone under-lit the
		// undersides to a dirty grey-brown (user report). The strength cvar
		// keeps its per-preset meaning on top of this.
		float3 ambientSky = lerp(cloudHorizonProbe.inscatter, cloudZenithProbe.inscatter, 0.35f) * (g_cloudParams3.y * 1.6f);
		// Night ambient: the physical probes go black once the sun sets, and
		// with the direct term now correctly off too the clouds would vanish
		// into the night sky. Light them with the scene ambient instead - the
		// weather preset authors it (grey for overcast/storm, and the CPU
		// side cools it toward blue at night), so clouds read as dim blue-grey
		// or grey at night, matching the ground they hang over.
		const float3 nightAmbient = g_atmosphere.ambientLight.rgb * (g_cloudParams3.y * 0.9f);
		ambientSky += nightAmbient * (1.0f - sunUp);
		const float skyLuma = dot(ambientSky, float3(0.299f, 0.587f, 0.114f));
		const float3 ambientNeutral = skyLuma.xxx;
		const float3 ambientChromatic = lerp(ambientNeutral, ambientSky, saturate(g_cloudParams5.y));
		// Warm-sun contributions scale with sunUp: lerping toward a zero sun
		// colour at night would only darken the ambient, not tint it.
		const float3 ambientShaded = lerp(ambientChromatic, cloudSunColour, saturate(g_cloudParams5.x) * 0.28f * sunUp);
		const float3 topTint = lerp(ambientShaded, cloudSunColour, 0.48f * sunUp);
		const float3 bottomTint = lerp(ambientNeutral, ambientShaded, 0.88f);

		float transmittance = 1.0f;
		float3 cloudLight = 0.0f.xxx;

		// PROGRESSIVE stepping for the horizon-scale domain: uniform steps
		// over a 10km+ trace either mush the near field or starve the step
		// budget. Steps grow with distance - full detail overhead, coarse
		// (but still sampled) toward the horizon deck.
		const float maxDist = max(1.0f, g_cloudParams0.w);
		float travelled = baseStep * jitter;
		[loop]
		for (int i = 0; i < viewSteps; ++i)
		{
			if (travelled >= maxTraceDistance)
				break;

			// Growth tamed (was rate 0.0011 / cap 7x): aggressive far steps
			// sliced the 700m height profile into visible horizontal bands on
			// distant clouds ("lined" look). 0.0005/3.5x keeps the horizon
			// deck inside budget while sampling the profile densely enough.
			const float stepLenView = baseStep * clamp(1.0f + (entryDist + travelled) * 0.0005f, 1.0f, 3.5f);
			const float3 samplePos = eyePos + rayDir * (entryDist + travelled);
			const float density = SampleCloudDensity(samplePos, boundsMin, boundsMax, windOffset);

			if (density > 0.0001f)
			{
				const float lightTrans = MarchToLight(samplePos, boundsMin, boundsMax, windOffset, sunDir, lightSteps);
				const float shadowAmount = 1.0f - lightTrans;
				const float viewToSun = saturate(dot(rayDir, sunDir));
				const float powder = 1.0f + g_cloudParams1.y * (1.0f - lightTrans);
				// Soft distance fade toward the trace limit - the horizon deck
				// dissolves into the atmosphere instead of ending at a wall.
				const float distanceFade = 1.0f - smoothstep(0.70f, 1.0f, (entryDist + travelled) / maxDist);
				const float scatter = density * stepLenView * invCloudHeight * transmittance * distanceFade;
				const float diffuseProbeDistance = max(1.0f, baseStep * 0.75f);
				const float densityTowardSun = SampleCloudDensity(samplePos + sunDir * diffuseProbeDistance, boundsMin, boundsMax, windOffset);
				const float derivativeDiffuse = saturate((density - densityTowardSun) * 2.25f + 0.12f);
				const float directionalDiffuse = lerp(1.0f, derivativeDiffuse, saturate(g_cloudParams5.z));
				const float densityTowardLightning = SampleCloudDensity(samplePos + lightningDir * diffuseProbeDistance, boundsMin, boundsMax, windOffset);
				const float lightningDerivative = saturate((density - densityTowardLightning) * 2.0f + 0.10f);
				const float silverLining = pow(saturate(shadowAmount), max(0.5f, g_cloudParams4.y)) * g_cloudParams4.x;
				// Energy-conserving multiple scattering: on top of the single-scatter
				// (octave 0 = 1.0), add dimmer, deeper-penetrating higher orders whose
				// extinction is progressively attenuated (lightTrans^0.5, ^0.25) so
				// dense cores glow with soft internal bounce instead of going flat.
				// Scaled by the weather-driven multi-scatter strength.
				const float msStrength = saturate(g_cloudParams4.z);
				const float multiScatter = 1.0f + msStrength * (0.60f * pow(lightTrans, 0.5f) + 0.35f * pow(lightTrans, 0.25f));
				const float height01 = saturate((samplePos.y - boundsMin.y) / max(1.0f, boundsMax.y - boundsMin.y));
				const float3 heightTint = lerp(bottomTint, topTint, smoothstep(0.08f, 0.92f, height01));
				const float3 stylizedTint = lerp(1.0f.xxx, heightTint, g_cloudParams4.w);
				// Softened (was 0.8 + 1.8x): stacked with coreDarken this
				// halved the underside ambient - the dark-brown base look.
				const float localAO = exp(-density * (0.5f + 1.0f * saturate(g_cloudParams5.w)));
				const float aoTerm = lerp(1.0f, localAO, saturate(g_cloudParams5.w));
				const float densityAhead = SampleCloudDensity(samplePos + rayDir * diffuseProbeDistance * 0.7f, boundsMin, boundsMax, windOffset);
				const float edgeFactor = saturate(abs(density - densityAhead) * 2.4f);
				const float forwardGlow = pow(viewToSun, 4.0f) * saturate(1.0f - density * 0.95f) * (0.25f + 0.75f * shadowAmount);
				const float coreDarken = lerp(1.0f, 0.85f, saturate(density * 0.9f));
				const float edgeBoost = 1.0f + edgeFactor * 0.24f;
				const float3 directLight = ((lightTrans * powder * phase * cloudSunColour + silverLining * cloudSunColour * (0.22f + 0.48f * viewToSun)) * edgeBoost + cloudSunColour * forwardGlow * g_cloudParams4.x * 0.22f) * multiScatter * directionalDiffuse;
				const float lightningScatterPhase = 0.35f + 0.65f * saturate(dot(rayDir, lightningDir) * 0.5f + 0.5f);
				const float lightningEdge = pow(saturate(1.0f - density), 1.4f);
				const float3 lightningLight = lightningColour * (0.34f + lightningDerivative * 0.66f) * lightningScatterPhase * (0.55f + lightningEdge * 0.45f);
				const float3 ambientLight = (ambientShaded * (0.35f + shadowAmount * 0.65f) + lightningColour * (0.08f + 0.14f * lightningEdge)) * (1.0f + shadowAmount * 0.25f * g_cloudParams4.z) * aoTerm * coreDarken;
				cloudLight += scatter * (directLight + ambientLight + lightningLight) * stylizedTint;

				transmittance *= exp(-density * stepLenView * invCloudHeight * g_cloudParams3.x * distanceFade);
				if (transmittance < 0.01f)
					break;
			}

			travelled += stepLenView;
		}

		// Keep dense cores from collapsing to pure black under aggressive shadowing.
		cloudLight = max(cloudLight, ambientShaded * (1.0f - transmittance) * 0.20f);

		const float alpha = saturate(1.0f - transmittance);
		if (alpha <= 1e-4f)
			return 0.0f.xxxx;

		// Clouds are composited with non-premultiplied alpha.
		const float safeAlpha = max(0.03f, alpha);
		const float3 straightCloud = min(cloudLight / safeAlpha, 8.0f.xxx);
		return float4(straightCloud, alpha);
	}
}
