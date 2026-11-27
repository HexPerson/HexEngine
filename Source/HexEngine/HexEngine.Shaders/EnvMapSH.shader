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
	// Project an octahedral environment atlas into 9 SH coefficients (P1-C).
	//
	// Output is a 1 x 9 RGBA16F texture: one coefficient per row. Each output
	// texel integrates the atlas's MIRROR row (row 0 - the unfiltered radiance)
	// against its own SH basis function over the sphere.
	//
	// Integrating in the pixel shader means 9 texels each doing a full spherical
	// sum, which is only sane because the source is tiny: the mirror row is
	// 128x128, so each coefficient is a 16k-tap reduction and the whole pass is 9
	// of those. That's cheap for a sky atlas regenerated per frame, and trivial
	// for a probe baked once.
	//
	// Solid angle: octahedral mapping is very close to equal-area, so a uniform
	// 4*PI/N weight per texel is accurate to well under the error already implied
	// by truncating at L2. No per-texel Jacobian needed.

	Texture2D g_envAtlas : register(t0);
	SamplerState g_linearSampler : register(s4);

	static const int kSideSamples = 64; // 64x64 = 4096 directions

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		// Which coefficient is this row?
		const int coeffIndex = clamp((int)floor(input.texcoord.y * ENVMAP_SH_COEFFS), 0, 8);

		// Equal-area directions (spherical Fibonacci). The previous loop walked
		// a 64x64 grid of OCTAHEDRAL texels and weighted every one 4PI/4096,
		// but the octahedral map is not equal-area - texels near the folds
		// and the axes cover different solid angles - so the projected
		// irradiance was biased toward the over-sampled directions. Fibonacci
		// points are equal-area, so 4PI/N per sample is exact.
		float3 accum = 0.0f.xxx;
		float weightSum = 0.0f;
		const int sampleCount = kSideSamples * kSideSamples;
		const float kGolden = 1.61803398875f;
		[loop]
		for (int i = 0; i < sampleCount; ++i)
		{
			const float phi = 6.28318530718f * frac((float)i / kGolden);
			const float cosTheta = 1.0f - (2.0f * (float)i + 1.0f) / (float)sampleCount;
			const float sinTheta = sqrt(saturate(1.0f - cosTheta * cosTheta));
			const float3 dir = float3(cos(phi) * sinTheta, cosTheta, sin(phi) * sinTheta);

			const float2 blk = OctUnitToBlock(OctEncodeDir(dir));
			const float2 atlasUv = float2(blk.x, blk.y / ENVMAP_ROUGHNESS_ROWS);
			const float3 radiance = g_envAtlas.SampleLevel(g_linearSampler, atlasUv, 0).rgb;

			float basis[9];
			ShBasis(dir, basis);

			accum += radiance * basis[coeffIndex];
			weightSum += 1.0f;
		}

		const float solidAnglePerSample = (4.0f * 3.141593f) / max(weightSum, 1.0f);
		return float4(accum * solidAnglePerSample, 1.0f);
	}
}
