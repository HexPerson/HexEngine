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
	Global
	DDGICommon
}
"VertexShader"
{
	UIPixelInput ShaderMain(UIVertexInput input)
	{
		UIPixelInput output;
		output.position = input.position;
		output.texcoord = input.texcoord;
		return output;
	}
}
"PixelShader"
{
	// Edge-aware denoise of the half-resolution SSGI gather (DDGISSGI) before
	// DDGIApply upsamples it. The gather uses 16 rotated taps per pixel, so
	// on its own it fizzes (visibly noisier than the probe term next to it);
	// TAA alone does not hide that. A 5x5 bilateral kernel at a stride of
	// g_ddgiGroundAlbedo.w half-res texels (9x9 -> 18x18 full-res pixels at
	// stride 2), with weights from:
	//   - eye distance (the gather's alpha), so bounce does not smear across
	//     depth edges,
	//   - the G-buffer normal at each tap, so the floor's bounce does not leak
	//     up a wall at the crease (same depth, different surface),
	//   - a Gaussian on the tap offset.

	GBUFFER_RESOURCE(0, 1, 2, 3, 4);
	Texture2D g_ssgiIn : register(t6);
	SamplerState g_pointSampler : register(s2);

	float3 LoadNormal(float2 uv)
	{
		const float3 n = GBUFFER_NORMAL.SampleLevel(g_pointSampler, uv, 0).xyz;
		const float l = dot(n, n);
		return l > 1e-8f ? n * rsqrt(l) : float3(0.0f, 1.0f, 0.0f);
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		uint w, h;
		g_ssgiIn.GetDimensions(w, h);
		const int2 dims = int2((int)w, (int)h);
		const int2 c = clamp(int2(input.texcoord * float2(dims)), int2(0, 0), dims - 1);
		const float4 center = g_ssgiIn.Load(int3(c, 0));
		if (center.a <= 0.0f)
			return center;

		const float2 invDims = 1.0f / float2(dims);
		const float3 nC = LoadNormal((float2(c) + 0.5f) * invDims);
		const int stride = max(1, (int)(g_ddgiGroundAlbedo.w + 0.5f));
		const float tolerance = 0.03f * center.a + 0.05f;

		float3 sum = 0.0f.xxx;
		float wsum = 0.0f;
		[unroll]
		for (int y = -2; y <= 2; ++y)
		{
			[unroll]
			for (int x = -2; x <= 2; ++x)
			{
				const int2 p = clamp(c + int2(x, y) * stride, int2(0, 0), dims - 1);
				const float4 s = g_ssgiIn.Load(int3(p, 0));
				if (s.a <= 0.0f)
					continue;
				const float dz = (s.a - center.a) / tolerance;
				const float wd = exp(-dz * dz);
				const float wn = pow(saturate(dot(LoadNormal((float2(p) + 0.5f) * invDims), nC)), 8.0f);
				const float ws = exp(-(float)(x * x + y * y) * 0.2222f); // sigma 1.5 taps
				const float wt = wd * wn * ws;
				sum += s.rgb * wt;
				wsum += wt;
			}
		}
		return float4(wsum > 1e-5f ? sum / wsum : center.rgb, center.a);
	}
}
