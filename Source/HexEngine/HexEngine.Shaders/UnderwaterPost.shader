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
	WaterCommon
	ShadowUtils
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
		output.colour = input.colour;

		return output;
	}
}
"PixelShader"
{
	// Underwater camera (underwater S1). Fullscreen, reads beauty + the opaque
	// gbuffer, runs right after the transparent pass so bloom / exposure / DoF
	// all see the result.
	//
	// THE MASK IS PER PIXEL, not a camera flag. Each pixel's point on the
	// NEAR PLANE is tested against the SAME wave function the water surface is
	// displaced by (WaterCommon::OceanSurfaceOffset - at distance 0, where the
	// tessellation band-limit is fully open, so it agrees with the rendered
	// geometry). A binary "camera is under" switch pops, and is simply wrong
	// whenever a crest cuts across the lens; this draws the wavy waterline
	// across the screen for free.
	//
	// Inside the mask: Beer-Lambert absorption along the view path with the
	// SAME spectral coefficients Water.shader uses for its see-through (so the
	// sea looks like one medium from above and from inside), in-scatter from
	// the scene's authored body colours, a gentle refraction wobble, and a
	// meniscus line along the mask edge.

	GBUFFER_RESOURCE(0, 1, 2, 3, 4);
	Texture2D g_beauty : register(t5);

	SamplerState g_pointSampler : register(s2);
	SamplerState g_linearSampler : register(s4); // WRAP - clamp UVs manually

	// Caustic atlas (same one Deferred.shader lights the seabed with) - the
	// god rays are marched through the SAME field, so a shaft in the water
	// lands on its own bright filament on the sand.
	Texture2D g_causticsAtlas : register(t6);

	// Sun cascade shadow maps at the same slots the transparent pass uses
	// (bound by RenderUnderwater together with the b2 caster constants;
	// g_underwaterP1.w says whether they are valid this frame).
	SHADOWMAPS_RESOURCE(15);
	SamplerComparisonState g_cmpSampler : register(s1);

	cbuffer UnderwaterParams : register(b6)
	{
		// x = fog scale (multiplies r_oceanAbsorption for the view path),
		// y = distortion amplitude (uv), z = meniscus strength, w = in-scatter gain
		float4 g_underwaterP0;
		// x = seconds since the camera dived (huge when not applicable),
		// y = god-ray strength (0 = off / atlas unbound), z = bubble strength,
		// w = 1 when the sun cascades + caster constants are bound
		float4 g_underwaterP1;
	};

	// Atlas layout - MIRRORS Deferred.shader (kCaustic*) and
	// Tools/BuildCausticsAtlas.py. Change all three together.
	static const float2 kCausticAtlasPx = float2(2080.0f, 1040.0f);
	static const float  kCausticCellPx = 260.0f;
	static const float  kCausticTilePx = 256.0f;
	static const float  kCausticGutterPx = 2.0f;

	float SampleCausticFrame(float2 tileUv, float frame)
	{
		const float f = fmod(frame, 32.0f);
		const float2 cell = float2(fmod(f, 8.0f), floor(f / 8.0f));
		const float2 px = cell * kCausticCellPx + kCausticGutterPx + frac(tileUv) * kCausticTilePx;
		return g_causticsAtlas.SampleLevel(g_linearSampler, px / kCausticAtlasPx, 0).r;
	}

	float Hash21(float2 p)
	{
		float3 q = frac(float3(p.xyx) * float3(0.1031f, 0.1030f, 0.0973f));
		q += dot(q, q.yzx + 33.33f);
		return frac((q.x + q.y) * q.z);
	}

	// Dive bubble burst: two layers of rising screen-space bubbles. Returns
	// the ring highlight; `refractOffset` accumulates a lens-like UV push so
	// each bubble bends the scene behind it instead of being a flat sprite.
	float DiveBubbles(float2 uv, float aspect, float seconds, inout float2 refractOffset)
	{
		float highlight = 0.0f;
		[unroll]
		for (int layer = 0; layer < 2; ++layer)
		{
			const float cols = (layer == 0) ? 9.0f : 17.0f;
			const float rise = (layer == 0) ? 0.55f : 0.85f;   // screens per second
			float2 p = float2(uv.x * aspect, uv.y) * cols;
			p.y += seconds * rise * cols;                        // bubbles travel UP the screen
			const float2 cell = floor(p);
			const float2 local = frac(p);

			const float present = Hash21(cell + 17.0f * (float)layer);
			if (present < 0.62f)
				continue;

			const float2 centre = float2(0.25f + 0.5f * Hash21(cell + 3.1f), 0.25f + 0.5f * Hash21(cell + 7.7f))
				+ float2(sin(seconds * 5.0f + present * 40.0f) * 0.06f, 0.0f); // wobble as they rise
			const float radius = lerp(0.07f, 0.20f, Hash21(cell + 11.3f));
			const float2 d = local - centre;
			const float dist = length(d);

			const float inside = 1.0f - smoothstep(radius * 0.92f, radius, dist);
			const float ring = inside * smoothstep(radius * 0.55f, radius * 0.95f, dist);
			// Bright crescent on the upper-left, like a lit air bubble.
			const float glint = inside * pow(saturate(dot(normalize(d + 1e-5f), normalize(float2(-0.6f, -0.8f)))), 6.0f);

			highlight += ring * 0.55f + glint * 0.9f;
			refractOffset += (d / max(radius, 1e-3f)) * inside * (0.012f / cols * 9.0f);
		}
		return highlight;
	}

	// Same weights as Water.shader's spectral transmission: red dies in the
	// first couple of metres, green carries several, blue longest.
	static const float3 kAbsorbRGB = float3(2.30f, 0.80f, 0.55f);

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float2 uv = input.texcoord;
		const float4 source = g_beauty.SampleLevel(g_pointSampler, uv, 0);

		if (g_oceanConfig3.w < 0.5f)
			return source;

		// This pixel's point on the near plane, and its view ray.
		const float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
		const float4 nearH = mul(float4(ndc, 0.0f, 1.0f), g_viewProjectionMatrixInverse);
		const float4 farH = mul(float4(ndc, 1.0f, 1.0f), g_viewProjectionMatrixInverse);
		const float3 nearPos = nearH.xyz / max(nearH.w, 1e-6f);
		const float3 farPos = farH.xyz / max(farH.w, 1e-6f);
		const float3 rayDir = normalize(farPos - nearPos);

		const float seaLevel = g_oceanConfig3.x;
		const float ampBound = g_oceanConfig3.y;
		const float rel = nearPos.y - seaLevel;

		// Only pixels within the wave envelope need the wave function; the
		// rest are decided by sea level alone (nothing above seaLevel + bound
		// can be wet, nothing below seaLevel - bound can be dry).
		float surfaceOffset = 0.0f;
		const bool inBand = abs(rel) <= ampBound + 0.02f;
		[branch]
		if (inBand)
		{
			float2 windDir;
			float windAlign, ampScale;
			OceanWindParams(g_weatherSurface.windDirectionAndSpeed, g_oceanConfig2.x, windDir, windAlign, ampScale);
			surfaceOffset = OceanSurfaceOffset(nearPos.xz, g_time, windDir, windAlign, ampScale, 0.0f);
		}

		// > 0 = this pixel's near-plane point is under the surface (metres).
		const float submergence = surfaceOffset - rel;

		// Anti-aliased waterline: one pixel of world-space depth either side.
		const float edgeWidth = max(fwidth(submergence), 1e-5f);
		const float mask = smoothstep(-edgeWidth, edgeWidth, submergence);
		if (mask <= 0.0f)
			return source;

		// ---- Refraction wobble -------------------------------------------
		// Small, slow, two unrelated frequencies - reads as looking through
		// moving water rather than a heat haze. Scaled by the mask so the
		// dry part of a split view stays rock steady.
		const float t = g_time;
		const float2 wobble = float2(
			sin(uv.y * 17.0f + t * 1.31f) + 0.5f * sin(uv.y * 41.0f - t * 1.87f),
			cos(uv.x * 13.0f + t * 1.13f) + 0.5f * cos(uv.x * 37.0f + t * 1.59f));
		// ---- Dive bubble burst (S5) ---------------------------------------
		// For ~2.4 s after the camera goes under: the air dragged down with
		// it, rising past the lens. Each bubble refracts the scene behind it.
		float2 bubbleOffset = float2(0.0f, 0.0f);
		float bubbleHighlight = 0.0f;
		const float diveSeconds = g_underwaterP1.x;
		if (g_underwaterP1.z > 0.0f && diveSeconds < 2.4f)
		{
			const float burst = (1.0f - smoothstep(1.1f, 2.4f, diveSeconds)) * g_underwaterP1.z;
			const float aspect = (float)g_screenWidth / max((float)g_screenHeight, 1.0f);
			bubbleHighlight = DiveBubbles(uv, aspect, diveSeconds, bubbleOffset) * burst;
			bubbleOffset *= burst;
		}

		const float2 uvRefracted = clamp(uv + (wobble * g_underwaterP0.y + bubbleOffset) * mask, 0.001f.xx, 0.999f.xx);
		const float3 scene = g_beauty.SampleLevel(g_linearSampler, uvRefracted, 0).rgb;

		// ---- Path length through water ------------------------------------
		// Opaque distance from the gbuffer view depth (normal.w; <= 0 = sky).
		const float viewDepth = GBUFFER_NORMAL.SampleLevel(g_pointSampler, uvRefracted, 0).w;
		const float cosToForward = max(dot(rayDir, normalize(g_eyeDir.xyz)), 0.05f);
		float pathLength = (viewDepth > 0.0f) ? (viewDepth / cosToForward) : 1.0e5f;

		// An upward ray leaves the water at the surface - beyond that it is
		// air, not fog. (The water surface is a transparent, it is not in the
		// gbuffer.) Plane through the local surface height: exact at the lens,
		// and the waves are small against the distances where it matters.
		if (rayDir.y > 1e-3f)
			pathLength = min(pathLength, max(submergence, 0.0f) / rayDir.y);

		// ---- Absorption + in-scatter --------------------------------------
		const float absorbK = (g_oceanConfig.reflection_pad0 > 0.0f ? g_oceanConfig.reflection_pad0 : max(g_oceanConfig2.z, 0.005f))
			* max(g_underwaterP0.x, 0.0f);
		const float3 transmission = exp(-pathLength * absorbK * kAbsorbRGB);

		// In-scatter: the same volume term Water.shader uses for the body seen
		// from above (authored shallow->deep colour, lit by sun elevation +
		// ambient - NOT by any surface normal), dimmed by how much daylight
		// survives down to the camera, and brighter looking up toward the
		// surface than down into the deep.
		const float cameraDepth = max(g_oceanConfig3.z, 0.0f);
		const float3 lightDir = -normalize(g_lightDirection.xyz);
		const float sunUp = saturate(lightDir.y);
		//
		// DIRECTIONAL, not a flat fog colour. The first version used one
		// colour for every ray and the sea read as a lit swimming pool. The
		// radiance you see along a ray is the daylight scattered INTO it, and
		// there is far more of that looking up (short path to the bright
		// surface) than level, and almost none looking down into the deep:
		// dark navy below, mid blue ahead, bright turquoise only near the
		// surface. Both the hue (deep -> shallow authored colours) and the
		// level follow the ray's elevation; camera depth then pulls the whole
		// lot toward the deep colour.
		const float up01 = saturate(rayDir.y * 0.5f + 0.5f);          // 0 = straight down, 1 = straight up
		// (exponent 1.6 resolved a level ray almost entirely to the deep
		// colour - a metre under on a sunny day read as night.)
		const float towardSurface = pow(up01, 1.15f);
		const float depthToDeep = saturate(1.0f - exp(-cameraDepth * 0.12f));
		const float3 bodyColour = lerp(g_oceanConfig.deepColour.rgb, g_oceanConfig.shallowColour.rgb,
			towardSurface * (1.0f - 0.65f * depthToDeep));
		const float3 downwelling = exp(-cameraDepth * absorbK * 0.55f * kAbsorbRGB);
		const float lookUp = lerp(0.45f, 2.4f, towardSurface);
		const float3 inscatter = bodyColour
			* (0.40f * sunUp * g_globalLight[0] + g_atmosphere.ambientLight.rgb * 0.35f)
			* downwelling * lookUp * g_underwaterP0.w;

		float3 underwater = scene * transmission + inscatter * (1.0f - transmission);

		// ---- God rays (S5) -------------------------------------------------
		// Light shafts are the caustic net seen SIDEWAYS: the same focusing
		// that draws bright filaments on the sand lights up the water column
		// above them. March the view ray, and at each step walk back up the
		// REFRACTED sun ray to the surface and read the caustic field there -
		// the identical projection Deferred.shader uses for the seabed, so a
		// shaft in the water ends on its own bright patch of sand. One layer
		// at a coarser scale (broad shafts, not a fine net), jittered per
		// pixel + frame so TAA resolves the ten steps into a smooth volume.
		if (g_underwaterP1.y > 0.0f && sunUp > 0.02f)
		{
			const float3 sunInWater = refract(-lightDir, float3(0.0f, 1.0f, 0.0f), 1.0f / 1.333f);
			const float marchLength = min(pathLength, 26.0f);
			const float2 drift = g_timeParams2.zw * 64.0f;
			const float tileMetres = max(g_oceanConfig4.y, 0.25f) * 1.7f;
			const float frameTime = g_time * g_oceanConfig4.z * 0.8f;
			const float frame0 = floor(frameTime);
			const float frameBlend = frameTime - frame0;
			const float surfaceY = seaLevel + surfaceOffset;

			// INTERLEAVED GRADIENT NOISE for the step offset, not a white hash.
			// White noise gives neighbouring pixels unrelated offsets, so the
			// banding it is meant to hide comes straight back as GRAIN (the
			// first version: visibly stippled shafts). IGN is built so every
			// small neighbourhood covers [0,1) evenly - the error is pushed to
			// a high, regular frequency the eye and TAA both integrate away -
			// and the per-frame shift (golden-ratio stride over a 64-frame
			// cycle) makes successive frames sample the gaps between steps.
			const int kSteps = 16;
			const float2 ignPos = input.position.xy + 5.588238f * (float)(g_frame % 64u);
			const float jitter = frac(52.9829189f * frac(dot(ignPos, float2(0.06711056f, 0.00583715f))));
			float shaft = 0.0f;
			[loop]
			for (int i = 0; i < kSteps; ++i)
			{
				const float t = ((float)i + jitter) / (float)kSteps * marchLength;
				const float3 p = nearPos + rayDir * t;
				const float depthHere = surfaceY - p.y;
				if (depthHere <= 0.0f)
					continue;

				const float2 surfaceXZ = p.xz - sunInWater.xz * (depthHere / max(-sunInWater.y, 0.2f));
				const float2 tileUv = (surfaceXZ - drift * 0.9f) / tileMetres;
				const float c = lerp(SampleCausticFrame(tileUv, frame0), SampleCausticFrame(tileUv, frame0 + 1.0f), frameBlend);
				const float beam = saturate((c - 0.50f) / 0.50f);

				// SHADOWED: a shaft is sunlight, so it stops under a pier, a
				// hull or a cliff exactly as the caustics on the seabed below
				// it do (those go through the deferred sun term, which is
				// already shadowed). Without this the rays shone straight
				// through anything standing in the water. The cascades are
				// rendered along the straight sun direction while the light
				// under water is refracted; the offset over a few metres of
				// depth is small, and it is the same approximation the seabed's
				// own shadows make, so the two stay consistent.
				float sunVisible = 1.0f;
				if (g_underwaterP1.w > 0.5f)
					sunVisible = CalculateShadowsCheapPCF(p, g_cmpSampler, SHADOWMAPS, g_shadowConfig.biasMultiplier);

				// Light reaching this point (down the sun ray) x light reaching
				// the eye from it (back along the view ray). Cubed: shafts are
				// the BRIGHT filaments only; the cells between must stay dark
				// or the volume just reads as brighter fog.
				shaft += beam * beam * beam * sunVisible * exp(-(depthHere * 0.8f + t * 0.9f) * absorbK);
			}
			shaft *= marchLength / (float)kSteps;

			// Strongly forward-scattering: shafts blaze looking up-sun and all
			// but vanish looking away from it.
			const float towardSun = saturate(dot(rayDir, -sunInWater));
			const float phase = 0.12f + 0.88f * towardSun * towardSun * towardSun;
			underwater += g_oceanConfig.shallowColour.rgb * (0.40f * sunUp * g_globalLight[0])
				* (shaft * phase * 5.0f * g_underwaterP1.y); // (0.16 was invisible - first capture)
		}

		// Bubble highlights sit on top of everything in the water.
		underwater += inscatter * (bubbleHighlight * 2.5f) + bubbleHighlight * 0.04f;

		float3 result = lerp(source.rgb, underwater, mask);

		// ---- Meniscus ------------------------------------------------------
		// Surface tension pulls the water up the lens: a dark line hugging
		// the waterline with a faint bright lip on its dry side. Only drawn
		// where the wave function was actually evaluated.
		if (inBand)
		{
			// Widths are in 1080p pixels and scale with the target, or the
			// line vanishes at 4K (first capture: invisible).
			const float pxScale = max((float)g_screenHeight / 1080.0f, 0.5f);
			const float distPx = abs(submergence) / (edgeWidth * pxScale);
			const float darkLine = (1.0f - smoothstep(1.5f, 5.0f, distPx)) * g_underwaterP0.z;
			const float lip = (1.0f - smoothstep(4.0f, 9.0f, distPx)) * step(submergence, 0.0f) * g_underwaterP0.z;
			result *= lerp(1.0f, 0.45f, saturate(darkLine));
			result += inscatter * (0.35f * saturate(lip));
		}

		return float4(result, source.a);
	}
}
