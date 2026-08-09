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
	// P4.5 motion blur, pass 1 of 3 (McGuire 2012): per-tile dominant
	// velocity. Each output texel scans its kTileSize x kTileSize block of
	// the full-res velocity buffer and keeps the largest-magnitude vector,
	// CONVERTED to the blur working space: texture-UV offset units (clip
	// y-negated), pre-scaled by the shutter factor and clamped to the max
	// blur radius. Doing the scale/clamp here keeps every later pass in one
	// consistent space.
	//
	// g_mbParams:  x = velocity scale (shutter * frame-rate normalise)
	//              y = max blur radius in PIXELS
	//              z = gather sample count (unused here)
	//              w = tile size in pixels (20)
	// g_mbParams2: xy = full-res dimensions, zw = tile-grid dimensions

	Texture2D g_velocity : register(t0);

	cbuffer MotionBlurConstants : register(b6)
	{
		float4 g_mbParams;
		float4 g_mbParams2;
	};

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const int tileSize = (int)g_mbParams.w;
		const int2 tileOrigin = (int2)(input.texcoord * g_mbParams2.zw) * tileSize;
		const int2 sourceMax = (int2)g_mbParams2.xy - 1;

		float2 best = 0.0f.xx;
		float bestLenSq = 0.0f;

		[loop]
		for (int y = 0; y < tileSize; ++y)
		{
			[loop]
			for (int x = 0; x < tileSize; ++x)
			{
				const int2 p = min(tileOrigin + int2(x, y), sourceMax);
				// Velocity RT holds the [0,1] clip-space delta (+y = up).
				// Texture-space offset negates y (Utils.shader convention).
				float2 v = g_velocity.Load(int3(p, 0)).xy;
				v = float2(v.x, -v.y) * g_mbParams.x;

				const float lenSq = dot(v, v);
				if (lenSq > bestLenSq)
				{
					bestLenSq = lenSq;
					best = v;
				}
			}
		}

		// Clamp to the max blur radius (pixels), preserving direction.
		const float2 bestPx = best * g_mbParams2.xy;
		const float lenPx = length(bestPx);
		if (lenPx > g_mbParams.y)
			best *= g_mbParams.y / lenPx;

		return float4(best, 0.0f, 1.0f);
	}
}
