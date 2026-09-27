#pragma once

// CPU mirror of the ocean surface defined in HexEngine.Shaders/WaterCommon.shader.
//
// The rendered sea is displaced on the GPU (Water.shader domain stage). Anything
// on the CPU that needs to know where the surface IS - the underwater camera
// test, audio muffling, and later swimming/buoyancy - evaluates the same
// function here. The two copies cannot share source (HLSL vs C++), so:
//
//   EVERY CONSTANT AND FORMULA BELOW MUST MATCH WaterCommon.shader.
//   Change one, change the other, in the same commit. Names are kept
//   identical (kWaveA, kDetailLambdaMax, OceanDetailPhase, ...) so a grep
//   finds both. `r_oceanDebugHeight 1` logs the CPU surface height under the
//   camera - park the camera at the waterline to check the two agree.
//
// Header-only and dependency-free (plain floats, <cmath> only) on purpose:
// HexEngine.Tests compiles it directly without linking Core.

#include <cmath>
#include <algorithm>

namespace HexEngine::OceanWaves
{
	/** @brief The exact values the renderer uploaded for the frame being queried. */
	struct WaveInputs
	{
		float windDirX = 1.0f;   // g_weatherSurface.windDirectionAndSpeed.x
		float windDirZ = 0.0f;   // .z
		float windSpeed = 0.0f;  // .w
		float waveScale = 1.0f;  // g_oceanConfig2.x (r_oceanWaveScale)
		float time = 0.0f;       // g_time
	};

	namespace detail
	{
		inline float Saturate(float v) { return std::min(std::max(v, 0.0f), 1.0f); }
		inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
		inline float Frac(float v) { return v - std::floor(v); }
		inline float Smoothstep(float e0, float e1, float x)
		{
			const float t = Saturate((x - e0) / (e1 - e0));
			return t * t * (3.0f - 2.0f * t);
		}

		// --- WaterCommon.shader: swell table (dir.xy, steepness, wavelength) ---
		constexpr float kWaveSizeMultiplier = 4.9f;
		constexpr float kWaves[4][4] = {
			{ 0.6f,    0.12f,  0.10f,  140.0f },
			{ 0.7f,   -1.0f,   0.051f, 125.0f },
			{ 0.4564f, 0.348f, 0.05f,   20.0f },
			{ -0.1f,   0.12f,  0.067f, 175.0f },
		};

		// --- WaterCommon.shader: detail spectrum ---
		constexpr int   kDetailWaveCount = 20;
		constexpr int   kDetailDisplaceCount = 7;
		constexpr float kDetailLambdaMax = 18.0f;
		constexpr float kDetailLambdaRatio = 0.8127f;
		constexpr float kDetailBaseAngle = 0.197f;

		struct WindParams { float dirX, dirZ, align, ampScale; };

		inline WindParams OceanWindParams(const WaveInputs& in)
		{
			WindParams w{};
			const float windNorm = Saturate(in.windSpeed / 30.0f);
			w.ampScale = (0.18f + 3.8f * std::pow(windNorm, 1.5f)) * in.waveScale;
			w.align = 0.4f * windNorm;
			const float len = std::sqrt(in.windDirX * in.windDirX + in.windDirZ * in.windDirZ);
			if (len > 0.001f) { w.dirX = in.windDirX / len; w.dirZ = in.windDirZ / len; }
			else { w.dirX = 1.0f; w.dirZ = 0.0f; }
			return w;
		}

		inline float OceanDetailSteepness(float ampScale)
		{
			return 0.045f * std::sqrt(std::max(ampScale, 0.0f));
		}

		/** @brief One swell wave: horizontal (dx, dz) and vertical (dy) displacement at grid point (x, z). */
		inline void GerstnerWave(const float wave[4], float x, float z, float time, const WindParams& wind,
			float& dx, float& dy, float& dz)
		{
			const float steepness = (wave[2] / kWaveSizeMultiplier) * wind.ampScale;
			const float wavelength = wave[3] / kWaveSizeMultiplier;
			const float k = 2.0f * 3.14159f / wavelength;
			const float c = std::sqrt(9.8f / k);

			// d = normalize(lerp(normalize(wave.xy), windDir, windAlign))
			const float wl = std::sqrt(wave[0] * wave[0] + wave[1] * wave[1]);
			float ddx = Lerp(wave[0] / wl, wind.dirX, wind.align);
			float ddz = Lerp(wave[1] / wl, wind.dirZ, wind.align);
			const float dl = std::sqrt(ddx * ddx + ddz * ddz);
			ddx /= dl; ddz /= dl;

			const float f = k * ((ddx * x + ddz * z) - c * time);
			const float a = steepness / k;

			dx += ddx * (a * std::cos(f));
			dy += a * std::sin(f);
			dz += ddz * (a * std::cos(f));
		}

