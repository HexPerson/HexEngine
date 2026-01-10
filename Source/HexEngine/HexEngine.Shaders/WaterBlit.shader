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
	Texture2D waterTexture : register(t0);	

	SamplerState PointSampler : register(s3);

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		float4 colour = waterTexture.Sample(PointSampler, input.texcoord);

		if(length(colour.rgba) == 0.0f)
			clip(-1);

		return colour;
	}
}