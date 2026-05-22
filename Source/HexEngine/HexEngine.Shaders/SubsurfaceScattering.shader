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
		output.colour = input.colour;
		return output;
	}
}
"PixelShader"
{
	// Screen-space separable subsurface scattering (Jorge Jimenez 2015).
	//
	// Run this shader twice per frame as a post-process: once horizontally then once
	// vertically. The result is a depth-aware Gaussian blur of the lit beauty RT,
	// applied only to pixels the gbuffer marked as SSS-shaded (MATERIAL_MODEL_SSS in
	// the features RT). Per-channel falloff biases red further than green/blue so the
	// scattering tints toward warm under thin areas (skin, leaves, wax) — the
	// signature look of subsurface materials.
	//
	// Inputs:
	//   t0 = beauty (lit) — what we're blurring
	//   t1 = features gbuffer — .r = model id (we only act when MATERIAL_MODEL_SSS)
	//                           .g = SSS mask, .ba = scatter colour packed
	//   t2 = normal/depth gbuffer — .w = view-space depth, used for depth-aware
	//                               weighting so the blur doesn't bleed across
	//                               silhouettes onto background pixels.
	//
	// Cbuffer:
	//   b6 (post pass cbuffer) — .x = pass direction (0=horizontal, 1=vertical),
	//                            .y = world-space blur radius (m), .zw = unused
	Texture2D g_beauty   : register(t0);
	Texture2D g_features : register(t1);
	Texture2D g_normalDepth : register(t2);
	SamplerState g_linearSampler : register(s0);

	cbuffer SssParams : register(b6)
	{
		float4 g_sssParams; // x=direction (0=h,1=v), y=radiusMetres, z=intensity, w=unused
	};

	float ModelIdChannelToModel(float r)
	{
		// Features .r packs (modelId << 5) | modelParams.w_quant (see
		// DefaultPixel). Upper 3 bits = id; recover by scaling to byte space and
		// taking the high nibble.
		const uint byteVal = (uint)floor(r * 255.0f + 0.5f);
		return (float)(byteVal >> 5);
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float2 uv = input.texcoord;

		const float4 centreBeauty = g_beauty.Sample(g_linearSampler, uv);
		const float4 features = g_features.Sample(g_linearSampler, uv);

		// Bail early for non-SSS pixels — most of the scene goes here, so the cost is
		// dominated by the single sample above plus this branch.
		const float modelId = ModelIdChannelToModel(features.r);
		const float mask = features.g;
		if (modelId < 0.5f || mask <= 0.001f)
		{
			return centreBeauty;
		}

		// Sample centre depth — used as the reference for depth-aware blur weighting
		// so the kernel doesn't bleed onto background pixels at silhouette edges.
		const float centreDepth = g_normalDepth.Sample(g_linearSampler, uv).w;
		if (centreDepth <= 0.0f)
		{
			return centreBeauty;
		}

		// World-space blur radius -> screen-space pixel radius. Closer pixels get a
		// larger pixel radius; farther pixels get a smaller one. Without this, a fixed
		// pixel-radius blur would over-soften distant heads and barely affect close-ups.
		// 1 metre at depth d (in world units) projects to roughly (worldRadius / d) *
		// (screenHeight / (2 * tan(fov/2))). The viewport scale is rolled into the
		// projection matrix, so we approximate with a simple inverse-depth factor that
		// holds well enough for typical fov ranges (60-90 deg).
		const float worldRadius = g_sssParams.y * mask;
		const float screenHeight = (float)g_screenHeight;
		const float pixelRadius = (worldRadius / max(centreDepth, 0.001f)) * (screenHeight * 0.5f);
		if (pixelRadius < 0.5f)
		{
			return centreBeauty; // sub-pixel blur, skip
		}

		// Step direction. b6.x picks horizontal vs vertical for the two-pass
		// separable blur.
		const float2 stepDir = g_sssParams.x < 0.5f
			? float2(1.0f / (float)g_screenWidth, 0.0f)
			: float2(0.0f, 1.0f / screenHeight);

		// Jimenez SSS kernel - 11 taps, separable, per-channel falloff (red scatters
		// further than green/blue so skin tints warm under thin areas like ears /
		// eyelids). These weights are precomputed for a normalised gaussian profile
		// integrated over the diffusion distance for typical human skin. Per-channel
		// scaling at the bottom of this block applies the scatter colour tint from
		// the gbuffer's features.ba so non-skin SSS materials (foliage, wax) can
		// shift the colour without changing the kernel.
		static const float kKernelOffsets[11] = {
			-3.0f, -2.4f, -1.7f, -1.1f, -0.5f, 0.0f, 0.5f, 1.1f, 1.7f, 2.4f, 3.0f
		};
		static const float3 kKernelWeights[11] = {
			float3(0.0064f, 0.0033f, 0.0019f),
			float3(0.0182f, 0.0125f, 0.0094f),
			float3(0.0492f, 0.0353f, 0.0228f),
			float3(0.1029f, 0.0851f, 0.0598f),
			float3(0.1781f, 0.1654f, 0.1372f),
			float3(0.2434f, 0.2487f, 0.2421f),
			float3(0.1781f, 0.1654f, 0.1372f),
			float3(0.1029f, 0.0851f, 0.0598f),
			float3(0.0492f, 0.0353f, 0.0228f),
			float3(0.0182f, 0.0125f, 0.0094f),
			float3(0.0064f, 0.0033f, 0.0019f),
		};

		// Scatter colour from gbuffer (decoded - features.ba is .ba of (1, mask, color.r, color.g)
		// so we need to recover RGB). We stored modelParams.x=mask, .y=color.r, .z=color.g.
		// color.b is implicit (use 1 - color.r * 0.5 - color.g * 0.5 with a floor), or
		// just default to a warm blue-shifted blue for skin-ish look when unset.
		float3 scatterColor = float3(features.b, features.a, 1.0f - 0.5f * (features.b + features.a));
		// If both channels are ~0, fall back to a classic warm skin tint so users who
		// don't explicitly author a colour still get useful output.
		const float colourMagnitude = features.b + features.a;
		if (colourMagnitude < 0.02f)
		{
			scatterColor = float3(1.0f, 0.45f, 0.30f);
		}
		scatterColor = saturate(scatterColor);

		float3 accum = float3(0.0f, 0.0f, 0.0f);
		float3 weightSum = float3(0.0f, 0.0f, 0.0f);

		[unroll]
		for (int i = 0; i < 11; ++i)
		{
			const float2 sampleUV = uv + stepDir * (kKernelOffsets[i] * pixelRadius);
			const float4 sampleBeauty = g_beauty.SampleLevel(g_linearSampler, sampleUV, 0);
			const float sampleDepth = g_normalDepth.SampleLevel(g_linearSampler, sampleUV, 0).w;

			// Depth-aware weight: if the sample's depth differs by more than the world
			// blur radius, it's a different surface (different head, background, etc.)
			// and we shouldn't bleed colour from it. Smooth falloff to avoid hard edges.
			const float depthDelta = abs(sampleDepth - centreDepth);
			const float depthWeight = saturate(1.0f - depthDelta / max(worldRadius, 0.001f));

			const float3 w = kKernelWeights[i] * depthWeight;
			accum += sampleBeauty.rgb * w * scatterColor;
			weightSum += w * scatterColor;
		}

		// Normalise so the total energy of the blurred result matches the centre.
		// Without normalisation, the depth-weighting can darken the SSS pixels.
		float3 blurred = accum / max(weightSum, 0.0001f);

		// Lerp between original beauty and the blurred result based on the SSS mask
		// and the global intensity slider. Mask=0 -> original; mask=1 -> fully blurred.
		const float intensity = saturate(g_sssParams.z) * mask;
		const float3 finalRgb = lerp(centreBeauty.rgb, blurred, intensity);
		return float4(finalRgb, centreBeauty.a);
	}
}
