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

#define TONEMAP_GAMMA 2.2

	float3 jodieReinhardTonemap(float3 c)
	{
		float l = dot(c, float3(0.2126, 0.7152, 0.0722));
		float3 tc = c / (c + 1.0);

		return lerp(c / (l + 1.0), tc, tc);
	}

	float3 tonemap_uncharted2(in float3 x)
	{
		float A = 0.15;
		float B = 0.50;
		float C = 0.10;
		float D = 0.20;
		float E = 0.02;
		float F = 0.30;

		return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
	}

	float3 tonemap_uc2(in float3 color)
	{
		float W = 11.2;

		color *= 16;  // Hardcoded Exposure Adjustment

		float exposure_bias = 2.0f;
		float3 curr = tonemap_uncharted2(exposure_bias * color);

		float3 white_scale = 1.0f / tonemap_uncharted2(W);
		float3 ccolor = curr * white_scale;

		float3 ret = pow(abs(ccolor), TONEMAP_GAMMA); // gamma

		return ret;
	}

	float3 tonemap_filmic(in float3 color)
	{
		color = max(0, color - 0.004f);
		color = (color * (6.2f * color + 0.5f)) / (color * (6.2f * color + 1.7f) + 0.06f);

		// result has 1/2.2 baked in
		return pow(color, TONEMAP_GAMMA);
	}

	float3 Tonemap_Inv(float3 s)
	{
		return s / (float3(1, 1, 1) - s);
	}

	float3 Tonemap(float3 s)
	{
		return s / (float3(1, 1, 1) + s);
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		float4 colour = shaderTexture.Sample(PointSampler, input.texcoord) * g_material.diffuseColour;

		float3 finalColour = colour.rgb;

		//finalColour.r = 1.0f;
		finalColour = tonemap_filmic(finalColour);
		//finalColour = pow(finalColour, float3(2.2, 2.2, 2.2));
		//float4 colour = shaderTexture.Load(input.texcoord, 1);//

		//finalColour = jodieReinhardTonemap(finalColour);

		//finalColour = pow(finalColour, g_gamma);

		return float4(finalColour, colour.a);
	}
}