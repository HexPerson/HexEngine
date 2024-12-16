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

#define EPSILON 1e-6f

#define ACEScc_MAX      1.4679964
#define ACEScc_MIDGRAY  0.4135884

	static const float g_fContrast = 1.1f;
	static const float g_fExposure = 1.1f;
	static const float3 g_fColourFilter = float3(1.00f, 0.92f, 0.94f);
	static const float g_fHueShift = 0.0f;
	static const float g_fSaturation = 1.05f;

	half Luminance(half3 linearRgb)
	{
		return dot(linearRgb, float3(0.2126729, 0.7151522, 0.0721750));
	}

	half Luminance(half4 linearRgba)
	{
		return Luminance(linearRgba.rgb);
	}

	float3 RgbToHsv(float3 c)
	{
		float4 K = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
		float4 p = lerp(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
		float4 q = lerp(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));
		float d = q.x - min(q.w, q.y);
		float e = EPSILON;
		return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
	}

	float3 HsvToRgb(float3 c)
	{
		float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
		float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
		return c.z * lerp(K.xxx, saturate(p - K.xxx), c.y);
	}

	float RotateHue(float value, float low, float hi)
	{
		return (value < low)
			? value + hi
			: (value > hi)
			? value - hi
			: value;
	}

	float3 ColorGradingContrast(float3 color)
	{
		return (color - ACEScc_MIDGRAY) * g_colourGrading.contrast + ACEScc_MIDGRAY;
	}

	float3 ColorGradePostExposure(float3 color)
	{
		return color * g_colourGrading.exposure;
	}

	float3 ColorGradeColorFilter(float3 color) {
		return color * g_fColourFilter;
	}

	float3 ColorGradingHueShift(float3 color) {
		color = RgbToHsv(color);
		float hue = color.x + g_colourGrading.hueShift;
		color.x = RotateHue(hue, 0.0, 1.0);
		return HsvToRgb(color);
	}

	float3 ColorGradingSaturation(float3 color) {
		float luminance = Luminance(color);
		return (color - luminance) * g_colourGrading.saturation + luminance;
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		float4 colour = shaderTexture.Sample(PointSampler, input.texcoord) * input.colour;

		colour = min(colour, 60.0f);

		colour.rgb = ColorGradePostExposure(colour.rgb);
		colour.rgb = ColorGradingContrast(colour.rgb);
		colour.rgb = ColorGradeColorFilter(colour.rgb);

		colour = max(colour, 0.0f);

		colour.rgb = ColorGradingHueShift(colour.rgb);
		colour.rgb = ColorGradingSaturation(colour.rgb);

		

		return max(colour, 0.0f);;
	}
}