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
		output.colour   = input.colour;
		return output;
	}
}
"PixelShader"
{
	// Wide separable bilateral blur of the diffuse-GI AO alpha.
	//
	// The voxel cone trace packs occlusion into _giResolved.a per-probe (each
	// clipmap voxel is ~1-2 metres of world space). At typical view distances
	// that projects to 45-130 pixels on screen, so the 5x5 bilateral in
	// DiffuseGIResolve (~10 pixels at full res) leaves clearly visible
	// square grid artifacts when the AO is used as a screen-space multiplier.
	//
	// This pass runs after the GI resolve, twice (horizontal then vertical
	// driven by g_aoBlurParams.xy) with a wide stride and depth/normal-aware
	// weights. The bilateral degenerates to a near-pure spatial blur on flat
	// surfaces (where the grid is the entire signal) and preserves edges at
	// geometry discontinuities (where AO is supposed to step). Output is a
	// single-channel R8 buffer that DiffuseGIAOProvider samples in place of
	// the raw alpha.
	//
	// Implementation notes:
	//   - 13 taps each axis (radius 6) at stride g_aoBlurParams.z pixels;
	//     covers up to ~144 pixels worth of receptive field per axis on the
	//     wide pass. Separable so total cost is 26 taps + a tiny amount of
	//     ALU per pixel, fits easily inside the GI work the renderer already
	//     pays for.
	//   - Spatial weight is a Gaussian over the tap index (not the pixel
	//     distance) so wider strides remain energy-normalised.
	//   - Normal weight: pow(saturate(dot(centerN, sampleN)), 32) - matches
	//     the upsample shader's edge response.
	//   - Depth weight: exp(-|dz| * scale). Scale picked so a 0.3m view-space
	//     depth gap halves the weight (33% retain), 1m gap kills it (<2%).
	//   - Address-mode clamping handled via saturate() on the UV; texture
	//     sampler doesn't need a special address mode.
	Texture2D    g_sourceAO       : register(t0); // either _giResolved (RGBA, AO in .a) on pass 1, or _giAoBlurredH (R8) on pass 2
	Texture2D    g_normalDepth    : register(t1); // gbuffer normal RT; .xyz = normal, .w = view-space depth
	SamplerState g_pointSampler   : register(s2);

	cbuffer GiAoBlurConstants : register(b4)
	{
		// .xy = blur direction in UV space (one pixel = (1/screenW, 0) or (0, 1/screenH)
		//       scaled by stride). Pass 1 = horizontal, pass 2 = vertical.
		// .z  = source-channel selector. 0 = read .a (raw _giResolved), 1 = read .r (blurred intermediate).
		// .w  = depth weight scale (typical 3.5; lower = wider depth tolerance).
		float4 g_aoBlurParams;
	};

	static const int   KERNEL_RADIUS = 6;
	static const float SPATIAL_SIGMA = 3.0f; // tap-index sigma

	float SampleAO(float2 uv)
	{
		const float4 src = g_sourceAO.SampleLevel(g_pointSampler, saturate(uv), 0);
		return g_aoBlurParams.z > 0.5f ? src.r : src.a;
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float2 uv = input.texcoord;
		const float4 centerND = g_normalDepth.SampleLevel(g_pointSampler, uv, 0);
		const float3 centerN  = normalize(centerND.xyz + float3(1e-5f, 1e-5f, 1e-5f));
		const float  centerZ  = centerND.w;

		// Sky pixels (z=0 in this engine's gbuffer normal RT) shouldn't
		// contribute or receive AO - bail with a neutral 1.0 so the
		// downstream multiplicative apply is a no-op there.
		if (centerZ <= 0.0f)
			return float4(1.0f, 0.0f, 0.0f, 0.0f);

		const float depthScale = max(g_aoBlurParams.w, 0.001f);

		float accum  = 0.0f;
		float accumW = 0.0f;
		[unroll]
		for (int i = -KERNEL_RADIUS; i <= KERNEL_RADIUS; ++i)
		{
			const float2 sampleUv = uv + g_aoBlurParams.xy * (float)i;
			const float4 nd       = g_normalDepth.SampleLevel(g_pointSampler, saturate(sampleUv), 0);
			const float3 sampleN  = normalize(nd.xyz + float3(1e-5f, 1e-5f, 1e-5f));
			const float  sampleZ  = nd.w;

			// Skip sky neighbours entirely - their normal/depth are
			// meaningless and would otherwise drag the AO toward 1.
			if (sampleZ <= 0.0f)
				continue;

			const float ao         = SampleAO(sampleUv);
			const float spatialW   = exp(-((float)(i * i)) / (2.0f * SPATIAL_SIGMA * SPATIAL_SIGMA));
			const float normalW    = pow(saturate(dot(centerN, sampleN)), 32.0f);
			const float depthW     = exp(-abs(sampleZ - centerZ) * depthScale);
			const float w          = max(1e-6f, spatialW * normalW * depthW);

			accum  += ao * w;
			accumW += w;
		}

		const float blurred = accum / max(accumW, 1e-6f);
		return float4(blurred, 0.0f, 0.0f, 1.0f);
	}
}
