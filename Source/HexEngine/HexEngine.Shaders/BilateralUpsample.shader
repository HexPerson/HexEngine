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
	Global
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
	GBUFFER_RESOURCE(0, 1, 2, 3, 4);

	Texture2D shaderTexture : register(t5);
	SamplerState PointSampler : register(s2);

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		//return float4(1,1,1,1);
		float2 screenPos = float2(input.position.x /*/ (float)g_screenWidth*/, input.position.y /*/ (float)g_screenHeight*/);
		float2 screenPosDownscaled = float2(input.position.x /*/ (float)g_screenWidth*/ / 2, input.position.y /*/ (float)g_screenHeight*/ / 2);

		//float4 colour = shaderTexture.Sample(PointSampler, input.texcoord) * g_material.diffuseColour;

		float upSampledDepth = GBUFFER_NORMAL.Load(int3(screenPos, 0)).w;

		if (upSampledDepth == -1)
			upSampledDepth = g_frustumDepths[3];

		upSampledDepth /= g_frustumDepths[3];

		float3 color = 0.0f.xxx;
		float totalWeight = 0.0f;

		// Select the closest downscaled pixels.

		int xOffset = screenPos.x % 2 == 0 ? -1 : 1;
		int yOffset = screenPos.y % 2 == 0 ? -1 : 1;

		int2 offsets[] = { int2(0, 0),
		int2(0, yOffset),
		int2(xOffset, 0),
		int2(xOffset, yOffset) };

		for (int i = 0; i < 4; i++)
		{

			float3 downscaledColor = shaderTexture.Load(int3(screenPosDownscaled + float2(offsets[i].x, offsets[i].y), 0)).rgb;

			float downscaledDepth = GBUFFER_NORMAL.Load(int3(screenPosDownscaled + float2(offsets[i].x, offsets[i].y), 1)).w;

			downscaledDepth /= g_frustumDepths[3];

			if (downscaledDepth == -1)
				downscaledDepth = g_frustumDepths[3];

			float currentWeight = 1.0f;
			currentWeight *= max(0.0f, 1.0f - (1.0f) * abs(downscaledDepth - upSampledDepth));

			color += downscaledColor * currentWeight;
			totalWeight += currentWeight;

		}

		float3 volumetricLight;
		const float epsilon = 0.0001f;
		volumetricLight.xyz = color / (totalWeight + epsilon);

		return float4(volumetricLight.xyz, 1.0f);

		//return colour;
	}
}