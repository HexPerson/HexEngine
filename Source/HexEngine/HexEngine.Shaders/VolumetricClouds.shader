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
	AtmosphereCommon
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
	// S3 unified cloud lighting: the same Hillaire LUTs the sky dome samples.
	// Bound by RenderVolumetricClouds only when g_cloudWindOffset.w says the
	// LUT subsystem is live; otherwise the analytic fallback below runs.
	Texture2D g_atmTransmittanceLUT : register(t9);
	Texture2D g_atmSkyViewLUT : register(t10);

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


	// Fixed cone kernel for the light march (Schneider/Nubis): unit offsets
	// applied laterally, scaled by distance along the sun ray, so the march
	// samples an expanding cone instead of a 1-texel line. A line march
	// through structured noise aliases (one wisp shadows the whole sample);
	// the cone integrates the neighbourhood the way a finite sun does.
	static const float3 kConeKernel[6] =
	{
		float3( 0.38051305f,  0.92453449f, -0.02111345f),
		float3(-0.50625799f, -0.03590792f, -0.86163418f),
		float3(-0.32509218f, -0.94557439f,  0.28180862f),
		float3( 0.09026238f, -0.27376545f,  0.95755165f),
		float3( 0.28128598f,  0.42443639f, -0.86065785f),
		float3(-0.16852403f,  0.14748697f,  0.97460106f)
	};

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
		// Cone half-tangent ~0.15 (=~8.5 deg): wide enough to break line
		// aliasing, narrow enough that near-sample shadowing stays local.
		const float coneSpread = 0.15f;
		float travelled = stepLen * 0.5f;
		float opticalDepth = 0.0f;

		[loop]
		for (int i = 0; i < lightSteps; ++i)
		{
			if (travelled >= lightSpan)
				break;

			const float3 coneOffset = kConeKernel[i % 6] * (coneSpread * travelled);
			const float3 p = samplePos + sunDir * travelled + coneOffset;
			// COARSE density (no detail erosion): grazing sunset light paths
			// through the detailed field speckled the whole deck white.
			const float density = SampleCloudDensityTexCoarse(g_shapeNoise, g_detailNoise, g_mirrorSampler, p, boundsMin, boundsMax, windOffset);
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
		// windOffset.w = "atmosphere LUTs valid" (set by BuildCloudConstants).
		const bool useAtmosphereLuts = g_cloudWindOffset.w > 0.5f;
		const float3 sunDir = normalize(-g_lightDirection.xyz + float3(1e-5f, 1e-5f, 1e-5f));
		const float3 cloudProbeOrigin = float3(0.0f, lerp(boundsMin.y, boundsMax.y, 0.58f), 0.0f);
		const float sunElevation = -g_lightDirection.y;
		// Sun visibility: the direct term must switch OFF once the sun is below
		// the horizon. It used to carry a hard max(lightMult, 0.35) floor, so a
		// 35%-strength sun lit the clouds all night - and the sunset warmth
		// below kept RISING past the horizon instead of fading back out, so
		// that night-time floor was pulled 55% toward orange: brown-orange
		// clouds in every cloudy preset after dark.
		const float sunUp = smoothstep(-0.12f, 0.02f, sunElevation);

		// S3 UNIFIED CLOUD LIGHTING. Sun radiance at cloud altitude from the
		// SAME transmittance LUT the sky dome uses (SampleSunColourFromLUT in
		// SkySphere.shader) - so cloud sunset hue matches the sky's exactly,
		// where the old analytic path + hand-tuned hue push gave the two a
		// different sunset. Sampling at the CLOUD's altitude (not the
		// camera's) also means high clouds stay sunlit-red after the ground
		// is in shadow - the reference "afterglow" - for free. Full HDR
		// magnitude is kept (no luma-normalize / x0.55): the phase function's
		// 1/4pi and the scatter integral bring it onto the sky dome's scale,
		// which is what the clouds composite against now that the dome is
		// linear HDR.
		float3 cloudSunColour;
		if (useAtmosphereLuts)
		{
			const float probeAltMM = WorldYToAtmosphereAltitudeMM(cloudProbeOrigin.y);
			const float2 tuv = TransmittanceLutParamsToUv(probeAltMM, sunDir.y);
			const float3 sunTrans = g_atmTransmittanceLUT.SampleLevel(g_linearSampler, tuv, 0).rgb;
			const float sunEnergy = lerp(18.0f, 30.0f, saturate(sunDir.y * 0.5f + 0.5f)) * max(g_globalLight[0], 0.35f);
			cloudSunColour = float3(1.0f, 0.985f, 0.965f) * sunTrans * sunEnergy * sunUp;
		}
		else
		{
			// Analytic fallback (LUTs off / D3D12): ComputePhysicalSunColour
			// already includes the same 18-30 energy ramp internally.
			cloudSunColour = ComputePhysicalSunColour(cloudProbeOrigin, sunDir) * sunUp;
		}

		// Ambient: two sky-view LUT taps (anti-sun horizon + zenith) replace
		// the two 12-step analytic integrations that ran per pixel - cheaper
		// AND consistent with the visible sky by construction.
		const float3 horizonProbeDir = normalize(float3(-sunDir.x, 0.10f, -sunDir.z));
		const float3 zenithProbeDir = float3(0.0f, 1.0f, 0.0f);
		float3 ambientHorizon;
		float3 ambientZenith;
		if (useAtmosphereLuts)
		{
			ambientHorizon = g_atmSkyViewLUT.SampleLevel(g_linearSampler, SkyViewLutParamsToUv(horizonProbeDir, sunDir), 0).rgb;
			ambientZenith  = g_atmSkyViewLUT.SampleLevel(g_linearSampler, SkyViewLutParamsToUv(zenithProbeDir,  sunDir), 0).rgb;
		}
		else
		{
			const PhysicalAtmosphereSample cloudHorizonProbe = IntegrateAtmospherePhysical(cloudProbeOrigin, horizonProbeDir, g_frustumDepths[3], sunDir, 12, false);
			const PhysicalAtmosphereSample cloudZenithProbe = IntegrateAtmospherePhysical(cloudProbeOrigin, zenithProbeDir, g_frustumDepths[3], sunDir, 12, false);
			ambientHorizon = cloudHorizonProbe.inscatter;
			ambientZenith  = cloudZenithProbe.inscatter;
		}
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
		// Ambient lift retuned for the LUT era: the x1.6 boost was authored
		// against the DIM analytic probes; the sky-view LUT taps are ~4-6x
		// brighter at midday and clouds rendered as structureless white
		// blobs brighter than the sky behind them. 0.55 puts a fully-lit
		// cloud face just above the sky radiance and lets the AO/core
		// terms carve visible form again.
		// 0.24 was compensating for the non-conserving scatter integral
		// (see the scatter term below); with albedo <= 1 the footing is
		// ~0.4-0.55 again. 0.42 (checked live at 13:00, fair-weather
		// cumulus): undersides at roughly 40% of the sky radiance, enough
		// lit-top vs base contrast to read as cumulus rather than cotton.
		const float ambientLift = g_cloudParams3.y * 0.42f;
		// Zenith-weighted ambient: cloud bodies receive their diffuse light
		// from the hemisphere ABOVE, but the old 0.35 lerp weighted the
		// HORIZON tap 65% - the brightest direction of the midday sky-view
		// LUT (~3-4x the zenith radiance) - which alone pushed deck ambient
		// past the white point. 0.8 toward zenith reads as sky-lit, not
		// horizon-glare-lit.
		float3 ambientSky = lerp(ambientHorizon, ambientZenith, 0.80f) * ambientLift;
		// Weather overcast: the Hillaire LUT is clear-sky only - in a storm
		// it still returns bright blue, which lit storm clouds nearly white.
		// Pull the cloud ambient toward the same dimmed overcast colour the
		// sky dome / fog / IBL lerp toward (g_skyOvercast, per-frame), scaled
		// by the ambient-strength cvar so presets keep their meaning.
		ambientSky = lerp(ambientSky, g_skyOvercast.rgb * ambientLift, saturate(g_skyOvercast.w));

		// Stylized-term sun basis: the HDR sun radiance (~28 at noon on the
		// LUT energy scale) is correct for the PHYSICAL direct term (the
		// 1/4pi phase brings it down), but the additive stylized terms
		// (silver lining, forward glow, height tint, warm ambient pull) were
		// authored against the old ~0.6-magnitude sun colour and go nuclear
		// at x28 - storm decks rendered almost pure white. 0.035 restores
		// their authored scale while keeping the LUT's hue.
		const float3 sunStylized = cloudSunColour * 0.035f;
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
		const float3 ambientShaded = lerp(ambientChromatic, sunStylized, saturate(g_cloudParams5.x) * 0.28f * sunUp);
		const float3 topTint = lerp(ambientShaded, sunStylized, 0.48f * sunUp);
		const float3 bottomTint = lerp(ambientNeutral, ambientShaded, 0.88f);

		float transmittance = 1.0f;
		float3 cloudLight = 0.0f.xxx;
		float apDistWeighted = 0.0f;

		// PROGRESSIVE stepping for the horizon-scale domain: uniform steps
		// over a 10km+ trace either mush the near field or starve the step
		// budget. Steps grow with distance - full detail overhead, coarse
		// (but still sampled) toward the horizon deck.
		const float maxDist = max(1.0f, g_cloudParams0.w);
		float travelled = baseStep * jitter;
		// TWO-RATE march (Schneider/Nubis): coarse steps through empty air
		// using the cheap un-eroded density (conservative - erosion only ever
		// removes), then on the first hit step BACK one coarse step and
		// re-march that span at kFineScale so the eroded surface is sampled
		// densely. Drop back to coarse after kEmptyToCoarse empty fine
		// samples. Cures two artifacts the single-rate march had once the
		// slab grew to 2400m: horizontal striations on tall cumulus faces
		// (the height profile sliced by ~300-900m steps) and sparkle grain on
		// the thinnest wisps (one or two samples across a 100m feature).
		// Fine steps burn the same iteration budget - the ceiling is 2x the
		// nominal count, but dense rays still terminate on transmittance
		// within a handful of samples and empty air now costs no detail fetch.
		const float kFineScale = 0.30f;
		const int kEmptyToCoarse = 5;
		const int maxIters = viewSteps * 2;
		bool fineMode = false;
		int emptyRun = 0;
		[loop]
		for (int i = 0; i < maxIters; ++i)
		{
			if (travelled >= maxTraceDistance)
				break;

			// Growth tamed (was rate 0.0011 / cap 7x): aggressive far steps
			// sliced the 700m height profile into visible horizontal bands on
			// distant clouds ("lined" look). 0.0005/3.5x keeps the horizon
			// deck inside budget while sampling the profile densely enough.
			const float coarseStep = baseStep * clamp(1.0f + (entryDist + travelled) * 0.0005f, 1.0f, 3.5f);
			const float stepLenView = fineMode ? coarseStep * kFineScale : coarseStep;
			const float3 samplePos = eyePos + rayDir * (entryDist + travelled);
			// Distance LOD on the detail erosion: full detail near, coarse
			// far - far detail aliases into sparkle grain at dusk.
			const float detailLod = fineMode ? (1.0f - smoothstep(1200.0f, 6000.0f, entryDist + travelled)) : 0.0f;
			const float density = SampleCloudDensityTexImpl(g_shapeNoise, g_detailNoise, g_mirrorSampler, samplePos, boundsMin, boundsMax, windOffset, detailLod);

			if (!fineMode)
			{
				if (density > 0.0001f)
				{
					// Hit in coarse mode: rewind so the fine march covers the
					// span this coarse step just leapt over.
					fineMode = true;
					emptyRun = 0;
					travelled = max(0.0f, travelled - coarseStep);
					continue;
				}
				travelled += stepLenView;
				continue;
			}

			if (density > 0.0001f)
			{
				emptyRun = 0;
				const float lightTrans = MarchToLight(samplePos, boundsMin, boundsMax, windOffset, sunDir, lightSteps);
				const float shadowAmount = 1.0f - lightTrans;
				const float viewToSun = saturate(dot(rayDir, sunDir));
				// Schneider beer-powder: the light march gives
				// lightTrans = exp(-tau), so exp(-2*tau) = lightTrans^2 and the
				// powder term (1 - exp(-2*tau)) = 1 - lightTrans^2 needs no
				// extra sampling. Thin sun-facing wisps (tau~0) darken - the
				// "dark edges" real cumulus show against the sun - and
				// mid-depth regions gain the soft in-scatter lift. The old
				// form (1 + k*(1-lightTrans)) only ever brightened shadowed
				// samples and produced neither effect. x2 keeps the mid-range
				// above unity so the strength cvar's meaning survives.
				const float schneiderPowder = 1.0f - lightTrans * lightTrans;
				const float powder = lerp(1.0f, 2.0f * schneiderPowder, saturate(g_cloudParams1.y));
				// Soft distance fade toward the trace limit - the horizon deck
				// dissolves into the atmosphere instead of ending at a wall.
				const float distanceFade = 1.0f - smoothstep(0.70f, 1.0f, (entryDist + travelled) / maxDist);
				// ENERGY-CONSERVING scatter: the extinction below is scaled by
				// the view-absorption cvar (g_cloudParams3.x) but the scatter
				// term was not, so the integrated single-scatter albedo was
				// 1/absorption - 2.4 for the fair-weather presets (0.42), ~1.1
				// for storms (0.9). That is why cumulus went nuclear white at
				// midday while overcast decks looked sane, and why every
				// lighting term had to be hand-dimmed (ambientLift 0.55 ->
				// 0.24, stylized sun x0.035). Scattering = extinction x albedo
				// (<= 1): the path integral now tops out at (1 - T).
				const float scatter = density * stepLenView * invCloudHeight * g_cloudParams3.x * transmittance * distanceFade;
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
				// Physical single-scatter keeps the FULL HDR sun radiance
				// (phase has the 1/4pi); the stylized silver-lining and
				// forward-glow additions run on the normalized sunStylized
				// basis they were authored against.
				const float3 directLight = ((lightTrans * powder * phase * cloudSunColour + silverLining * sunStylized * (0.22f + 0.48f * viewToSun)) * edgeBoost + sunStylized * forwardGlow * g_cloudParams4.x * 0.22f) * multiScatter * directionalDiffuse;
				const float lightningScatterPhase = 0.35f + 0.65f * saturate(dot(rayDir, lightningDir) * 0.5f + 0.5f);
				const float lightningEdge = pow(saturate(1.0f - density), 1.4f);
				const float3 lightningLight = lightningColour * (0.34f + lightningDerivative * 0.66f) * lightningScatterPhase * (0.55f + lightningEdge * 0.45f);
				// Sun-occlusion DARKENS ambient (was 0.35+0.65*shadow, which
				// BRIGHTENED shadowed samples - a legacy fill that inverted the
				// deck's vertical gradient: undersides lit up instead of
				// falling off). Deep-shadow interiors keep 55%.
				const float3 ambientLight = (ambientShaded * (1.0f - shadowAmount * 0.45f) + lightningColour * (0.08f + 0.14f * lightningEdge)) * aoTerm * coreDarken;
				cloudLight += scatter * (directLight + ambientLight + lightningLight) * stylizedTint;

				const float transmittanceBefore = transmittance;
				transmittance *= exp(-density * stepLenView * invCloudHeight * g_cloudParams3.x * distanceFade);
				// Opacity-weighted mean distance of the cloud along this ray,
				// for the aerial-perspective blend after the march.
				apDistWeighted += (entryDist + travelled) * (transmittanceBefore - transmittance);
				if (transmittance < 0.01f)
					break;
			}
			else if (++emptyRun >= kEmptyToCoarse)
			{
				fineMode = false;
			}

			travelled += stepLenView;
		}

		// Keep dense cores from collapsing to pure black under aggressive shadowing.
		cloudLight = max(cloudLight, ambientShaded * (1.0f - transmittance) * 0.20f);

		const float alpha = saturate(1.0f - transmittance);
		if (alpha <= 1e-4f)
			return 0.0f.xxxx;

		// AERIAL PERSPECTIVE on the cloud itself. The scene's AP pass skips
		// sky pixels (the dome already contains the atmosphere), so clouds
		// composited over the dome received none: a bank 20 km out rendered
		// as flat paper-white against a hazy blue horizon - the "horizon
		// white-wash". Blend toward the sky-view LUT in-scatter for this
		// ray by the opacity-weighted cloud distance. 2e-5/m is close to the
		// sea-level Rayleigh+Mie extinction (~1.5e-5): a 15 km bank sits
		// ~26% into the haze, 30 km ~45%. (6e-5 was tried first and washed
		// the mid-distance deck into the horizon LUT - form gone again.)
		{
			const float meanDist = apDistWeighted / max(alpha, 1e-3f);
			const float apBlend = 1.0f - exp(-meanDist * 2.0e-5f);
			float3 skyInscatter;
			if (useAtmosphereLuts)
				skyInscatter = g_atmSkyViewLUT.SampleLevel(g_linearSampler, SkyViewLutParamsToUv(rayDir, sunDir), 0).rgb;
			else
				skyInscatter = ambientHorizon;
			skyInscatter = lerp(skyInscatter, g_skyOvercast.rgb, saturate(g_skyOvercast.w));
			cloudLight = lerp(cloudLight, skyInscatter * alpha, apBlend);
		}

		// Clouds are composited with non-premultiplied alpha. Clamp raised
		// 8 -> 64 for the HDR sky: sunset silver linings on the LUT energy
		// scale legitimately exceed the old cap (which was sized for the
		// pre-HDR tonemapped-in-shader sky).
		const float safeAlpha = max(0.03f, alpha);
		const float3 straightCloud = min(cloudLight / safeAlpha, 64.0f.xxx);
		return float4(straightCloud, alpha);
	}
}
