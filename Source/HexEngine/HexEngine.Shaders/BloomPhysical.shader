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

	float Luminance(float3 colour)
	{
		return dot(colour, float3(0.2126f, 0.7152f, 0.0722f));
	}

	float3 PrefilterPhysical(float3 sceneColour)
	{
		sceneColour = max(sceneColour, 0.0f);

		const float luminance = max(Luminance(sceneColour), 0.0f);
		const float referenceLuminance = max(g_bloom.luminosityThreshold, 0.0001f);
		const float bloomIntensity = max(g_bloom.bloomIntensity, 0.0f);
		const float bloomClamp = max(g_bloom.bloomClamp, 0.0f);

		// Continuous lens-scatter response: brighter values smoothly leak more energy.
		const float scatterResponse = 1.0f - exp(-luminance / referenceLuminance);
		float3 bloom = sceneColour * (scatterResponse * bloomIntensity);

		// Optional safety clamp to limit isolated spikes if desired.
		if (bloomClamp > 0.0f)
		{
			bloom = min(bloom, bloomClamp);
		}

		return bloom;
	}

	float4 ShaderMain(UIPixelInput input) : SV_TARGET
	{
		const float2 screenPos = float2(
			input.position.x / (float)g_screenWidth * g_bloom.viewportScale,
			input.position.y / (float)g_screenHeight * g_bloom.viewportScale
		);

		const float4 sceneColour = g_backBuffer.SampleLevel(g_pointSampler, screenPos, 0);
		const float3 filteredValue = PrefilterPhysical(sceneColour.rgb);

		return float4(filteredValue, 1.0f);
	}
}
