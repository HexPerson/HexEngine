"ComputeShaderIncludes"
{
	Global
}
"ComputeShader"
{
	Texture2D<float4> g_beauty : register(t0);
	RWStructuredBuffer<uint> g_lumaOut : register(u0);

	// Auto-exposure metering constants. Packed in b5 so we don't collide with the GI/cull
	// constant buffers further up the slot chain.
	cbuffer AutoExposureConstants : register(b5)
	{
		uint2 g_inputSize;       // beauty texture (w, h) in pixels
		uint2 g_sampleStride;    // pixel stride between samples (e.g. (4, 4) gives 1/16 of pixels)
		float g_minLogLuma;      // log-luminance floor (in natural log units), e.g. -10
		float g_logLumaRange;    // log-luminance span above floor, e.g. 20 -> covers exp(-10) .. exp(10)
		uint  g_sampleCount;     // total number of samples this dispatch contributes (group-count * 256)
		uint  _pad;
	};

	// Group reduction buffer. 16x16 = 256 threads per group; each writes its normalised log-luma
	// here, then a parallel reduction sums them down to lane 0 which InterlockedAdd's into the
	// shared UAV. We encode the sum as fixed-point uint (mul by 1024 then round to int) so the
	// InterlockedAdd works on integers. 1024 keeps the global accumulator from overflowing
	// uint32 at any sane render resolution (1024 * 256 per group * ~16k groups = headroom).
	groupshared float gs_logLuma[256];

	[numthreads(16, 16, 1)]
	void ShaderMain(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex)
	{
		const uint2 pixel = dtid.xy * g_sampleStride;
		float3 colour = 0.0f.xxx;
		if (all(pixel < g_inputSize))
		{
			colour = g_beauty[pixel].rgb;
		}

		// Rec. 709 luminance. Clamp tiny values so log() doesn't go to -inf and dominate the mean.
		const float lum = max(dot(colour, float3(0.2126f, 0.7152f, 0.0722f)), 1e-5f);
		const float logLuma = log(lum);

		// Normalise into [0, 1] so the per-group sum can't overflow the uint encoding. Clamp out-
		// of-range samples (e.g. very bright sky pixels) so they don't disproportionately pull
		// the average up.
		const float normalised = saturate((logLuma - g_minLogLuma) / max(g_logLumaRange, 1e-3f));
		gs_logLuma[gi] = normalised;
		GroupMemoryBarrierWithGroupSync();

		// Parallel reduction in groupshared.
		[unroll]
		for (uint stride = 128u; stride > 0u; stride >>= 1)
		{
			if (gi < stride)
				gs_logLuma[gi] += gs_logLuma[gi + stride];
			GroupMemoryBarrierWithGroupSync();
		}

		// Lane 0 commits this group's sum to the global accumulator.
		if (gi == 0u)
		{
			// 1024x fixed-point. Per-group sum is at most 256 (256 threads * 1.0), encoded at
			// most 262144 (18 bits). A 1080p dispatch with stride 4 produces ~510 groups, so the
			// global accumulator tops out around 134M -- nowhere near uint32 wrap (4.29B).
			const uint encoded = (uint)round(gs_logLuma[0] * 1024.0f);
			InterlockedAdd(g_lumaOut[0], encoded);
		}
	}
}
