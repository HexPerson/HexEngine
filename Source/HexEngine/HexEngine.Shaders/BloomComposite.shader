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
	// Phase 4 bloom chain: energy-normalised composite. Replaces the old
	// BlendTo_Additive full-quad (which added the unnormalised gaussian sum
	// on top of the undiminished scene - created energy, and made the
	// intensity cvar resolution/radius dependent).
	//
	// The accumulated chain top holds the SUM of N tent-upsampled levels, so
	// dividing by N (g_bloomPass2.x = 1/levelCount) makes it an average and
	// g_bloom.bloomIntensity means the same thing at any chain length:
	// "fraction of light the lens scatters" - Cyberpunk-restrained values
	// live around 0.03-0.07.
	//
	// t0 = scene HDR, t1 = bloom chain top (half res, upscaled bilinearly).

	Texture2D g_scene : register(t0);
	Texture2D g_bloomChain : register(t1);
	SamplerState g_pointSampler : register(s2);
	SamplerState g_linearSampler : register(s4);

	cbuffer BloomPassConstants : register(b6)
	{
		float4 g_bloomPass;
		float4 g_bloomPass2;
	};

	// Sample the bloom chain top (already a blurred highlight image) with the
	// in-frame WRAP sampler clamped to a valid texel.
	float3 SampleChain(float2 uv)
	{
		const float2 halfTexel = 0.5f * g_bloomPass.xy;
		return g_bloomChain.SampleLevel(g_linearSampler, clamp(uv, halfTexel, 1.0f - halfTexel), 0).rgb;
	}

	// Procedural lens-dirt, no authored texture needed. Static in screen
	// space (a real smudged lens) and lit by the bloom at the call site.
	// Structure matters now that dirt is an independent effect: real lens
	// dirt is many small sharp specks plus a few broad grease smears - the
	// old 6 giant gaussian lobes read as vignette splotches.
	float LensDirtHash(float2 p)
	{
		return frac(sin(dot(p, float2(127.1f, 311.7f))) * 43758.5453f);
	}

	float LensDirt(float2 uv)
	{
		// Aspect-corrected so specks stay round on widescreen.
		const float2 p = uv * float2(1.7778f, 1.0f);

		float d = 0.0f;

		// Fine specks: jittered-grid dots at two scales; ~40% of cells carry
		// one, with per-cell size and brightness variation.
		[unroll]
		for (int layer = 0; layer < 2; ++layer)
		{
			const float scale = (layer == 0) ? 22.0f : 9.0f;
			const float2 g = p * scale + (float)layer * 13.7f;
			const float2 cell = floor(g);
			const float2 f = frac(g) - 0.5f;
			const float2 jitter = float2(LensDirtHash(cell + 1.3f), LensDirtHash(cell + 17.9f)) - 0.5f;
			const float2 dd = f - jitter * 0.6f;
			const float size = 0.06f + 0.14f * LensDirtHash(cell + 31.4f);
			const float keep = step(0.62f, LensDirtHash(cell + 7.7f));
			const float bright = 0.35f + 0.65f * LensDirtHash(cell + 3.1f);
			d += keep * bright * exp(-dot(dd, dd) / max(size * size, 1e-5f)) * ((layer == 0) ? 0.55f : 0.40f);
		}

		// Broad smudges: a few soft, strongly-eccentric lobes (grease smears),
		// kept subtle so the specks carry the look.
		[unroll]
		for (int i = 0; i < 4; ++i)
		{
			const float fi = (float)i;
			const float2 c = float2(frac(sin(fi * 12.9898f + 1.0f) * 43758.5453f),
			                        frac(sin(fi * 78.233f + 2.0f) * 24634.6345f)) * float2(1.7778f, 1.0f);
			const float2 seed = float2(fi, fi);
			float2 axis = float2(LensDirtHash(seed + 5.0f) - 0.5f, LensDirtHash(seed + 9.0f) - 0.5f);
			axis = normalize(axis + float2(1e-3f, 2e-3f));
			float2 dd = p - c;
			dd = float2(dot(dd, axis), dot(dd, float2(-axis.y, axis.x)));
			dd /= float2(0.30f + 0.25f * LensDirtHash(seed + 2.0f), 0.10f + 0.08f * LensDirtHash(seed + 4.0f));
			d += exp(-dot(dd, dd)) * 0.18f;
		}

		return saturate(d);
	}

	// P4.12 lens flare: John-Chapman-style single-pass ghosts + halo + a cheap
	// anamorphic horizontal streak, all sampled from the bloom chain top so
	// the flare is driven by the frame's actual bright sources. Screen-centre
	// symmetric, chromatically split, and radially masked so ghosts fade at
	// the frame edge the way a real lens's do.
	// Flare source: the chain through a soft display-luminance knee, so ghosts
	// and halos come from genuinely bright sources (sun disc, hot emissives)
	// rather than from every moderately lit pixel of a daylight frame. Knee in
	// display units (exposure-normalised) - the chain itself is scene-referred.
	float3 SampleFlareSource(float2 uv)
	{
		const float3 c = SampleChain(uv);
		// Judge in display units using the INSTANT exposure (histogram target,
			// unsmoothed). The smoothed multiplier lags during camera movement and
			// made bloom overshoot until adaptation settled.
			const float exposure = (g_colourGrading.exposureInstant > 1e-4f)
				? g_colourGrading.exposureInstant
				: max(g_colourGrading.exposure, 1e-4f); // fallback: binary predating exposureInstant
		const float lumaDisplay = dot(c, float3(0.2126f, 0.7152f, 0.0722f)) * exposure;
		return c * smoothstep(0.35f, 1.6f, lumaDisplay);
	}

	float3 LensFlare(float2 uv)
	{
		const float flareI = g_lensParams.x;
		if (flareI <= 0.0f)
			return 0.0f.xxx;

		const float2 toCentre = 0.5f.xx - uv;
		const float dispersal = max(g_lensParams.z, 0.01f);
		const float2 ghostVec = toCentre * dispersal;

		// Small chromatic offset along the ghost axis for coloured fringing.
		const float2 caOffset = normalize(toCentre + 1e-4f) * (2.5f * g_bloomPass.x);

		float3 ghosts = 0.0f.xxx;
		[unroll]
		for (int i = 0; i < 6; ++i)
		{
			const float2 suv = uv + ghostVec * (float)i;
			// Radial weight: bright toward centre, gone at the edges.
			const float weight = pow(1.0f - saturate(length(0.5f.xx - suv) * 2.0f), 4.0f);
			ghosts.r += SampleFlareSource(suv + caOffset).r * weight;
			ghosts.g += SampleFlareSource(suv).g * weight;
			ghosts.b += SampleFlareSource(suv - caOffset).b * weight;
		}

		// Halo: a ring at a fixed radius along the centre axis.
		const float2 haloDir = normalize(toCentre + 1e-4f) * 0.32f;
		const float2 huv = uv + haloDir;
		const float haloW = pow(1.0f - saturate(abs(length(0.5f.xx - huv) - 0.28f) / 0.28f), 4.0f);
		const float3 halo = SampleFlareSource(huv) * haloW;

		// Anamorphic streak: horizontal decaying taps of the chain top.
		float3 streak = 0.0f.xxx;
		const float streakI = g_lensParams.w;
		if (streakI > 0.0f)
		{
			[unroll]
			for (int s = 1; s <= 8; ++s)
			{
				const float w = exp(-(float)s * 0.5f);
				const float2 dx = float2((float)s * g_bloomPass.x * 6.0f, 0.0f);
				streak += (SampleFlareSource(uv + dx) + SampleFlareSource(uv - dx)) * w;
			}
			streak *= streakI;
		}

		return (ghosts + halo + streak) * flareI;
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float2 uv = input.texcoord;

		const float4 scene = g_scene.SampleLevel(g_pointSampler, uv, 0);

		const float3 bloom = SampleChain(uv);

		const float normalised = max(g_bloomPass2.x, 0.0f); // 1/levelCount
		float3 result = scene.rgb + bloom * normalised * g_bloom.bloomIntensity;

		// P4.12: lens flare, driven by the bloom chain.
		if (g_lensParams.x > 0.0f)
		{
			result += LensFlare(uv) * normalised;
		}
		// Lens dirt: smudges revealed by the BLOOM (veiling glare across the
		// whole lens), not by the flare ghosts. The old wiring multiplied the
		// flare term, which made r_lensDirt read as a second flare-intensity
		// dial and do nothing with the flare off. Bloom-driven, it brightens
		// dirt wherever any bright light blooms - independent of ghosts.
		if (g_lensParams.y > 0.0f)
		{
			// x8: the raw bloom veil is subtle by design (bloomIntensity ~0.05),
			// and the dirt pattern averages ~0.1 coverage - without a strong
			// boost r_lensDirt 1 was nearly invisible. At x8, 1.0 reads clearly
			// with a bright source in frame and the 0-4 range gives real reach.
			result += bloom * normalised * LensDirt(uv) * g_lensParams.y * 8.0f;
		}

		return float4(result, scene.a);
	}
}
