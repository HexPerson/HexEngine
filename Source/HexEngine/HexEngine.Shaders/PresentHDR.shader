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

	// Pure passthrough. The camera RT already contains the fully tonemapped
	// scRGB output from TonemapHDR.hcs (which maps post-ACES values into
	// absolute nits via r_hdrPaperWhiteNits / r_hdrPeakNits, then converts
	// to scRGB with the canonical 1.0 = 80 nits scale). Any extra multiply
	// here gets applied ON TOP of the calibrated output and only happens in
	// the launcher (Game3DEnvironment::Run gates this present pass behind
	// !_inEditorMode), so it shows up as a launcher-only over-brightness
	// vs the editor preview. The previous 1.15x "scene scale" was leftover
	// compensation for the old TonemapHDR's broken pre-multiply (the same
	// kind of double-scale bug as the 1.45x kHdrSceneViewScale we already
	// removed from TonemapHDR.shader).
	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		return shaderTexture.Sample(PointSampler, input.texcoord) * input.colour;
	}
}
