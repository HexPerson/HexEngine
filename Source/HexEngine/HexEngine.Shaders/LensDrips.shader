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

	// One droplet layer of round beads on a jittered cell grid. Returns
	// xy = refraction offset (aspect-corrected screen space), z = bead mask,
	// w = rim highlight. Beads pulse in and fade out on a per-cell lifetime.
	float4 DropletLayer(float2 st, float scale, float t, float seed)
	{
		const float2 gv = st * scale;
		const float2 id = floor(gv);
		const float2 f  = frac(gv) - 0.5f;

		const float2 rnd = Hash22(id + seed);
		const float2 centre = (rnd - 0.5f) * 0.7f;         // jitter within the cell
		const float  phase  = Hash21(id + seed + 3.7f);
		// Slow per-cell lifetime: grow in, linger, fade (a new bead lands).
		const float life = frac(t * (0.10f + phase * 0.15f) + phase);
		const float grow = smoothstep(0.0f, 0.15f, life) * (1.0f - smoothstep(0.55f, 1.0f, life));
		const float radius = (0.10f + rnd.x * 0.16f) * grow;

		const float2 d = f - centre;
		const float dist = length(d);
		if (radius < 1e-3f)
			return float4(0, 0, 0, 0);

		const float bead = smoothstep(radius, radius * 0.65f, dist);   // 1 inside, 0 out
		// Lens: refract the background toward the bead centre (magnify + invert).
		const float2 offset = -d * bead / scale;
		// Bright rim + a small specular dot toward the top-left of the bead.
		const float rim = smoothstep(radius * 0.9f, radius, dist) * bead;
		const float spec = smoothstep(0.06f, 0.0f, length(d - float2(-0.02f, 0.02f))) * bead;
		return float4(offset, bead, rim * 0.5f + spec);
	}

	float4 ShaderMain(UIPixelInput input) : SV_TARGET
	{
		const float2 uv = input.texcoord;
		const float precip = saturate(g_weatherSurface.precipitationIntensity);
		if (precip < 0.01f)
			return shaderTexture.Sample(LinearSampler, uv);

		// Aspect-correct so beads stay round regardless of screen ratio.
		const float aspect = g_screenWidth / max(g_screenHeight, 1.0f);
		const float2 st = uv * float2(aspect, 1.0f);
		const float t = g_time;

		float2 refr = float2(0.0f, 0.0f);
		float mask = 0.0f;
		float hi = 0.0f;
		[unroll]
		for (int L = 0; L < 2; ++L)
		{
			const float scale = (L == 0) ? 11.0f : 17.0f;
			const float4 dl = DropletLayer(st, scale, t, (float)L * 21.0f);
			refr += dl.xy;
			mask = max(mask, dl.z);
			hi += dl.w;
		}

		// Refraction offset back into UV space (undo the aspect scale on x).
		const float2 uvOffset = float2(refr.x / aspect, refr.y) * (0.9f * precip);
		float3 col = shaderTexture.Sample(LinearSampler, uv + uvOffset).rgb;
		// Rim/spec highlight; scale by precipitation so it fades with the storm.
		col += hi * precip * 0.25f;
		return float4(col, 1.0f);
	}
}
