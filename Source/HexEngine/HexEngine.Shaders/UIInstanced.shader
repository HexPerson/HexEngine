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

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		//float4 colour = shaderTexture.Load(int3(input.texcoord.x * g_screenWidth,  input.texcoord.y * g_screenHeight, 0)) * g_material.diffuseColour;
		float4 colour = shaderTexture.Sample(PointSampler, input.texcoord) * input.colour;// g_material.diffuseColour;

		return colour;
	}
}
