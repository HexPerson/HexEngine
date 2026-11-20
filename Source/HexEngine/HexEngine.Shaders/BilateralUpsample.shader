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

		// Pixel p sits in half-res texel p/2; its other nearest texel is to
		// the left/up for an even p, right/down for an odd p. (screenPos is
		// SV_Position, a pixel CENTRE p+0.5, so the old float "% 2 == 0" test
		// was never true and every pixel blended the texels right of and
		// below it - a one-sided smear of up to 3 px across silhouettes.)
		const int2 pixel = int2(screenPos);
		int xOffset = (pixel.x & 1) == 0 ? -1 : 1;
		int yOffset = (pixel.y & 1) == 0 ? -1 : 1;

		int2 offsets[] = { int2(0, 0),
		int2(0, yOffset),
		int2(xOffset, 0),
		int2(xOffset, yOffset) };

		for (int i = 0; i < 4; i++)
		{
			const int2 lowResCoord = int2(screenPosDownscaled) + offsets[i];
			// max(0): CloudTemporal marks geometry texels with alpha -1 (so
			// its history fetch can tell them from clear sky). They are never
			// cloud - treat them as empty here.
			float4 downscaledColor = max(shaderTexture.Load(int3(lowResCoord, 0)), 0.0f.xxxx);

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

		// No matching tap: a sky pixel whose whole 2x2 footprint was geometry
		// at half res (a small gap between leaves, a pixel beside a thin
		// branch). Its nearest texel is a geometry texel with no cloud in it,
		// which let the bright cloudless dome through as a speck/rim. Widen
		// to the 5x5 half-res neighbourhood and take the distance-weighted
		// average of the SKY texels there.
		if (totalWeight <= 0.0f)
		{
			const int2 centre = int2(screenPosDownscaled);
			[unroll]
			for (int wy = -2; wy <= 2; ++wy)
			{
				[unroll]
				for (int wx = -2; wx <= 2; ++wx)
				{
					const int2 lowResCoord = centre + int2(wx, wy);
					if (GBUFFER_DIFFUSE.Load(int3(lowResCoord * 2 + int2(1, 1), 0)).a >= -0.5f)
						continue;
					const float w = 1.0f / (1.0f + (float)(wx * wx + wy * wy));
					color += max(shaderTexture.Load(int3(lowResCoord, 0)), 0.0f.xxxx) * w;
					totalWeight += w;
				}
			}
			// Still nothing: a pinhole deep inside a canopy. The cloud field
			// is low-frequency, so the nearest open sky is a good stand-in;
			// probe 8 directions at 4/8/16 half-res texels (up to ~32 px).
			if (totalWeight <= 0.0f)
			{
				const int2 dirs[8] = { int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1),
					int2(1, 1), int2(-1, 1), int2(1, -1), int2(-1, -1) };
				[loop]
				for (int radius = 4; radius <= 16 && totalWeight <= 0.0f; radius *= 2)
				{
					[unroll]
					for (int d = 0; d < 8; ++d)
					{
						const int2 lowResCoord = centre + dirs[d] * radius;
						if (GBUFFER_DIFFUSE.Load(int3(lowResCoord * 2 + int2(1, 1), 0)).a >= -0.5f)
							continue;
						color += max(shaderTexture.Load(int3(lowResCoord, 0)), 0.0f.xxxx);
						totalWeight += 1.0f;
					}
				}
			}
			if (totalWeight <= 0.0f)
				return 0.0f.xxxx;
		}

		float4 volumetricLight;
		const float epsilon = 0.0001f;
		volumetricLight = color / (totalWeight + epsilon);

		return volumetricLight;

		//return colour;
	}
}
