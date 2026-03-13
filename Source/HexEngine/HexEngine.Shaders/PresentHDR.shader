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

	static const float kHdrSceneScale = 1.15f;

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		float4 colour = shaderTexture.Sample(PointSampler, input.texcoord) * input.colour;
		colour.rgb *= kHdrSceneScale;
		return colour;
	}
}
