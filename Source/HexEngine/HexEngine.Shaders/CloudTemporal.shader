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
	// Cloud temporal accumulation. The half-res cloud march jitters its ray
	// start every frame; without accumulation that jitter IS the boiling
	// seen under camera motion. This pass reprojects last frame's result
	// and EMA-blends it with the new one, turning the jitter into
	// supersampling.
	//
	// Reprojection is ROTATION-ONLY: clouds sit kilometres away, so camera
	// translation moves them by a fraction of a texel per frame, while a
	// turning camera moves them by the full rotation. Reprojecting the view
	// DIRECTION (w = 0 drops the translation rows) through the previous
	// view-projection is exactly the treatment the sky dome uses for its
	// own velocity, and needs no depth.
	//
	// GEOMETRY TEXELS ARE NOT CLOUD DATA. They are written as the sentinel
	// (0,0,0,-1) and excluded from every history / neighbourhood fetch.
	// They used to be written as plain 0 - indistinguishable from "clear
	// sky, no cloud" - and the history was fetched with a hardware bilinear
	// tap at the (TAA-jittered, so never texel-centred) reprojected UV. Every
	// frame each sky texel touching a silhouette therefore blended in a
	// share of its zero-alpha geometry neighbours, and the EMA compounded it
	// (alpha settles near a third of its true value with a 25% leak at
	// 0.88 history weight). The neighbourhood clamp could not undo it
	// because its min also included those zeros. Upsampled, that is the
	// bright halo hugging every foliage / branch / roofline silhouette
	// against the cloud deck: the cloudless dome showing through.
	// BilateralUpsample clamps the sentinel back to 0 before using a tap.

	Texture2D g_cloudCurrent : register(t0);
	Texture2D g_cloudHistory : register(t1);
	Texture2D g_gbufferDiffuse : register(t2); // .a == -1 marks sky (FULL res)
	SamplerState g_pointSampler : register(s2);
	SamplerState g_linearSampler : register(s4); // unused (manual masked bilinear below)

	cbuffer CloudTemporalParams : register(b6)
	{
		// x = history weight (0 = history invalid, take current), y = 1/width,
		// z = 1/height (of the cloud buffer), w = unused.
		float4 g_cloudTemporalParams;
	};

	// Sky test for a HALF-res texel. The march samples the full-res gbuffer
	// at the half-res texel centre, which lands on full-res pixel 2c+1 -
	// BilateralUpsample tests the same pixel, so all three passes agree on
	// which texels are sky.
	bool IsSkyTexel(int2 halfCoord)
	{
		return g_gbufferDiffuse.Load(int3(halfCoord * 2 + int2(1, 1), 0)).a < -0.5f;
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float2 uv = input.texcoord;
		const int2 coord = int2(input.position.xy);
		const float4 current = g_cloudCurrent.Load(int3(coord, 0));

		// Geometry pixels carry no clouds. Never let history leak clouds onto
		// them (a turning camera would otherwise ghost the deck across
		// building silhouettes), and mark them so neither this pass next
		// frame nor the upsample mistakes them for clear sky.
		if (!IsSkyTexel(coord))
			return float4(0.0f, 0.0f, 0.0f, -1.0f);

		const float histWeight = saturate(g_cloudTemporalParams.x);
		if (histWeight <= 0.0f)
			return current;

		// View direction for this pixel, reprojected into last frame.
		const float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
		const float4 farH = mul(float4(ndc, 1.0f, 1.0f), g_viewProjectionMatrixInverse);
		const float3 dir = normalize(farH.xyz / max(farH.w, 1e-6f) - g_eyePos.xyz);
		const float4 prevClip = mul(float4(dir, 0.0f), g_viewProjectionMatrixPrev);
		if (prevClip.w <= 1e-4f)
			return current;
		const float2 prevNdc = prevClip.xy / prevClip.w;
		const float2 prevUv = float2(prevNdc.x * 0.5f + 0.5f, 0.5f - prevNdc.y * 0.5f);
		if (any(prevUv < 0.0f) || any(prevUv > 1.0f))
			return current;

		// Masked bilinear history fetch: the four surrounding history texels,
		// bilinear-weighted, with last frame's geometry texels (sentinel
		// alpha) dropped and the remaining weights renormalised.
		uint histW, histH;
		g_cloudHistory.GetDimensions(histW, histH);
		const int2 histMax = int2((int)histW - 1, (int)histH - 1);
		// uv is normalised to the half-res VIEWPORT (width*0.5 x height*0.5,
		// e.g. 1035.5 rows for a 2071-row screen), not to the integer texture
		// size - map back through the same extent (1 / params.yz) or the fetch
		// drifts up to half a texel toward the top-left across the screen.
		const float2 histPos = prevUv / g_cloudTemporalParams.yz - 0.5f;
		const int2 base = int2(floor(histPos));
		const float2 f = histPos - float2(base);
		const float bilinear[4] = { (1.0f - f.x) * (1.0f - f.y), f.x * (1.0f - f.y), (1.0f - f.x) * f.y, f.x * f.y };
		const int2 tapOffsets[4] = { int2(0, 0), int2(1, 0), int2(0, 1), int2(1, 1) };

		float4 history = 0.0f.xxxx;
		float historyWeight = 0.0f;
		[unroll]
		for (int t = 0; t < 4; ++t)
		{
			const int2 tc = clamp(base + tapOffsets[t], int2(0, 0), histMax);
			const float4 h = g_cloudHistory.Load(int3(tc, 0));
			const float w = (h.a > -0.5f) ? bilinear[t] : 0.0f;
			history += h * w;
			historyWeight += w;
		}

		// Every tap was geometry last frame: this sky texel was just
		// disoccluded, there is no history for it.
		if (historyWeight <= 1e-3f)
			return current;
		history /= historyWeight;

		// Neighbourhood clamp (TAA anti-ghosting). The reprojection is
		// rotation-only, so it is correct for the distant clouds but NOT for
		// where foreground geometry (lamp posts, building edges) disoccludes
		// the sky as the camera moves - there the reprojected history is stale
		// and dragged the object's silhouette through the cloud layer as a
		// vertical smear. Clamp the history to the range of the current cloud
		// in a 3x3 neighbourhood so a just-revealed sky pixel snaps to its true
		// value instead of trailing the geometry that used to cover it.
		// SKY neighbours only: a geometry neighbour's value is not cloud and
		// would drag the box minimum to zero along every silhouette.
		float4 nmin = current;
		float4 nmax = current;
		[unroll]
		for (int dy = -1; dy <= 1; ++dy)
		{
			[unroll]
			for (int dx = -1; dx <= 1; ++dx)
			{
				if (dx == 0 && dy == 0)
					continue;
				const int2 nc = coord + int2(dx, dy);
				if (any(nc < int2(0, 0)) || any(nc > histMax) || !IsSkyTexel(nc))
					continue;
				const float4 s = g_cloudCurrent.Load(int3(nc, 0));
				nmin = min(nmin, s);
				nmax = max(nmax, s);
			}
		}
		history = clamp(history, nmin, nmax);

		// Fade the history weight out toward the screen edge: the reprojected
		// sample there is a stretched border texel, not a real neighbour.
		const float2 edge = min(prevUv, 1.0f - prevUv);
		const float edgeFade = smoothstep(0.0f, 0.04f, min(edge.x, edge.y));

		return lerp(current, history, histWeight * edgeFade);
	}
}
