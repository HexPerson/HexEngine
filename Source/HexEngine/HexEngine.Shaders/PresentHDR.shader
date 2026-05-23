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

	// True passthrough. The camera RT already contains fully-tonemapped
	// scRGB output from TonemapHDR.hcs (post-ACES values converted to
	// absolute nits via r_hdrPaperWhiteNits / r_hdrPeakNits, then divided
	// by 80 to land in scRGB where 1.0 = 80 nits). The HDR backbuffer
	// expects scRGB linear, so any transformation here (saturate / gamma /
	// scale) would corrupt the calibrated tonemap output.
	//
	// This is the SCENE-RT present shader specifically; UI elements drawn
	// on top of the scene use UIBasicHDR.hcs (also via _activeBasicShader)
	// which DOES apply saturate + gamma + 1.9x scaling, because UI assets
	// are sRGB-encoded textures that need decoding and brightening to
	// composite correctly on an HDR-display backbuffer. The two shaders
	// look similar but have different jobs - don't merge them.
	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		return shaderTexture.Sample(PointSampler, input.texcoord) * input.colour;
	}
}
