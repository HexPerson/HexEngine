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

#endif
}
