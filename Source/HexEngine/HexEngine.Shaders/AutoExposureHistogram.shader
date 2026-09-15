"ComputeShaderIncludes"
{
	Global
}
"ComputeShader"
{
	// Phase 4 auto-exposure: 256-bin log-luminance histogram, replacing the
	// single mean accumulator (AutoExposureLuminance). A mean meter lets a
	// small blazing sliver (or a black letterbox) drag the whole frame; the
	// CPU side now walks this histogram's CDF and averages only the
	// percentile band it cares about.
	//
	// Same b5 constants block and one-frame-deferred readback contract as
	// the mean shader; the UAV is now 256 uints (bin counts), cleared by the
	// CPU before dispatch.

	Texture2D<float4> g_beauty : register(t0);
	// Sky mask: the gbuffer diffuse target - its .a == -1 marks sky pixels.
	// May be unbound (env captures, old callers): a null SRV reads alpha 0,
	// which classifies every pixel as scene = full weight.
	Texture2D<float4> g_skyMask : register(t1);
	RWStructuredBuffer<uint> g_histogramOut : register(u0);

	cbuffer AutoExposureConstants : register(b5)
	{
		uint2 g_inputSize;       // beauty texture (w, h) in pixels
		uint2 g_sampleStride;    // pixel stride between samples (e.g. (4, 4) gives 1/16 of pixels)
		float g_minLogLuma;      // log-luminance floor (natural log units), e.g. -10
		float g_logLumaRange;    // log-luminance span above floor, e.g. 20
		uint  g_sampleCount;     // total samples this dispatch contributes (unused here)
		uint  g_skyWeightFixed;  // sky-pixel weight, 1/256 fixed point (256 = full)
	};

	groupshared uint gs_bins[256];

	[numthreads(16, 16, 1)]
	void ShaderMain(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex)
	{
		// 256 threads per group, 256 bins: one zero + one merge lane each.
		gs_bins[gi] = 0u;
		GroupMemoryBarrierWithGroupSync();

		const uint2 pixel = dtid.xy * g_sampleStride;
		if (all(pixel < g_inputSize))
		{
			const float3 colour = g_beauty[pixel].rgb;

			// Rec. 709 luminance; clamp so log() stays finite. NATURAL log -
			// the CPU decode shares kMinLogLuma/kLogLumaRange with the old
			// mean path.
			const float lum = max(dot(colour, float3(0.2126f, 0.7152f, 0.0722f)), 1e-5f);
			const float normalised = saturate((log(lum) - g_minLogLuma) / max(g_logLumaRange, 1e-3f));
			const uint bin = (uint)(normalised * 255.0f + 0.5f);

			// Weighted counts (1/256 fixed point). The HDR sky is several
			// times brighter than lit ground and can be half the frame; at
			// full weight it drags the metered mean up and the ground exposes
			// too dark. Sky pixels (gbuffer diffuse .a == -1) count at the
			// weight the CPU passes; the CPU CDF walk derives its total from
			// the bins themselves, so scaled counts change nothing there.
			const bool isSky = g_skyMask[pixel].a < -0.5f;
			const uint weight = isSky ? g_skyWeightFixed : 256u;
			if (weight > 0u)
				InterlockedAdd(gs_bins[bin], weight);
		}
		GroupMemoryBarrierWithGroupSync();

		// Merge this group's bins into the global histogram.
		if (gs_bins[gi] > 0u)
		{
			InterlockedAdd(g_histogramOut[gi], gs_bins[gi]);
		}
	}
}
