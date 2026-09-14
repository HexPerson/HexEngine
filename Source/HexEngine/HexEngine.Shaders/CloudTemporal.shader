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

	Texture2D g_cloudCurrent : register(t0);
	Texture2D g_cloudHistory : register(t1);
	Texture2D g_gbufferDiffuse : register(t2); // .a == -1 marks sky
	SamplerState g_pointSampler : register(s2);
	SamplerState g_linearSampler : register(s4); // WRAP in-frame: clamp UVs manually

	cbuffer CloudTemporalParams : register(b6)
	{
		// x = history weight (0 = history invalid, take current), y = 1/width,
		// z = 1/height (of the cloud buffer), w = unused.
		float4 g_cloudTemporalParams;
	};

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float2 uv = input.texcoord;
		const float4 current = g_cloudCurrent.SampleLevel(g_pointSampler, uv, 0);

		// Geometry pixels carry no clouds (the march writes 0 there). Never
		// let history leak clouds onto them: a turning camera would otherwise
		// ghost the deck across building silhouettes for a few frames.
		const float skyFlag = g_gbufferDiffuse.SampleLevel(g_pointSampler, uv, 0).a;
		if (skyFlag > -0.5f)
			return 0.0f.xxxx;

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

		const float2 halfTexel = 0.5f * g_cloudTemporalParams.yz;
		float4 history = g_cloudHistory.SampleLevel(g_linearSampler, clamp(prevUv, halfTexel, 1.0f - halfTexel), 0);

		// Neighbourhood clamp (TAA anti-ghosting). The reprojection is
		// rotation-only, so it is correct for the distant clouds but NOT for
		// where foreground geometry (lamp posts, building edges) disoccludes
		// the sky as the camera moves - there the reprojected history is stale
		// and dragged the object's silhouette through the cloud layer as a
		// vertical smear. Clamp the history to the range of the current cloud
		// in a 3x3 neighbourhood so a just-revealed sky pixel snaps to its true
		// value instead of trailing the geometry that used to cover it.
		const float2 texel = g_cloudTemporalParams.yz;
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
				const float4 s = g_cloudCurrent.SampleLevel(g_pointSampler, uv + float2(dx, dy) * texel, 0);
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
