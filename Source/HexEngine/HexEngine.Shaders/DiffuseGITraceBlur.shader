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
		return output;
	}
}
"PixelShader"
{
	// Spatial pre-filter of the half-res GI trace before the temporal
	// resolve. The voxel-field term is already smooth, but the SSGI gather
	// (16 animated taps) carries per-pixel variance that the temporal EMA
	// only partly integrates - the residual grain reported on SSGI. A
	// depth- and normal-aware 5x5 blur at half res removes the variance
	// cheaply while the bilateral weights keep contact creases and
	// silhouettes intact. Runs only when SSGI is on (g_giParams14.w > 0).

	Texture2D g_trace       : register(t0); // half-res GI trace (rgb GI, a occlusion)
	Texture2D g_normalDepth : register(t1); // gbuffer normal RT (.xyz normal, .w view depth)
	SamplerState g_pointSampler : register(s2);

	cbuffer GIConstants : register(b4)
	{
		float4 g_clipCenterExtent[4];
		float4 g_clipPreviousCenterExtent[4];
		float4 g_clipVoxelInfo[4];
		float4 g_giParams0;
		float4 g_giParams1;
		float4 g_giParams2;
		float4 g_giParams3;
		float4 g_giParams4;
		float4 g_giParams5;
		float4 g_giParams6;
		float4 g_giParams7;
		float4 g_giParams8;
		float4 g_giParams9;
		float4 g_giParams10;
		float4 g_giParams11;
		float4 g_giParams12;
		float4 g_giParams13;
		float4 g_giParams14;
	};

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float2 uv = input.texcoord;
		const float4 centre = g_trace.SampleLevel(g_pointSampler, uv, 0);
		const float radiusTexels = g_giParams14.w;
		if (radiusTexels < 0.5f)
			return centre;

		uint w, h;
		g_trace.GetDimensions(w, h);
		const float2 texel = 1.0f / float2(max(w, 1u), max(h, 1u));

		const float4 nd0 = g_normalDepth.SampleLevel(g_pointSampler, uv, 0);
		const float3 n0 = normalize(nd0.xyz + 1e-5f.xxx);
		const float z0 = nd0.w;
		if (z0 <= 0.0f)
			return centre; // sky / no geometry

		// Depth tolerance scales with distance (half-res depth discontinuities
		// are the main thing we must not blur across).
		const float depthTol = max(0.02f, 0.04f * z0);
		const int r = (int)round(min(radiusTexels, 3.0f));
		const float sigma = max(radiusTexels, 1.0f);

		float4 sum = 0.0f.xxxx;
		float wsum = 0.0f;
		[loop]
		for (int y = -r; y <= r; ++y)
		{
			[loop]
			for (int x = -r; x <= r; ++x)
			{
				const float2 suv = saturate(uv + float2(x, y) * texel);
				const float4 nd = g_normalDepth.SampleLevel(g_pointSampler, suv, 0);
				if (nd.w <= 0.0f)
					continue;
				const float3 n = normalize(nd.xyz + 1e-5f.xxx);
				const float wSpatial = exp(-(float)(x * x + y * y) / (2.0f * sigma * sigma));
				const float wNormal = pow(saturate(dot(n0, n)), 16.0f);
				const float wDepth = exp(-abs(nd.w - z0) / depthTol);
				const float wt = wSpatial * wNormal * wDepth;
				sum += g_trace.SampleLevel(g_pointSampler, suv, 0) * wt;
				wsum += wt;
			}
		}
		return wsum > 1e-4f ? sum / wsum : centre;
	}
}
