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

	cbuffer UnderwaterParams : register(b6)
	{
		// x = fog scale (multiplies r_oceanAbsorption for the view path),
		// y = distortion amplitude (uv), z = meniscus strength, w = in-scatter gain
		float4 g_underwaterP0;
	};

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
		const float2 uvRefracted = clamp(uv + wobble * (g_underwaterP0.y * mask), 0.001f.xx, 0.999f.xx);
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
