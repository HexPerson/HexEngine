"ComputeShaderIncludes"
{
	Global
}
"ComputeShader"
{
	// Spatial filter over the scatter volume, between the density and
	// integrate passes (Frostbite runs the same stage). The density pass
	// jitters its sample point per frame, so a froxel that straddles a
	// shadow edge, spot-cone edge, or emissive glow falloff alternates
	// between very different values frame to frame. The integrate pass's
	// temporal EMA averages that over time, but the residual variance is
	// proportional to the signal amplitude - visible as constant FIZZ on
	// bright signals (neon glow especially), and as per-cell convergence
	// differences that read as BLOCKINESS. Sharing the estimate across a
	// 3x3x3 tent kernel divides the variance by roughly the effective
	// sample count (~8x for this kernel) at the cost of half a froxel of
	// spatial sharpness the volume never really had (one froxel already
	// covers ~15x15 screen pixels at 1080p).
	//
	// Kernel weights are the separable triangle [1,2,1] per axis. Border
	// froxels clamp - repeating the edge sample keeps the kernel energy
	// normalised without special-case weight sums.
	//
	// Filtering extinction (.a) along with inscatter (.rgb) is deliberate:
	// extinction carries the same jittered variance from the height-fog
	// profile sampling, and a mismatch between smoothed inscatter and
	// noisy extinction would just move the fizz into transmittance.

	Texture3D<float4>   g_scatterVolume  : register(t0);
	RWTexture3D<float4> g_filteredVolume : register(u0);

	[numthreads(8, 8, 8)]
	void ShaderMain(uint3 dtid : SV_DispatchThreadID)
	{
		uint3 dims;
		g_scatterVolume.GetDimensions(dims.x, dims.y, dims.z);
		if (any(dtid >= dims))
			return;

		const int3 maxIdx = int3(dims) - 1;
		float4 sum = 0.0f.xxxx;
		float weightSum = 0.0f;

		[unroll]
		for (int dz = -1; dz <= 1; ++dz)
		{
			[unroll]
			for (int dy = -1; dy <= 1; ++dy)
			{
				[unroll]
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int3 p = clamp(int3(dtid) + int3(dx, dy, dz), int3(0, 0, 0), maxIdx);
					const float w = (dx == 0 ? 2.0f : 1.0f)
					              * (dy == 0 ? 2.0f : 1.0f)
					              * (dz == 0 ? 2.0f : 1.0f);
					sum += g_scatterVolume.Load(int4(p, 0)) * w;
					weightSum += w;
				}
			}
		}

		g_filteredVolume[dtid] = sum / weightSum;
	}
}
