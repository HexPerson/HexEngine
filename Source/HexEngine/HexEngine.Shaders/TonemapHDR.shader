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
	// P4.7: 3D colour LUT, same binding and authoring contract as the SDR
	// path (display-referred, gamma-encoded input). Applied on the
	// tonemapped [0,1] base range BEFORE the nit scaling, so one LUT grades
	// SDR and HDR identically in the mid-range; the HDR highlight extension
	// above paper white is deliberately left ungraded.
	Texture3D g_colourLut : register(t1);
	SamplerState PointSampler : register(s2);
	SamplerState LinearSampler : register(s4);

	float3 ApplyColourLut(float3 c)
	{
		const float n = g_lutParams.y;
		if (n < 2.0f || g_lutParams.x <= 0.0f)
			return c;
		// LUTs are authored display-encoded: encode, sample, decode.
		const float3 encoded = pow(saturate(c), 1.0f / 2.2f);
		const float3 uvw = encoded * ((n - 1.0f) / n) + (0.5f / n);
		const float3 graded = g_colourLut.SampleLevel(LinearSampler, uvw, 0).rgb;
		return lerp(c, pow(graded, 2.2f), saturate(g_lutParams.x));
	}

	float3 ApplyHdrDisplayMap(float3 colour)
	{
		// Output is scRGB linear (1.0 = 80 nits per Windows definition), but
		// what _looks_ like "white" on screen depends on the system paper-
		// white target, which differs by presentation path: DWM composition
		// rescales against the Windows "SDR content brightness" slider while
		// Independent Flip hands the linear values to the display verbatim.
		// To make both paths produce the same brightness we map our tonemap
		// output into absolute nits using the user-configured paper-white
		// and peak-nit targets, then convert nits -> scRGB at the end.
		colour = max(colour, 0.0f);

		// Mid-tones: route through the selected tonemap operator. All
		// operators normalise their output to roughly [0, 1], which we then
		// scale into [0, paperWhiteNits]. For ACES specifically this lands
		// at ~0.866 * paperWhite for fully-saturated SDR-equivalent input;
		// other operators have similar but distinct rolloffs.
		const int op = (int)g_tonemapOperator;
		float3 baseRange = ApplyTonemap(min(colour, 1.0f), op);
		baseRange = ApplyColourLut(baseRange);

		// Highlights: extend input values above 1.0 into the headroom between
		// paper white and display peak. log2(1+x) is gentle (1 stop of input
		// over 1.0 = 1 stop of headroom consumed); divided by 4 it takes
		// ~16x over-white to consume the full headroom, which leaves room
		// for very bright sources without clipping the display.
		const float headroomNits = max(g_hdrPeakNits - g_hdrPaperWhiteNits, 0.0f);
		const float3 highlightLog = log2(1.0f + max(colour - 1.0f, 0.0f));
		const float3 highlightNits = saturate(highlightLog * 0.25f) * headroomNits;

		// Compose absolute nits, then convert to scRGB (1.0 = 80 nits).
		const float3 totalNits = baseRange * g_hdrPaperWhiteNits + highlightNits;
		return totalNits / 80.0f;
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		float4 colour = shaderTexture.Sample(PointSampler, input.texcoord);
		return float4(ApplyHdrDisplayMap(colour.rgb), colour.a);
	}
}
