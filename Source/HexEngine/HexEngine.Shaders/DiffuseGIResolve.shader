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
}
"VertexShader"
{
	UIPixelInput ShaderMain(UIVertexInput input)
	{
		UIPixelInput output;

		output.position = input.position;
		output.texcoord = input.texcoord;
		output.positionSS = output.position;
		output.colour = input.colour;
		return output;
	}
}
"PixelShader"
{
	Texture2D g_currentGiHalfRes : register(t0);
	Texture2D g_historyGi : register(t1);
	Texture2D g_motionVectors : register(t2);
	Texture2D g_normalDepth : register(t3);

	SamplerState g_pointSampler : register(s2);
	SamplerState g_linearSampler : register(s4);

	cbuffer GIConstants : register(b4)
	{
		float4 g_clipCenterExtent[4];
		float4 g_clipVoxelInfo[4];
		float4 g_giParams0; // x=intensity, y=energyClamp, z=debugMode, w=activeClipmap
		float4 g_giParams1; // x=hysteresis, y=historyReject, z=halfInvW, w=halfInvH
		float4 g_giParams2; // x=screenBounce, y=probeBlend, z=reserved, w=useVoxelAlphaOpacity
		float4 g_giParams3; // xyz=sunDirectionWS, w=sunDirectionality
		float4 g_giParams4; // x=jitterScale, y=clipBlendWidth, z=pixelMotionStart, w=pixelMotionStrength
		float4 g_giParams5; // x=luminanceRejectScale, y=ditherDarkAmp, z=ditherBrightAmp, w=movementPreset
	};

	float3 UpsampleGiBilateral(float2 uv)
	{
		const float2 halfTexel = max(g_giParams1.zw, float2(1.0f / 4096.0f, 1.0f / 4096.0f));
		const float4 centerNormalDepth = g_normalDepth.Sample(g_pointSampler, uv);
		const float3 centerNormal = normalize(centerNormalDepth.xyz + float3(1e-5f, 1e-5f, 1e-5f));
		const float centerDepth = centerNormalDepth.w;

		float3 accum = 0.0f.xxx;
		float accumWeight = 0.0f;

		[unroll]
		for (int y = -2; y <= 2; ++y)
		{
			[unroll]
			for (int x = -2; x <= 2; ++x)
			{
				const float2 offset = float2((float)x, (float)y) * halfTexel;
				const float2 sampleUv = saturate(uv + offset);

				const float3 giSample = g_currentGiHalfRes.Sample(g_linearSampler, sampleUv).rgb;
				const float4 ndSample = g_normalDepth.Sample(g_pointSampler, sampleUv);
				const float3 normalSample = normalize(ndSample.xyz + float3(1e-5f, 1e-5f, 1e-5f));

				const float normalWeight = pow(saturate(dot(centerNormal, normalSample)), 10.0f);
				const float depthWeight = exp(-abs(ndSample.w - centerDepth) * 42.0f);
				const float2 pixelOffset = float2((float)x, (float)y);
				const float spatialWeight = exp(-dot(pixelOffset, pixelOffset) * 0.24f);
				const float weight = max(1e-4f, normalWeight * depthWeight * spatialWeight);

				accum += giSample * weight;
				accumWeight += weight;
			}
		}

		return accum / max(accumWeight, 1e-4f);
	}

	float Hash12(float2 p)
	{
		const float h = dot(p, float2(127.1f, 311.7f));
		return frac(sin(h) * 43758.5453123f);
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float2 uv = input.texcoord;
		const float3 current = UpsampleGiBilateral(uv);
		const float debugMode = g_giParams0.z;

		// Keep temporal resolve active in debug mode 1 (indirect-only) so debugging reflects
		// the stabilized GI path instead of raw noisy half-res data.
		if (debugMode >= 2.0f)
		{
			return float4(min(current, g_giParams0.y.xxx), 1.0f);
		}

		const float2 velocity = g_motionVectors.Sample(g_pointSampler, uv).xy;
		const float2 historyUv = uv + velocity;
		const bool historyUvValid = all(historyUv >= 0.0f.xx) && all(historyUv <= 1.0f.xx);
		const float warmStabilize = saturate((g_giParams1.x - 0.84f) * 8.0f);
		float3 history = historyUvValid ? g_historyGi.Sample(g_linearSampler, historyUv).rgb : 0.0f.xxx;
		const float4 centerNormalDepth = g_normalDepth.Sample(g_pointSampler, uv);
		const float4 historyNormalDepth = g_normalDepth.Sample(g_pointSampler, saturate(historyUv));
		const float3 centerNormal = normalize(centerNormalDepth.xyz + float3(1e-5f, 1e-5f, 1e-5f));
		const float3 historyNormal = normalize(historyNormalDepth.xyz + float3(1e-5f, 1e-5f, 1e-5f));
		const float normalMismatch = 1.0f - saturate(dot(centerNormal, historyNormal));
		const float depthMismatch = saturate(abs(centerNormalDepth.w - historyNormalDepth.w) * 32.0f);
		const float disocclusion = saturate(max((normalMismatch - 0.20f) * 1.25f, (depthMismatch - 0.10f) * 1.35f));

		const float motionLength = length(velocity);
		const float rejectThreshold = max(g_giParams1.y, 1e-5f);
		const float rejectFactor = saturate((motionLength - rejectThreshold) / rejectThreshold);
		const float2 velPixels = abs(velocity) * float2((float)g_screenWidth, (float)g_screenHeight);
		const float pixelMotion = max(velPixels.x, velPixels.y);
		// Be less aggressive while moving camera to avoid GI scintillation.
		const float pixelMotionStart = max(g_giParams4.z, 0.0f);
		const float pixelMotionStrength = max(g_giParams4.w, 0.0f);
		const float pixelMotionReject = saturate((pixelMotion - pixelMotionStart) * pixelMotionStrength * 0.65f);

		const float currentLum = dot(current, float3(0.2126f, 0.7152f, 0.0722f));
		const float historyLum = dot(history, float3(0.2126f, 0.7152f, 0.0722f));
		const float luminanceReject = saturate(abs(historyLum - currentLum) * max(g_giParams5.x, 0.0f));
		const float stabilityPreset = saturate(g_giParams5.w * 0.5f);

		// Clamp history near current to avoid long ghost trails when GI updates rapidly.
		const float rejectT = saturate(max(disocclusion * 0.70f, max(pixelMotionReject, luminanceReject)));
		const float minScale = lerp(0.35f, lerp(0.72f, 0.80f, stabilityPreset), rejectT);
		const float maxScale = lerp(2.85f, lerp(1.45f, 1.20f, stabilityPreset), rejectT);
		const float bias = lerp(0.020f, 0.006f, rejectT);
		const float3 historyMin = current * minScale;
		const float3 historyMax = current * maxScale + bias.xxx;
		history = clamp(history, historyMin, historyMax);

		float historyWeight = lerp(g_giParams1.x, 0.0f, rejectFactor);
		historyWeight *= (1.0f - pixelMotionReject);
		historyWeight *= (1.0f - luminanceReject);
		historyWeight *= (1.0f - disocclusion * 0.55f);
		if (!historyUvValid)
		{
			historyWeight = 0.0f;
		}
		else
		{
			// Keep a small stability floor to avoid visible GI shimmer during camera motion.
			const float stabilityFloor = (1.0f - disocclusion) * 0.38f * g_giParams1.x;
			historyWeight = max(historyWeight, stabilityFloor);
		}

		float3 resolved = lerp(current, history, historyWeight);
		resolved = min(resolved, g_giParams0.y.xxx);

		// During clipmap warm-up we temporarily cap per-frame GI deltas to suppress residual
		// recenter flicker (single-frame dark/bright spikes).
		if (historyUvValid && warmStabilize > 0.0f)
		{
			const float deltaLumScale = 0.08f + currentLum * 0.12f;
			const float3 looseLimit = (0.22f + deltaLumScale).xxx;
			const float3 tightLimit = (0.06f + deltaLumScale * 0.45f).xxx;
			const float3 deltaLimit = lerp(looseLimit, tightLimit, warmStabilize);
			resolved = clamp(resolved, history - deltaLimit, history + deltaLimit);
		}

		// Tiny luminance-aware dither to mask residual quantization bands on flat surfaces.
		const float2 pixelCoord = uv * float2((float)g_screenWidth, (float)g_screenHeight);
		const float dither = Hash12(pixelCoord) - 0.5f;
		const float lum = dot(resolved, float3(0.2126f, 0.7152f, 0.0722f));
		const float ditherAmp = lerp(max(g_giParams5.y, 0.0f), max(g_giParams5.z, 0.0f), saturate(lum * 2.0f)) * 0.55f;
		const float motionDitherScale = (1.0f - saturate(pixelMotionReject * 0.85f)) * (1.0f - warmStabilize * 0.85f);
		resolved = max(0.0f.xxx, resolved + dither.xxx * ditherAmp * motionDitherScale);

		if (debugMode == 1.0f)
		{
			float3 visual = resolved / (1.0f + resolved);
			return float4(visual, 1.0f);
		}
		return float4(resolved, 1.0f);
	}
}
