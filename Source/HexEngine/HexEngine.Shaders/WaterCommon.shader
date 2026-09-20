"Global"
{
#ifndef WATER_COMMON_SHADER
#define WATER_COMMON_SHADER

	// Shared by the water tessellation VS/HS/DS (Water.shader). PURE like
	// SnowCommon: no cbuffer or Global.shader references - include files
	// are prepended in REVERSE list order, so this file lands BEFORE
	// Global.shader in the assembled stage and any g_* reference here would
	// be an undeclared identifier. Globals travel in as parameters.
	//
	// Wind-coupled Gerstner ocean (O5). Pure function of (position, time,
	// wind) so it can be evaluated at BOTH g_time and g_timePrev - the
	// displacement delta between the two IS the water's motion vector (the
	// sea tiles themselves are static). Physical deep-water phase speed
	// (c = sqrt(g/k); the legacy x4.2 fast-forward is gone).

	// Water's own copy of the legacy wave table (Global.shader's _WaveA..D
	// are file-scope statics this file cannot see; nothing else consumes
	// them since WaterMask died). dir.xy, steepness, wavelength.
	static const float  kWaveSizeMultiplier = 4.9f;
	static const float4 kWaveA = float4(0.6, 0.12, 0.10, 140);
	static const float4 kWaveB = float4(0.7, -1, 0.051, 125);
	static const float4 kWaveC = float4(0.4564, 0.348, 0.05, 20);
	static const float4 kWaveD = float4(-0.1, 0.12, 0.067, 175);

	// Control point handed from the water VS to the hull/domain stages
	// (O7 tessellation). World-space, UNDISPLACED - the domain shader owns
	// the wave evaluation after subdivision.
	struct WaterCP
	{
		float3 worldPos   : WORLDPOS;
		float3 worldPrev  : WORLDPREV;
		float2 texcoord   : TEXCOORD0;
		uint   instanceID : TEXCOORD1;
	};

	struct WaterHSConst
	{
		float edges[3] : SV_TessFactor;
		float inside   : SV_InsideTessFactor;
	};

	float3 GerstnerWave(
		float4 wave, float3 p, float time, float2 windDir, float windAlign, float ampScale,
		inout float3 tangent, inout float3 binormal, inout float crest
	) {
		float steepness = (wave.z / kWaveSizeMultiplier) * ampScale;
		float wavelength = wave.w / kWaveSizeMultiplier;
		float k = 2 * 3.14159f / wavelength;
		float c = sqrt(9.8 / k);
		float2 d = normalize(lerp(normalize(wave.xy), windDir, windAlign));
		float f = k * (dot(d, p.xz) - c * time);

		float a = steepness / k;

		// Crest proxy for foam (O4): steepness-weighted phase height.
		crest += steepness * sin(f);

		tangent += float3(
			-d.x * d.x * (steepness * sin(f)),
			d.x * (steepness * cos(f)),
			-d.x * d.y * (steepness * sin(f))
			);
		binormal += float3(
			-d.x * d.y * (steepness * sin(f)),
			d.y * (steepness * cos(f)),
			-d.y * d.y * (steepness * sin(f))
			);

		return float3(
			d.x * (a * cos(f)),
			a * sin(f),
			d.y * (a * cos(f))
			);
	}

	float3 EvalOcean(float3 gridPoint, float time, float2 windDir, float windAlign, float ampScale,
		out float3 tangent, out float3 binormal, out float crest01)
	{
		// Flat-plane seed (the sea tiles are flat grids) - keeps the
		// function pure so the t and t-dt evaluations are structurally
		// identical.
		tangent = float3(1.0f, 0.0f, 0.0f);
		binormal = float3(0.0f, 0.0f, 1.0f);
		float crest = 0.0f;

		float3 p = gridPoint;
		p += GerstnerWave(kWaveA, gridPoint, time, windDir, windAlign, ampScale, tangent, binormal, crest);
		p += GerstnerWave(kWaveB, gridPoint, time, windDir, windAlign, ampScale, tangent, binormal, crest);
		p += GerstnerWave(kWaveC, gridPoint, time, windDir, windAlign, ampScale, tangent, binormal, crest);
		p += GerstnerWave(kWaveD, gridPoint, time, windDir, windAlign, ampScale, tangent, binormal, crest);

		tangent = normalize(tangent);
		binormal = normalize(binormal);

		// Normalise the crest sum against the (wind-scaled) theoretical
		// maximum - all four waves peaking in phase.
		const float totalSteepness =
			(kWaveA.z + kWaveB.z + kWaveC.z + kWaveD.z) / kWaveSizeMultiplier * ampScale;
		crest01 = saturate(crest / max(totalSteepness, 0.0001f));

		return p;
	}

	// Wind parameters for the two domain-shader evaluations. PURE: callers
	// pass g_weatherSurface.windDirectionAndSpeed and g_oceanConfig2.x.
	// Scale reaches well ABOVE 1: the authored 4-wave table only sums to
	// ~15 cm of swell, so storms reach ~4x (about a metre combined;
	// per-wave steepness stays under the Gerstner loop-over bound).
	// Wavelengths deliberately do NOT scale and alignment stays moderate -
	// both feed the phase term, and varying them with wind teleports/slides
	// the sea during weather transitions.
	void OceanWindParams(float4 windDirectionAndSpeed, float waveScale,
		out float2 windDir, out float windAlign, out float ampScale)
	{
		const float windSpeed = windDirectionAndSpeed.w;
		const float windNorm = saturate(windSpeed / 30.0f);
		ampScale = (0.18f + 3.8f * pow(windNorm, 1.5f)) * waveScale;
		windAlign = 0.4f * windNorm;
		windDir = windDirectionAndSpeed.xz;
		const float windDirLen = length(windDir);
		windDir = (windDirLen > 0.001f) ? windDir / windDirLen : float2(1.0f, 0.0f);
	}

	// Tessellation segment length at a given camera distance - the SAME curve
	// as Water.shader's WaterTessFactor (~8 m grid triangles / factor).
	// Mirrored on the CPU by OceanWaves::TessSegmentLength.
	float OceanTessSegmentLength(float distanceFromCamera)
	{
		const float tessFactor = max(1.0f, lerp(16.0f, 1.0f, saturate((distanceFromCamera - 15.0f) / 150.0f)));
		return 8.0f / tessFactor;
	}

	// ------------------------------------------------------------------
	// DETAIL SPECTRUM (realism pass). The four-wave table above is swell:
	// 20-35 m wavelengths, which on its own shades as big smooth blobs -
	// the "cartoon sea". A real surface carries energy at EVERY scale down
	// to capillary ripples, and it is the mid/short waves (10 m -> 0.3 m)
	// that give the sea its texture: each wavelet face has its own Fresnel
	// reflectance and its own sun glint.
	//
	// 20 directional waves, wavelengths geometric from 18 m to ~0.35 m,
	// EQUAL STEEPNESS per wave (the saturation range of an ocean spectrum
	// has equal slope variance per octave), deep-water dispersion
	// w = sqrt(g k), peaked-crest profile h = A (2 exp(sin t - 1) - 1)
	// (sharp crests, flat troughs - a sine sum reads as corrugated iron).
	//
	// RULE KEPT: directions and wavelengths are FIXED constants. They feed
	// the phase term, so coupling them to the wind would teleport the sea
	// during weather transitions (see OceanWindParams). Wind scales
	// STEEPNESS only, which is transition-safe.
	//
	// Evaluated PER PIXEL for normals (analytic gradient, exact at any
	// distance, no tiling) and, for the longest few, per domain vertex for
	// real displacement. Both are band-limited against what the consumer
	// can represent - pixel footprint / tessellation segment - the snow
	// lesson: never feed a sampler frequencies it cannot carry.
	static const int   kDetailWaveCount   = 20;
	static const int   kDetailDisplaceCount = 7;     // 18 m .. ~5.2 m
	static const float kDetailLambdaMax   = 18.0f;
	static const float kDetailLambdaRatio = 0.8127f; // -> 0.35 m at i = 19
	static const float kDetailBaseAngle   = 0.197f;  // kWaveA's heading

	// Per-wave steepness (A k) from the wind amplitude scale. Mean-square
	// slope = N s^2 / 2: calm 0.004, fresh breeze ~0.035, storm ~0.08 -
	// the Cox-Munk range.
	float OceanDetailSteepness(float ampScale)
	{
		return 0.045f * sqrt(max(ampScale, 0.0f));
	}

	void OceanDetailWaveParams(int i, out float2 d, out float k, out float omega, out float phase)
	{
		const float fi = (float)i;
		const float lambda = kDetailLambdaMax * pow(kDetailLambdaRatio, fi);
		k = 6.2831853f / lambda;
		omega = sqrt(9.8f * k);
		// Golden-ratio sequence across the fan: long waves hold close to
		// the dominant heading, short chop spreads wide.
		const float t = frac(fi * 0.6180339f + 0.31f) * 2.0f - 1.0f;
		const float spread = lerp(0.45f, 1.15f, fi / (float)(kDetailWaveCount - 1));
		const float ang = kDetailBaseAngle + t * spread;
		d = float2(cos(ang), sin(ang));
		phase = fi * 2.399963f;
	}

	// SHORT-CRESTED waves. A plain sine has an infinitely long, perfectly
	// straight crest; at any one scale only two or three of the 20 waves
	// dominate, and straight crests crossing at fixed angles weave a regular
	// diamond LATTICE across the sea (first capture of this pass - it looked
	// like a mesh artifact and was not). Real wind seas are short-crested:
	// crests are finite, curved, and travel in groups. Two cheap fixes, both
	// shared by the displacement and the per-pixel gradient so they cannot
	// drift apart:
	//  - PHASE WARP: two low-frequency sines (6-11 wavelengths long, mostly
	//    ALONG the crest) bend each crest line; the warp's own gradient is
	//    returned so the analytic normal stays exact.
	//  - GROUP ENVELOPE: a slow amplitude modulation, so energy arrives in
	//    sets instead of one uniform corduroy. (Its gradient is negligible
	//    next to k and is ignored.)
	// Both drift at a fraction of the phase speed, like real wave groups.
	// Also: capillary-scale waves carry far less slope than the gravity
	// range, so steepness rolls off below ~1 m.
	void OceanDetailPhase(int i, float2 xz, float time,
		out float theta, out float2 gradTheta, out float envelope, out float k, out float steepScale)
	{
		float2 d;
		float omega, phase;
		OceanDetailWaveParams(i, d, k, omega, phase);

		const float fi = (float)i;
		const float2 crestDir = float2(-d.y, d.x);
		const float2 q1 = (crestDir * 0.90f + d * 0.25f) * (k / 6.3f);
		const float2 q2 = (crestDir * -0.62f + d * 0.48f) * (k / 10.7f);
		const float drift = time * omega * 0.11f;
		const float a1 = dot(q1, xz) + fi * 1.713f - drift;
		const float a2 = dot(q2, xz) + fi * 4.127f + drift * 0.6f;

		const float warpAmp = 1.35f;
		theta = k * dot(d, xz) - omega * time + phase + warpAmp * (sin(a1) + 0.8f * sin(a2));
		gradTheta = d * k + warpAmp * (q1 * cos(a1) + 0.8f * q2 * cos(a2));

		const float2 q3 = (d * 0.85f + crestDir * 0.35f) * (k / 8.9f);
		envelope = 0.62f + 0.38f * sin(dot(q3, xz) + fi * 2.931f - time * omega * 0.055f);

		// Full slope down to ~1.2 m, ~40% at the 0.35 m end.
		steepScale = lerp(1.0f, 0.40f, smoothstep(12.0f, (float)(kDetailWaveCount - 1), fi));
	}

	// Per-pixel: surface gradient (dh/dx, dh/dz), normalised height and the
	// slope variance that was FILTERED OUT because the wave is smaller than
	// the pixel footprint. The caller turns lostVariance into roughness -
	// an unresolved ripple field is, optically, a rougher surface - which is
	// what makes the far sea a smooth bright band with a broad sun glitter
	// instead of per-pixel sparkle noise.
	void OceanDetailWaves(float2 xz, float time, float steep, float footprint,
		out float2 slope, out float height01, out float lostVariance, out float totalVariance)
	{
		slope = float2(0.0f, 0.0f);
		lostVariance = 0.0f;
		totalVariance = 0.0f;
		float height = 0.0f;
		float heightNorm = 0.0f;

		[loop]
		for (int i = 0; i < kDetailWaveCount; ++i)
		{
			float theta, envelope, k, steepScale;
			float2 gradTheta;
			OceanDetailPhase(i, xz, time, theta, gradTheta, envelope, k, steepScale);

			// Full weight from ~6 px per wavelength, gone by ~2 px.
			const float lambda = 6.2831853f / k;
			const float w = saturate((lambda / max(footprint, 1e-4f) - 2.0f) * 0.25f);

			const float waveSteep = steep * steepScale;
			const float e = exp(sin(theta) - 1.0f);
			const float amp = waveSteep / k * envelope;

			height += w * amp * (2.0f * e - 1.0f);
			heightNorm += waveSteep / k;
			// dh/dx = A * d/dtheta[2 exp(sin t - 1) - 1] * grad(theta)
			slope += gradTheta * (w * amp * 2.0f * e * cos(theta));

			// Envelope mean-square ~0.46 folds into the expected variance.
			const float variance = 0.5f * waveSteep * waveSteep * 0.46f;
			totalVariance += variance;
			lostVariance += variance * (1.0f - w * w);
		}

		height01 = saturate(height / max(heightNorm, 1e-4f) * 1.5f + 0.5f);
	}

	// Domain stage: vertical displacement from the longest detail waves,
	// band-limited against the local tessellation segment length.
	float OceanDetailDisplacement(float2 xz, float time, float steep, float segmentLength)
	{
		float height = 0.0f;
		[loop]
		for (int i = 0; i < kDetailDisplaceCount; ++i)
		{
			float theta, envelope, k, steepScale;
			float2 gradTheta;
			OceanDetailPhase(i, xz, time, theta, gradTheta, envelope, k, steepScale);

			const float lambda = 6.2831853f / k;
			// Needs ~6 segments per wavelength to carry the peaked profile.
			const float w = saturate((lambda / max(segmentLength, 1e-3f) - 3.0f) / 3.0f);

			height += w * (steep * steepScale / k) * envelope * (2.0f * exp(sin(theta) - 1.0f) - 1.0f);
		}
		return height;
	}

	// ------------------------------------------------------------------
	// SURFACE QUERY (underwater S0): height of the rendered sea ABOVE the
	// undisplaced tile plane at world (x, z). Used by anything that must know
	// which side of the surface a point is on - the underwater post pass
	// (per pixel on the near plane), caustic/wet-line depth tests.
	//
	// The swell is Gerstner: grid points move HORIZONTALLY too, so the
	// surface over (x, z) belongs to the grid point q that was displaced ONTO
	// (x, z). Solve q + horiz(q) = xz by fixed-point iteration. The
	// detail displacement is vertical-only and is evaluated at q, exactly
	// where the domain shader evaluated it (its gridPos).
	//
	// CPU MIRROR: HexEngine.Core/Scene/OceanWaveModel.hpp (SurfaceOffset).
	// Keep the two in lockstep - same constants, same iteration count.
	float OceanSurfaceOffset(float2 xz, float time, float2 windDir, float windAlign, float ampScale,
		float distanceFromCamera)
	{
		float2 q = xz;
		float3 disp = float3(0.0f, 0.0f, 0.0f);
		[unroll]
		for (int iter = 0; iter < 9; ++iter)
		{
			float3 t = float3(1.0f, 0.0f, 0.0f);
			float3 b = float3(0.0f, 0.0f, 1.0f);
			float crest = 0.0f;
			const float3 gp = float3(q.x, 0.0f, q.y);
			disp  = GerstnerWave(kWaveA, gp, time, windDir, windAlign, ampScale, t, b, crest);
			disp += GerstnerWave(kWaveB, gp, time, windDir, windAlign, ampScale, t, b, crest);
			disp += GerstnerWave(kWaveC, gp, time, windDir, windAlign, ampScale, t, b, crest);
			disp += GerstnerWave(kWaveD, gp, time, windDir, windAlign, ampScale, t, b, crest);
			// 8 fixed-point rounds + a final evaluation at the converged q
			// (OceanWaves::kInversionIterations on the CPU). The contraction
			// factor is the summed swell steepness - ~0.5 in a full storm -
			// so fewer rounds leave centimetres of error there.
			if (iter < 8)
				q = xz - disp.xz;
		}

		return disp.y + OceanDetailDisplacement(q, time, OceanDetailSteepness(ampScale),
			OceanTessSegmentLength(distanceFromCamera));
	}

#endif
}
