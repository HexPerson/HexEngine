"InputLayout"
{
	PosTex
}
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
	Utils
}
"VertexShader"
{
	UIPixelInput ShaderMain(UIVertexInput input)
	{
		UIPixelInput output;

		output.position = input.position;
		output.texcoord = input.texcoord;
		output.positionSS = output.position;

		return output;
	}
}
"PixelShader"
{
	Texture2D shaderTexture : register(t0);
	SamplerState PointSampler : register(s3);

	static const float redOffset = 0.0026;
	static const float greenOffset = 0.0012;
	static const float blueOffset = -0.0026;

	static const float2 focalPoint = float2(0.5f, 0.5f);

	float4 ShaderMain(UIPixelInput input) : SV_TARGET
	{
		float4 c0 = shaderTexture.Sample(PointSampler, input.texcoord);

		float2 direction = (input.texcoord.xy - focalPoint);

		float4 colour = c0;

		colour.r = shaderTexture.Sample(PointSampler, input.texcoord + (direction * redOffset * g_chromaticAbberationAmmount)).r;
		colour.g = shaderTexture.Sample(PointSampler, input.texcoord + (direction * greenOffset * g_chromaticAbberationAmmount)).g;
		colour.b = shaderTexture.Sample(PointSampler, input.texcoord + (direction * blueOffset * g_chromaticAbberationAmmount)).b;

		return colour;
	}
}