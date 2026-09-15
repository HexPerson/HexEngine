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
	// P4.5 motion blur, pass 2 of 3: 3x3 neighbourhood max over the tile-max
	// grid, so a fast-moving object's blur reaches into the tiles it streaks
	// ACROSS, not just the tiles it occupies. Same working space as pass 1
	// (scaled, clamped texture-UV offsets).

	Texture2D g_tileMax : register(t0);

	cbuffer MotionBlurConstants : register(b6)
	{
		float4 g_mbParams;
		float4 g_mbParams2; // zw = tile-grid dimensions
	};

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const int2 tile = (int2)(input.texcoord * g_mbParams2.zw);
		const int2 tileMaxIdx = (int2)g_mbParams2.zw - 1;

		float2 best = 0.0f.xx;
		float bestLenSq = 0.0f;

		[unroll]
		for (int y = -1; y <= 1; ++y)
		{
			[unroll]
			for (int x = -1; x <= 1; ++x)
			{
				const int2 p = clamp(tile + int2(x, y), int2(0, 0), tileMaxIdx);
				const float2 v = g_tileMax.Load(int3(p, 0)).xy;
				const float lenSq = dot(v, v);
				if (lenSq > bestLenSq)
				{
					bestLenSq = lenSq;
					best = v;
				}
			}
		}

		return float4(best, 0.0f, 1.0f);
	}
}
