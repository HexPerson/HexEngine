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

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		float4 texel = shaderTexture.Sample(PointSampler, input.texcoord);
		return float4(texel.rgb * input.colour.rgb, texel.a * input.colour.a);
	}
}
