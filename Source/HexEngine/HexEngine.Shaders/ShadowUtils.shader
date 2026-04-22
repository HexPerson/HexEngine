"GlobalIncludes"
{
	MeshCommon
	PCSS
}
"Global"
{
#ifndef SHADOWUTILS_SHADER
#define SHADOWUTILS_SHADER

	struct ShadowInput
	{
		float pixelDepth;
		float4 positionWS;
		float2 positionSS;
		float samples;
	};

	static const int MAX_SHADOW_CASCADES = 6;

	float2 TexOffset(float u, float v)
	{
		return float2(u * 1.0f / g_shadowConfig.shadowMapSize, v * 1.0f / g_shadowConfig.shadowMapSize);
	}

	float InterleavedGradientNoise(float2 position_screen)
	{
		float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
		return frac(magic.z * frac(dot(position_screen, magic.xy)));
	}

	float SampleDepth(SamplerComparisonState cmpSampler, SamplerState pointSampler, Texture2D depthMap, float lightDepthValue, float2 projectTexCoord, float2 screenPos, int numSamples, int cascadeIndex)
	{
#if 1
		if (numSamples > 0)
		{
			if (cascadeIndex == 0)
			{
				float pcssShadow = PCSS(depthMap, cmpSampler, pointSampler, projectTexCoord.xy, lightDepthValue, screenPos, numSamples);

				// Extra stable AA pass for near cascade: helps thin/elongated jagged edges.
				float shadowMapSize = max(g_shadowConfig.shadowMapSize, 1.0f);
				float aaRadiusUV = 1.25f / shadowMapSize;
				float aa = 0.0f;
				aa += depthMap.SampleCmpLevelZero(cmpSampler, projectTexCoord.xy + float2(-aaRadiusUV, -aaRadiusUV), lightDepthValue).r;
				aa += depthMap.SampleCmpLevelZero(cmpSampler, projectTexCoord.xy + float2( aaRadiusUV, -aaRadiusUV), lightDepthValue).r;
				aa += depthMap.SampleCmpLevelZero(cmpSampler, projectTexCoord.xy + float2(-aaRadiusUV,  aaRadiusUV), lightDepthValue).r;
				aa += depthMap.SampleCmpLevelZero(cmpSampler, projectTexCoord.xy + float2( aaRadiusUV,  aaRadiusUV), lightDepthValue).r;
				aa *= 0.25f;

				return lerp(pcssShadow, aa, 0.4f);
			}
			else
			{
				// Deterministic PCF for cascades > 0 to avoid motion shimmer from derivative/noise-driven kernels.
				float shadowMapSize = max(g_shadowConfig.shadowMapSize, 1.0f);
				float baseRadiusTexels = lerp(1.5f, 2.75f, saturate((float)cascadeIndex / 3.0f));
				float radiusUV = baseRadiusTexels / shadowMapSize;
				float rotation = (float)cascadeIndex * 1.0471975512f;
				int sampleCount = max(numSamples, 16);

				float visibility = 0.0f;
				[loop]
				for (int s = 0; s < sampleCount; ++s)
				{
					float2 offset = PCSS_VogelDiskSample(s, sampleCount, rotation) * radiusUV;
					visibility += depthMap.SampleCmpLevelZero(cmpSampler, projectTexCoord.xy + offset, lightDepthValue).r;
				}

				return visibility / (float)sampleCount;
			}
		}
		else
		{
			return depthMap.SampleCmpLevelZero(cmpSampler, projectTexCoord.xy, lightDepthValue).x;
		}
#else
		if (numSamples > 0)
		{
			float sum = 0;
			float x, y;
			int num = 0;
			const float pcfFactor = (float)numSamples;
			for (y = -pcfFactor; y <= pcfFactor; y += 1.0)
			{
				for (x = -pcfFactor; x <= pcfFactor; x += 1.0)
				{
					sum += depthMap.SampleCmpLevelZero(cmpSampler, projectTexCoord.xy + TexOffset(x, y), lightDepthValue).r;
					num = num + 1;
				}
			}

			return sum / (float)num;
		}
		else
			return depthMap.Sample(pointSampler, projectTexCoord.xy).r;
#endif
	}

	float4 CalculateLightViewPosition(int index, float4 positionWS)
	{
		positionWS.w = 1.0f; // make homogenous
		return mul(positionWS, g_lightViewProjectionMatrix[index]);
	}

	float2 GetProjectedTexCoord(float4 lightViewPosition)
	{
		float2 projectTexCoord;

		projectTexCoord.x = lightViewPosition.x / lightViewPosition.w / 2.0f + 0.5f;
		projectTexCoord.y = -lightViewPosition.y / lightViewPosition.w / 2.0f + 0.5f;

		return projectTexCoord;
	}

	float CalculateShadows(ShadowInput input, SamplerComparisonState cmpSampler, SamplerState pointSampler, Texture2D depthMaps[MAX_SHADOW_CASCADES], float bias)
	{
		int index = 0;
		float2 projectTexCoord;
		float depthValue = 0.0f;
		float4 lightViewPosition;
		float shadowDelta = 0.0f;
		float lightDepthValue;
		float cameraDistance = distance(g_eyePos.xyz, input.positionWS.xyz);
		//float bias = 0.000001f;

		if (g_shadowConfig.cascadeOverride != -1)
		{
			index = g_shadowConfig.passIndex;
			shadowDelta = 999999.0f;
		}
		else
		{
			if (cameraDistance <= g_frustumDepths[0])
			{
				index = 0;
				shadowDelta = g_frustumDepths[0] - cameraDistance;
			}
			else if (cameraDistance <= g_frustumDepths[1])
			{
				index = 1;
				shadowDelta = g_frustumDepths[1] - cameraDistance;
			}
			else if (cameraDistance <= g_frustumDepths[2])
			{
				index = 2;
				shadowDelta = g_frustumDepths[2] - cameraDistance;
			}
			else
			{
				index = 3;
				shadowDelta = g_frustumDepths[3] - cameraDistance;
			}
		}

		// sample the depth from the largest cascade first of all
		//float4 largestLightView = CalculateLightViewPosition(3, input.positionWS);
		//float2 largestTexCoord = GetProjectedTexCoord(largestLightView);
		//float largestDepth = SampleDepth(cmpSampler, pointSampler, depthMaps[3], (largestLightView.z / largestLightView.w), largestTexCoord, input.positionSS, input.samples);

		//[loop]
		for (int i = 0; i < MAX_SHADOW_CASCADES; i++)
		{
			if (i >= index)
			{
				lightViewPosition = CalculateLightViewPosition(i, input.positionWS);

				projectTexCoord = GetProjectedTexCoord(lightViewPosition);

				// Determine if the projected coordinates are in the 0 to 1 range.  If so then this pixel is in the view of the light.
				if ((saturate(projectTexCoord.x) == projectTexCoord.x) && (saturate(projectTexCoord.y) == projectTexCoord.y))
				{
					// Calculate the depth of the light.
					lightDepthValue = (lightViewPosition.z / lightViewPosition.w);

					if (lightDepthValue < 1.0f)
					{
						// Subtract the bias from the lightDepthValue.
						lightDepthValue = lightDepthValue - bias;



						depthValue = SampleDepth(cmpSampler, pointSampler, depthMaps[i], lightDepthValue, projectTexCoord, input.positionSS, input.samples, i);



						//lowestDepth = min(depthValue, lowestDepth);

						// sample the next depth and lerp between them
						float cascadeBlendRange = g_shadowConfig.cascadeBlendRange;
						if (i == 0)
						{
							cascadeBlendRange *= 0.35f;
						}

						if (shadowDelta < cascadeBlendRange && i < MAX_SHADOW_CASCADES - 1)
						{
							float4 nextLightViewPosition = CalculateLightViewPosition(i + 1, input.positionWS);

							projectTexCoord.x = nextLightViewPosition.x / nextLightViewPosition.w / 2.0f + 0.5f;
							projectTexCoord.y = -nextLightViewPosition.y / nextLightViewPosition.w / 2.0f + 0.5f;

							if ((saturate(projectTexCoord.x) == projectTexCoord.x) && (saturate(projectTexCoord.y) == projectTexCoord.y))
							{
								float lightDepthValueNext = (nextLightViewPosition.z / nextLightViewPosition.w) - bias;

								float nextCascadeDepth = SampleDepth(cmpSampler, pointSampler, depthMaps[i + 1], lightDepthValueNext, projectTexCoord, input.positionSS, input.samples, i + 1);

								float lerpValue = shadowDelta / max(cascadeBlendRange, 0.0001f);

								depthValue = lerp(depthValue, nextCascadeDepth, 1.0f - lerpValue);
							}

							//break; // prevent further sampling, we're done now

						}


						// if it wasn't occluded at the largest level, no need to check here either
						//if(largestDepth == 1.0f)
						break;
						
					}
					//else break;
				}
			}
		}

		return max(0.00f, saturate(depthValue));
	}

#endif
}
