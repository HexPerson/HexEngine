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
	SamplerState TextureSampler : register(s0);

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		return shaderTexture.Sample(TextureSampler, input.texcoord) * input.colour;
	}
}