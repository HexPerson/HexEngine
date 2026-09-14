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
	AtmosphereCommon
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

		// Velocity - SKY SPECIAL CASE. The sky is an infinite background: its
		// only honest per-pixel motion is CAMERA ROTATION. Reproject the view
		// DIRECTION through both frames' matrices with w = 0 (drops the
		// translation rows), ignoring the dome entity's transform entirely.
		// The old path used instance.worldPrev like a regular mesh, and any
		// staleness in that per-instance previous world painted an enormous
		// radial phantom-velocity disc across the whole sky - which TAA has
		// been silently eating for ages and motion blur turned into a
		// full-screen smear.
		{
			const float3 skyDir = normalize(output.positionWS.xyz - g_eyePos.xyz);
			output.currentPositionUnjittered = mul(float4(skyDir, 0.0f), g_viewProjectionMatrix);
			output.previousPositionUnjittered = mul(float4(skyDir, 0.0f), g_viewProjectionMatrixPrev);
		}

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

	// Hillaire 2020 atmosphere LUTs (Phase B). When USE_SKY_VIEW_LUT is
	// true (default), the integrate-atmosphere call further down becomes
	// a single sky-view LUT tap and the per-pixel sun ray attenuation is
	// driven by the transmittance LUT instead of ComputePhysicalSunTransmittance.
	// SceneRenderer binds these textures at t0/t1 before each Layer::Sky
	// draw; PS sampler slot 4 is already a linear sampler by engine
	// convention (see GraphicsDeviceD3D11::BeginFrame).
	Texture2D    g_atmSkyViewLUT       : register(t0);
	Texture2D    g_atmTransmittanceLUT : register(t1);
	SamplerState g_atmLutSampler       : register(s4);

	// Post-LUT overcast tint. SceneRenderer derives these from the weather
	// surface params (precipitationIntensity, wetness) and binds the
	// cbuffer at PS b6 before the sky entity draws. The Hillaire model
	// itself can only produce clear-sky / sunset gradients; this tint
	// fakes overcast / storm / rain weather by lerping the LUT result
	// toward a cloud-cover colour. Amount 0 = pure Hillaire, 1 = pure tint.
	cbuffer SkyRenderParams : register(b6)
	{
		float4 g_skyOvercastColour;   // .rgb = tint, .a unused
		float  g_skyOvercastAmount;
		float3 g_skyRenderPad;        // .x = LUT available, .y = moon phase (0 new..1 full), .z = moon diameter (deg)
		// HDR sky params (layout must match AtmosphereLUTs.cpp
		// SkyRenderParamsCB). x = moon intensity (0 = no moon),
		// y = sun disc angular diameter in degrees,
		// z = sun disc intensity multiplier, w = star field intensity.
		float4 g_skyHdrParams;
		// Cirrus layer (S4). x = amount, y = type (0 = streaks, 1 =
		// cirrocumulus ripples), z/w = CPU-integrated wind offset (metres).
		float4 g_skyCirrusParams;
	};

	// Flip to false to bypass the LUT path and fall back to the analytic
	// IntegrateAtmospherePhysical / ComputePhysicalSunColour calls below.
	// Useful for A/B comparison when iterating on LUT parameterisation.
	// Static const so the shader compiler eliminates the dead branch.
	static const bool USE_SKY_VIEW_LUT = true;

	float3 SampleSkyFromLUT(float3 viewDir, float3 sunDir)
	{
		const float2 uv = SkyViewLutParamsToUv(viewDir, sunDir);
		return g_atmSkyViewLUT.SampleLevel(g_atmLutSampler, uv, 0).rgb;
	}

	// LUT-driven sun colour: near-white solar radiance attenuated by the
	// physical transmittance LUT at the camera's altitude, scaled by the
	// same energy factor the LUT generation uses so disc and sky share a
	// footing. The LUT's ozone + Rayleigh + Mie extinction is what reddens
	// the low sun - the old hand-tuned green/blue crush and artificial
	// sunset radiance lerp fought it and double-tinted (HDR sky S1 removed
	// them; retune sunset saturation via the atmosphere params, not here).
	float3 SampleSunColourFromLUT(float3 sunDir, float cameraHeightMM)
	{
		const float sunCosZenith = sunDir.y; // y-up world
		const float2 uv = TransmittanceLutParamsToUv(cameraHeightMM, sunCosZenith);
		const float3 sunTransmittance = g_atmTransmittanceLUT.SampleLevel(g_atmLutSampler, uv, 0).rgb;

		const float sunEnergy = lerp(18.0f, 30.0f, saturate(sunDir.y * 0.5f + 0.5f)) * max(g_globalLight[0], 0.35f);
		const float3 solarRadiance = float3(1.0f, 0.985f, 0.965f); // slightly warm white
		return solarRadiance * sunTransmittance * sunEnergy;
	}

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

	// 2D value noise + fbm for the cirrus layer.
	float skyValueNoise2(float2 p)
	{
		const float2 pi = floor(p);
		const float2 pf = frac(p);
		const float2 w = pf * pf * (3.0f - 2.0f * pf);
		const float n00 = hash12(pi);
		const float n10 = hash12(pi + float2(1.0f, 0.0f));
		const float n01 = hash12(pi + float2(0.0f, 1.0f));
		const float n11 = hash12(pi + float2(1.0f, 1.0f));
		return lerp(lerp(n00, n10, w.x), lerp(n01, n11, w.x), w.y);
	}

	float skyFbm2(float2 p)
	{
		return skyValueNoise2(p) * 0.55f
		     + skyValueNoise2(p * 2.17f + 13.7f.xx) * 0.28f
		     + skyValueNoise2(p * 4.71f + 41.3f.xx) * 0.17f;
	}

	// 3D value noise + fbm, for full-sky features sampled by DIRECTION.
	// Any 2D parameterisation of the sphere either has poles (equirect:
	// radial smear at the zenith) or a collapsed axis (plane projection:
	// linear streaking along the band) - both were tried for the Milky Way
	// and both artifacted. Isotropic 3D noise on the direction has neither.
	float hash13(float3 p)
	{
		float3 p3 = frac(p * 0.1031f);
		p3 += dot(p3, p3.zyx + 31.32f);
		return frac((p3.x + p3.y) * p3.z);
	}

	float skyValueNoise3(float3 p)
	{
		const float3 pi = floor(p);
		const float3 pf = frac(p);
		const float3 w = pf * pf * (3.0f - 2.0f * pf);
		const float n000 = hash13(pi);
		const float n100 = hash13(pi + float3(1, 0, 0));
		const float n010 = hash13(pi + float3(0, 1, 0));
		const float n110 = hash13(pi + float3(1, 1, 0));
		const float n001 = hash13(pi + float3(0, 0, 1));
		const float n101 = hash13(pi + float3(1, 0, 1));
		const float n011 = hash13(pi + float3(0, 1, 1));
		const float n111 = hash13(pi + float3(1, 1, 1));
		const float nx00 = lerp(n000, n100, w.x);
		const float nx10 = lerp(n010, n110, w.x);
		const float nx01 = lerp(n001, n101, w.x);
		const float nx11 = lerp(n011, n111, w.x);
		const float nxy0 = lerp(nx00, nx10, w.y);
		const float nxy1 = lerp(nx01, nx11, w.y);
		return lerp(nxy0, nxy1, w.z);
	}

	float skyFbm3(float3 p)
	{
		return skyValueNoise3(p) * 0.55f
		     + skyValueNoise3(p * 2.17f + 13.7f.xxx) * 0.28f
		     + skyValueNoise3(p * 4.71f + 41.3f.xxx) * 0.17f;
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

		// Phase B: prefer the SkyView LUT tap over the per-pixel atmosphere
		// march. Falls back to the analytic integrator when USE_SKY_VIEW_LUT
		// is flipped off (A/B comparison hook). Camera altitude is lifted
		// into atmosphere-Mm space via the AtmosphereCommon helper so the
		// transmittance sample agrees with the LUT's parameterisation.
		float3 atmosphereColour;
		const float cameraHeightMM = WorldYToAtmosphereAltitudeMM(g_eyePos.y);
		if (USE_SKY_VIEW_LUT)
		{
			atmosphereColour = SampleSkyFromLUT(viewDir, sunDir);
		}
		else
		{
			int steps = fullSkyPass ? 32 : 20;
			PhysicalAtmosphereSample skySample = IntegrateAtmospherePhysical(
				g_eyePos.xyz,
				viewDir,
				g_frustumDepths[3],
				sunDir,
				steps,
				fullSkyPass && sunVisible);
			atmosphereColour = skySample.inscatter;
		}
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

		// HDR sky S1: these weights are trims on top of the LUT's physical
		// sunset, not the sunset itself any more. Pre-HDR they also fought a
		// double gamma (in-shader pow 2.2 + display) that washed the LUT's
		// own colours out, so they carried most of the look; in linear HDR
		// the LUT sunset reads at full strength and the stack runs at 60% of
		// its old weights. env_sunsetWarm/CoolStrength remain the live trims.
		atmosphereColour = lerp(atmosphereColour, sunsetWarmTarget, sunsetHorizon * sunFacing * (0.20f * sunsetWarmStrength));
		atmosphereColour = lerp(atmosphereColour, sunsetOrangeTarget, sunsetHorizon * sunFacing * (0.11f * sunsetWarmStrength));
		atmosphereColour = lerp(atmosphereColour, sunsetPurpleTarget, sunsetHorizon * antiSunFacing * (0.13f * sunsetCoolStrength));
		atmosphereColour = lerp(atmosphereColour, sunsetVioletTarget, sunsetZenith * antiSunFacing * (0.10f * sunsetCoolStrength));

		// Weather-driven overcast/storm tint. Hillaire's clear-sky model
		// can't produce flat grey or stormy white skies (those need cloud
		// cover). SceneRenderer derives g_skyOvercastColour/Amount from
		// WeatherSurfaceParams.precipitationIntensity + .wetness; the
		// shader lerps the LUT-driven sky toward the cloud-bottom colour.
		// Amount 0 = pure Hillaire (clear day). Amount near 1 = pure
		// overcast/storm grey, dimmed by sun height so night storms read
		// dark and noon storms read dramatic-bright-white.
		const float overcastAmount = saturate(g_skyOvercastAmount);
		atmosphereColour = lerp(atmosphereColour, g_skyOvercastColour.rgb, overcastAmount);

		// ---- S4: high-altitude cirrus layer -------------------------------
		// A 2D layer at ~7 km, drawn in the sky pass so the volumetric
		// clouds (much lower) composite over it later. Lit by the
		// transmittance LUT at ITS altitude: after ground sunset the LUT
		// still passes red light at 7 km, so the layer catches the
		// afterglow while the low clouds are already dark - the RDR2
		// reference-shot look. Faded out under overcast (real cirrus is
		// hidden above the deck).
		float cirrusAlpha = 0.0f;
		const float cirrusAmount = saturate(g_skyCirrusParams.x) * (1.0f - overcastAmount);
		if (fullSkyPass && cirrusAmount > 0.003f && viewDir.y > 0.015f)
		{
			const float cirrusHeightM = 7000.0f;
			const float planeT = (cirrusHeightM - g_eyePos.y) / max(viewDir.y, 0.015f);
			const float2 planePos = g_eyePos.xz + viewDir.xz * planeT + g_skyCirrusParams.zw;

			// Two characters, blended by type: streaky cirrus (strongly
			// anisotropic fbm - wind-combed filaments) and cirrocumulus
			// ripples (finer isotropic billow grains).
			const float cirrusType = saturate(g_skyCirrusParams.y);
			const float streakField = skyFbm2(planePos * float2(1.0f / 1500.0f, 1.0f / 7500.0f));
			const float rippleBase = skyFbm2(planePos * (1.0f / 1050.0f));
			const float rippleGrain = skyValueNoise2(planePos * (1.0f / 260.0f) + 7.7f.xx);
			const float rippleField = saturate(rippleBase * 0.72f + rippleGrain * 0.38f);
			const float field = lerp(streakField, rippleField, cirrusType);

			// Coverage threshold, same quantile style as the cloud weather
			// map (the fbm lives roughly in [0.2, 0.8]).
			const float cirrusThreshold = lerp(0.72f, 0.30f, cirrusAmount);
			const float cirrusMask = smoothstep(cirrusThreshold, cirrusThreshold + 0.26f, field);

			// Slant-path thickening toward the horizon (a thin layer viewed
			// at grazing angle reads denser), then a hard fade at the very
			// horizon where the plane projection degenerates.
			const float slant = saturate(1.0f / max(viewDir.y * 3.2f, 0.35f));
			const float horizonCut = smoothstep(0.015f, 0.05f, viewDir.y);
			cirrusAlpha = cirrusMask * lerp(0.30f, 0.44f, cirrusType) * slant * horizonCut;

			// Sun radiance transmitted to 7 km - the afterglow driver.
			const float cirrusAltMM = WorldYToAtmosphereAltitudeMM(cirrusHeightM);
			const float3 cirrusSun = USE_SKY_VIEW_LUT
				? SampleSunColourFromLUT(sunDir, cirrusAltMM)
				: ComputePhysicalSunColour(float3(0.0f, cirrusHeightM, 0.0f), sunDir);

			// Thin-cloud scatter: mostly forward (bright wash near the sun),
			// small isotropic body, plus the sky behind as the ambient term.
			const float mu = dot(viewDir, sunDir);
			const float forwardScatter = 0.045f + 0.16f * pow(saturate(mu * 0.5f + 0.5f), 8.0f);
			const float3 cirrusColour = cirrusSun * forwardScatter + atmosphereColour * 0.40f;

			atmosphereColour = lerp(atmosphereColour, cirrusColour, cirrusAlpha);
		}

		if (fullSkyPass && sunVisible)
		{
			// HDR sky S1: physically-sized sun disc with limb darkening.
			// The old disc was a hardcoded smoothstep ~1.5-4x oversize whose
			// output was then Reinhard-clamped below 1.0; now the disc is
			// genuine HDR radiance and bloom produces the glare naturally.
			float mu = dot(viewDir, sunDir);
			float3 sunColour = USE_SKY_VIEW_LUT
				? SampleSunColourFromLUT(sunDir, cameraHeightMM)
				: ComputePhysicalSunColour(g_eyePos.xyz, sunDir);

			// Angle between view ray and sun centre. acos(mu) loses precision
			// exactly where we need it (mu ~ 1); the small-angle identity
			// sqrt(2*(1-mu)) is exact to ~1e-4 rad over the disc's extent.
			const float sunAngle = sqrt(2.0f * max(1.0f - mu, 0.0f));
			// g_skyHdrParams.y = angular DIAMETER in degrees (physical 0.53).
			const float discRadius = max(g_skyHdrParams.y, 0.05f) * (3.14159265f / 180.0f) * 0.5f;

			// Antialiased disc edge: feather over ~12% of the radius (a
			// couple of pixels at 1080p for the physical size).
			const float discEdge = discRadius * 0.12f;
			float sunDisk = 1.0f - smoothstep(discRadius - discEdge, discRadius + discEdge, sunAngle);

			// Limb darkening: standard power-law fit, wavelength-dependent
			// exponents (red darkens least - the solar limb looks orange).
			// u = normalised disc radius, cosTheta' = emergent angle.
			const float u = saturate(sunAngle / discRadius);
			const float limbCos = sqrt(max(1.0f - u * u, 0.0f));
			const float3 limb = pow(max(limbCos, 1e-3f).xxx, float3(0.397f, 0.573f, 0.719f));

			// Aureole (forward-scatter glow hugging the disc) + wide halo.
			// Re-derived around the physical radius: aureole to ~4 radii,
			// halo to ~12. These are small trims - bloom carries the real
			// glare now - but they keep the near-sun sky from reading as a
			// hard cutout, and grow at sunset like the old look did.
			float aureoleT = saturate(1.0f - (sunAngle - discRadius) / (discRadius * 3.0f));
			float haloT    = saturate(1.0f - (sunAngle - discRadius) / (discRadius * 11.0f));
			float sunAureole = pow(aureoleT, lerp(3.2f, 2.2f, sunsetAmount));
			float sunHalo    = pow(haloT,    lerp(6.5f, 4.2f, sunsetAmount));

			// Damp the sun disk and aureole/halo when overcast - the sun
			// is occluded by cloud cover in storms/rain so it shouldn't
			// punch through full strength. Squared to keep light overcast
			// still showing some sun glow. Cirrus at this pixel also veils
			// the disc (thin ice cloud: mostly haloed, not blocked).
			const float sunVisibility = saturate(1.0f - overcastAmount * overcastAmount)
				* (1.0f - cirrusAlpha * 0.85f);

			// Disc radiance: sunColour is the transmitted solar radiance on
			// the LUT's energy scale (~28 at noon). The x6 base lift puts the
			// disc solidly above the bloom threshold across the day while
			// staying far below a physical 1e5x (which would swamp 16F
			// intermediates); r_sunDiscIntensity scales from there.
			const float discIntensity = 6.0f * max(g_skyHdrParams.z, 0.0f);

			float3 aureoleColour = lerp(sunColour, atmosphereColour, 0.36f);
			float3 haloColour = lerp(sunColour, atmosphereColour, 0.74f);

			atmosphereColour += sunColour * limb * (sunDisk * discIntensity * sunVisibility);
			atmosphereColour += aureoleColour * (sunAureole * lerp(0.045f, 0.24f * sunsetGlowStrength, sunsetAmount) * sunVisibility);
			atmosphereColour += haloColour * (sunHalo * lerp(0.005f, 0.055f * sunsetGlowStrength, sunsetAmount) * sunVisibility);
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

		// HDR sky S1: the sky writes LINEAR HDR radiance into the GBuffer.
		// The Reinhard tonemap + gamma 2.2 that used to sit here clamped the
		// whole dome (sun included) below 1.0 INSIDE the shader - upstream of
		// bloom, auto-exposure and the real tonemapper - so the sun could
		// never bloom and the dome sat on a different brightness footing than
		// fog/IBL (which sample the sky-view LUT linearly). The engine's
		// post chain (exposure -> bloom -> ACES/AgX) now handles the mapping.

		// ---- Night sky (S8 moon + S9 star polish), visible pass only. In
		// linear HDR every radiance here is data - the night exposure lifts
		// the frame, so these are authored faint and exposure does the work.
		if (g_shadowConfig.passIndex != 0)
		{
			float sunElevation = -g_lightDirection.y;
			float nightFactor = saturate((-sunElevation + 0.03f) / 0.25f);
			float horizonFade = smoothstep(0.02f, 0.20f, viewDir.y);

			// SIDEREAL ROTATION: rotate the star/galaxy lookup direction
			// around the sun's orbit axis by the sun's own orbit angle, so
			// the celestial sphere turns with the night instead of being
			// bolted to the world. Exact for the default yaw-0 sun orbit
			// (YZ plane); approximate but stable for authored yaws.
			const float orbitAngle = atan2(sunDir.z, sunDir.y);
			const float oc = cos(orbitAngle);
			const float os = sin(orbitAngle);
			const float3 starDir = float3(viewDir.x,
				viewDir.y * oc - viewDir.z * os,
				viewDir.y * os + viewDir.z * oc);

			// ---- Moon (S8): opaque disc antipodal to the sun, so it rises
			// as the sun sets. Phase/diameter/intensity from the cbuffer.
			float moonMask = 0.0f;
			const float moonIntensity = max(g_skyHdrParams.x, 0.0f);
			if (moonIntensity > 0.001f && nightFactor > 0.01f)
			{
				const float3 moonDir = -sunDir;
				const float mmu = dot(viewDir, moonDir);
				if (mmu > 0.9f)
				{
					const float mAngle = sqrt(2.0f * max(1.0f - mmu, 0.0f));
					const float mRadius = max(g_skyRenderPad.z, 0.05f) * (3.14159265f / 180.0f) * 0.5f;
					const float mEdge = mRadius * 0.10f;
					moonMask = 1.0f - smoothstep(mRadius - mEdge, mRadius + mEdge, mAngle);
					if (moonMask > 0.001f)
					{
						// Disc-plane coordinates in [-1,1] for phase + albedo.
						const float3 mRight = normalize(cross(float3(0.0f, 1.0f, 0.0f), moonDir) + float3(1e-4f, 0.0f, 0.0f));
						const float3 mUp = cross(moonDir, mRight);
						const float3 offs = viewDir - moonDir * mmu;
						float2 dc = float2(dot(offs, mRight), dot(offs, mUp)) / max(mRadius, 1e-5f);
						dc = clamp(dc, -1.0f.xx, 1.0f.xx);
						const float dz = sqrt(max(1.0f - dot(dc, dc), 0.0f)); // sphere bulge

						// Procedural albedo: bright highlands + darker maria.
						const float highlands = 0.62f + 0.38f * skyFbm2(dc * 5.3f + 17.0f.xx);
						const float maria = smoothstep(0.35f, 0.75f, skyValueNoise2(dc * 2.1f + 4.2f.xx));
						const float albedo = highlands * lerp(1.0f, 0.55f, maria);

						// Phase terminator sweeping across the disc x-axis;
						// earthshine keeps the dark side faintly visible.
						const float phase = saturate(g_skyRenderPad.y);
						const float termX = lerp(1.2f, -1.2f, phase);
						const float lit = smoothstep(termX - 0.22f, termX + 0.22f, dc.x);
						const float earthshine = 0.05f;

						const float3 moonColour = float3(0.94f, 0.95f, 1.0f)
							* albedo * (lit + earthshine)
							* lerp(0.65f, 1.0f, dz)          // limb darkening
							* (0.30f * moonIntensity);

						// Opaque body: REPLACES the sky (and any star that
						// would be behind it), veiled by cirrus.
						atmosphereColour = lerp(atmosphereColour, moonColour,
							moonMask * nightFactor * (1.0f - cirrusAlpha * 0.9f));
					}
				}
			}

			// ---- Stars (S9): rotated domain, per-star colour temperature.
			float starsA = starField(starDir, float3(17.13f, 53.91f, 11.0f), 1.0f, 0.85f);
			float starsB = starField(starDir, float3(91.71f, 11.37f, 29.0f), 1.65f, 0.60f);
			float stars = saturate(starsA * 0.85f + starsB * 0.55f);

			float twinkle = 0.85f + 0.15f * sin(g_time * 2.2f + hash12(viewDir.xz * 137.0f) * 6.2831f);
			stars *= twinkle * nightFactor * horizonFade * (1.0f - cirrusAlpha) * (1.0f - moonMask);

			// Colour temperature variation: hash the star-space direction so
			// each star keeps its tint as the sphere rotates.
			const float tempHash = hash12(floor(starDir.xz * 401.0f) + floor(starDir.yy * 397.0f));
			const float3 starWarm = float3(1.0f, 0.86f, 0.70f);
			const float3 starCool = float3(0.74f, 0.84f, 1.08f);
			const float3 starColour = lerp(starWarm, starCool, tempHash);
			atmosphereColour += starColour * stars * max(g_skyHdrParams.w, 0.0f);

			// ---- Milky Way (S9): a granular band along a fixed great circle
			// in star space, rotating with the stars. Kept DIMMER than the
			// star cores (the real thing is barely above the sky background -
			// stars punch through it), with strong patchiness and hue
			// variation: warm golden glow toward the galactic-core end of the
			// band, blue-white along the arms, rusty dark dust rifts.
			{
				const float3 galaxyNormal = normalize(float3(0.36f, 0.52f, 0.78f));
				const float3 galaxyTangent = normalize(cross(galaxyNormal, float3(0.0f, 1.0f, 0.0f)) + float3(1e-4f, 0.0f, 0.0f));
				const float bandDist = dot(starDir, galaxyNormal);
				const float alongBand = dot(starDir, galaxyTangent);
				const float band = exp(-(bandDist * bandDist) / (0.14f * 0.14f));
				// Noise domain: isotropic 3D value noise on the star-space
				// DIRECTION. Equirect uv smeared radial spokes at the zenith;
				// the galactic-plane projection collapsed the across-band
				// axis into linear streaks. 3D-by-direction has no poles, no
				// seams, and no preferred axis (see skyFbm3).
				const float clumps = skyFbm3(starDir * 6.5f + 3.1f.xxx);
				const float grain  = skyFbm3(starDir * 22.0f + 12.9f.xxx);
				// Dust rifts: the dark rivers that split the band - sharpened
				// and strong, they carry most of the perceived structure.
				const float rift = smoothstep(0.35f, 0.62f, skyFbm3(starDir * 4.2f + 8.7f.xxx));
				const float milky = band
					* pow(saturate(clumps * 0.6f + grain * 0.5f), 2.4f)
					* lerp(1.0f, 0.15f, rift);

				const float coreT = saturate(alongBand * 1.4f + 0.2f);
				float3 milkyColour = lerp(float3(0.62f, 0.72f, 0.98f), float3(1.0f, 0.86f, 0.62f), coreT * 0.75f);
				milkyColour = lerp(milkyColour, float3(0.75f, 0.58f, 0.45f), rift * 0.5f);

				atmosphereColour += milkyColour * milky
					* (max(g_skyHdrParams.w, 0.0f) * 1.6f)
					* nightFactor * horizonFade * (1.0f - cirrusAlpha) * (1.0f - moonMask);
			}
		}

		GBufferOut output;

		// Direction-based reprojection interpolants (see the VS): w goes
		// non-positive only if the camera rotated more than 90 degrees in a
		// single frame - zero the velocity rather than divide by it.
		float2 velocity = 0.0f.xx;
		if (input.currentPositionUnjittered.w > 1e-4f && input.previousPositionUnjittered.w > 1e-4f)
		{
			velocity = CalcVelocity(input.currentPositionUnjittered, input.previousPositionUnjittered, float2(g_screenWidth, g_screenHeight));
		}
		output.diff = float4(atmosphereColour, -1);

		output.mat = float4(0, 0, 0, 0);

		output.norm = float4(0, 0, 0, g_frustumDepths[3]);

		output.velocity = float2(velocity);

		// project it out as far as possible to mimic far away sky
		float3 worldSpaceDir = normalize(input.positionWS.xyz - g_eyePos);
		float3 worldSpacePos = g_eyePos + worldSpaceDir * g_frustumDepths[3];

		output.pos = float4(worldSpacePos, -1.0f);

		// Sky doesn't need any of the material features - emits its own colour through
		// the standard deferred path's sky branch (diff.a == -1).
		output.feat = float4(0.0f, 0.0f, 0.0f, 0.0f);

		return output;
	}
}