		inline void EvalSwell(float x, float z, float time, const WindParams& wind, float& dx, float& dy, float& dz)
		{
			dx = dy = dz = 0.0f;
			for (int i = 0; i < 4; ++i)
				GerstnerWave(kWaves[i], x, z, time, wind, dx, dy, dz);
		}

		/** @brief Mirrors OceanDetailWaveParams + OceanDetailPhase (height terms only - no gradient needed on the CPU). */
		inline void OceanDetailPhase(int i, float x, float z, float time,
			float& theta, float& envelope, float& k, float& steepScale)
		{
			const float fi = (float)i;
			const float lambda = kDetailLambdaMax * std::pow(kDetailLambdaRatio, fi);
			k = 6.2831853f / lambda;
			const float omega = std::sqrt(9.8f * k);
			const float t = Frac(fi * 0.6180339f + 0.31f) * 2.0f - 1.0f;
			const float spread = Lerp(0.45f, 1.15f, fi / (float)(kDetailWaveCount - 1));
			const float ang = kDetailBaseAngle + t * spread;
			const float dX = std::cos(ang), dZ = std::sin(ang);
			const float phase = fi * 2.399963f;

			const float cX = -dZ, cZ = dX; // crestDir
			const float q1X = (cX * 0.90f + dX * 0.25f) * (k / 6.3f);
			const float q1Z = (cZ * 0.90f + dZ * 0.25f) * (k / 6.3f);
			const float q2X = (cX * -0.62f + dX * 0.48f) * (k / 10.7f);
			const float q2Z = (cZ * -0.62f + dZ * 0.48f) * (k / 10.7f);
			const float drift = time * omega * 0.11f;
			const float a1 = (q1X * x + q1Z * z) + fi * 1.713f - drift;
			const float a2 = (q2X * x + q2Z * z) + fi * 4.127f + drift * 0.6f;

			const float warpAmp = 1.35f;
			theta = k * (dX * x + dZ * z) - omega * time + phase + warpAmp * (std::sin(a1) + 0.8f * std::sin(a2));

			const float q3X = (dX * 0.85f + cX * 0.35f) * (k / 8.9f);
			const float q3Z = (dZ * 0.85f + cZ * 0.35f) * (k / 8.9f);
			envelope = 0.62f + 0.38f * std::sin((q3X * x + q3Z * z) + fi * 2.931f - time * omega * 0.055f);

			steepScale = Lerp(1.0f, 0.40f, Smoothstep(12.0f, (float)(kDetailWaveCount - 1), fi));
		}

		/** @brief Mirrors OceanDetailDisplacement. segmentLength = local tessellation segment (0.5 m at the camera). */
		inline float OceanDetailDisplacement(float x, float z, float time, float steep, float segmentLength)
		{
			float height = 0.0f;
			for (int i = 0; i < kDetailDisplaceCount; ++i)
			{
				float theta, envelope, k, steepScale;
				OceanDetailPhase(i, x, z, time, theta, envelope, k, steepScale);
				const float lambda = 6.2831853f / k;
				const float w = Saturate((lambda / std::max(segmentLength, 1e-3f) - 3.0f) / 3.0f);
				height += w * (steep * steepScale / k) * envelope * (2.0f * std::exp(std::sin(theta) - 1.0f) - 1.0f);
			}
			return height;
		}
	}

	// Fixed-point rounds for the Gerstner inversion - MUST match the loop in
	// WaterCommon.shader::OceanSurfaceOffset.
	constexpr int kInversionIterations = 8;

