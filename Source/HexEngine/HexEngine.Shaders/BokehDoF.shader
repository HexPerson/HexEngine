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
		output.colour = input.colour;
		return output;
	}
}
"PixelShader"
{
	// Bokeh depth-of-field, single-pass. Reads the lit/tonemapped beauty + the
	// gbuffer normal/depth RT (view-space depth in .w), computes a per-pixel
	// Circle-of-Confusion (CoC) radius from focus distance + aperture, and
	// gathers a Vogel-disk of samples weighted to avoid sharp-foreground/blurred-
	// background colour bleed.
	//
	// The bokeh "shape" comes from the Vogel disk sample placement: uniform 2D
	// coverage with a golden-angle spiral. With 32 taps the gathered set
	// approximates a disc kernel - i.e. circular bokeh balls, the typical
	// large-aperture cinema look. For hexagonal / octagonal iris bokeh you'd
	// swap the sample placement for a polygon iterator, but circular looks
	// good for typical photographic / cinematic use.
	//
	// Inputs:
	//   t0 = source colour (post-tonemap beauty)
	//   t1 = gbuffer normal/depth (.w = view-space depth in metres)
	//
	// Cbuffer at b6:
	//   .x = focus distance (m)
	//   .y = focus range (m, fully sharp inside this band)
	//   .z = aperture / blur scale - bigger = wider DoF blur
	//   .w = max CoC radius in pixels (clamp so far blur doesn't explode)
	Texture2D g_source : register(t0);
	Texture2D g_normalDepth : register(t1);
	SamplerState g_linearSampler : register(s0);

	cbuffer DofParams : register(b6)
	{
		float4 g_dofParams;
	};

	// Compute the Circle of Confusion (signed-style) for a view-space depth.
	// Returns a value in [-1, 1] where:
	//   negative = closer than focus -> "near" out-of-focus (CoC magnitude)
	//   zero     = in focus
	//   positive = farther than focus -> "far" out-of-focus
	//
	// Curve is hyperbolic: reach=(signedDelta/focusDistance)*aperture, then
	// coc = reach/(1+reach). This asymptotes to 1.0 at "infinity" rather than
	// snapping to 1.0 immediately past the focus band. With the old saturate
	// form, a pixel 2x focusDistance past the band would already hit max blur
	// (aperture=1 -> coc=1 at signedDelta=focusDistance), so the entire mid-
	// to-far field rendered fully-blurred and the gather averaged most of the
	// image to a uniform grey. The hyperbolic curve keeps that pixel at ~50%
	// blur and only approaches max at 10x+ focus distance, which matches the
	// out-of-focus falloff a real lens produces and keeps the gathered colour
	// localized to nearby pixels rather than the entire screen.
	float ComputeCoC(float depthMetres, float focusDistance, float focusRange, float aperture)
	{
		const float signedDelta = depthMetres - focusDistance;
		const float magnitude = max(abs(signedDelta) - focusRange * 0.5f, 0.0f);
		const float divisor = max(focusDistance, 0.5f);
		const float reach = (magnitude / divisor) * aperture;
		const float coc = reach / (1.0f + reach);
		return sign(signedDelta) * coc;
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float2 uv = input.texcoord;
		const float4 centre = g_source.Sample(g_linearSampler, uv);
		const float centreDepth = g_normalDepth.Sample(g_linearSampler, uv).w;

		// Sky / unwritten pixels (depth <= 0) get no blur - they're at infinity
		// and the lens has already drawn them. Without this guard a pixel with
		// depth 0 would compute CoC against focusDistance and either be fully
		// sharp or fully blurred based on how the user authored focus.
		if (centreDepth <= 0.0f)
			return centre;

		const float focusDistance = max(g_dofParams.x, 0.1f);
		const float focusRange = max(g_dofParams.y, 0.0f);
		const float aperture = max(g_dofParams.z, 0.0f);
		const float maxCocPixels = max(g_dofParams.w, 0.0f);

		const float centreCoC = ComputeCoC(centreDepth, focusDistance, focusRange, aperture);
		const float centreCoCMagnitude = abs(centreCoC);

		// Bail when sharp - we save the 32-tap cost on the dominant portion of
		// the image (inside the depth-of-field band).
		if (centreCoCMagnitude < 0.005f || aperture <= 0.0001f)
			return centre;

		const float pixelRadius = centreCoCMagnitude * maxCocPixels;
		if (pixelRadius < 0.5f)
			return centre;

		const float2 invRes = float2(1.0f / (float)g_screenWidth, 1.0f / (float)g_screenHeight);

		// Vogel disk taps. The golden angle gives uniform 2D coverage with no
		// concentric ringing. 32 taps is a good compromise between cost and
		// bokeh smoothness; halving it (16) leaves visible spiral noise on
		// high-contrast highlights.
		const int kTapCount = 32;
		const float kGoldenAngle = 2.39996323f; // (3 - sqrt(5)) * PI
		const float jitter = frac(sin(dot(uv, float2(12.9898f, 78.233f))) * 43758.5453f);

		float3 colourSum = centre.rgb;
		float weightSum = 1.0f;

		[unroll]
		for (int i = 0; i < kTapCount; ++i)
		{
			const float t = ((float)i + 0.5f) / (float)kTapCount;
			const float r = sqrt(t); // sqrt for area-uniform disk
			const float theta = (float)i * kGoldenAngle + jitter * 6.28318f;
			const float2 offset = float2(cos(theta), sin(theta)) * r * pixelRadius * invRes;

			const float2 sampleUV = uv + offset;
			const float4 sampleColour = g_source.SampleLevel(g_linearSampler, sampleUV, 0);
			const float sampleDepth = g_normalDepth.SampleLevel(g_linearSampler, sampleUV, 0).w;
			if (sampleDepth <= 0.0f)
				continue;

			const float sampleCoC = ComputeCoC(sampleDepth, focusDistance, focusRange, aperture);

			// Sample weight is the sample's own CoC magnitude. This is the key
			// rule that gives bokeh its "highlight bloom" look: bright sharp
			// pixels contribute almost nothing to nearby blurred pixels (they'd
			// pollute the bokeh), but bright blurred pixels contribute strongly
			// to other blurred pixels (the classic bokeh disc).
			//
			// Also: clamp the weight at the centre's CoC magnitude when the
			// sample is sharper than the centre - prevents bleeding sharp
			// background onto a foreground that should be blurred.
			float weight = saturate(abs(sampleCoC));
			weight = min(weight, centreCoCMagnitude + 0.05f);
			// Foreground samples (negative CoC, in front of focus) should NOT
			// contribute their colour to background blur - they'd halo behind
			// foreground objects. Gate by matching CoC sign on samples that lie
			// at substantially different depths.
			const float depthDelta = abs(sampleDepth - centreDepth);
			if (depthDelta > 0.5f && sign(sampleCoC) != sign(centreCoC))
			{
				weight *= 0.05f;
			}

			colourSum += sampleColour.rgb * weight;
			weightSum += weight;
		}

		const float3 blurred = colourSum / max(weightSum, 0.0001f);
		// Lerp by CoC magnitude directly (no 1.5x amplification) so the
		// blend curve matches the CoC curve. The previous 1.5x meant a pixel
		// with coc=0.67 was already 100% blurred, so even slightly-out-of-
		// focus regions got the full-radius gather averaged colour.
		const float blendFactor = saturate(centreCoCMagnitude);
		return float4(lerp(centre.rgb, blurred, blendFactor), centre.a);
	}
}
