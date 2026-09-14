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

		float3 accum = 0.0f.xxx;
		float weightSum = 0.0f;

		[loop]
		for (int y = 0; y < kSideSamples; ++y)
		{
			for (int x = 0; x < kSideSamples; ++x)
			{
				// Uniform sample over the octahedral square, which maps to a
				// near-uniform distribution over the sphere.
				const float2 uv = (float2((float)x, (float)y) + 0.5f) / (float)kSideSamples;
				const float3 dir = OctDecodeDir(uv);

				// Mirror row only - rows 1+ are already GGX-blurred and would
				// double-filter the irradiance. Sample through the same inset
				// the atlas is written with, so this reads the radiance actually
				// stored for `dir` (the octahedral content lives in the inner
				// gutter-bordered region now, not the raw [0,1] square).
				const float2 blk = OctUnitToBlock(uv);
				const float2 atlasUv = float2(blk.x, blk.y / ENVMAP_ROUGHNESS_ROWS);
				const float3 radiance = g_envAtlas.SampleLevel(g_linearSampler, atlasUv, 0).rgb;

				float basis[9];
				ShBasis(dir, basis);

				accum += radiance * basis[coeffIndex];
				weightSum += 1.0f;
			}
		}

		// 4*PI is the sphere's solid angle; dividing by the sample count turns the
		// sum into the integral.
		const float solidAnglePerSample = (4.0f * 3.141593f) / max(weightSum, 1.0f);
		return float4(accum * solidAnglePerSample, 1.0f);
	}
}
