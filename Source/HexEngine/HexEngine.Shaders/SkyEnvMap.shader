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
	AtmosphereCommon
	EnvMapCommon
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
	// Prefiltered sky environment atlas (IBL step 1).
	//
	// One fullscreen draw over the 128 x (128*ENVMAP_ROUGHNESS_ROWS) octahedral
	// atlas (see EnvMapCommon). Each output texel decodes to a direction,
	// GGX-prefilters the Hillaire sky-view LUT around it at the row's roughness,
	// and writes prefiltered radiance. Row 0 (mirror) is a direct LUT sample.
	//
	// This regenerates every frame by design: the day/night cycle moves the sun
	// continuously, the overcast/weather tint changes the LUT, and the draw is
	// tiny (128x640 texels sampling a 192x108 LUT). Caching on sun direction
	// would save well under half a millisecond and buy a set of invalidation
	// bugs (weather, time-of-day, teleported cameras at different altitudes).
	//
	// Split-sum convention (Karis 2013): this is the pre-integrated incoming
	// radiance half. The BRDF half (EnvBRDF) is applied by the consumer at
	// resolve time, not here.

	Texture2D g_skyViewLut : register(t0); // bound as the fullscreen quad's source texture
	SamplerState g_linearSampler : register(s4);

	static const uint kPrefilterSampleCount = 256u;

	float3 SampleSky(float3 dir)
	{
		const float3 sunDir = normalize(-g_lightDirection.xyz);
		const float2 uv = SkyViewLutParamsToUv(normalize(dir), sunDir);
		float3 sky = g_skyViewLut.SampleLevel(g_linearSampler, uv, 0).rgb;

		// Weather overcast tint - the same lerp SkySphere.shader applies on top
		// of the LUT.
		//
		// The header above says "the overcast/weather tint changes the LUT". It
		// does not: Hillaire's model is a CLEAR-SKY model and cannot produce a
		// flat grey overcast, so the tint is applied by the sky sphere after
		// sampling. Prefiltering the raw LUT therefore built an environment that
		// disagreed with the sky the player could see - a wet road mirroring
		// clear blue under a grey-brown storm.
		//
		// Applied per SAMPLE rather than to the final result so the mirror row
		// and every GGX-integrated row get it identically.
		sky = lerp(sky, g_skyOvercast.rgb, saturate(g_skyOvercast.w));

		return sky;
	}

	float2 Hammersley(uint i, uint count)
	{
		// Van der Corput radical inverse, base 2.
		uint bits = i;
		bits = (bits << 16u) | (bits >> 16u);
		bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
		bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
		bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
		bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
		return float2((float)i / (float)count, (float)bits * 2.3283064365386963e-10f);
	}

	float3 ImportanceSampleGGX(float2 xi, float roughness, float3 N)
	{
		// Disney/UE4 convention: alpha = roughness^2.
		const float a = roughness * roughness;

		const float phi = 2.0f * 3.14159265f * xi.x;
		const float cosTheta = sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
		const float sinTheta = sqrt(1.0f - cosTheta * cosTheta);

		const float3 h = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

		// Tangent frame around N.
		const float3 up = abs(N.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
		const float3 tangentX = normalize(cross(up, N));
		const float3 tangentY = cross(N, tangentX);

		return tangentX * h.x + tangentY * h.y + N * h.z;
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		// Which roughness row is this texel in, and where within the row?
		const float rowsF = ENVMAP_ROUGHNESS_ROWS;
		const float rowIdx = floor(input.texcoord.y * rowsF);
		const float2 innerUv = float2(input.texcoord.x, frac(input.texcoord.y * rowsF));

		const float3 N = OctDecodeDir(innerUv);
		const float roughness = rowIdx / (rowsF - 1.0f);

		// TEMPORAL ACCUMULATION: the C++ side draws this quad with src-alpha
		// blending, so the alpha we return is the per-row EMA rate:
		// atlas = lerp(atlas, thisFrameEstimate, alpha). Per-frame rotation of
		// the sample set (below) makes successive estimates independent, so
		// the rough rows integrate ~1/alpha frames' worth of samples - the
		// in-frame Monte-carlo variance that TAA could not absorb (its
		// neighbourhood clamp rejects high-amplitude flicker) averages out
		// HERE, in the atlas, before any consumer sees it. The mirror row
		// writes alpha 1 (plain overwrite) so sun/weather changes stay
		// frame-exact where the content is cheap to compute exactly.

		// Mirror row: the LUT already is the radiance in this direction.
		if (rowIdx < 0.5f)
			return float4(SampleSky(N), 1.0f);

		// N = V = R approximation (Karis split-sum prefilter).
		float3 accum = 0.0f.xxx;
		float weight = 0.0f;

		// Per-texel Cranley-Patterson rotation of the sample set's azimuth.
		// A fixed Hammersley set shared by every texel turns the LUT's thin,
		// bright horizon band into concentric rings in the prefiltered rows
		// (verified in the first capture of this atlas); rotating the set per
		// texel decorrelates neighbours, so the same error shows up as fine
		// noise instead.
		//
		// The rotation must ALSO vary per frame: a fixed seed freezes the
		// residual Monte-carlo variance into the atlas, and because a flat
		// receiver's reflection vectors sweep only a few atlas texels, that
		// frozen per-texel noise gets bilinearly magnified into large smooth
		// blotches crawling across floors (worst in shadow, where the added
		// sky term dominates local contrast). The atlas regenerates every
		// frame anyway, so a golden-ratio frame offset makes the error a
		// zero-mean temporal dither that TAA integrates away.
		const float2 pix = input.position.xy;
		const float azimuthRotation = frac(
			sin(dot(pix, float2(12.9898f, 78.233f))) * 43758.5453f
			+ (float)(g_frame & 1023u) * 0.6180339887f);

		[loop]
		for (uint i = 0u; i < kPrefilterSampleCount; ++i)
		{
			float2 xi = Hammersley(i, kPrefilterSampleCount);
			xi.x = frac(xi.x + azimuthRotation);
			const float3 h = ImportanceSampleGGX(xi, roughness, N);
			const float3 l = normalize(2.0f * dot(N, h) * h - N);

			const float NoL = dot(N, l);
			if (NoL > 0.0f)
			{
				accum += SampleSky(l) * NoL;
				weight += NoL;
			}
		}

		// Sanitise before it enters the EMA: a single NaN/inf would otherwise
		// persist in the accumulator forever (NaN * (1-a) + x is still NaN).
		const float3 estimate = clamp(accum / max(weight, 1e-4f), 0.0f.xxx, 65504.0f.xxx);

		// EMA rate halves per row: 1/2, 1/4, 1/8, 1/16 - the wider the lobe
		// (and so the higher the per-frame variance), the longer the window.
		// Row 4 integrates ~16 frames: variance drops ~5x on top of the
		// in-frame sample count, and a weather retint still converges in
		// about a quarter second.
		return float4(estimate, exp2(-rowIdx));
	}
}
