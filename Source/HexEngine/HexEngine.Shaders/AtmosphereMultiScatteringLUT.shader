"ComputeShaderIncludes"
{
	Global
	AtmosphereCommon
}
"ComputeShader"
{
	// Hillaire 2020 multi-scattering LUT generator (§4.4 Algorithm 1).
	//
	// Single-scattering misses the diffuse ambient skylight that lights
	// up shadowed regions and crucially fills the night-side hemisphere
	// when the sun is near the horizon. The MS LUT approximates the
	// total multi-scattering contribution as an isotropic energy term
	// f_ms / (1 - f_ms), summing the geometric series of bounces under
	// the assumption that subsequent bounces preserve the same spatial
	// distribution as the first bounce.
	//
	// Per-texel: (cosSunZenith, viewHeight)
	//   u (32) = (sunZenithCos + 1) / 2  -- linear remap of cos(zenith)
	//   v (32) = (viewHeight - groundR) / (topR - groundR)
	//
	// For each of 64 sample directions on the unit sphere:
	//   - ray-march outward, sampling TransmittanceLUT at each step for
	//     sun-light arrival
	//   - accumulate L_2 (second-order radiance) and f_ms (the multi-
	//     scattering factor for that direction)
	// Final per-texel output: L_2 / (1 - f_ms) scaled by 1/N

	RWTexture2D<float4> g_multiScatteringLUT : register(u0);
	Texture2D           g_transmittanceLUT   : register(t0);
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

	static const uint2 LUT_SIZE = uint2(32u, 32u);
	static const uint  MS_SAMPLE_DIRS = 64u;
	static const uint  MS_MARCH_STEPS = 20u;

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

	// TransmittanceLutParamsToUv lives in AtmosphereCommon now.
	float3 SampleTransmittance(float viewHeight, float viewZenithCos)
	{
		const float2 uv = TransmittanceLutParamsToUv(viewHeight, viewZenithCos);
		return g_transmittanceLUT.SampleLevel(g_linearSampler, uv, 0).rgb;
	}

	// Fibonacci-sphere sample direction. Yields well-distributed unit
	// vectors across the sphere for indexed sampling - cheaper than
	// blue-noise + table without significant quality loss for 64 samples.
	float3 SphericalDirection(uint i, uint n)
	{
		const float goldenRatio = 1.6180339887498948f;
		const float phi = 2.0f * kATM_PI * frac((float)i / goldenRatio);
		const float cosTheta = 1.0f - 2.0f * ((float)i + 0.5f) / (float)n;
		const float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));
		return float3(cos(phi) * sinTheta, cosTheta, sin(phi) * sinTheta);
	}

	[numthreads(8, 8, 1)]
	void ShaderMain(uint3 dtid : SV_DispatchThreadID)
	{
		if (any(dtid.xy >= LUT_SIZE))
			return;

		const float2 uv = (float2(dtid.xy) + 0.5f) / float2(LUT_SIZE);

		// u = cos(sunZenith) remapped to [-1, 1], v = altitude lerp.
		const float sunCosZenith = uv.x * 2.0f - 1.0f;
		const float viewHeight   = groundRadiusMM + uv.y * (topRadiusMM - groundRadiusMM);

		const float3 origin = float3(0.0f, viewHeight, 0.0f);
		const float  sunSinZenith = sqrt(max(0.0f, 1.0f - sunCosZenith * sunCosZenith));
		const float3 sunDir = float3(sunSinZenith, sunCosZenith, 0.0f);

		// Solar irradiance at top of atmosphere; absorb the scale into the
		// LUT so downstream samplers don't need to know about it. The 1.0
		// constant is the standard normalised "sun radiance" used by the
		// Hillaire paper - the final brightness is dialled in by the C++
		// sun colour multiplier.
		const float3 sunRadiance = 1.0f.xxx;

		// Accumulators across sample directions. luminance2 is the second-
		// order radiance; f_ms is the cumulative multi-scattering factor.
		float3 luminance2 = 0.0f.xxx;
		float3 f_ms       = 0.0f.xxx;

		[loop]
		for (uint i = 0u; i < MS_SAMPLE_DIRS; ++i)
		{
			const float3 dir = SphericalDirection(i, MS_SAMPLE_DIRS);

			// March outward to atmosphere boundary or ground.
			const float tTop    = RaySphereIntersectExit(origin, dir, topRadiusMM);
			const float tGround = RaySphereIntersectEntry(origin, dir, groundRadiusMM);
			const bool  hitsGround = tGround > 0.0f;
			const float tMax    = hitsGround ? tGround : tTop;
			if (tMax <= 0.0f)
				continue;

			float3 transmittance = 1.0f.xxx;
			float3 lumScatter    = 0.0f.xxx;
			float3 lumMultiScat  = 0.0f.xxx;
			const float dt = tMax / (float)MS_MARCH_STEPS;
			float t = 0.5f * dt;
			[loop]
			for (uint s = 0u; s < MS_MARCH_STEPS; ++s)
			{
				const float3 p = origin + dir * t;
				const AtmosphereSamplePoint sp = SampleAtmosphereMedium(p);

				const float3 sampleTransmittance = exp(-sp.extinction * dt);
				const float pAltitudeFromCentre = length(p);
				const float pSunCos = dot(normalize(p), sunDir);
				const float3 sunVisible = SampleTransmittance(pAltitudeFromCentre, pSunCos);

				// First-order: isotropic phase factor 1/(4*PI). The full single-
				// scattering integration would use real phase functions, but for
				// the MS factor we treat all directions equally. Hillaire's paper
				// shows this is a good approximation.
				const float3 scatterRayleigh = sp.rayleighScattering;
				const float  scatterMie      = sp.mieScattering;
				const float  isoPhase        = 1.0f / (4.0f * kATM_PI);
				const float3 inscatter       = (scatterRayleigh + scatterMie.xxx) * isoPhase;

				const float3 S = inscatter * sunRadiance * sunVisible;

				// Integrate L and ms factor across this step under
				// constant-medium-extinction approximation.
				const float3 stepGain = (1.0f.xxx - sampleTransmittance) / max(sp.extinction, 1e-6f.xxx);
				lumScatter   += transmittance * S * stepGain;
				lumMultiScat += transmittance * inscatter * stepGain;

				transmittance *= sampleTransmittance;
				t += dt;
			}

			// Ground bounce contribution if the ray hit the planet.
			if (hitsGround)
			{
				const float3 groundPoint = origin + dir * tMax;
				const float3 groundN     = normalize(groundPoint);
				const float  groundSunCos = max(0.0f, dot(groundN, sunDir));
				const float3 groundT      = SampleTransmittance(groundRadiusMM, groundSunCos);
				// Lambertian albedo - planet ground colour. 0.3 is the
				// standard "average earth" value; we'll expose this via
				// AtmosphereParams later for stylised worlds.
				const float3 groundAlbedo = 0.3f.xxx;
				lumScatter += transmittance * groundT * groundSunCos * groundAlbedo * sunRadiance / kATM_PI;
			}

			luminance2 += lumScatter;
			f_ms       += lumMultiScat;
		}

		// Sample-mean normalisation (1/N * total).
		const float invN = 1.0f / (float)MS_SAMPLE_DIRS;
		luminance2 *= invN;
		f_ms       *= invN;

		// Geometric series of infinite bounces: F = sum_{k>=1} f^k = f/(1-f).
		// luminance2 already covers k=1; multiplying by F/k=1 = 1/(1-f_ms)
		// yields the full multi-scattering contribution.
		const float3 mScat = luminance2 / max(1.0f.xxx - f_ms, 1e-3f.xxx);

		g_multiScatteringLUT[dtid.xy] = float4(mScat, 1.0f);
	}
}
