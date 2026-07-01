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
	// One jump-flood step. For each pixel, look at 9 neighbours at +/- g_jumpStep
	// pixels and keep whichever stored seed is nearest to THIS pixel. After
	// log2(thickness) halving steps every pixel within `thickness` of the
	// silhouette holds the coordinate of its nearest covered (seed) pixel.
	cbuffer OutlineParams : register(b5)
	{
		float4 g_outlineColour;     // rgb = glow colour
		float  g_outlineThickness;  // outline width in pixels
		float  g_jumpStep;          // current JFA step in pixels
		float  g_outlinePad0;
		float  g_outlinePad1;
	};

	Texture2D seedTex : register(t0);
	SamplerState PointSampler : register(s3);

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		float2 screen = float2(g_screenWidth, g_screenHeight);
		float2 pixel  = input.texcoord * screen;

		float2 bestSeed = float2(-1.0f, -1.0f);
		float  bestDist = 1e20f;

		[unroll] for (int dy = -1; dy <= 1; ++dy)
		{
			[unroll] for (int dx = -1; dx <= 1; ++dx)
			{
				float2 sampleUV = input.texcoord + float2(dx, dy) * (g_jumpStep / screen);
				float2 seed = seedTex.SampleLevel(PointSampler, sampleUV, 0).xy;
				if (seed.x >= 0.0f)
				{
					float d = distance(seed, pixel);
					if (d < bestDist)
					{
						bestDist = d;
						bestSeed = seed;
					}
				}
			}
		}

		return float4(bestSeed, 0.0f, 1.0f);
	}
}
