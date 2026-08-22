"GlobalIncludes"
{
	Global
}
"Global"
{
#ifndef PCSS_SHADER
#define PCSS_SHADER

	static const int PCSS_MAX_SAMPLES = 64;
	static const float PCSS_PI2 = 6.28318530718f;
	static const float PCSS_EPSILON = 1e-5f;

	float2 PCSS_VogelDiskSample(int sampleIndex, int samplesCount, float phi)
	{
		float goldenAngle = 2.4f;
		float radius = sqrt((float)sampleIndex + 0.5f) / sqrt((float)samplesCount);
		float theta = (float)sampleIndex * goldenAngle + phi;

		float s, c;
		sincos(theta, s, c);
		return float2(c, s) * radius;
	}

	float PCSS_InterleavedGradientNoise(float2 positionScreen)
	{
		float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
		return frac(magic.z * frac(dot(positionScreen, magic.xy)));
	}

	void PCSS_FindBlockers(
		Texture2D shadowMapTex,
		SamplerState pointSampler,
		float2 uv,
		float zReceiver,
		float rotation,
		int sampleCount,
		float searchRadiusUV,
		float blockerDepthBias,
		out float avgBlockerDepth,
		out float blockerCount)
	{
		float blockerSum = 0.0f;
		blockerCount = 0.0f;

		[loop]
		for (int i = 0; i < sampleCount; ++i)
		{
			float2 offset = PCSS_VogelDiskSample(i, sampleCount, rotation) * searchRadiusUV;
			float shadowDepth = shadowMapTex.SampleLevel(pointSampler, uv + offset, 0).r;

			if (shadowDepth < (zReceiver - blockerDepthBias))
			{
				blockerSum += shadowDepth;
				blockerCount += 1.0f;
			}
		}

		avgBlockerDepth = (blockerCount > 0.0f) ? blockerSum / blockerCount : 0.0f;
	}

	float PCSS_Filter(
		Texture2D shadowMapTex,
		SamplerComparisonState cmpSampler,
		float2 uv,
		float zReceiver,
		float rotation,
		int sampleCount,
		float filterRadiusUV)
	{
		float visibility = 0.0f;

		[loop]
		for (int i = 0; i < sampleCount; ++i)
		{
			float2 offset = PCSS_VogelDiskSample(i, sampleCount, rotation) * filterRadiusUV;
			visibility += shadowMapTex.SampleCmpLevelZero(cmpSampler, uv + offset, zReceiver).r;
		}

		return visibility / (float)sampleCount;
	}

	// Percentage-closer soft shadows with a WORLD-SPACE penumbra estimate.
	//
	// The previous version estimated the penumbra as ((zR - zB) / zR)^2 on raw
	// shadow-NDC depths. The sun's ortho depth window spans kilometres (farZ +
	// r_shadowNearClip + the fit sphere), so a metres-scale occluder gap produced
	// a signal of ~1e-6 and the filter radius sat pinned at its 1-texel minimum
	// for every pixel - r_shadowFilterMaxSize could not have any visible effect.
	//
	// Now the per-cascade world metrics come straight from the bound projection
	// matrix (no extra plumbing; diagonal elements are transpose-invariant, so
	// the row/column upload convention doesn't matter):
	//   ortho (sun):     _m00 = 2/orthoWidth  -> metres per full UV = 2/_m00
	//                    _m22 = 1/(zn - zf)   -> metres per depth unit = |1/_m22|
	//   perspective (spot): _m33 == 0, uses the classic relative-depth estimate.
	// The sun penumbra is physical: occluderGap(m) * tan(sunAngle/2), where the
	// angle comes from r_sunAngularDiameter via g_shadowConfig.sunTanHalfAngle.
	// Because the estimate is in metres, the softness is CONSISTENT ACROSS
	// CASCADES - no more softness pop at cascade seams.
	float PCSS(Texture2D shadowMapTex, SamplerComparisonState cmpSampler, SamplerState pointSampler, float2 uv, float zReceiver, float2 screenPos, int requestedSamples, int cascadeIndex)
	{
		int sampleCount = max(1, min(requestedSamples, PCSS_MAX_SAMPLES));
		zReceiver = saturate(zReceiver);
		float texelSize = 1.0f / max(g_shadowConfig.shadowMapSize, 1.0f);
		float rotation = PCSS_InterleavedGradientNoise(screenPos) * PCSS_PI2;

		float4x4 proj = g_lightProjectionMatrix[cascadeIndex];
		bool isOrtho = proj._m33 > 0.5f;

		// Blocker classification bias. For the sun, a fixed 0.25 m in world units
		// (converted through the depth range) so near-contact geometry doesn't
		// classify its own surface as a blocker; NDC-constant bias is meaningless
		// across cascades with kilometre depth windows.
		float blockerDepthBias;
		if (isOrtho)
			blockerDepthBias = max(0.25f * abs(proj._m22), PCSS_EPSILON);
		else
			blockerDepthBias = max(g_shadowConfig.biasMultiplier * 2.0f, PCSS_EPSILON);

		float searchRadiusUV = max(g_shadowConfig.penumbraFilterMaxSize * texelSize, 2.0f * texelSize);

		// The search only feeds an average - it doesn't need the full filter
		// density. Half the taps of the filter pass is plenty.
		int blockerSampleCount = min(32, max(sampleCount, 16));

		float avgBlockerDepth;
		float blockerCount;
		PCSS_FindBlockers(shadowMapTex, pointSampler, uv, zReceiver, rotation,
			blockerSampleCount, searchRadiusUV, blockerDepthBias, avgBlockerDepth, blockerCount);

		if (blockerCount < 1.0f)
		{
			// Fully lit as far as the search can see. Hard compare instead of 1.0
			// to avoid bright salt speckles from undersampled thin blockers.
			return shadowMapTex.SampleCmpLevelZero(cmpSampler, uv, zReceiver).r;
		}

		// 1.25-texel floor doubles as the anti-aliasing filter for hard contact
		// edges (this replaced SampleDepth's old fixed 4-tap box blend).
		float minFilterRadius = 1.25f * texelSize;
		float maxFilterRadius = max(g_shadowConfig.shadowFilterMaxSize * texelSize, minFilterRadius);

		float filterRadiusUV;
		if (isOrtho)
		{
			float metresPerDepth = abs(1.0f / min(proj._m22, -PCSS_EPSILON));
			float gapWorld = max(zReceiver - avgBlockerDepth, 0.0f) * metresPerDepth;
			float penumbraWorld = gapWorld * g_shadowConfig.sunTanHalfAngle;
			float metresPerUV = 2.0f / max(proj._m00, PCSS_EPSILON);
			filterRadiusUV = penumbraWorld / metresPerUV;
		}
		else
		{
			// Perspective (spot) heuristic: relative depth gap scaled by the max
			// radius. NDC depth is nonlinear here but the spot depth range is
			// short (1..lightRadius) so this reads acceptably.
			float contactDelta = max(zReceiver - avgBlockerDepth, 0.0f);
			filterRadiusUV = (contactDelta / max(avgBlockerDepth, PCSS_EPSILON)) * maxFilterRadius;
		}
		filterRadiusUV = clamp(filterRadiusUV, minFilterRadius, maxFilterRadius);

		int filterSampleCount = min(PCSS_MAX_SAMPLES, max(sampleCount * 2, 16));
		// Contact-sharp penumbras don't need the full disk - a big win at 4K
		// where most shadowed pixels are near their occluder.
		if (filterRadiusUV <= 3.0f * texelSize)
			filterSampleCount = min(filterSampleCount, 16);

		return PCSS_Filter(shadowMapTex, cmpSampler, uv, zReceiver, rotation, filterSampleCount, filterRadiusUV);
	}

#endif
}
