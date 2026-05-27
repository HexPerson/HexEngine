"Requirements"
{
}
"GlobalIncludes"
{
}
"Global"
{
	// Tonemap operator library. Pure HLSL functions, no state. Both
	// Tonemap.shader (SDR path) and TonemapHDR.shader (HDR path) include
	// this and dispatch on g_tonemapOperator (per-frame cbuffer field
	// driven by the r_tonemapOperator HVar) to pick which curve to use.
	//
	// Operator IDs (must match Renderer-side enum in SceneRenderer.cpp):
	//   0 = Reinhard           - simple x/(x+1), most basic, soft mids
	//   1 = ReinhardExtended   - Reinhard with explicit white point
	//   2 = ACES Fitted        - Krzysztof Narkowicz fit, default
	//   3 = Uncharted 2 / Hable - John Hable's GDC 2010 curve, filmic
	//   4 = Lottes              - Lottes 2016, configurable midpoint/contrast
	//   5 = Linear              - no tonemap, debug / pass-through
	//
	// All operators accept any non-negative linear-RGB float3 and return
	// values approximately in [0, 1]. The SDR/HDR shaders then apply their
	// own post-curve transforms (gamma encode / nit scaling) on top.

	float3 Tonemap_Reinhard(float3 c)
	{
		return c / (1.0f + c);
	}

	float3 Tonemap_ReinhardExtended(float3 c, float whitePoint)
	{
		const float Lw2 = max(whitePoint * whitePoint, 1e-4f);
		return (c * (1.0f + c / Lw2)) / (1.0f + c);
	}

	float3 Tonemap_AcesFitted(float3 c)
	{
		const float a = 2.51f;
		const float b = 0.03f;
		const float c0 = 2.43f;
		const float d = 0.59f;
		const float e = 0.14f;
		return saturate((c * (a * c + b)) / (c * (c0 * c + d) + e));
	}

	float3 Tonemap_Uncharted2Partial(float3 x)
	{
		const float A = 0.15f;
		const float B = 0.50f;
		const float C = 0.10f;
		const float D = 0.20f;
		const float E = 0.02f;
		const float F = 0.30f;
		return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
	}

	float3 Tonemap_Uncharted2(float3 c)
	{
		const float exposureBias = 2.0f;
		float3 curr = Tonemap_Uncharted2Partial(c * exposureBias);
		float3 whiteScale = 1.0f / Tonemap_Uncharted2Partial(11.2f.xxx);
		return saturate(curr * whiteScale);
	}

	float3 Tonemap_Lottes(float3 c)
	{
		// Lottes 2016 GDC parametric tone mapper. Constants tuned for
		// 18% midpoint -> 26.7% output (typical photography sensitometry),
		// hdrMax = 8.0 = ~3 stops above middle grey.
		const float a = 1.6f;
		const float d = 0.977f;
		const float hdrMax = 8.0f;
		const float midIn = 0.18f;
		const float midOut = 0.267f;
		const float b =
			(-pow(midIn, a) + pow(hdrMax, a) * midOut) /
			((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);
		const float c0 =
			(pow(hdrMax, a * d) * pow(midIn, a) - pow(hdrMax, a) * pow(midIn, a * d) * midOut) /
			((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);
		return saturate(pow(c, a.xxx) / (pow(c, (a * d).xxx) * b + c0));
	}

	float3 Tonemap_Linear(float3 c)
	{
		return saturate(c);
	}

	// Dispatch table. HLSL compiler will dead-strip the branches that aren't
	// selected per-pixel since the operator id is a uniform from the cbuffer.
	float3 ApplyTonemap(float3 c, int op)
	{
		c = max(c, 0.0f);
		switch (op)
		{
		case 0: return Tonemap_Reinhard(c);
		case 1: return Tonemap_ReinhardExtended(c, 11.2f);
		case 2: return Tonemap_AcesFitted(c);
		case 3: return Tonemap_Uncharted2(c);
		case 4: return Tonemap_Lottes(c);
		case 5: return Tonemap_Linear(c);
		default: return Tonemap_AcesFitted(c);
		}
	}
}
