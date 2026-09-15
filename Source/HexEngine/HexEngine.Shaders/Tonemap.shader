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
	TonemapOperators
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
	Texture2D shaderTexture : register(t0);
	// P4.7: 3D colour LUT (.cube, loaded by ColourLut.cpp). Bound slotless
	// AFTER the source texture, so it lands at t1. Author LUTs against the
	// GAMMA-ENCODED SDR output (what Resolve/Photoshop export by default).
	Texture3D g_colourLut : register(t1);
	// P4.9: blue-noise (LDR_RGBA_0.png) bound slotless at t2. s2 is a
	// POINT-WRAP sampler in-frame, so the 64px tile repeats across screen.
	Texture2D g_blueNoise : register(t2);
	SamplerState PointSampler : register(s2);
	SamplerState LinearSampler : register(s4);

	static const float kInvGamma = 1.0f / 2.2f;

	// P4.9 film grain + output dither, applied in display-encoded space as
	// the LAST step before 8-bit quantisation.
	//  - Grain: one blue-noise sample tiled + frame-scrambled, luminance-
	//    weighted so it lives in the mid-tones and fades toward pure black/
	//    white (where it would just read as sensor noise). g_frame scramble
	//    keeps it animating; TAA does not smear it because it is added after
	//    the resolve, per pixel.
	//  - Dither: triangular-PDF blue noise at 1 LSB kills the gradient
	//    banding an 8-bit UNORM backbuffer shows on dusk skies.
	float3 ApplyGrainAndDither(float3 c, float2 pixel)
	{
		const float grain = g_grainParams.x;
		if (grain > 0.0f)
		{
			const float size = max(g_grainParams.y, 0.25f);
			const float2 fo = float2((g_frame * 113u) & 63u, (g_frame * 71u) & 63u);
			const float2 guv = ((pixel / size) + fo) / 64.0f;
			const float n = g_blueNoise.SampleLevel(PointSampler, guv, 0).r;
			const float luma = dot(c, float3(0.299f, 0.587f, 0.114f));
			const float response = 1.0f - abs(2.0f * luma - 1.0f); // 0 at black/white
			c += (n - 0.5f) * grain * response;
		}

		// Triangular dither (two decorrelated taps) at one 8-bit LSB.
		const float n1 = g_blueNoise.SampleLevel(PointSampler, pixel / 64.0f, 0).r;
		const float n2 = g_blueNoise.SampleLevel(PointSampler, (pixel + float2(37.0f, 17.0f)) / 64.0f, 0).r;
		c += (n1 + n2 - 1.0f) / 255.0f;
		return c;
	}

	// c in [0,1] display-encoded -> LUT texel centres. c spans exactly
	// [0.5/N, 1-0.5/N] after the remap, so the in-frame WRAP sampler can't
	// bleed opposite LUT edges together.
	float3 ApplyColourLut(float3 c)
	{
		const float n = g_lutParams.y;
		if (n < 2.0f || g_lutParams.x <= 0.0f)
			return c;
		const float3 uvw = saturate(c) * ((n - 1.0f) / n) + (0.5f / n);
		const float3 graded = g_colourLut.SampleLevel(LinearSampler, uvw, 0).rgb;
		return lerp(c, graded, saturate(g_lutParams.x));
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		float4 colour = shaderTexture.Sample(PointSampler, input.texcoord);
		float3 mapped = ApplyTonemap(colour.rgb, (int)g_tonemapOperator);
		mapped = pow(mapped, kInvGamma);
		mapped = ApplyColourLut(mapped);
		mapped = ApplyGrainAndDither(mapped, input.position.xy);
		return float4(mapped, colour.a);
	}
}
