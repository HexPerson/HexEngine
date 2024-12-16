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
		//float4 colour = shaderTexture.Load(int3(input.texcoord.x * g_screenWidth,  input.texcoord.y * g_screenHeight, 0)) * g_material.diffuseColour;
		float4 colour = shaderTexture.Sample(PointSampler, input.texcoord) * input.colour;// g_material.diffuseColour;

		return saturate(colour);
	}
}