	/** @brief Tessellation segment length the domain shader uses at a given camera distance (Water.shader WaterTessFactor). */
	inline float TessSegmentLength(float distanceFromCamera)
	{
		const float factor = std::max(1.0f, detail::Lerp(16.0f, 1.0f, detail::Saturate((distanceFromCamera - 15.0f) / 150.0f)));
		return 8.0f / factor;
	}

	/**
	 * @brief Surface height ABOVE the undisplaced sea plane at world (x, z).
	 *
	 * The swell waves are Gerstner: they move grid points HORIZONTALLY as well as
	 * up, so "the surface above (x, z)" is not the wave evaluated AT (x, z) - it is
	 * the wave evaluated at whichever grid point q got displaced to land on (x, z).
	 * Solve q + horiz(q) = (x, z) by fixed-point iteration. The map's contraction
	 * factor is the SUMMED swell steepness: ~0.27 in ordinary weather (converged
	 * to microns in a few rounds) but ~0.5 in a full storm at a high
	 * r_oceanWaveScale, where four rounds left centimetres of error (caught by
	 * TestOceanWaveModel). kInversionIterations rounds bound the storm case to
	 * millimetres of height. The GPU twin uses the same count.
	 *
	 * @param distanceFromCamera Picks the tessellation band-limit the GPU applied
	 *        there (0 = at the camera = full detail). Matters only for the seven
	 *        displaced detail waves.
	 */
	inline float SurfaceOffset(float x, float z, const WaveInputs& in, float distanceFromCamera = 0.0f)
	{
		const detail::WindParams wind = detail::OceanWindParams(in);

		float qx = x, qz = z;
		float dx = 0.0f, dy = 0.0f, dz = 0.0f;
		for (int iter = 0; iter < kInversionIterations; ++iter)
		{
			detail::EvalSwell(qx, qz, in.time, wind, dx, dy, dz);
			qx = x - dx;
			qz = z - dz;
		}
		detail::EvalSwell(qx, qz, in.time, wind, dx, dy, dz);

		const float steep = detail::OceanDetailSteepness(wind.ampScale);
		return dy + detail::OceanDetailDisplacement(qx, qz, in.time, steep, TessSegmentLength(distanceFromCamera));
	}

	/** @brief Residual of the Gerstner inversion inside SurfaceOffset, in metres (test/diagnostic). */
	inline float InversionResidual(float x, float z, const WaveInputs& in)
	{
		const detail::WindParams wind = detail::OceanWindParams(in);
		float qx = x, qz = z, dx = 0.0f, dy = 0.0f, dz = 0.0f;
		for (int iter = 0; iter < kInversionIterations; ++iter)
		{
			detail::EvalSwell(qx, qz, in.time, wind, dx, dy, dz);
			qx = x - dx;
			qz = z - dz;
		}
		detail::EvalSwell(qx, qz, in.time, wind, dx, dy, dz);
		const float ex = (qx + dx) - x, ez = (qz + dz) - z;
		return std::sqrt(ex * ex + ez * ez);
	}

	/**
	 * @brief Upper bound on |SurfaceOffset| for the current sea state.
	 *
	 * Every wave peaking in phase - never reached, but a safe envelope: nothing
	 * more than this above sea level can be underwater, and the wet band / the
	 * underwater-pass gate are sized from it.
	 */
	inline float AmplitudeBound(const WaveInputs& in)
	{
		const detail::WindParams wind = detail::OceanWindParams(in);
		float bound = 0.0f;
		for (int i = 0; i < 4; ++i)
		{
			const float steepness = (detail::kWaves[i][2] / detail::kWaveSizeMultiplier) * wind.ampScale;
			const float k = 2.0f * 3.14159f / (detail::kWaves[i][3] / detail::kWaveSizeMultiplier);
			bound += steepness / k;
		}
		const float steep = detail::OceanDetailSteepness(wind.ampScale);
		for (int i = 0; i < detail::kDetailDisplaceCount; ++i)
		{
			const float fi = (float)i;
			const float k = 6.2831853f / (detail::kDetailLambdaMax * std::pow(detail::kDetailLambdaRatio, fi));
			const float steepScale = detail::Lerp(1.0f, 0.40f, detail::Smoothstep(12.0f, (float)(detail::kDetailWaveCount - 1), fi));
			bound += steep * steepScale / k; // envelope <= 1, peaked profile <= 1
		}
		return bound;
	}
}
