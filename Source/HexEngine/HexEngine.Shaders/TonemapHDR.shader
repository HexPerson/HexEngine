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

		return output;
	}
}
"PixelShader"
{
	Texture2D shaderTexture : register(t0);
	SamplerState PointSampler : register(s2);

	float3 AcesFitted(float3 colour)
	{
		const float a = 2.51f;
		const float b = 0.03f;
		const float c = 2.43f;
		const float d = 0.59f;
		const float e = 0.14f;
		return saturate((colour * (a * colour + b)) / (colour * (c * colour + d) + e));
	}

	float3 ApplyHdrDisplayMap(float3 colour)
	{
		colour = max(colour, 0.0f);

		const float kHdrSceneViewScale = 1.45f;
		const float kHdrHighlightScale = 0.55f;
		const float3 linearScene = colour * kHdrSceneViewScale;
		const float3 baseRange = AcesFitted(min(linearScene, 1.0f));
		const float3 hdrHighlights = log2(1.0f + max(linearScene - 1.0f, 0.0f)) * kHdrHighlightScale;

		return baseRange + hdrHighlights;
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		float4 colour = shaderTexture.Sample(PointSampler, input.texcoord);
		return float4(ApplyHdrDisplayMap(colour.rgb), colour.a);
	}
}
