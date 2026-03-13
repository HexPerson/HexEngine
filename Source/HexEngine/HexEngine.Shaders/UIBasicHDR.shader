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
