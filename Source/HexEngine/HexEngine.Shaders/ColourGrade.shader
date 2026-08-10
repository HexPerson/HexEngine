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

	static const float LINEAR_MIDGRAY = 0.18f;

	// P4.6: contrast in LOG2 space about mid-grey. The old linear pivot
	// compressed shadows and blew highlights asymmetrically: one linear
	// stop above mid-grey is 0.18 of range but one below is only 0.09, so a
	// linear scale about the pivot treats them completely differently. In
	// log2, "contrast 1.2" means every STOP moves 20% further from mid-grey
	// - the filmic definition. Guarded so neutral (1.0) is bit-identical
	// (a log2/exp2 round-trip is NOT exact in float).
	float3 ColorGradingContrast(float3 color)
	{
		if (g_colourGrading.contrast == 1.0f)
			return color;
		const float3 stops = log2(max(color, EPSILON) / LINEAR_MIDGRAY);
		return exp2(stops * g_colourGrading.contrast) * LINEAR_MIDGRAY;
	}

	float3 ColorGradePostExposure(float3 color)
	{
		return color * g_colourGrading.exposure;
	}

	// White balance: RGB gains computed CPU-side from r_whiteBalanceTemp /
	// r_whiteBalanceTint (von-Kries adaptation in LMS, normalised to green).
	float3 ColorGradeWhiteBalance(float3 color)
	{
		return color * g_whiteBalance.rgb;
	}

	// ASC-CDL-style lift / gamma / gain, applied after the log contrast:
	// gain scales, lift offsets (raises blacks without touching a pure-white
	// gain response), gamma bends the mids. Guarded for bit-identical
	// neutral (pow(x, 1) is not exact for all x).
	float3 ColorGradeCdl(float3 color)
	{
		const bool neutral =
			all(g_cdlLift.rgb == 0.0f.xxx) &&
			all(g_cdlGamma.rgb == 1.0f.xxx) &&
			all(g_cdlGain.rgb == 1.0f.xxx);
		if (neutral)
			return color;
		color = max(color * g_cdlGain.rgb + g_cdlLift.rgb, 0.0f);
		return pow(color, 1.0f / max(g_cdlGamma.rgb, 0.01f.xxx));
	}

	float3 ColorGradeColorFilter(float3 color) {
		// Use the uploaded cbuffer value, not the file-static below. This read the static
		// copy, which made the r_colourFilter cvar completely inert - it was being uploaded
		// every frame and then ignored.
		return color * g_colourGrading.colourFilter;
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

		// P4.6 grading order: exposure (linear) -> white balance (linear
		// gains) -> contrast (log2 stops) -> CDL lift/gamma/gain -> colour
		// filter -> hue shift -> saturation. Every stage is a guarded no-op
		// at its neutral value, so default settings are bit-identical.
		colour.rgb = ColorGradePostExposure(colour.rgb);
		colour.rgb = ColorGradeWhiteBalance(colour.rgb);
		colour.rgb = ColorGradingContrast(colour.rgb);
		colour.rgb = ColorGradeCdl(colour.rgb);
		colour.rgb = ColorGradeColorFilter(colour.rgb);

		colour = max(colour, 0.0f);

		colour.rgb = ColorGradingHueShift(colour.rgb);
		colour.rgb = ColorGradingSaturation(colour.rgb);

		return max(colour, 0.0f);
	}
}