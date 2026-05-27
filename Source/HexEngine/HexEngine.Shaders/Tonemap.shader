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
	Global
	TonemapOperators
}
"VertexShader"
{
	UIPixelInput ShaderMain(UIVertexInput input)
	{
		UIPixelInput output;

		output.position = input.position;
		output.texcoord = input.texcoord;

		return output;
	}
}
"PixelShader"
{
	Texture2D shaderTexture : register(t0);
	SamplerState PointSampler : register(s2);

	static const float kInvGamma = 1.0f / 2.2f;

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		float4 colour = shaderTexture.Sample(PointSampler, input.texcoord);
		float3 mapped = ApplyTonemap(colour.rgb, (int)g_tonemapOperator);
		mapped = pow(mapped, kInvGamma);
		return float4(mapped, colour.a);
	}
}
