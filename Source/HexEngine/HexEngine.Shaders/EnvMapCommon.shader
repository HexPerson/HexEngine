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
}
