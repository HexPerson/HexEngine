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
	// Screen-space "rain on the lens" (Phase 3). A full-screen post pass that
	// beads water droplets over the finished frame while precipitation is
	// falling: each droplet acts as a tiny lens (refracts + magnifies the
	// background) with a bright rim highlight. Strength tracks
	// g_weatherSurface.precipitationIntensity, so it fades in/out with the
	// storm; the pass is gated off in C++ when precipitation is zero.
	Texture2D shaderTexture : register(t0);
	SamplerState LinearSampler : register(s3);

	float Hash21(float2 p)
	{
		p = frac(p * float2(123.34f, 456.21f));
		p += dot(p, p + 45.32f);
		return frac(p.x * p.y);
	}
	float2 Hash22(float2 p)
	{
		const float n = Hash21(p);
		return float2(n, Hash21(p + n));
	}

	// Horizontal offset of a runner's meandering path at a given vertical
	// position y. The head AND the whole trail use this, so the trail follows
	// the drip's actual history (not a straight vertical line).
	float PathX(float baseX, float y, float freq, float amp, float phase)
	{
		return baseX + sin(y * freq + phase) * amp + sin(y * freq * 2.3f + phase * 1.7f) * amp * 0.35f;
	}

	float4 ShaderMain(UIPixelInput input) : SV_TARGET
	{
		const float2 uv = input.texcoord;
		// WATER on the lens - only for RAIN precipitation. Snow and sandstorms
		// also drive precipitationIntensity, so gate them out: no water beads
		// when it's snowing or during a sandstorm.
		const float rainType = saturate(1.0f - g_weatherSurface.snowCoverage * 3.0f)
			* saturate(1.0f - g_weatherSurface.dirtAmount * 3.0f);
		const float rain = saturate(g_weatherSurface.precipitationIntensity) * rainType;
		if (rain < 0.01f)
			return shaderTexture.SampleLevel(LinearSampler, uv, 0);

		const float aspect = g_screenWidth / max(g_screenHeight, 1.0f);
		const float2 asp = float2(aspect, 1.0f);
		const float t = g_time;

		// PURE REFRACTION. Each droplet only DISTORTS the background behind it -
		// no highlights, rims or bright trails (those read as cartoon bubbles /
		// "sperm"). We accumulate a screen-space refraction OFFSET from every
		// drop head + its path-following trail, then sample the background once
		// at the displaced UV. Drops are visible only where they bend background
		// detail - exactly like real water on glass.
		float2 refr = float2(0.0f, 0.0f);
		[unroll]
		for (int i = 0; i < 16; ++i)
		{
			const float2 h  = Hash22(float2((float)i * 1.73f, 3.31f));
			const float2 h2 = Hash22(float2((float)i * 2.91f, 8.13f));
			const float baseX = h.x;
			const float R     = 0.005f + h.y * 0.007f;
			const float speed = 0.05f + h2.x * 0.11f;
			const float freq  = 5.0f + h2.y * 7.0f;
			const float amp   = 0.008f + h.y * 0.018f;
			const float phase = h.x * 31.0f;
			const float cyc   = t * speed + h2.y * 17.0f;
			const float headY = pow(frac(cyc), 1.35f);
			const float headX = PathX(baseX, headY, freq, amp, phase);

			// Head lens: bend the background toward the drop centre (magnify),
			// strongest near the rim.
			const float2 d = (uv - float2(headX, headY)) * asp;
			const float dist = length(d);
			if (dist < R)
			{
				const float k = dist / R;
				const float2 dir = d / max(dist, 1e-5f);
				refr += -dir * k * R * 1.1f / asp;
			}

			// Trail: a subtle refraction streak that FOLLOWS the drip's wobble
			// path (path evaluated at this pixel's y), fading behind the head.
			const float along = headY - uv.y;
			if (along > 0.0f && along < 0.28f)
			{
				const float trailX = PathX(baseX, uv.y, freq, amp, phase);
				const float tdist = abs(uv.x - trailX) * aspect;
				const float tw = R * lerp(0.55f, 0.12f, saturate(along / 0.28f));
				const float fall = saturate(1.0f - along / 0.28f);
				const float tmask = smoothstep(tw, 0.0f, tdist) * fall;
				const float side = (uv.x < trailX) ? -1.0f : 1.0f;
				refr += float2(side * tmask * tw * 0.5f / aspect, 0.0f);
			}
		}

		return shaderTexture.SampleLevel(LinearSampler, uv + refr * rain, 0);
	}
}
