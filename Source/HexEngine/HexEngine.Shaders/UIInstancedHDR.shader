"InputLayout"
{
	UI_INSTANCED
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
	UIPixelInput ShaderMain(FontVertexInput input, UIInstance instance, uint vertexID : SV_VertexID)
	{
		UIPixelInput output;

		uint vxid = vertexID % 4;
		output.position = mul(input.position, instance.rotation);
		output.position = float4(instance.center + output.position * instance.scale, 0.0f, 1.0f);

		if (vxid == 0)
		{
			output.colour = instance.colourb;
			output.texcoord = float2(instance.texcoord0.x, instance.texcoord1.y);
		}
		else if (vxid == 1)
		{
			output.colour = instance.colourt;
			output.texcoord = float2(instance.texcoord0.x, instance.texcoord0.y);
		}
		else if (vxid == 2)
		{
			output.colour = instance.colourt;
			output.texcoord = float2(instance.texcoord1.x, instance.texcoord0.y);
		}
		else if (vxid == 3)
		{
			output.colour = instance.colourb;
			output.texcoord = float2(instance.texcoord1.x, instance.texcoord1.y);
		}

		return output;
	}
}
"PixelShader"
{
	Texture2D shaderTexture : register(t0);
	SamplerState PointSampler : register(s2);

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
