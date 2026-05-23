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
		// scRGB linear: 1.0 = the system's "paper white" nit target (Win11
		// default is ~200 nits; users can move it via Windows HDR settings).
		// We want the SDR sub-range [0,1] of the post-tonemap value to land at
		// scRGB 1.0 so a "100% white" surface reads as paper white - that is,
		// the same perceptual brightness as the SDR Tonemap.hcs path produces
		// on an SDR monitor. The previous shader pre-multiplied input by 1.45x
		// "to compensate for SDR gamma," but that's a confusion: SDR's
		// pow(x, 1/2.2) gamma encoding is undone by the display's 2.2 decode,
		// so the SDR pipeline's displayed luminance == ACES(x). The HDR
		// pipeline's displayed luminance is also ACES(x) (in linear scRGB).
		// The 1.45x just made HDR uniformly 45% brighter than SDR, then the
		// log highlight band stacked extra brightness on top - blowing out
		// daytime scenes on real HDR displays.
		colour = max(colour, 0.0f);

		// Mid-tones: same ACES curve as the SDR path, no pre-scale.
		const float3 baseRange = AcesFitted(min(colour, 1.0f));

		// Highlights: log-compress values above 1.0 into the HDR extended
		// range. 0.30 slope keeps even a 16x over-white sun reflection
		// roughly within 1 stop above paper white - bright enough to read
		// as a highlight, not bright enough to blow.
		const float kHdrHighlightScale = 0.30f;
		const float3 hdrHighlights = log2(1.0f + max(colour - 1.0f, 0.0f)) * kHdrHighlightScale;

		return baseRange + hdrHighlights;
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		float4 colour = shaderTexture.Sample(PointSampler, input.texcoord);
		return float4(ApplyHdrDisplayMap(colour.rgb), colour.a);
	}
}
