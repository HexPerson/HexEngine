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
	SamplerState PointSampler : register(s2);
	SamplerState LinearSampler : register(s4);

	static const float kInvGamma = 1.0f / 2.2f;

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
		return float4(mapped, colour.a);
	}
}
