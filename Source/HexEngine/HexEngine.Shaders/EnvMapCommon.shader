"GlobalIncludes"
{
	Global
}
"Global"
{
	// Shared helpers for the IBL environment atlas.
	//
	// The engine's texture API exposes one implicit RTV/SRV per texture - no
	// per-face render targets, no per-mip UAVs, no subresource copies - so a
	// TextureCube with a roughness mip chain isn't reachable without extending
	// the plugin interface. Instead the environment is stored as an OCTAHEDRAL
	// ATLAS: a plain Texture2D of ENVMAP_FACE_SIZE width and
	// ENVMAP_FACE_SIZE * ENVMAP_ROUGHNESS_ROWS height, where each 128px row is
	// a full-sphere octahedral map prefiltered at one roughness level
	// (row r => roughness r / (rows-1)). One fullscreen draw fills the whole
	// atlas; sampling is two taps + a lerp across the two nearest rows.
	//
	// Octahedral projection is seam-free enough for this use: radiance stored
	// here is low-frequency (prefiltered sky today, prefiltered probe captures
	// later), so the diagonal fold seams are invisible. The engine is Y-up;
	// the standard octahedral formulas below are written for Z-up, so
	// directions are swizzled (x, z, y) on the way in and out.

	static const float ENVMAP_FACE_SIZE      = 128.0f;
	static const float ENVMAP_ROUGHNESS_ROWS = 5.0f;

	// Direction (normalized, Y-up) -> octahedral UV in [0,1]^2.
	float2 OctEncodeDir(float3 dirYUp)
	{
		// To Z-up octahedral space.
		const float3 d = float3(dirYUp.x, dirYUp.z, dirYUp.y);

		const float invL1 = 1.0f / (abs(d.x) + abs(d.y) + abs(d.z));
		float2 p = d.xy * invL1;

		if (d.z < 0.0f)
		{
			// Fold the lower hemisphere outward. sign() is avoided because
			// sign(0) = 0 would collapse texels on the axes.
			const float sx = (p.x >= 0.0f) ? 1.0f : -1.0f;
			const float sy = (p.y >= 0.0f) ? 1.0f : -1.0f;
			p = (1.0f - abs(p.yx)) * float2(sx, sy);
		}

		return p * 0.5f + 0.5f;
	}

	// Octahedral UV in [0,1]^2 -> direction (normalized, Y-up).
	float3 OctDecodeDir(float2 uv)
	{
		const float2 p = uv * 2.0f - 1.0f;
		float3 d = float3(p.x, p.y, 1.0f - abs(p.x) - abs(p.y));

		if (d.z < 0.0f)
		{
			const float sx = (d.x >= 0.0f) ? 1.0f : -1.0f;
			const float sy = (d.y >= 0.0f) ? 1.0f : -1.0f;
			d.xy = (1.0f - abs(d.yx)) * float2(sx, sy);
		}

		// Back to Y-up.
		return normalize(float3(d.x, d.z, d.y));
	}

	// ---- Spherical-harmonic irradiance (L2, 9 coefficients) --------------------
	//
	// A GGX roughness-1 prefilter is NOT a diffuse irradiance integral - it's a
	// specular lobe pushed wide, and using it as diffuse produced a flat wash that
	// erased interior contrast. Proper Lambertian irradiance is the radiance
	// convolved with a clamped cosine, which order-2 SH captures almost exactly
	// (Ramamoorthi & Hanrahan 2001: 9 coefficients, ~1% error for diffuse).
	//
	// Layout: 9 float4 rows in a 1 x 9 texture, .rgb = coefficient, .a unused.

	static const float ENVMAP_SH_COEFFS = 9.0f;

	// Evaluate the 9 SH basis functions for a direction.
	void ShBasis(float3 d, out float basis[9])
	{
		basis[0] = 0.282095f;                       // L0
		basis[1] = 0.488603f * d.y;                 // L1
		basis[2] = 0.488603f * d.z;
		basis[3] = 0.488603f * d.x;
		basis[4] = 1.092548f * d.x * d.y;           // L2
		basis[5] = 1.092548f * d.y * d.z;
		basis[6] = 0.315392f * (3.0f * d.z * d.z - 1.0f);
		basis[7] = 1.092548f * d.x * d.z;
		basis[8] = 0.546274f * (d.x * d.x - d.y * d.y);
	}

	// Reconstruct cosine-convolved irradiance from SH coefficients along a normal.
	// The A-hat factors are the Lambertian convolution weights; the 1/PI turns
	// irradiance into outgoing radiance for a white Lambertian surface, so the
	// caller multiplies by albedo only.
	float3 ShIrradiance(Texture2D shTex, SamplerState samp, float3 N)
	{
		float basis[9];
		ShBasis(normalize(N), basis);

		// Ramamoorthi's convolution coefficients per band.
		const float a0 = 3.141593f;   // PI
		const float a1 = 2.094395f;   // 2PI/3
		const float a2 = 0.785398f;   // PI/4
		const float bandA[9] = { a0, a1, a1, a1, a2, a2, a2, a2, a2 };

		float3 irradiance = 0.0f.xxx;
		[unroll]
		for (int i = 0; i < 9; ++i)
		{
			const float v = ((float)i + 0.5f) / ENVMAP_SH_COEFFS;
			const float3 c = shTex.SampleLevel(samp, float2(0.5f, v), 0).rgb;
			irradiance += c * basis[i] * bandA[i];
		}

		return max(irradiance, 0.0f.xxx) / 3.141593f;
	}
	// ----------------------------------------------------------------------------

	// Sample the atlas for a direction at a roughness in [0,1]. Two taps, one
	// per neighbouring roughness row, lerped. The inner V is clamped a texel
	// inside the row so hardware bilinear can't pull taps from the adjacent
	// roughness level; the sub-texel of direction resolution this costs at the
	// atlas border is invisible in prefiltered content.
	float3 SampleEnvAtlas(Texture2D atlas, SamplerState samp, float3 dir, float roughness)
	{
		float2 uv = OctEncodeDir(normalize(dir));

		const float texel = 1.0f / ENVMAP_FACE_SIZE;
		uv = clamp(uv, texel, 1.0f - texel);

		const float level = saturate(roughness) * (ENVMAP_ROUGHNESS_ROWS - 1.0f);
		const float row0 = floor(level);
		const float row1 = min(row0 + 1.0f, ENVMAP_ROUGHNESS_ROWS - 1.0f);
		const float rowLerp = level - row0;

		const float v0 = (row0 + uv.y) / ENVMAP_ROUGHNESS_ROWS;
		const float v1 = (row1 + uv.y) / ENVMAP_ROUGHNESS_ROWS;

		const float3 c0 = atlas.SampleLevel(samp, float2(uv.x, v0), 0).rgb;
		const float3 c1 = atlas.SampleLevel(samp, float2(uv.x, v1), 0).rgb;
		return lerp(c0, c1, rowLerp);
	}

	// ----------------------------------------------------------------------------
	// Reflection probe placement
	// ----------------------------------------------------------------------------

	// Weight for a probe at this world position: 1 well inside its box, falling to
	// 0 at the boundary over the outer 25%. Used both to fade a probe out at its
	// own edge and to cross-fade against the second-nearest probe, so a pixel in
	// the overlap of two volumes gets a weighted mix rather than whichever one
	// happened to win the sort.
	float ProbeWeight(float3 worldPos, float4 centre, float4 extents)
	{
		if (centre.w < 0.5f)
			return 0.0f;
		const float3 a = abs(worldPos - centre.xyz) / max(extents.xyz, 0.001f.xxx);
		const float boxDist = max(a.x, max(a.y, a.z)); // <1 inside
		return saturate((1.0f - boxDist) / 0.25f);
	}

	// Box-projected direction for a probe (Lagarde): intersect the reflection ray
	// with the probe's box and look from the probe centre toward that hit, so flat
	// floors reflect the actual walls instead of infinitely-distant radiance.
	float3 ProbeSpecularDir(float3 R, float3 worldPos, float4 centre, float4 extents)
	{
		if (extents.w < 0.5f)
			return R;

		const float3 localPos = worldPos - centre.xyz;
		const float3 ext = max(extents.xyz, 0.001f.xxx);
		const float3 safeR = sign(R) * max(abs(R), 1e-4f.xxx);
		const float3 planeA = ( ext - localPos) / safeR;
		const float3 planeB = (-ext - localPos) / safeR;
		const float3 furthest = max(planeA, planeB);
		const float hitDist = min(furthest.x, min(furthest.y, furthest.z));
		return normalize(localPos + R * max(hitDist, 0.0f));
	}

	// Karis' analytic environment-BRDF fit (SIGGRAPH 2014 mobile approximation).
	// Returns the (scale, bias) pair that a split-sum DFG lookup would give.
	// Lives here rather than in PBRutils because the SSR resolve needs the whole
	// environment-specular evaluator below without pulling in the full PBR
	// lighting header; it is only ever the fallback for a missing DFG LUT.
	float2 EnvBRDFApprox(float NdotV, float perceptualRoughness)
	{
		const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
		const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
		const float4 r = perceptualRoughness * c0 + c1;
		const float a004 = min(r.x * r.x, exp2(-9.28f * NdotV)) * r.x + r.y;
		return float2(-1.04f, 1.04f) * a004 + r.zw;
	}

	// ----------------------------------------------------------------------------
	// Environment specular
	// ----------------------------------------------------------------------------

	// Split-sum image-based specular: prefiltered environment radiance along the
	// reflection vector, weighted by the env-BRDF, with reflection probes taking
	// over from the sky inside their boxes.
	//
	// This lives in the shared include because TWO passes have to agree on it
	// exactly. The deferred lighting pass owns the term normally; when the SSR
	// resolve is composing (g_iblComposeInResolve) the resolve owns it instead
	// and blends it against the screen-space reflection by confidence. If the two
	// evaluations diverged by so much as a horizon-fade constant, toggling the
	// composition would visibly change the image on every pixel SSR missed -
	// which is precisely the measurement we need to stay trustworthy.
	//
	// Returns the BRDF-weighted term ready to add to a lit pixel. envRadianceOut
	// hands back the unweighted radiance for debug views.
	//
	// The probe/sky split, the separate specular strength, the horizon fade and
	// the multi-scatter compensation are all documented at their use sites below.
	float3 EvaluateEnvSpecular(
		Texture2D skyAtlas,
		Texture2D probeAtlas,
		Texture2D probeAtlas2,
		Texture2D dfgLut,
		SamplerState samp,
		float3 N,
		float3 V,
		float3 worldPos,
		float3 baseColour,
		float metallic,
		float perceptualRoughnessRaw,
		float4 iblParams,
		float2 dfgToggles,     // x = use the DFG LUT, y = use multi-scatter compensation
		float4 probeC,  float4 probeE,
		float4 probeC2, float4 probeE2,
		out float3 envRadianceOut,
		// Directional-hemispherical reflectance of the specular lobe: the
		// fraction of arriving light this surface sends into the reflection
		// rather than into its diffuse. The SSR resolve uses it to take that
		// fraction back OFF the base layer, so a mirror floor stops emitting a
		// full diffuse AND a full reflection. Split-sum, not raw Schlick, so
		// rough surfaces aren't over-darkened.
		out float3 specularReflectanceOut)
	{
		// Local copies of PBRutils' dielectric F0 and roughness floor. Duplicated
		// rather than included because PBRutils pulls the whole lighting header and
		// this include is deliberately standalone. Clamping here rather than at the
		// call sites is what keeps the deferred pass and the SSR resolve agreeing
		// on the roughness even though only one of them includes PBRutils.
		const float3 kDielectricF0 = 0.04f.xxx;
		const float kMinPerceptualRoughness = 0.04f; // == PBRutils' MinRoughness

		const float perceptualRoughness = clamp(perceptualRoughnessRaw, kMinPerceptualRoughness, 1.0f);

		const float NdotV = saturate(dot(N, V));
		const float3 R = reflect(-V, N);

		const float3 specularColour = lerp(kDielectricF0, baseColour, metallic);

		// Real DFG lookup, with the analytic fit as the fallback for the first
		// frame (before the table is generated) and when disabled.
		const float3 dfgSample = dfgLut.SampleLevel(samp, float2(NdotV, perceptualRoughness), 0).rgb;
		const bool useLut = (dfgToggles.x > 0.5f) && (dfgSample.b > 1e-4f);
		const float2 dfg = useLut ? dfgSample.rg : EnvBRDFApprox(NdotV, perceptualRoughness);

		// Multi-scatter energy compensation (Fdez-Aguera 2019). Single-scatter
		// GGX models ONE bounce off the microfacet surface, so the light that
		// would have bounced again is simply lost - a loss that grows with
		// roughness and makes rough metals render noticeably too dark. Ess (the
		// DFG table's .b channel) is the energy a white-Fresnel surface actually
		// returns, so 1-Ess is what went missing.
		float3 energyCompensation = 1.0f.xxx;
		if (useLut && dfgToggles.y > 0.5f)
		{
			const float Ess = max(dfgSample.b, 1e-3f);
			energyCompensation = 1.0f.xxx + specularColour * (1.0f / Ess - 1.0f);
		}

		// The atlas is prefiltered per roughness row, so the lookup uses the true
		// mirror direction - no normal-bias hack needed.
		const float3 skySpec = SampleEnvAtlas(skyAtlas, samp, R, perceptualRoughness);

		// Horizon fade: the sky LUT carries no ground radiance, so a downward-facing
		// reflection would otherwise light undersides with horizon sky.
		const float specHorizon = saturate(R.y * 3.0f + 0.35f);

		float3 envSpecRadiance = skySpec * specHorizon * iblParams.x;

		// A captured probe is local radiance with occlusion baked in - inside its
		// box it REPLACES the sky term (which is unoccluded and therefore wrong
		// indoors) rather than adding to it. Fades back to the sky over the outer
		// 15% of the box so walking out of a probe's volume doesn't pop.
		const float w1 = ProbeWeight(worldPos, probeC,  probeE);
		const float w2 = ProbeWeight(worldPos, probeC2, probeE2);
		const float wSum = w1 + w2;

		if (wSum > 0.0f)
		{
			// Normalise so overlapping volumes hand back one probe's worth of
			// energy, then fade the whole probe term against the sky term by the
			// UNnormalised coverage - a pixel only partly covered by any probe
			// should still see some sky rather than a full-strength probe
			// stretched to fill.
			const float n1 = w1 / wSum;
			const float n2 = w2 / wSum;
			const float coverage = saturate(wSum);

			float3 probeSpec = 0.0f.xxx;
			if (w1 > 0.0f)
			{
				const float3 d1 = ProbeSpecularDir(R, worldPos, probeC, probeE);
				probeSpec += n1 * SampleEnvAtlas(probeAtlas, samp, d1, perceptualRoughness);
			}
			if (w2 > 0.0f)
			{
				const float3 d2 = ProbeSpecularDir(R, worldPos, probeC2, probeE2);
				probeSpec += n2 * SampleEnvAtlas(probeAtlas2, samp, d2, perceptualRoughness);
			}

			envSpecRadiance = lerp(envSpecRadiance, probeSpec * iblParams.z, coverage);
		}

		const float3 specularReflectance = (specularColour * dfg.x + dfg.y) * energyCompensation;

		envRadianceOut = envSpecRadiance;
		specularReflectanceOut = saturate(specularReflectance);
		return envSpecRadiance * specularReflectance;
	}
}
