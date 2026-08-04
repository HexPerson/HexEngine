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

	// Sparse STATIC beads: only a fraction of cells hold a bead (spawn gate),
	// pulsing in/out on a long lifetime. Aspect-corrected `st`. Returns
	// xy = refraction offset, z = mask, w = rim/spec highlight.
	float4 StaticBeads(float2 st, float scale, float t, float seed)
	{
		const float2 gv = st * scale;
		const float2 id = floor(gv);
		const float2 f  = frac(gv) - 0.5f;
		const float2 rnd = Hash22(id + seed);
		// Spawn gate: ~28% of cells actually carry a bead - keeps it sparse.
		if (rnd.x > 0.28f)
			return float4(0, 0, 0, 0);

		const float2 centre = (Hash22(id + seed + 9.1f) - 0.5f) * 0.6f;
		const float life = frac(t * (0.04f + rnd.y * 0.06f) + rnd.y);
		const float grow = smoothstep(0.0f, 0.12f, life) * (1.0f - smoothstep(0.6f, 1.0f, life));
		const float radius = (0.13f + rnd.y * 0.13f) * grow;
		const float2 d = f - centre;
		const float dist = length(d);
		if (radius < 1e-3f)
			return float4(0, 0, 0, 0);

		const float bead = smoothstep(radius, radius * 0.6f, dist);
		const float2 offset = -d * bead / scale;
		const float rim = smoothstep(radius * 0.85f, radius, dist) * bead;
		return float4(offset, bead, rim * 0.5f);
	}

	// RUNNERS: some columns carry a droplet sliding DOWN the screen (uv.y is
	// down), with a thin fading trail behind the head. Aspect-corrected `st`.
	float4 Runners(float2 st, float cols, float t, float seed)
	{
		const float x = st.x * cols;
		const float col = floor(x);
		const float fx = (frac(x) - 0.5f);
		const float2 rnd = Hash22(float2(col, seed));
		// ~30% of columns have a runner.
		if (rnd.x > 0.30f)
			return float4(0, 0, 0, 0);

		const float speed = 0.05f + rnd.y * 0.12f;
		const float cyc = t * speed + rnd.y * 5.3f;
		const float headY = frac(cyc);                          // 0..1 travels down
		const float wob = (Hash21(float2(col, floor(cyc))) - 0.5f) * 0.5f;
		const float px = (fx - wob) / cols;                     // back to st.x units
		const float dy = st.y - headY;

		const float headR = 0.010f + rnd.x * 0.012f;
		const float head = smoothstep(headR, headR * 0.5f, length(float2(px, dy)));
		// Trail above the head (above = -dy > 0): thin vertical streak, brightest
		// mid-way, fading out along its length.
		const float above = -dy;
		const float trailLen = 0.12f + rnd.y * 0.10f;
		float trail = smoothstep(headR * 0.7f, 0.0f, abs(px))
			* saturate(above / trailLen) * (1.0f - saturate(above / trailLen)) * 4.0f;
		trail = saturate(trail) * step(0.0f, above);

		const float mask = max(head, trail * 0.6f);
		const float2 offset = float2(px, dy) * head;            // refract at the head
		return float4(offset, mask, head * 0.6f);
	}

	float4 ShaderMain(UIPixelInput input) : SV_TARGET
	{
		const float2 uv = input.texcoord;
		// Rain effect: fade it out as the precipitation turns to snow (drips are
		// water on the lens, not snowflakes).
		const float rain = saturate(g_weatherSurface.precipitationIntensity)
			* saturate(1.0f - g_weatherSurface.snowCoverage * 0.9f);
		if (rain < 0.01f)
			return shaderTexture.Sample(LinearSampler, uv);

		const float aspect = g_screenWidth / max(g_screenHeight, 1.0f);
		const float2 st = uv * float2(aspect, 1.0f);
		const float t = g_time;

		float2 refr = float2(0.0f, 0.0f);
		float hi = 0.0f;

		// One sparse static layer + one runner layer - far fewer than before.
		const float4 b = StaticBeads(st, 8.0f, t, 0.0f);
		refr += b.xy; hi += b.w;
		const float4 r = Runners(st, 9.0f, t, 3.0f);
		refr += r.xy; hi += r.w;

		const float2 uvOffset = float2(refr.x / aspect, refr.y) * (0.8f * rain);
		float3 col = shaderTexture.Sample(LinearSampler, uv + uvOffset).rgb;
		col += hi * rain * 0.20f;
		return float4(col, 1.0f);
	}
}
