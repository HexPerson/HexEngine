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
	SamplerState TextureSampler : register(s0);
	SamplerState PointSampler : register(s2);

    static const float weight[5] = { 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 };

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		float4 colour = shaderTexture.Sample(PointSampler, input.texcoord) * g_material.diffuseColour;

		// we use alpha channel as mask
		//if (colour.a == 0.0f)
		//	return float4(colour.rgb, 0.0f);

        float2 tex_offset = 1.0 / float2(g_screenWidth / 4, g_screenHeight / 4); // gets size of single texel
        float4 result = colour * weight[0];// texture(image, TexCoords).rgb* weight[0]; // current fragment's contribution
       
        for (int i = 1; i < 5; ++i)
        {
			float4 result1 = shaderTexture.Sample(PointSampler, input.texcoord.xy + float2(tex_offset.x * i * 2, 0.0));

			//if (length(result1.rgb) > 0.0f)
				result += result1 * weight[i];

			float4 result2 = shaderTexture.Sample(PointSampler, input.texcoord.xy - float2(tex_offset.x * i * 2, 0.0));

			//if (length(result2.rgb) > 0.0f)
				result += result2 * weight[i];
        }
        
		return result;
	}
}