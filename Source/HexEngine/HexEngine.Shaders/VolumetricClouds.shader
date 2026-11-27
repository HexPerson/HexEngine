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

	// THREE-LOBE cloud phase. The old dual lobe peaked at only ~3x isotropic
	// toward the sun (g 0.35) - real cloud droplets (Mie) have a forward
	// peak ~100x isotropic, and THAT is the silver lining: thin edges of a
	// cloud in front of a low sun light up brilliantly while its body stays
	// dark. broad = the cvar lobe + back-scatter (unchanged character away
	// from the sun); silver = a narrow g~0.86 diffraction peak weighted by the
	// silver-lining cvar (0.55 -> 22%). k scales the eccentricity down for
	// the higher multi-scatter octaves (light that has bounced several times
	// has lost its direction).
	float CloudPhase(float cosTheta, float anisotropy, float silverWeight, float k)
	{
		const float broad = lerp(HenyeyGreenstein(cosTheta, anisotropy * k), HenyeyGreenstein(cosTheta, -0.25f * k), 0.30f);
		const float silver = HenyeyGreenstein(cosTheta, 0.86f * k);
		return lerp(broad, silver, silverWeight);
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

	// Light march -> sun OPTICAL DEPTH (absorption applied), not a floored
	// transmittance: the multi-scatter octaves need tau itself.
	//
	// QUADRATIC step distribution. The old march took 12 equal ~75 m steps,
	// first sample 37 m in - every billow smaller than that was invisible
	// to the light, so a cumulus shaded as one smooth gradient ("blobby").
	// Sample i sits at span*(i^2+i+0.5)/N^2: the first steps are ~8 m, the
	// last ~190 m, total still = span. Near steps resolve the self-shadow
	// of individual cauliflower lobes (sculpted, crisp terminator); far
	// steps still integrate the whole tower behind them.
	// nearDetail: detail erosion for the first 3 (short) steps so lobes cast
	// on each other. The caller zeroes it for grazing sun - long horizontal
	// detail paths at sunset speckled the deck (see SampleCloudDensityTexImpl).
	float MarchToLightTau(float3 samplePos, float3 boundsMin, float3 boundsMax, float3 windOffset, float3 sunDir, int lightSteps, float nearDetail)
	{
		const float2 lightHit = RayBoxDist(boundsMin, boundsMax, samplePos, sunDir);
		if (lightHit.y <= 0.0f)
			return 0.0f;

		// Light beyond ~1.2 km of cloud is fully extinct anyway - for a HIGH
		// sun. A low sun's ray runs nearly horizontally under/through the
		// deck, and the cell that should shadow a base can be 3-5 km
		// upwind: clamped at 1.2 km, every storm base saw a clear path to
		// the setting sun and glowed one flat orange. Grow the span toward
		// 5 km as the sun drops (same step count - the quadratic spacing
		// keeps the first steps short).
		const float lightSpan = min(lightHit.y, lerp(5000.0f, 1200.0f, saturate(sunDir.y * 4.0f)));
		const float invN2 = 1.0f / (float)max(1, lightSteps * lightSteps);
		// Cone half-tangent ~0.15 (=~8.5 deg): wide enough to break line
		// aliasing, narrow enough that near-sample shadowing stays local.
		const float coneSpread = 0.15f;
		float opticalDepth = 0.0f;

		[loop]
		for (int i = 0; i < lightSteps; ++i)
		{
			const float fi = (float)i;
			const float t = lightSpan * (fi * fi + fi + 0.5f) * invN2;
			const float ds = lightSpan * (2.0f * fi + 1.0f) * invN2;
			const float3 p = samplePos + sunDir * t + kConeKernel[i % 6] * (coneSpread * t);
			const float density = SampleCloudDensityTexImpl(g_shapeNoise, g_detailNoise, g_mirrorSampler, p, boundsMin, boundsMax, windOffset, i < 3 ? nearDetail : 0.0f);
			opticalDepth += density * ds;
		}

		// Physical per-metre extinction (see ShaderMain).
		return opticalDepth * 0.012f * g_cloudParams1.x;
	}

	// RAIN SHAFTS / VIRGA under storm cells. A second, cheap march through
	// the air BELOW the cloud base (the cloud march never looks there):
	// precipitation falls from the fullest weather-map cores, slanted
	// downwind, streaked by a 2D noise extruded vertically, and some shafts
	// evaporate before reaching the ground (virga). Lit by the sun through
	// the deck above (3 coarse taps) with a strong forward phase - the
	// backlit grey-gold veils of the reference - plus the dim under-deck
	// ambient. Starts 450 m out: near rain is the particle system's job.
	// Returns (in-scattered radiance, transmittance).
	float4 MarchRainShafts(float3 eyePos, float3 rayDir, float sceneDepth, float3 boundsMin, float3 boundsMax,
		float3 windOffset, float3 sunDir, float3 sunColour, float3 ambientRain, float jitter)
	{
		const float precip = saturate(g_weatherSurface.precipitationIntensity);
		if (precip < 0.02f)
			return float4(0.0f, 0.0f, 0.0f, 1.0f);

		// Metres above the slab floor (was 5% of the slab - 400 m into the
		// cloud once storms got a tall slab).
		const float baseY = boundsMin.y + 100.0f;
		if (eyePos.y >= baseY)
			return float4(0.0f, 0.0f, 0.0f, 1.0f);

		const float tNear = 450.0f;
		float tFar = min(sceneDepth, min(g_cloudParams0.w, 16000.0f));
		if (rayDir.y > 1e-4f)
			tFar = min(tFar, (baseY - eyePos.y) / rayDir.y);
		if (tFar <= tNear + 1.0f)
			return float4(0.0f, 0.0f, 0.0f, 1.0f);

		const int steps = 14;
		const float ds = (tFar - tNear) / steps;
		float2 windXz = g_cloudWindDirection.xz;
		windXz = dot(windXz, windXz) > 1e-6f ? normalize(windXz) : float2(1.0f, 0.0f);
		const float coverage = saturate(g_cloudParams0.y);
		const float threshold = lerp(0.80f, 0.16f, coverage);
		const float cosSun = dot(rayDir, sunDir);
		// Drops scatter forward (large-particle Mie). Moderated (g 0.5, half
		// weight) and the sun term scaled 0.3: the cloud sun basis is floored
		// at 35% of a clear-day sun, and at g 0.7 a shaft between camera and
		// a low sun rendered as a white blown-out wall on the horizon.
		const float phase = lerp(1.0f / (4.0f * PI), HenyeyGreenstein(cosSun, 0.50f), 0.50f) * 0.30f;

		float T = 1.0f;
		float3 L = 0.0f.xxx;
		[loop]
		for (int i = 0; i < steps; ++i)
		{
			const float t = tNear + ((float)i + jitter) * ds;
			const float3 p = eyePos + rayDir * t;
			const float fall = baseY - p.y;
			if (fall <= 0.0f)
				continue;
			const float2 slantXz = p.xz + windXz * fall * 0.30f;
			const float wm = CloudColumnCoverage(slantXz, windOffset.xz);
			const float core = smoothstep(threshold + 0.08f, threshold + 0.26f, wm);
			if (core <= 0.001f)
				continue;

			const float streak = CloudValueNoise2(slantXz * (1.0f / 260.0f) + 5.3f.xx) * 0.65f
			                   + CloudValueNoise2(slantXz * (1.0f / 85.0f) + 11.7f.xx) * 0.35f;
			const float reach = lerp(0.30f, 1.15f, CloudValueNoise2(slantXz * (1.0f / 1700.0f) + 3.1f.xx));
			const float fallFrac = fall / max(1.0f, baseY);
			const float virga = 1.0f - smoothstep(reach - 0.30f, reach, fallFrac);
			const float topFade = smoothstep(0.0f, 80.0f, fall);
			const float nearFade = smoothstep(tNear, tNear + 700.0f, t);
			// 0.00045 -> 0.0008: shafts were invisible at real distances.
			const float sigma = 0.0008f * precip * core * smoothstep(0.30f, 0.72f, streak) * virga * topFade * nearFade;
			if (sigma <= 1e-7f)
				continue;

			float sunVis = 0.0f;
			if (sunDir.y > 0.01f)
			{
				const float tEnter = (boundsMin.y - p.y) / sunDir.y;
				const float d0 = SampleCloudDensityTexCoarse(g_shapeNoise, g_detailNoise, g_mirrorSampler, p + sunDir * (tEnter + 150.0f), boundsMin, boundsMax, windOffset);
				const float d1 = SampleCloudDensityTexCoarse(g_shapeNoise, g_detailNoise, g_mirrorSampler, p + sunDir * (tEnter + 450.0f), boundsMin, boundsMax, windOffset);
				const float d2 = SampleCloudDensityTexCoarse(g_shapeNoise, g_detailNoise, g_mirrorSampler, p + sunDir * (tEnter + 900.0f), boundsMin, boundsMax, windOffset);
				sunVis = exp(-(d0 * 300.0f + d1 * 300.0f + d2 * 450.0f) * 0.012f * g_cloudParams1.x);
			}

			const float3 Ls = sunColour * (phase * sunVis) + ambientRain;
			L += T * sigma * ds * Ls;
			T *= exp(-sigma * ds);
		}
		return float4(L, T);
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float2 uv = input.texcoord;
		// Gbuffer texel for this half-res texel: full-res pixel 2c+1, fetched
		// with an explicit Load. It used to be a point Sample at uv, but uv *
		// fullRes lands EXACTLY on the 2c / 2c+1 pixel boundary, so rounding
		// sometimes picked 2c - the row above / column left. CloudTemporal and
		// BilateralUpsample test 2c+1, so a texel just below a silhouette was
		// marched as GEOMETRY here (no cloud) yet accepted as SKY there: a
		// cloud-free texel feeding the sky, upsampled and temporally smeared
		// into a bright rim under every leaf and branch. All three passes
		// must agree on this texel.
		const int3 gbufferTexel = int3(int2(input.position.xy) * 2 + int2(1, 1), 0);
		float pixelDepth = GBUFFER_NORMAL.Load(gbufferTexel).w;
		// SKY detection must match the GI trace: the SKYDOME IS GEOMETRY and
		// writes a real gbuffer depth (its radius), so a depth-only test
		// never fires for sky - the clouds were being depth-clipped against
		// the dome sphere, which reads as a circular cap overhead that never
		// reaches the horizon. Sky is flagged by diffuse.a == -1.
		const float skyFlag = GBUFFER_DIFFUSE.Load(gbufferTexel).a;
		if (skyFlag < -0.5f || pixelDepth <= 0.0f || pixelDepth == -1.0f)
		{
			// No scene occluder: march to the cloud system's own distance
			// limit (maxTraceDistance is still clamped by g_cloudParams0.w).
			pixelDepth = 1e9f;
		}

		const float3 boundsMin = g_cloudBoundsMin.xyz;
		// EFFECTIVE SLAB TOP. Only storm cells use height above the 2050 m
		// design depth (CloudCommon's metre profiles), but a tall slab
		// lengthened every ray - and so every coarse/fine step - for ALL
		// skies: overcast gaps turned to speckle at an 8 km slab. Clip the
		// march to the design depth unless the sky can hold storm cells
		// (same coverage/type gates as CloudCommon's storminess, applied to
		// the global coverage + type bias).
		const float stormPotential = CloudStormSky();
		const float3 boundsMax = float3(g_cloudBoundsMax.x,
			min(g_cloudBoundsMax.y, g_cloudBoundsMin.y + lerp(2050.0f, max(2050.0f, g_cloudBoundsMax.y - g_cloudBoundsMin.y), stormPotential)),
			g_cloudBoundsMax.z);
		const float3 eyePos = g_eyePos.xyz;
		const float3 rayDir = GetWorldRay(uv);
		const float2 cloudHit = RayBoxDist(boundsMin, boundsMax, eyePos, rayDir);

		const float entryDist = cloudHit.x;
		float maxTraceDistance = min(cloudHit.y, max(0.0f, pixelDepth - entryDist));
		maxTraceDistance = min(maxTraceDistance, g_cloudParams0.w);
		// Rays that miss the slab may still cross rain shafts below it.
		const bool rainPossible = g_weatherSurface.precipitationIntensity >= 0.02f && eyePos.y < boundsMin.y;
		if ((cloudHit.y <= 0.0f || maxTraceDistance <= 0.0f) && !rainPossible)
			return float4(0.0f, 0.0f, 0.0f, 0.0f);
		if (cloudHit.y <= 0.0f)
			maxTraceDistance = 0.0f;

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
		// Fixed ~1.2 km into the deck (was 58% of the slab): with a tall storm
		// slab the probe sat at ~5 km, under-reddening every sunset cloud.
		const float3 cloudProbeOrigin = float3(0.0f, min(boundsMin.y + 1200.0f, boundsMax.y), 0.0f);
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
		// LOCALISED lightning: the flash used to light every cloud in the sky
		// uniformly. Put the discharge INSIDE the cell along the bolt's sky
		// direction (35% up the slab, at most 9 km out) and let it glow
		// through the surrounding cloud with distance falloff; a small
		// sky-wide term remains for the sheet-lightning brightening.
		float3 boltPos;
		{
			const float boltT = min(9000.0f, max(0.0f, lerp(boundsMin.y, boundsMax.y, 0.35f) - eyePos.y) / max(lightningDir.y, 0.08f));
			boltPos = eyePos + lightningDir * boltT;
		}
		const float cosSun = dot(rayDir, sunDir);
		const float isotropicPhase = 1.0f / (4.0f * PI);
		const float phase = lerp(isotropicPhase, DualLobeHG(cosSun, g_cloudParams1.z), 0.85f) * g_cloudParams3.w;
		const float silverWeight = saturate(g_cloudParams4.x * 0.40f);
		// Octave phases are per-RAY (cosSun is fixed along the march).
		const float phaseOct0 = CloudPhase(cosSun, g_cloudParams1.z, silverWeight, 1.0f) * g_cloudParams3.w;
		const float phaseOct1 = CloudPhase(cosSun, g_cloudParams1.z, silverWeight, 0.5f) * g_cloudParams3.w;
		const float phaseOct2 = CloudPhase(cosSun, g_cloudParams1.z, silverWeight, 0.25f) * g_cloudParams3.w;
		// Octave contribution b (Wrenninge): the multi-scatter cvar 0.45 -> ~0.5.
		const float msB = saturate(g_cloudParams4.z * 1.1f);
		// Light-march detail only for a well-raised sun (grazing paths speckle).
		const float lightDetailGate = smoothstep(0.06f, 0.25f, sunDir.y) * 0.6f;
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
		// Keep part of the SKY's hue when pulling toward the neutral overcast
		// grey: storm undersides in the reference are dark BLUE-grey (lit by
		// what open sky remains), not neutral - a pure grey ambient against
		// the warm sun term is a big part of why storm decks read flat/muddy.
		{
			const float3 lumaW = float3(0.299f, 0.587f, 0.114f);
			const float3 skyHue = min(ambientSky / max(dot(ambientSky, lumaW), 1e-5f), 2.5f.xxx);
			const float3 overcastTinted = dot(g_skyOvercast.rgb * ambientLift, lumaW) * lerp(1.0f.xxx, skyHue, 0.40f);
			ambientSky = lerp(ambientSky, overcastTinted, saturate(g_skyOvercast.w));
		}

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
		// Warm sun pull is applied PER SAMPLE by sun visibility (see the march):
		// baked into the ambient it tinted the SHADOWED side orange/tan as
		// well, so lit and shadowed faces shared one colour. Shadowed bases
		// keep the cool sky chroma (the reference's blue-grey undersides).
		const float3 ambientShaded = ambientChromatic;
		const float3 ambientWarm = lerp(ambientChromatic, sunStylized, saturate(g_cloudParams5.x) * 0.28f * sunUp);
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
		// x3 (was x2) for the metre-capped fine steps; dense rays still
		// terminate on transmittance within a handful of samples.
		const int maxIters = viewSteps * 3;
		bool fineMode = false;
		int emptyRun = 0;
		float emptyDist = 0.0f;
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
			// Fine steps CAPPED in metres: coarseStep*0.3 reached ~290 m at
			// 5 km, which sampled 100-300 m billow lobes once or not at all -
			// they aliased into soft mush (+ jitter fur). Dense cells still
			// terminate within a few fine steps, so the cost is bounded.
			// Past ~6 km the cap relaxes back to the old 0.3x coarse step:
			// long grazing far rays otherwise exhausted maxIters mid-cloud
			// and ended half-transparent (a sparkly streak band on the deck).
			const float marchDist = entryDist + travelled;
			const float fineStep = min(coarseStep * kFineScale, 45.0f + marchDist * 0.008f + max(0.0f, marchDist - 6000.0f) * 0.03f);
			const float stepLenView = fineMode ? fineStep : coarseStep;
			const float3 samplePos = eyePos + rayDir * (entryDist + travelled);
			// Distance LOD on the detail erosion: full detail near, coarse
			// far - far detail aliases into sparkle grain at dusk.
			// LOD range widened 1.2-6 km -> 3-14 km with a 0.3 floor: with
			// the deck seen almost entirely at 2-15 km, the old ramp stripped
			// detail from nearly every visible cloud (the "blobby" read).
			// The 154 m detail period still spans ~4+ half-res pixels at 10 km.
			const float detailLod = fineMode ? lerp(0.30f, 1.0f, 1.0f - smoothstep(3000.0f, 14000.0f, entryDist + travelled)) : 0.0f;
			const float density = SampleCloudDensityTexImpl(g_shapeNoise, g_detailNoise, g_mirrorSampler, samplePos, boundsMin, boundsMax, windOffset, detailLod);

			if (!fineMode)
			{
				if (density > 0.0001f)
				{
					// Hit in coarse mode: rewind so the fine march covers the
					// span this coarse step just leapt over.
					fineMode = true;
					emptyRun = 0;
					emptyDist = 0.0f;
					travelled = max(0.0f, travelled - coarseStep);
					continue;
				}
				travelled += stepLenView;
				continue;
			}

			if (density > 0.0001f)
			{
				emptyRun = 0;
				emptyDist = 0.0f;
				const float sunTau = MarchToLightTau(samplePos, boundsMin, boundsMax, windOffset, sunDir, lightSteps, detailLod * lightDetailGate);
				const float rawLightTrans = exp(-sunTau);
				const float lightTrans = max(g_cloudParams3.z, rawLightTrans);
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
				// Powder is a view-dependent effect: looking TOWARD the sun the
				// thin edges must glow (silver lining), not darken - fade it
				// out with view-sun alignment (Nubis). Capped at 1: the old x2
				// form brightened mid-depth bulk and flattened the gradient.
				const float powder = lerp(1.0f, min(1.0f, 2.0f * schneiderPowder), saturate(g_cloudParams1.y) * (1.0f - viewToSun * viewToSun));
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
				// Directional-derivative diffuse (off in the shipped scene: cvar
				// pinned 0) - only pay for its full-detail fetch when enabled.
				float directionalDiffuse = 1.0f;
				[branch] if (g_cloudParams5.z > 0.001f)
				{
					const float densityTowardSun = SampleCloudDensity(samplePos + sunDir * diffuseProbeDistance, boundsMin, boundsMax, windOffset);
					const float derivativeDiffuse = saturate((density - densityTowardSun) * 2.25f + 0.12f);
					directionalDiffuse = lerp(1.0f, derivativeDiffuse, saturate(g_cloudParams5.z));
				}

				// MULTIPLE SCATTERING OCTAVES (Wrenninge / Hillaire 2016):
				// sum_i b^i * phase(g * 0.5^i) * exp(-tau * 0.5^i). Each octave
				// penetrates deeper (halved extinction) with a flatter phase.
				// Replaces (a) a multiplier on the single-scatter term - which
				// is ~0 wherever lightTrans is ~0, so it never lit a core - and
				// (b) the stylized "silver lining" pow(shadowAmount, 5) and
				// "forward glow" terms, which were strongest in DEEP SHADOW and
				// painted sun colour over the dark side of every cloud: the
				// main reason lit vs shadowed sides barely differed. The silver
				// lining now comes from the narrow forward lobe in CloudPhase
				// acting on thin edges (tau ~ 0) only.
				const float oct0 = rawLightTrans * phaseOct0 * powder;
				const float oct1 = msB * exp(-sunTau * 0.5f) * phaseOct1;
				const float oct2 = msB * msB * exp(-sunTau * 0.25f) * phaseOct2;
				// Shadow floor applied to the SUM (an isotropic-phase floor), so
				// the octaves can't amplify it into a flat lift.
				const float sunScatter = max(oct0 + oct1 + oct2, g_cloudParams3.z * isotropicPhase * g_cloudParams3.w);
				const float3 directLight = cloudSunColour * (sunScatter * directionalDiffuse);

				// Metre scale (2050 m design depth, see CloudCommon) so a tall
				// storm slab doesn't push every low cloud into the bottom tint.
				const float height01 = saturate((samplePos.y - boundsMin.y) / 2050.0f);
				const float3 heightTint = lerp(bottomTint, topTint, smoothstep(0.08f, 0.92f, height01));
				const float3 stylizedTint = lerp(1.0f.xxx, heightTint, g_cloudParams4.w);
				const float localAO = exp(-density * (0.5f + 1.0f * saturate(g_cloudParams5.w)));
				const float aoTerm = lerp(1.0f, localAO, saturate(g_cloudParams5.w));

				// SKY VISIBILITY from above (2 coarse taps straight up). Cloud
				// bodies are lit by the sky hemisphere ABOVE them; a sample under
				// 1 km of tower sees almost none of it. The old ambient had no
				// notion of this, so a storm base was lit like a cloud top -
				// that is the "dark blue-grey underside vs bright crown"
				// contrast of the reference. Diffuse skylight is attenuated less
				// than the collimated sun (x0.25 on the extinction).
				// Taps at 40 m and 170 m (were 90/300): the near tap resolves
				// the crevices BETWEEN billow lobes, so the lobes themselves
				// read through shading instead of one flat ambient wash.
				const float upD1 = SampleCloudDensityTexCoarse(g_shapeNoise, g_detailNoise, g_mirrorSampler, samplePos + float3(0.0f, 40.0f, 0.0f), boundsMin, boundsMax, windOffset);
				const float upD2 = SampleCloudDensityTexCoarse(g_shapeNoise, g_detailNoise, g_mirrorSampler, samplePos + float3(0.0f, 170.0f, 0.0f), boundsMin, boundsMax, windOffset);
				const float skyVis = exp(-(upD1 * 110.0f + upD2 * 300.0f) * 0.012f * g_cloudParams1.x * 0.25f);
				// Under-cloud floor: bounce from the ground + the open sky at the
				// horizon (neutral-ish, dim).
				const float3 ambientLight = (lerp(ambientShaded, ambientWarm, rawLightTrans) * (0.14f + 0.86f * skyVis) * (1.0f - shadowAmount * 0.20f)
				                          + ambientNeutral * 0.06f) * aoTerm;

				// Localised lightning glow (see boltPos).
				float3 lightningLight = 0.0f.xxx;
				[branch] if (lightningFlash > 0.001f)
				{
					const float boltDist = length(samplePos - boltPos);
					const float lightningEdge = pow(saturate(1.0f - density), 1.4f);
					lightningLight = lightningColour * (0.10f + 2.6f * exp(-boltDist / 1100.0f)) * (0.6f + 0.4f * lightningEdge);
				}
				cloudLight += scatter * (directLight + ambientLight + lightningLight) * stylizedTint;

				const float transmittanceBefore = transmittance;
				transmittance *= exp(-density * stepLenView * invCloudHeight * g_cloudParams3.x * distanceFade);
				// Opacity-weighted mean distance of the cloud along this ray,
				// for the aerial-perspective blend after the march.
				apDistWeighted += (entryDist + travelled) * (transmittanceBefore - transmittance);
				if (transmittance < 0.01f)
					break;
			}
			else
			{
				// Leave fine mode only after BOTH 5 empty samples and >= 1.5
				// coarse steps of empty air. With metre-capped fine steps and
				// lobed (mid-octave) clouds, 5 samples could be just ~225 m - a
				// crevice between lobes - and the next coarse leap (up to ~1 km)
				// jumped clean over the following lobe: sky showing through
				// cracks along a constant-distance arc.
				++emptyRun;
				emptyDist += stepLenView;
				if (emptyRun >= kEmptyToCoarse && emptyDist >= coarseStep * 1.5f)
					fineMode = false;
			}

			travelled += stepLenView;
		}

		// Keep dense cores from collapsing to pure black under aggressive
		// shadowing. 0.20 -> 0.08: with sky-visibility ambient the floor was
		// the dominant term on storm bases and pinned them at a flat grey.
		cloudLight = max(cloudLight, ambientShaded * (1.0f - transmittance) * 0.08f);

		// Rain shafts sit BELOW the base, i.e. in front of the cloud on every
		// ray from a camera under the deck.
		float4 rain = float4(0.0f, 0.0f, 0.0f, 1.0f);
		if (rainPossible)
		{
			const float3 ambientRain = ambientShaded * 0.45f + ambientNeutral * 0.10f;
			rain = MarchRainShafts(eyePos, rayDir, pixelDepth, boundsMin, boundsMax, windOffset, sunDir, cloudSunColour, ambientRain, jitter);
		}

		const float alpha = saturate(1.0f - transmittance);
		if (alpha <= 1e-4f)
		{
			const float rainAlpha = 1.0f - rain.a;
			if (rainAlpha <= 1e-4f)
				return 0.0f.xxxx;
			return float4(min(rain.rgb / max(0.03f, rainAlpha), 64.0f.xxx), rainAlpha);
		}

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
		const float3 totalLight = rain.rgb + rain.a * cloudLight;
		const float totalAlpha = 1.0f - rain.a * (1.0f - alpha);
		const float safeAlpha = max(0.03f, totalAlpha);
		const float3 straightCloud = min(totalLight / safeAlpha, 64.0f.xxx);
		return float4(straightCloud, totalAlpha);
	}
}
