"InputLayout"
{
	Pos
}
"VertexShaderIncludes"
{
	Global
}
"PixelShaderIncludes"
{
	Global
}
"VertexShader"
{
	// The auto-puddle quad lives in clip space directly - vertex positions are
	// (-1, -1, 0)..(1, 1, 0). No transforms in the VS. Bypasses the GuiRenderer
	// fullscreen path because that path only writes to RT0 (FullScreenTexturedQuad
	// is single-target), and the auto-puddle pass needs to write both diff and
	// mat in one draw.
	struct AutoPuddleVSIn
	{
		float3 position : POSITION;
	};

	struct AutoPuddlePSIn
	{
		float4 position : SV_POSITION;
		float2 texcoord : TEXCOORD0;
	};

	AutoPuddlePSIn ShaderMain(AutoPuddleVSIn input)
	{
		AutoPuddlePSIn output;
		output.position = float4(input.position, 1.0f);
		// Map clip-space [-1, 1] to UV [0, 1]. Y is flipped because D3D's clip Y
		// is up while UV Y is down.
		output.texcoord = float2((input.position.x + 1.0f) * 0.5f,
		                         (1.0f - input.position.y) * 0.5f);
		return output;
	}
}
"PixelShader"
{
	// Auto-puddles pass: a single fullscreen draw that paints puddles into the
	// GBuffer at pixels where ALL of the following hold:
	//   1. The underlying surface is near-horizontal (normal.y > cutoff)
	//   2. A 3D-noise-driven mask exceeds a threshold (gives patchy distribution
	//      so puddles don't carpet the entire scene)
	//   3. g_weatherSurface.puddleAmount > 0 (driven by the weather system)
	//
	// Runs in the decal pass with the same render-target binding (diff + mat at
	// SV_TARGET0/1, no normal write) so puddles fall under the same shading path
	// as manual decals. Output is alpha-blended so non-puddle pixels are left
	// untouched.
	Texture2D g_positionTex : register(t0); // GBuffer position copy
	Texture2D g_normalTex   : register(t1); // GBuffer normal (RT - safe to read here since the auto-puddle pass doesn't write to it)

	SamplerState g_pointSampler : register(s2);

	cbuffer AutoPuddleConstants : register(b4)
	{
		// x = noise scale (smaller = bigger puddles; world units per noise cycle)
		// y = noise threshold (higher = sparser puddles; 0..1)
		// z = normal cutoff (cos of max surface tilt that still receives puddles)
		// w = master opacity multiplier
		float4 g_autoPuddleParams;

		// x = darken amount (multiplies surface albedo by 1 - x at full puddle)
		// y = force-rain override (max'd against g_weatherSurface.puddleAmount
		//     so debug / first-look-at-the-feature workflows can crank puddles
		//     without setting up a WeatherController)
		// z = DEBUG: solid-red bypass. When > 0.5 the PS outputs solid red on diff
		//     + bright-blue on mat at full alpha, bypassing every check. Tells us
		//     whether the pass is even running / the RT binding is valid.
		// w reserved
		float4 g_autoPuddleAppearance;
	};

	struct AutoPuddlePSIn
	{
		float4 position : SV_POSITION;
		float2 texcoord : TEXCOORD0;
	};

	struct AutoPuddleOut
	{
		float4 diff : SV_TARGET0;
		float4 mat  : SV_TARGET1;
	};

	// Cheap 3D hash-noise (iq's "smooth voronoi"-style; standard GLSL idiom).
	// Good enough for puddle distribution - we just need spatially-coherent
	// random patches at the noise-scale frequency.
	float Hash13(float3 p)
	{
		p = frac(p * 0.1031f);
		p += dot(p, p.yzx + 33.33f);
		return frac((p.x + p.y) * p.z);
	}

	float ValueNoise3(float3 p)
	{
		const float3 pi = floor(p);
		const float3 pf = frac(p);
		const float3 w  = pf * pf * (3.0f - 2.0f * pf);

		const float n000 = Hash13(pi + float3(0,0,0));
		const float n100 = Hash13(pi + float3(1,0,0));
		const float n010 = Hash13(pi + float3(0,1,0));
		const float n110 = Hash13(pi + float3(1,1,0));
		const float n001 = Hash13(pi + float3(0,0,1));
		const float n101 = Hash13(pi + float3(1,0,1));
		const float n011 = Hash13(pi + float3(0,1,1));
		const float n111 = Hash13(pi + float3(1,1,1));

		const float nx00 = lerp(n000, n100, w.x);
		const float nx10 = lerp(n010, n110, w.x);
		const float nx01 = lerp(n001, n101, w.x);
		const float nx11 = lerp(n011, n111, w.x);
		const float nxy0 = lerp(nx00, nx10, w.y);
		const float nxy1 = lerp(nx01, nx11, w.y);
		return lerp(nxy0, nxy1, w.z);
	}

	// Two-octave noise so puddle blobs have organic edges instead of being
	// perfectly grid-aligned. Cheap (8 hash evals per octave) and good enough.
	float PuddleNoise(float3 wp)
	{
		float n = ValueNoise3(wp);
		n += 0.5f * ValueNoise3(wp * 2.07f);
		return n / 1.5f;
	}

	AutoPuddleOut ShaderMain(AutoPuddlePSIn input)
	{
		AutoPuddleOut o = (AutoPuddleOut)0;

		// DEBUG: solid-red bypass for diagnosing "puddles do not show". If this
		// turns the screen red, the pass + RT bindings are good and the issue is
		// in the puddle decision logic. If the screen stays normal, the pass
		// isn't reaching the OM (no draw / wrong RTs / clobbered state).
		if (g_autoPuddleAppearance.z > 0.5f)
		{
			o.diff = float4(1.0f, 0.0f, 0.0f, 1.0f);
			o.mat  = float4(0.0f, 0.0f, 1.0f, 1.0f);
			return o;
		}

		const float4 surfacePos = g_positionTex.Sample(g_pointSampler, input.texcoord);

		// Background / unwritten pixel: position .a flags this (1 = no opaque
		// surface here, e.g. skybox). Drop these so puddles don't paint the sky.
		if (surfacePos.a > 0.5f)
		{
			o.diff = float4(0, 0, 0, 0);
			o.mat  = float4(0, 0, 0, 0);
			return o;
		}

		// Skip when the weather system says it's dry - cheap early-out before the
		// noise eval, which keeps the pass essentially free in clear weather. The
		// debug-force override (g_autoPuddleAppearance.y) raises the minimum so
		// users can preview puddles without wiring up rain in their scene.
		const float rainMul = saturate(max(g_weatherSurface.puddleAmount, g_autoPuddleAppearance.y));
		if (rainMul <= 0.0f)
		{
			o.diff = float4(0, 0, 0, 0);
			o.mat  = float4(0, 0, 0, 0);
			return o;
		}

		// Surface flatness: GBuffer normal is signed-normalised xyz + depth in .a.
		// We want puddles only on near-horizontal surfaces (floor / roads / pavement),
		// not on walls or ceilings. Soft falloff between cutoff and cutoff+0.05
		// so kerb edges blend instead of hard-clipping.
		const float3 normalWS = normalize(g_normalTex.Sample(g_pointSampler, input.texcoord).xyz);
		const float  cutoff   = g_autoPuddleParams.z;
		const float  flatness = saturate((normalWS.y - cutoff) / max(1.0f - cutoff, 0.001f));
		if (flatness <= 0.0f)
		{
			o.diff = float4(0, 0, 0, 0);
			o.mat  = float4(0, 0, 0, 0);
			return o;
		}

		// World-position-driven noise gives patchy puddle distribution stable in
		// world space (puddles don't shift around as the camera moves). XZ-only
		// so puddles spread along the ground, not stacked vertically.
		const float scale = max(g_autoPuddleParams.x, 0.001f);
		const float3 noisePos = float3(surfacePos.x, 0.0f, surfacePos.z) / scale;
		const float  noise = PuddleNoise(noisePos);
		const float  threshold = g_autoPuddleParams.y;
		const float  mask = saturate((noise - threshold) * 4.0f);
		if (mask <= 0.0f)
		{
			o.diff = float4(0, 0, 0, 0);
			o.mat  = float4(0, 0, 0, 0);
			return o;
		}

		const float alpha = saturate(mask * flatness * rainMul * g_autoPuddleParams.w);
		if (alpha <= 0.001f)
		{
			o.diff = float4(0, 0, 0, 0);
			o.mat  = float4(0, 0, 0, 0);
			return o;
		}

		// Slight albedo darken so puddles read as "wet ground" against dry. The
		// darken is multiplicative (RGB = 1 - darken) but written as a regular
		// alpha-blended diffuse - we approximate the darken by writing black
		// with the per-puddle alpha; the OM's src-alpha blend then does
		// out = black * alpha + diff * (1 - alpha) which IS a darken proportional
		// to alpha. Cheap and good enough for v1.
		const float3 darkenTo = float3(0, 0, 0);
		o.diff = float4(darkenTo, alpha * g_autoPuddleAppearance.x);

		// Mat: smooth (roughness ~0.04) and non-metallic. Smoothness is 1 - roughness.
		// Alpha matches the diff write so the puddle reads as a cohesive surface.
		const float puddleRoughness = 0.04f;
		const float puddleMetallic  = 0.0f;
		o.mat = float4(puddleMetallic, puddleRoughness, 1.0f - puddleRoughness, alpha);

		return o;
	}
}
