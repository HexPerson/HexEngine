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

	// AgX (Troy Sobotka; minimal fit after Benjamin Wrensch). The current
	// AAA-reference display transform: hue-preserving desaturation into
	// white at the top end instead of ACES' notorious skew (saturated blues
	// to purple, reds to orange). Pipeline: inset matrix (widens the working
	// gamut so single-channel saturates late) -> log2 encode over
	// [-12.47, 4.03] stops -> 6th-order sigmoid fit -> outset matrix.
	// The sigmoid emits a 2.2-gamma-encoded value, so decode back to linear
	// at the end - this library's contract is linear display-referred out
	// (Tonemap.shader applies the display gamma itself).
	float3 AgxMul3(float3 r0, float3 r1, float3 r2, float3 v)
	{
		return float3(dot(r0, v), dot(r1, v), dot(r2, v));
	}

	float3 Tonemap_AgX(float3 c)
	{
		// Inset/outset rows (transposed from the reference column-major mats).
		c = AgxMul3(
			float3(0.842479062253094f,  0.0784335999999992f, 0.0792237451477643f),
			float3(0.0423282422610123f, 0.878468636469772f,  0.0791661274605434f),
			float3(0.0423756549057051f, 0.0784336f,          0.879142973793104f), c);

		// Log2 encode over AgX's dynamic range, then the sigmoid fit.
		const float minEv = -12.47393f;
		const float maxEv = 4.026069f;
		c = saturate((log2(max(c, 1e-10f)) - minEv) / (maxEv - minEv));

		const float3 x = c;
		const float3 x2 = x * x;
		const float3 x4 = x2 * x2;
		c = 15.5f * x4 * x2
		  - 40.14f * x4 * x
		  + 31.96f * x4
		  - 6.868f * x2 * x
		  + 0.4298f * x2
		  + 0.1191f * x
		  - 0.00232f;

		c = AgxMul3(
			float3( 1.19687900512017f,   -0.0980208811401368f, -0.0990297440797205f),
			float3(-0.0528968517574562f,  1.15190312990417f,   -0.0989611768448433f),
			float3(-0.0529716355144438f, -0.0980434501171241f,  1.15107367264116f), c);

		// The fit is display-encoded (2.2); return linear like every other
		// operator here.
		return pow(saturate(c), 2.2f);
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
		case 6: return Tonemap_AgX(c);
		default: return Tonemap_AcesFitted(c);
		}
	}
}
