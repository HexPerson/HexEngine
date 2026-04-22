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

	float PCSS_ComputeSearchRadius(float zReceiver, float texelSize)
	{
		float baseRadius = max(g_shadowConfig.penumbraFilterMaxSize, texelSize * 2.0f);
		float depthScale = saturate(zReceiver * 1.2f);
		return max(texelSize * 2.0f, baseRadius * (0.35f + 0.65f * depthScale));
	}

	void PCSS_FindBlockers(
		Texture2D shadowMapTex,
		SamplerState pointSampler,
		float2 uv,
		float zReceiver,
		float rotation,
		int sampleCount,
		float texelSize,
		out float avgBlockerDepth,
		out float blockerCount)
	{
		float searchRadius = PCSS_ComputeSearchRadius(zReceiver, texelSize);
		float blockerDepthBias = max(g_shadowConfig.biasMultiplier * 2.0f, PCSS_EPSILON);
		float blockerSum = 0.0f;
		blockerCount = 0.0f;

		[loop]
		for (int i = 0; i < sampleCount; ++i)
		{
			float2 offset = PCSS_VogelDiskSample(i, sampleCount, rotation) * searchRadius;
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

	float PCSS(Texture2D shadowMapTex, SamplerComparisonState cmpSampler, SamplerState pointSampler, float2 uv, float zReceiver, float2 screenPos, int requestedSamples)
	{
		screenPos = screenPos;
		int sampleCount = max(1, min(requestedSamples, PCSS_MAX_SAMPLES));
		zReceiver = saturate(zReceiver);
		float texelSize = 1.0f / max(g_shadowConfig.shadowMapSize, 1.0f);
		float rotation = 0.73f;

		int blockerSampleCount = min(PCSS_MAX_SAMPLES, max(sampleCount * 2, 16));

		float avgBlockerDepth;
		float blockerCount;
		PCSS_FindBlockers(shadowMapTex, pointSampler, uv, zReceiver, rotation, blockerSampleCount, texelSize, avgBlockerDepth, blockerCount);

		if (blockerCount < 1.0f)
		{
			// Avoid bright "salt" speckles by falling back to hard compare instead of fully lit.
			return shadowMapTex.SampleCmpLevelZero(cmpSampler, uv, zReceiver).r;
		}

		float contactDelta = max(zReceiver - avgBlockerDepth, 0.0f);
		float penumbraSignal = saturate(contactDelta / max(zReceiver, PCSS_EPSILON));
		penumbraSignal *= penumbraSignal;
		penumbraSignal = saturate(penumbraSignal * 0.85f);

		float minFilterRadius = texelSize;
		float maxFilterRadius = max(g_shadowConfig.shadowFilterMaxSize, minFilterRadius);
		float qualityScale = lerp(0.45f, 1.0f, saturate((float)sampleCount / 16.0f));
		maxFilterRadius = max(minFilterRadius, maxFilterRadius * qualityScale);
		float filterRadiusUV = lerp(minFilterRadius, maxFilterRadius, penumbraSignal);
		int filterSampleCount = min(PCSS_MAX_SAMPLES, max(sampleCount * 2, 16));
		return PCSS_Filter(shadowMapTex, cmpSampler, uv, zReceiver, rotation, filterSampleCount, filterRadiusUV);
	}

#endif
}
