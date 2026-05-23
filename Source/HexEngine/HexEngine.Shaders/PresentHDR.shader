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
		output.colour = input.colour;
		return output;
	}
}
"PixelShader"
{
	Texture2D shaderTexture : register(t0);
	SamplerState PointSampler : register(s3);

	// Match UIBasicHDR.hcs exactly. This shader is run by the launcher to
	// copy the camera RT to the HDR backbuffer; the editor displays the
	// same camera RT via FillTexturedQuad which routes through
	// _activeBasicShader = UIBasicHDR on an HDR backbuffer. If the two
	// shaders disagree, the launcher's shipped view diverges from the
	// editor preview the user authors against. Keeping them in lockstep
	// is more important than the HDR-purity argument for a "proper"
	// passthrough - the editor preview defines authored intent.
	//
	// Specifically:
	//   - saturate() clips above 1.0 so all bright pixels clump at the
	//     same peak value (the perceptual "HDR pop" the user sees in the
	//     editor). Real HDR extended-range output would spread bright
	//     values across the headroom and read as flat-by-comparison.
	//   - pow(..., 2.2) gamma-decodes assuming input is sRGB encoded.
	//     The camera RT isn't really sRGB after TonemapHDR but the curve
	//     pushes mid-tones up in a way the user has authored against.
	//   - 1.9x scale brightens to compete with bright HDR UI tints on a
	//     real HDR display.
	float3 SrgbToLinear(float3 colour)
	{
		return pow(saturate(colour), 2.2f);
	}

	static const float kHdrUiScale = 1.9f;

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		float4 texel = shaderTexture.Sample(PointSampler, input.texcoord);
		float3 linearTexture = SrgbToLinear(texel.rgb);
		float3 linearTint = SrgbToLinear(input.colour.rgb);
		return float4(linearTexture * linearTint * kHdrUiScale, texel.a * input.colour.a);
	}
}
