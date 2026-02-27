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
		output.positionSS = output.position;

		return output;
	}
}
"PixelShader"
{
	Texture2D g_backBuffer : register(t0);

	SamplerState g_pointSampler : register(s2);

	float3 Prefilter(float3 c) {
		float brightness = max(c.r, max(c.g, c.b));

		// soft knee
		float knee = g_bloom.luminosityThreshold * /*_SoftThreshold*/0.75f;
		float soft = brightness - g_bloom.luminosityThreshold + knee;
		soft = clamp(soft, 0, 2 * knee);
		soft = soft * soft / (4 * knee + 0.00001);

		float contribution = max(soft, brightness - g_bloom.luminosityThreshold);
		contribution /= max(brightness, 0.00001);
		return c * contribution;
	}

	float4 ShaderMain(UIPixelInput input) : SV_TARGET
	{
		float2 texcoord = input.texcoord;

		float2 screenPos = float2(input.position.x / (float)g_screenWidth * g_bloom.viewportScale, input.position.y / (float)g_screenHeight * g_bloom.viewportScale);

		float4 pixelColour = g_backBuffer.SampleLevel(g_pointSampler, screenPos, 0);

		float3 filteredValue = Prefilter(pixelColour);

		//float maxVal = max(max(filteredValue.r, filteredValue.g), filteredValue.b);

		return float4(/* pixelColour.rgb + */ filteredValue, 1.0f);

		/*float brightness = dot(pixelColour.rgb, float3(0.2126, 0.7152, 0.0722));
		
		if (brightness > g_bloom.luminosityThreshold)
			return saturate(float4(pixelColour.rgb, 1.0f));

		return float4(0, 0, 0, 1);*/
	}
}