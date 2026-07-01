"ComputeShaderIncludes"
{
	Global
	AtmosphereCommon
}
"ComputeShader"
{
	// Hillaire 2020 sky-view LUT generator.
	//
	// 192x108 RGBA16F per-frame compute target capturing the camera's
	// full-sky in-scattering for the current sun direction. The sky-sphere
	// shader samples this with one tap per pixel instead of doing a per-
	// pixel atmosphere ray-march - turns the sky shader from a heavy
	// integration into a cheap LUT lookup.
	//
	// Parameterisation (Hillaire §5.3):
	//   u (192) = view azimuth, in [0, 2*PI] relative to the sun.
	//             Wraps cleanly so the sky doesn't break at azimuth=0.
	//   v (108) = view zenith with a horizon-bias remap. The lower half
	//             of v is non-linearly compressed near v=0.5 so we keep
	//             texel density at the horizon (where the sun glow,
	//             sunset gradient, and earth shadow line all live) and
	//             leave less for the zenith (which is almost-constant
	//             blue and tolerates the lower density).
	//
	// Camera convention: looking from a point at (cameraHeight) altitude
	// above ground. View ray is constructed in atmosphere space; the
	// final sample colour is the integrated single + multi scattering
	// along that view ray.

	RWTexture2D<float4> g_skyViewLUT         : register(u0);
	Texture2D           g_transmittanceLUT   : register(t0);
	Texture2D           g_multiScatteringLUT : register(t1);
	SamplerState        g_linearSampler      : register(s4);

	cbuffer AtmosphereParams : register(b5)
	{
		float g_groundRadiusMM;
		float g_topRadiusMM;
		float g_rayleighScaleHeightMM;
		float g_mieScaleHeightMM;
		float g_ozoneCentreMM;
		float g_ozoneHalfWidthMM;
		float g__pad0;
		float g__pad1;
		float3 g_rayleighScatteringPerMM;
		float  g_mieScatteringPerMM;
		float g_mieExtinctionPerMM;
		float g_miePhaseG;
		float g__pad2;
		float g__pad3;
		float3 g_ozoneAbsorptionPerMM;
		float  g__pad4;
	};

	// Per-frame "where's the camera, where's the sun" data, filled in C++
	// before dispatch. cameraHeightMM is the camera's altitude above the
	// planet centre (i.e. groundRadiusMM + (cameraY metres) * 1e-6).
	cbuffer SkyViewParams : register(b6)
	{
		float  g_cameraHeightMM;
		float  g_skyViewPad0;
		float  g_skyViewPad1;
		float  g_skyViewPad2;
		float3 g_sunDirection;
		float  g_sunIntensity;
	};

	static const uint2 LUT_SIZE = uint2(192u, 108u);
	static const uint  MARCH_STEPS = 32u;

	// Forward cbuffer-driven coefficients into the common helper. See
	// the matching wrapper in AtmosphereTransmittanceLUT for the why.
	AtmosphereSamplePoint SampleAtmosphereMedium(float3 pos)
	{
		return SampleAtmosphereMediumWithCoefs(
			pos,
			g_rayleighScatteringPerMM,
			g_mieScatteringPerMM,
			g_mieExtinctionPerMM,
			g_ozoneAbsorptionPerMM);
	}

	// UV-mapping helpers live in AtmosphereCommon now.
	float3 SampleTransmittance(float viewHeight, float viewZenithCos)
	{
		const float2 uv = TransmittanceLutParamsToUv(viewHeight, viewZenithCos);
		return g_transmittanceLUT.SampleLevel(g_linearSampler, uv, 0).rgb;
	}

	float3 SampleMultiScattering(float viewHeight, float sunCosZenith)
	{
		const float2 uv = MultiScatteringLutParamsToUv(viewHeight, sunCosZenith);
		return g_multiScatteringLUT.SampleLevel(g_linearSampler, uv, 0).rgb;
	}

	// Inverse of the horizon-bias UV mapping. Keeps texel density at the
	// horizon (v=0.5) and stretches the zenith / nadir halves outward.
	void UvToSkyViewParams(float2 uv, out float viewZenithCos, out float viewAzimuth)
	{
		viewAzimuth = uv.x * 2.0f * kATM_PI;

		// Two halves with sqrt remap so the slope at v=0.5 is shallow
		// (high density) and steep away from it.
		const float vRemap = uv.y;
		float coord;
		if (vRemap < 0.5f)
		{
			coord = 1.0f - 2.0f * vRemap;
			coord = coord * coord;
			viewZenithCos = clamp(coord, 0.0f, 1.0f);   // upper hemisphere - cos in [1, 0]
		}
		else
		{
			coord = 2.0f * vRemap - 1.0f;
			coord = coord * coord;
			viewZenithCos = -clamp(coord, 0.0f, 1.0f);  // lower hemisphere - cos in [0, -1]
		}
	}

	[numthreads(8, 8, 1)]
	void ShaderMain(uint3 dtid : SV_DispatchThreadID)
	{
		if (any(dtid.xy >= LUT_SIZE))
			return;

		const float2 uv = (float2(dtid.xy) + 0.5f) / float2(LUT_SIZE);

		float viewZenithCos, viewAzimuth;
		UvToSkyViewParams(uv, viewZenithCos, viewAzimuth);

		// Build the view direction in the sun-aligned atmosphere frame.
		// Convention: the sun's horizontal projection lives along +X (see
		// sun-vector construction below). viewAzimuth = 0 therefore puts
		// the view at the sun's horizontal direction; +pi/2 rotates CCW
		// (looking down -Y) into +Z. This matches the inverse helper
		// SkyViewLutParamsToUv() in AtmosphereCommon, which computes
		// view azimuth relative to the sun via dot/cross with sunHoriz.
		const float sinZenith = sqrt(max(0.0f, 1.0f - viewZenithCos * viewZenithCos));
		const float3 viewDir = float3(
			sinZenith * cos(viewAzimuth),
			viewZenithCos,
			sinZenith * sin(viewAzimuth));

		// Rebuild the sun direction in this LOCAL sun-aligned frame. The
		// LUT is rotationally symmetric around the sun's vertical axis, so
		// we only need the sun's elevation (cbuffer's sunDirection.y) to
		// position it. Sun lives at (sinZenith, cosZenith, 0) in the frame
		// the view directions are built in - dot products of view-with-sun
		// then correctly produce the same cos(viewSunAngle) the sample-
		// time inverse helper computes from world directions.
		const float sunCosZenithLocal = clamp(g_sunDirection.y, -1.0f, 1.0f);
		const float sunSinZenithLocal = sqrt(max(0.0f, 1.0f - sunCosZenithLocal * sunCosZenithLocal));
		const float3 sunDirLocal = float3(sunSinZenithLocal, sunCosZenithLocal, 0.0f);

		const float cameraHeight = max(g_cameraHeightMM, groundRadiusMM + 1e-4f);
		const float3 origin = float3(0.0f, cameraHeight, 0.0f);

		// March length: to atmosphere top, or ground hit if any.
		const float tTop    = RaySphereIntersectExit(origin, viewDir, topRadiusMM);
		const float tGround = RaySphereIntersectEntry(origin, viewDir, groundRadiusMM);
		const bool  hitsGround = tGround > 0.0f;
		const float tMax = hitsGround ? tGround : tTop;
		if (tMax <= 0.0f)
		{
			g_skyViewLUT[dtid.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
			return;
		}

		// Phase functions evaluated once per pixel - constant along the ray.
		const float cosViewSun = dot(viewDir, sunDirLocal);
		const float phaseR     = RayleighPhaseAtm(cosViewSun);
		const float phaseM     = MiePhaseCornetteShanksAtm(cosViewSun, g_miePhaseG);

		float3 luminance     = 0.0f.xxx;
		float3 transmittance = 1.0f.xxx;
		const float dt = tMax / (float)MARCH_STEPS;
		float t = 0.5f * dt;

		[loop]
		for (uint s = 0u; s < MARCH_STEPS; ++s)
		{
			const float3 p = origin + viewDir * t;
			const AtmosphereSamplePoint sp = SampleAtmosphereMedium(p);

			const float pAlt = length(p);
			const float pSunCos = dot(normalize(p), sunDirLocal);
			const float3 sunTransmittance = SampleTransmittance(pAlt, pSunCos);
			const float3 msContribution   = SampleMultiScattering(pAlt, pSunCos);

			// Phase-weighted single-scattering + isotropic multi-scattering.
			const float3 scatterPhased = sp.rayleighScattering * phaseR
			                           + sp.mieScattering.xxx * phaseM;
			const float3 isoScatter    = sp.rayleighScattering + sp.mieScattering.xxx;

			const float3 inscatter = (scatterPhased * sunTransmittance + isoScatter * msContribution) * g_sunIntensity;

			const float3 sampleTransmittance = exp(-sp.extinction * dt);
			const float3 stepGain = (1.0f.xxx - sampleTransmittance) / max(sp.extinction, 1e-6f.xxx);
			luminance += transmittance * inscatter * stepGain;

			transmittance *= sampleTransmittance;
			t += dt;
		}

		g_skyViewLUT[dtid.xy] = float4(luminance, 1.0f);
	}
}
