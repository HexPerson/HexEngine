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

		// Sky marker (diffuse.a == -1) of the pixel being upsampled. The
		// half-res cloud march writes alpha 0 on geometry texels and the
		// temporal pass zeroes them too, so a geometry pixel gets nothing;
		// and a SKY pixel must only gather from SKY texels - averaging in
		// the zero-alpha geometry texels along a silhouette halved the cloud
		// alpha there and let the bright dome through as a light outline
		// round every lamp post and roofline.
		const bool fullResIsSky = GBUFFER_DIFFUSE.Load(int3(screenPos, 0)).a < -0.5f;

		float4 color = 0.0f.xxxx;
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
			const int2 lowResCoord = int2(screenPosDownscaled) + offsets[i];
			float4 downscaledColor = shaderTexture.Load(int3(lowResCoord, 0));

			// The half-res texel centre maps to full-res pixel 2*c+1 - that
			// is the gbuffer texel the march actually tested. (This used to
			// load the full-res gbuffer AT the half-res coordinate, i.e. the
			// depth of a pixel in the top-left quarter of the screen, so the
			// edge weighting was noise.)
			const int3 tapFull = int3(lowResCoord * 2 + int2(1, 1), 0);
			const bool tapIsSky = GBUFFER_DIFFUSE.Load(tapFull).a < -0.5f;

			float currentWeight = 1.0f;
			if (fullResIsSky)
			{
				currentWeight = tapIsSky ? 1.0f : 0.0f;
			}
			else
			{
				float downscaledDepth = GBUFFER_NORMAL.Load(tapFull).w;
				if (downscaledDepth == -1)
					downscaledDepth = g_frustumDepths[3];
				downscaledDepth /= g_frustumDepths[3];
				currentWeight *= max(0.0f, 1.0f - (1.0f) * abs(downscaledDepth - upSampledDepth));
			}

			color += downscaledColor * currentWeight;
			totalWeight += currentWeight;
		}

		// No matching tap (a sky pixel whose whole 2x2 footprint was
		// geometry at half res): take the nearest texel rather than a black
		// hole in the deck.
		if (totalWeight <= 0.0f)
			return shaderTexture.Load(int3(int2(screenPosDownscaled), 0));

		float4 volumetricLight;
		const float epsilon = 0.0001f;
		volumetricLight = color / (totalWeight + epsilon);

		return volumetricLight;

		//return colour;
	}
}
