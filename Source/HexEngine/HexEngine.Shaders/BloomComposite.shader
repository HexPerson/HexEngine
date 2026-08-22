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

	// Cheap procedural lens-dirt: a few smudge lobes from hashed centres, so
	// the effect ships without an authored dirt texture. Static in screen
	// space (a real smudged lens), modulated by flare brightness at the call
	// site so it only shows where light actually hits it.
	float LensDirt(float2 uv)
	{
		float d = 0.0f;
		[unroll]
		for (int i = 0; i < 6; ++i)
		{
			const float fi = (float)i;
			const float2 c = float2(frac(sin(fi * 12.9898f) * 43758.5453f),
			                        frac(sin(fi * 78.233f) * 24634.6345f));
			const float rad = 0.12f + 0.10f * frac(sin(fi * 3.7f) * 1000.0f);
			const float2 dd = (uv - c) / rad;
			d += exp(-dot(dd, dd)) * (0.4f + 0.6f * frac(fi * 0.37f));
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
		const float exposure = max(g_colourGrading.exposure, 1e-4f);
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

		// P4.12: lens flare + dirt, driven by the bloom chain. Dirt modulates
		// the flare so smudges only glow where light lands on them.
		const float3 flare = LensFlare(uv);
		if (g_lensParams.x > 0.0f)
		{
			float3 dirtied = flare;
			if (g_lensParams.y > 0.0f)
				dirtied += flare * LensDirt(uv) * g_lensParams.y * 3.0f;
			result += dirtied * normalised;
		}

		return float4(result, scene.a);
	}
}
