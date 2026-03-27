"InputLayout"
{
	PosTexColour
}
"VertexShaderIncludes"
{
	UICommon
}
"PixelShaderIncludes"
{
	UICommon
	ShadowUtils
	Atmosphere
	Utils
}
"VertexShader"
{
	UIPixelInput ShaderMain(UIVertexInput input)
	{
		UIPixelInput output;

		output.position = input.position;
		output.texcoord = input.texcoord;
		output.positionSS = output.position;

		return output;
	}
}
"PixelShader"
{
	GBUFFER_RESOURCE(0, 1, 2, 3, 4);
	SHADOWMAPS_RESOURCE(5);

	Texture2D g_noiseTexture : register(t11);

	SamplerState g_textureSampler : register(s0);
	SamplerComparisonState g_cmpSampler : register(s1);
	SamplerState g_pointSampler : register(s2);
	SamplerState g_LinearSampler : register(s4);

	static const float G_SCATTERING = -0.40f;// -0.5f;
	static const float PI = 3.14159f;

	float ComputeScattering(float lightDotView)
	{
		float result = 1.0f - g_atmosphere.volumetricScattering * g_atmosphere.volumetricScattering;
		result /= (4.0f * PI * pow(1.0f + g_atmosphere.volumetricScattering * g_atmosphere.volumetricScattering - (2.0f * g_atmosphere.volumetricScattering) * lightDotView, 1.5f));
		return result;
	}

	int GetCascadeIndexForDistance(float distanceFromCamera)
	{
		if (distanceFromCamera <= g_frustumDepths[0])
			return 0;
		else if (distanceFromCamera <= g_frustumDepths[1])
			return 1;
		else if (distanceFromCamera <= g_frustumDepths[2])
			return 2;

		return 3;
	}

	float GetCascadeStartDepth(int cascadeIndex)
	{
		if (cascadeIndex <= 0)
			return 0.0f;
		if (cascadeIndex == 1)
			return g_frustumDepths[0];
		if (cascadeIndex == 2)
			return g_frustumDepths[1];

		return g_frustumDepths[2];
	}

	float GetCascadeEndDepth(int cascadeIndex)
	{
		if (cascadeIndex <= 0)
			return g_frustumDepths[0];
		if (cascadeIndex == 1)
			return g_frustumDepths[1];
		if (cascadeIndex == 2)
			return g_frustumDepths[2];

		return g_frustumDepths[3];
	}

	float GetVolumetricPresetStepScale()
	{
		if (g_atmosphere.volumetricQuality <= 0)
			return 1.65f; // performance
		if (g_atmosphere.volumetricQuality >= 2)
			return 0.72f; // quality

		return 1.0f; // balanced
	}

	float GetVolumetricPresetJitterScale()
	{
		if (g_atmosphere.volumetricQuality <= 0)
			return 0.65f; // performance
		if (g_atmosphere.volumetricQuality >= 2)
			return 1.25f; // quality

		return 1.0f; // balanced
	}

	float GetVolumetricStepClampMinMul()
	{
		if (g_atmosphere.volumetricQuality <= 0)
			return 0.75f;
		if (g_atmosphere.volumetricQuality >= 2)
			return 0.45f;

		return 0.60f;
	}

	float GetVolumetricStepClampMaxMul()
	{
		if (g_atmosphere.volumetricQuality <= 0)
			return 2.40f;
		if (g_atmosphere.volumetricQuality >= 2)
			return 1.15f;

		return 1.60f;
	}

	float ComputeCascadeAdaptiveStep(int cascadeIndex, float traceLen, int targetSteps)
	{
		float cascadeStart = GetCascadeStartDepth(cascadeIndex);
		float cascadeEnd = GetCascadeEndDepth(cascadeIndex);
		float cascadeLength = max(0.001f, cascadeEnd - cascadeStart);

		float safeTargetSteps = max(8.0f, (float)targetSteps);
		float traceBasedStep = traceLen / safeTargetSteps;
		float cascadeBasedStep = cascadeLength / max(2.0f, safeTargetSteps * 0.25f);

		float autoStep = lerp(traceBasedStep, cascadeBasedStep, 0.45f);
		float minStep = traceBasedStep * GetVolumetricStepClampMinMul();
		float maxStep = traceBasedStep * GetVolumetricStepClampMaxMul();

		return clamp(autoStep, minStep, maxStep);
	}

	float3 CalculateVolumetricLighting(float pixelDepth, float3 pixelPos, float2 screenPos, float2 screenPosUV, float3 noise)
	{
		float3 viewEyePosition = g_eyePos;
		float3 eyeVec = normalize(pixelPos - g_eyePos);
		float3 viewPixelPosition = ScreenToWorldPosition2(pixelDepth, g_frustumDepths[3], eyeVec, g_eyePos);
		float3 startPos = viewEyePosition;
		float3 endPos = viewPixelPosition;

		float3 direction = (endPos - startPos);
		float traceLen = length(direction);
		if (traceLen <= 0.0001f)
			return 0.0f.xxx;

		direction /= traceLen;

		int index = 0;
		int numSteps = max(8, g_atmosphere.volumetricStepsMax);
		const float globalStepScale = max(0.01f, g_atmosphere.volumetricStepIncrement);

		float3 tracePos = startPos;
		float totalTraceLen = 0.0f;
		float marchedDistance = 0.0f;
		float accumFog = 0.0f;
		float phase = ComputeScattering(dot(direction, normalize(g_shadowCasterLightDir.xyz))) * g_atmosphere.volumetricStrength;
		float screenNoise = InterleavedGradientNoise(screenPos);
		float frameNoise = frac((float)(g_frame & 1023u) * 0.61803398875f);
		float jitter = frac(noise.x + screenNoise + frameNoise);
		float initialStep = ComputeCascadeAdaptiveStep(0, traceLen, numSteps) * GetVolumetricPresetStepScale() * globalStepScale;
		tracePos += direction * (initialStep * jitter * GetVolumetricPresetJitterScale());

		[loop]
		for (int i = 0; i < numSteps; i++)
		{
			if (totalTraceLen >= traceLen)
				break;

			if (g_shadowConfig.cascadeOverride != -1)
			{
				index = g_shadowConfig.passIndex;
			}
			else
			{
				index = GetCascadeIndexForDistance(totalTraceLen);
			}

			float stepLength = ComputeCascadeAdaptiveStep(index, traceLen, numSteps) * GetVolumetricPresetStepScale() * globalStepScale;

			if (g_shadowConfig.cascadeOverride == -1 && index < 3)
			{
				float cascadeStart = GetCascadeStartDepth(index);
				float cascadeEnd = GetCascadeEndDepth(index);
				float cascadeLength = max(0.001f, cascadeEnd - cascadeStart);
				float blendWidth = max(stepLength * 2.0f, cascadeLength * 0.15f);
				float distToEnd = cascadeEnd - totalTraceLen;
				float boundaryBlend = saturate(1.0f - (distToEnd / blendWidth));

				if (boundaryBlend > 0.0f)
				{
					float nextStepLength = ComputeCascadeAdaptiveStep(index + 1, traceLen, numSteps) * GetVolumetricPresetStepScale() * globalStepScale;
					stepLength = lerp(stepLength, nextStepLength, boundaryBlend);
				}
			}

			float remainingDistance = traceLen - totalTraceLen;
			stepLength = max(0.0001f, stepLength);
			stepLength = min(stepLength, remainingDistance);

			float4 pixelPosShadow = float4(tracePos, 1.0f);

			pixelPosShadow = mul(pixelPosShadow, g_lightViewProjectionMatrix[index]);

			float depthFromLight = pixelPosShadow.z / pixelPosShadow.w;			

			float2 projectTexCoord;

			projectTexCoord.x = pixelPosShadow.x / pixelPosShadow.w / 2.0f + 0.5f;
			projectTexCoord.y = -pixelPosShadow.y / pixelPosShadow.w / 2.0f + 0.5f;

			// Determine if the projected coordinates are in the 0 to 1 range.  If so then this pixel is in the view of the light.
			if ((saturate(projectTexCoord.x) == projectTexCoord.x) && (saturate(projectTexCoord.y) == projectTexCoord.y))
			{
				float shadowDepth = 1.0f;

				if (index == 0)
				{
					shadowDepth = SHADOWMAPS[0].Sample(g_LinearSampler, projectTexCoord.xy).x;
				}
				else if (index == 1)
				{
					shadowDepth = SHADOWMAPS[1].Sample(g_LinearSampler, projectTexCoord.xy).x;
				}
				else if (index == 2)
				{
					shadowDepth = SHADOWMAPS[2].Sample(g_LinearSampler, projectTexCoord.xy).x;
				}
				else if (index == 3)
				{
					shadowDepth = SHADOWMAPS[3].Sample(g_LinearSampler, projectTexCoord.xy).x;
				}

				float visibility = shadowDepth > depthFromLight ? 1.0f : 0.0f;
				accumFog += visibility * phase * stepLength;
			}

			tracePos += (direction * stepLength);

			totalTraceLen += stepLength;
			marchedDistance += stepLength;
			
			if (totalTraceLen >= traceLen)
				break;
		}

		accumFog /= max(0.0001f, marchedDistance);

		return saturate(getSunColour() * accumFog);
	}

	float4 ShaderMain(UIPixelInput input) : SV_TARGET
	{
		float2 texcoord = input.texcoord;

		float2 screenPos = float2(input.position.x / (float)g_screenWidth * 2, input.position.y / (float)g_screenHeight * 2);

		// Sample the gbuffer
		//
		float4 pixelNormal = GBUFFER_NORMAL.Sample(g_pointSampler, screenPos);
		float4 pixelPosWS = GBUFFER_POSITION.Sample(g_pointSampler, screenPos);

		float2 noiseSamplePos = screenPos * 8.0f;

		//noiseSamplePos += frac(g_time) * 5.0f;

		float3 noise = g_noiseTexture.Sample(g_pointSampler, noiseSamplePos).rgb;

		float3 singleNoiseVal = noise;

		// move from 0-1 to -1 to +1
		singleNoiseVal = (singleNoiseVal * 2.0f) - 1.0f;

		// close the range
		singleNoiseVal /= 16.0f;

		//singleNoiseVal *= 5.0f;

		float3 volumetric = CalculateVolumetricLighting(pixelNormal.w, pixelPosWS.xyz, input.position.xy, screenPos, noise);

		//volumetric.xyz += singleNoiseVal.xyz;

		return saturate(float4(volumetric, 1.0f));
	}
}
