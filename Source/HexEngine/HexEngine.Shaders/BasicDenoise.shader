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

#define val0 (1.0)
#define val1 (0.125)
#define effect_width (0.125)

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		//float4 colour = shaderTexture.Load(int3(input.texcoord.x * g_screenWidth,  input.texcoord.y * g_screenHeight, 0)) * g_material.diffuseColour;
		//float4 colour = shaderTexture.Sample(PointSampler, input.texcoord) * input.colour;// g_material.diffuseColour;

		float dx = 0.0f;
		float dy = 0.0f;
		float fTap = effect_width;

		float4 cAccum = shaderTexture.Sample(PointSampler, input.texcoord) * val0;

		for (int iDx = 0; iDx < 16; ++iDx)
		{
			dx = fTap / g_screenWidth;
			dy = fTap / g_screenHeight;

			cAccum += shaderTexture.Sample(PointSampler, input.texcoord + float2(-dx, -dy)) * val1;
			cAccum += shaderTexture.Sample(PointSampler, input.texcoord + float2(  0, -dy)) * val1;
			cAccum += shaderTexture.Sample(PointSampler, input.texcoord + float2(-dx,   0)) * val1;
			cAccum += shaderTexture.Sample(PointSampler, input.texcoord + float2( dx,   0)) * val1;
			cAccum += shaderTexture.Sample(PointSampler, input.texcoord + float2(  0,  dy)) * val1;
			cAccum += shaderTexture.Sample(PointSampler, input.texcoord + float2( dx,  dy)) * val1;
			cAccum += shaderTexture.Sample(PointSampler, input.texcoord + float2(-dx, +dy)) * val1;
			cAccum += shaderTexture.Sample(PointSampler, input.texcoord + float2(+dx, -dy)) * val1;

			fTap  += 0.1f;
		}

		return (cAccum / 16.0f);
	}
}