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
