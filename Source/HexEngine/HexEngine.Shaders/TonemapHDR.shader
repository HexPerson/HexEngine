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

		return output;
	}
}
"PixelShader"
{
	Texture2D shaderTexture : register(t0);
	SamplerState PointSampler : register(s2);

	float3 AcesFitted(float3 colour)
	{
		const float a = 2.51f;
		const float b = 0.03f;
		const float c = 2.43f;
		const float d = 0.59f;
		const float e = 0.14f;
		return saturate((colour * (a * colour + b)) / (colour * (c * colour + d) + e));
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

		// Mid-tones: same ACES curve as the SDR path. AcesFitted saturates at
		// ~0.866 for input >= 1.0, so the [0,1] input range maps into
		// [0, 0.866] which we'll then scale into [0, paperWhiteNits].
		const float3 baseRange = AcesFitted(min(colour, 1.0f));

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
