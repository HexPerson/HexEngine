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
	// Reads the finished jump-flood distance field and emits the outline ring:
	// pixels OUTSIDE the silhouette whose distance to the nearest covered pixel
	// is within g_outlineThickness. Output is the glow colour pre-multiplied by
	// a falloff that's brightest at the silhouette edge - the caller additively
	// blends it into the beauty buffer.
	cbuffer OutlineParams : register(b5)
	{
		float4 g_outlineColour;     // rgb = glow colour
		float  g_outlineThickness;  // outline width in pixels
		float  g_jumpStep;          // unused here
		float  g_outlinePad0;
		float  g_outlinePad1;
	};

	Texture2D jfaTex : register(t0);
	SamplerState PointSampler : register(s3);

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		float2 screen = float2(g_screenWidth, g_screenHeight);
		float2 pixel  = input.texcoord * screen;

		float2 seed = jfaTex.Sample(PointSampler, input.texcoord).xy;
		if (seed.x < 0.0f)
			return float4(0.0f, 0.0f, 0.0f, 0.0f); // no silhouette within reach

		float d = distance(seed, pixel);

		// d ~= 0 -> this pixel is inside the silhouette (its own seed): no ring.
		// 0 < d <= thickness -> the outer ring.
		if (d <= 0.5f || d > g_outlineThickness)
			return float4(0.0f, 0.0f, 0.0f, 0.0f);

		float a = 1.0f - saturate(d / g_outlineThickness); // bright at edge -> 0 at thickness
		a = a * a;                                          // sharper, glowier falloff

		return float4(g_outlineColour.rgb * a, a);
	}
}
