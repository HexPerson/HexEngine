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
		if (numSamples > 0)
		{
			// PCSS derives its penumbra in world units from this cascade's
			// projection matrix. The old fixed 4-tap box blend that lived here is
			// gone - PCSS's 1.25-texel minimum filter radius provides the edge AA.
			return PCSS(depthMap, cmpSampler, pointSampler, projectTexCoord.xy, lightDepthValue, screenPos, numSamples, cascadeIndex);
		}
		else
		{
			return depthMap.SampleCmpLevelZero(cmpSampler, projectTexCoord.xy, lightDepthValue).x;
		}
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

	// Cheap 4-tap PCF sun shadow. Same cascade selection as CalculateShadows
	// but a fixed four comparison taps instead of the full PCSS at
	// g_shadowConfig.samples - for surfaces that need "is this in shadow"
	// without contact hardening. Motivating case: the transparency phase's
	// glass, where full PCSS cost ~4 ms on window-heavy views (measured
	// 2026-07-30). No cascade blending - a subtle seam on glass is
	// invisible behind the reflection layer.
	float CalculateShadowsCheapPCF(float3 positionWS, SamplerComparisonState cmpSampler, Texture2D depthMaps[MAX_SHADOW_CASCADES], float bias)
	{
		const float cameraDistance = distance(g_eyePos.xyz, positionWS);
		int index = 3;
		if (cameraDistance <= g_frustumDepths[0])      index = 0;
		else if (cameraDistance <= g_frustumDepths[1]) index = 1;
		else if (cameraDistance <= g_frustumDepths[2]) index = 2;

		const float4 lightViewPosition = CalculateLightViewPosition(index, float4(positionWS, 1.0f));
		const float2 uv = GetProjectedTexCoord(lightViewPosition);
		if (saturate(uv.x) != uv.x || saturate(uv.y) != uv.y)
			return 1.0f;

		const float lightDepth = (lightViewPosition.z / lightViewPosition.w) - bias;
		if (lightDepth >= 1.0f)
			return 1.0f;

		const float texel = 1.0f / max(g_shadowConfig.shadowMapSize, 1.0f);
		float sum = 0.0f;

		// depthMaps[] wants a literal index in SM5 - same unrolled-loop trick
		// CalculateShadows uses.
		[unroll]
		for (int i = 0; i < 4; ++i)
		{
			if (i == index)
			{
				sum += depthMaps[i].SampleCmpLevelZero(cmpSampler, uv + float2(-0.5f, -0.5f) * texel, lightDepth);
				sum += depthMaps[i].SampleCmpLevelZero(cmpSampler, uv + float2( 0.5f, -0.5f) * texel, lightDepth);
				sum += depthMaps[i].SampleCmpLevelZero(cmpSampler, uv + float2(-0.5f,  0.5f) * texel, lightDepth);
				sum += depthMaps[i].SampleCmpLevelZero(cmpSampler, uv + float2( 0.5f,  0.5f) * texel, lightDepth);
			}
		}

		return sum * 0.25f;
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

		// Live cascade count, not the array capacity. Falls back to 4 if a caller left it
		// unset, which is the count the cascade selection above hardcodes via
		// g_frustumDepths[0..3].
		const int liveCascades = (g_shadowConfig.cascadeCount > 0)
			? min(g_shadowConfig.cascadeCount, MAX_SHADOW_CASCADES)
			: 4;

		//[loop]
		for (int i = 0; i < liveCascades; i++)
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

						// Bound by the LIVE cascade count. Using MAX_SHADOW_CASCADES - 1 here
						// meant the last real cascade (i == 3 with the default 4) blended
						// against depthMaps[4], which is never bound - it samples as 0, i.e.
						// fully occluded, darkening a band at the far edge of the shadow range.
						if (shadowDelta < cascadeBlendRange && i < liveCascades - 1)
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

	// Screen-space contact shadows. PCSS cascades blur out near-camera detail (the
	// closest cascade still covers many world metres and its resolution can't resolve
	// e.g. a finger casting a shadow on a palm, or hair-on-shoulder contact). This
	// fills the gap by raymarching the depth buffer from each shaded pixel toward
	// the sun: if the ray hits a pixel that's closer to the camera than the ray's
	// own depth (and within a thickness window so we don't see-through walls), the
	// pixel is in contact shadow. Returns 1 = lit, 0 = contact-shadowed; multiply
	// into the cascade shadow term.
	//
	// Cost: numSteps depth samples per fullscreen pixel. 12-16 steps is plenty.
	// Worst-case cliff failure mode is "ray exits the screen / hits sky" - we
	// early-out cleanly so off-screen geometry simply doesn't contact-shadow.
	float ScreenSpaceContactShadow(
		float3 positionWS,        // world position of the shaded pixel
		float3 lightDirectionWS,  // direction TOWARDS the sun (i.e. -g_lightDirection)
		float3 normalWS,          // surface normal (used for self-bias)
		Texture2D viewDepthSource,// gbuffer normal/depth texture (.w = view-space depth)
		SamplerState pointSampler,
		float2 screenPos,         // pixel screen coords (for jitter)
		int numSteps,
		float maxWorldLength,
		float thicknessThreshold)
	{
		// Self-bias along the surface normal so the ray's first step doesn't
		// immediately self-intersect. 0.05m matches the ~minDistSqr clamp used
		// by the punctual light shaders for the same reason.
		// Grazing light skims the surface, and on slopes the march then reads
		// the surface itself as a blocker (the mid-depth terrain flicker that
		// kept this feature off). Scale the bias up as N.L falls.
		const float grazing = 1.0f - saturate(dot(normalWS, lightDirectionWS));
		const float3 biasedOrigin = positionWS + normalWS * (0.05f + 0.20f * grazing);

		// Per-pixel jitter breaks the banding that comes from every pixel
		// stepping to the same set of distances. It is ANIMATED per frame
		// (golden-ratio rotation of the gradient noise): a frame-static pattern
		// is a constant TAA cannot average, and it shimmers against moving
		// geometry; an animated one integrates into a smooth result.
		const float jitter = frac(InterleavedGradientNoise(screenPos) + (float)(g_frame % 8u) * 0.618034f);

		const float stepLen = maxWorldLength / max((float)numSteps, 1.0f);
		const float3 stepWS = lightDirectionWS * stepLen;

		// Start the march at jitter*stepLen instead of 0 so neighboring pixels
		// sample different distances - the resulting noise dithers out under TAA.
		float3 samplePos = biasedOrigin + stepWS * jitter;

		float occlusion = 0.0f;
		[loop]
		for (int i = 1; i <= numSteps; ++i)
		{
			samplePos += stepWS;

			// Project to clip space, then screen UV.
			float4 clip = mul(float4(samplePos, 1.0f), g_viewProjectionMatrix);
			if (clip.w <= 0.0f)
				break; // behind camera

			float2 ndc = clip.xy / clip.w;
			float2 uv = float2(ndc.x, -ndc.y) * 0.5f + 0.5f;

			// Off-screen samples can't be tested; if the ray leaves the frustum,
			// just stop - we deliberately don't carry "uncertain" occlusion past
			// the screen edge because that produces dark halos at borders.
			if (any(uv < 0.0f) || any(uv > 1.0f))
				break;

			// Sample the scene's view-space depth at the projected UV.
			const float sceneViewDepth = viewDepthSource.SampleLevel(pointSampler, uv, 0).w;
			if (sceneViewDepth <= 0.0f)
				continue; // sky or invalid sample

			// Our sample's view-space depth (positive = distance into the scene).
			float4 viewSample = mul(float4(samplePos, 1.0f), g_viewMatrix);
			const float rayViewDepth = -viewSample.z;

			// Ray went BEHIND visible geometry along the projected pixel - blocker.
			// The thicknessThreshold gate ensures we don't shadow through a thin
			// surface (e.g. a wall whose far side is way past the ray); without
			// this the contact-shadow term darkens distant geometry seen through
			// any near-camera object.
			// Lower bound scales with depth: at mid range a half-pixel of
			// reconstruction error is centimetres, and accepting it as a
			// blocker is the other source of the slope flicker.
			const float blockerDepth = rayViewDepth - sceneViewDepth;
			const float minBlocker = max(0.015f, 0.004f * sceneViewDepth);
			if (blockerDepth > minBlocker && blockerDepth < thicknessThreshold)
			{
				// Fade the last few steps so the contact shadow edge is soft
				// rather than a hard step (otherwise the dither pattern shows).
				const float falloff = saturate(1.0f - (float)i / (float)numSteps);
				occlusion = max(occlusion, falloff);
				break;
			}
		}

		return 1.0f - occlusion;
	}

#endif
}
