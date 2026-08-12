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
	// P4.10: AMD FidelityFX CAS (Contrast-Adaptive Sharpening), the sharpen
	// half only (no upscale). Runs right after the TAA resolve on the linear
	// HDR beauty to recover the micro-detail temporal AA softens, WITHOUT the
	// ringing a plain unsharp mask throws around high-contrast edges - the
	// adaptive weight backs off exactly where an unsharp halo would form.
	// Amount from g_grainParams.z (r_sharpen); the C++ side skips the pass at 0.

	Texture2D g_source : register(t0);
	SamplerState g_pointSampler : register(s2);

	// The beauty is LINEAR HDR (unbounded), but CAS's min/max math assumes a
	// [0,1] domain - running it raw would clip every highlight to 1.0 before
	// the tonemapper ever sees it. Compress each tap with per-channel Reinhard
	// into [0,1), sharpen there, expand the single output back. Reversible, so
	// HDR range survives; and CAS adapting in a perceptual domain is arguably
	// more correct anyway.
	float3 Compress(float3 x) { return x / (1.0f + max(x, 0.0f)); }
	float3 Expand(float3 y)   { y = min(y, 0.9999f.xxx); return y / (1.0f - y); }

	// s2 is a POINT-WRAP sampler in-frame; Load by integer pixel instead so
	// there's no wrap bleed at the screen edges and no filtering of the taps.
	float3 Tap(int2 p, int2 maxP)
	{
		return Compress(g_source.Load(int3(clamp(p, int2(0, 0), maxP), 0)).rgb);
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const int2 maxP = int2((int)g_screenWidth - 1, (int)g_screenHeight - 1);
		const int2 p = (int2)(input.texcoord * float2(g_screenWidth, g_screenHeight));

		// 3x3 neighbourhood:  a b c / d e f / g h i
		const float3 a = Tap(p + int2(-1, -1), maxP);
		const float3 b = Tap(p + int2( 0, -1), maxP);
		const float3 c = Tap(p + int2( 1, -1), maxP);
		const float3 d = Tap(p + int2(-1,  0), maxP);
		const float3 e = Tap(p,                maxP);
		const float3 f = Tap(p + int2( 1,  0), maxP);
		const float3 g = Tap(p + int2(-1,  1), maxP);
		const float3 h = Tap(p + int2( 0,  1), maxP);
		const float3 i = Tap(p + int2( 1,  1), maxP);

		// Soft min/max of the cross, reinforced by the diagonals (CAS).
		float3 mnRGB = min(min(min(d, e), min(f, b)), h);
		mnRGB += min(mnRGB, min(min(a, c), min(g, i)));
		float3 mxRGB = max(max(max(d, e), max(f, b)), h);
		mxRGB += max(mxRGB, max(max(a, c), max(g, i)));

		// Adaptive amplitude: how much sharpening this pixel can take before
		// clipping. Low near flat/extreme regions, high on textured mid-tones.
		const float3 rcpMRGB = rcp(max(mxRGB, 1e-4f));
		float3 ampRGB = saturate(min(mnRGB, 2.0f - mxRGB) * rcpMRGB);
		ampRGB = sqrt(ampRGB);

		// Sharpness knob: -1/lerp(8,5,sharpness) is CAS's peak weight range.
		const float sharpness = saturate(g_grainParams.z);
		const float peak = -1.0f / lerp(8.0f, 5.0f, sharpness);
		const float3 wRGB = ampRGB * peak;
		const float3 rcpWeight = rcp(1.0f + 4.0f * wRGB);

		const float3 outRGB = saturate((b * wRGB + d * wRGB + f * wRGB + h * wRGB + e) * rcpWeight);
		return float4(Expand(outRGB), 1.0f);
	}
}
