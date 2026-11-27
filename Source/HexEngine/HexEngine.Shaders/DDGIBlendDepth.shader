"ComputeShaderIncludes"
{
	Global
	DDGICommon
}
"ComputeShader"
{
	// Blend probe rays into the depth-moment atlas (mean, mean^2 of hit
	// distance per direction - the Chebyshev visibility test that stops light
	// leaking through walls), then relocate / classify the probe and write
	// its state. One group = one probe tile (16x16 = 14x14 interior + border).
	// Runs AFTER DDGIBlendIrradiance (which needs the pre-update state).

	StructuredBuffer<float4> g_ddgiRaysIn : register(t67);
	RWStructuredBuffer<uint> g_ddgiDepthOut : register(u1);
	RWStructuredBuffer<DdgiProbeState> g_ddgiStateOut : register(u2);

	groupshared float4 s_rays[kDdgiRaysPerProbe];
	groupshared float2 s_tile[kDdgiDepthTile * kDdgiDepthTile];

	uint2 BorderSource(uint2 t, uint n)
	{
		const uint last = n + 1;
		if ((t.x == 0 || t.x == last) && (t.y == 0 || t.y == last))
			return uint2(t.x == 0 ? n : 1, t.y == 0 ? n : 1);
		if (t.y == 0 || t.y == last)
			return uint2(last - t.x, t.y == 0 ? 1 : n);
		return uint2(t.x == 0 ? 1 : n, last - t.y);
	}

	[numthreads(16, 16, 1)]
	void ShaderMain(uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint gi : SV_GroupIndex)
	{
		const uint cascade = g_ddgiUpdate.x;
		const uint period = max(g_ddgiUpdate.y, 1u);
		const uint slot = gid.x * period + g_ddgiUpdate.z;
		const bool valid = slot < (uint)g_ddgiCascade[cascade].dims.w;
		const float spacing = g_ddgiCascade[cascade].spacing.x;
		// Distances are clamped to ~1.5 probe cells: the visibility test only
		// has to resolve occluders between neighbouring probes (RTXGI).
		const float maxDepth = spacing * 1.5f;

		for (uint r0 = gi; r0 < kDdgiRaysPerProbe; r0 += 256u)
			s_rays[r0] = g_ddgiRaysIn[gid.x * kDdgiRaysPerProbe + r0];
		GroupMemoryBarrierWithGroupSync();

		int3 g = int3(0, 0, 0);
		bool fresh = true;
		DdgiProbeState st = (DdgiProbeState)0;
		if (valid)
		{
			g = DdgiSlotToGrid(cascade, slot);
			// Read through the UAV: this cascade's state buffer is bound for
			// writing here, so its SRV (t30-t32) is not bound in this pass.
			st = g_ddgiStateOut[slot];
			fresh = (st.coord.w == 0) || any(st.coord.xyz != g);
		}

		const uint n = kDdgiDepthInterior;
		const uint2 t = gtid.xy;
		const bool interior = t.x >= 1 && t.x <= n && t.y >= 1 && t.y <= n;
		const uint tileBase = slot * kDdgiDepthTile * kDdgiDepthTile;

		if (valid && interior)
		{
			const float2 e = ((float2(t - 1) + 0.5f) / (float)n) * 2.0f - 1.0f;
			const float3 texelDir = DdgiOctDecode(e);
			const float sharpness = max(g_ddgiParams2.x, 1.0f);
			float2 sum = 0.0f.xx;
			float sumW = 0.0f;
			[loop]
			for (uint r = 0; r < kDdgiRaysPerProbe; ++r)
			{
				const float4 ray = s_rays[r];
				const float w = pow(max(0.0f, dot(texelDir, DdgiRayDirection(r))), sharpness);
				if (w <= 0.0f)
					continue;
				// Back faces count at a fraction of their distance so a probe
				// pressed against a wall still reads "occluded" that way.
				const float d = min(ray.w < 0.0f ? abs(ray.w) * 0.2f : ray.w, maxDepth);
				sum += float2(d, d * d) * w;
				sumW += w;
			}
			const uint index = tileBase + t.y * kDdgiDepthTile + t.x;
			const float2 previous = DdgiUnpackDepth(g_ddgiDepthOut[index]);
			float2 result = fresh ? float2(maxDepth, maxDepth * maxDepth) : previous;
			if (sumW > 1e-5f)
			{
				const float hysteresis = fresh ? 0.0f : saturate(g_ddgiParams0.x);
				result = lerp(sum / sumW, previous, hysteresis);
			}
			s_tile[t.y * kDdgiDepthTile + t.x] = result;
			g_ddgiDepthOut[index] = DdgiPackDepth(result);
		}
		GroupMemoryBarrierWithGroupSync();
		if (valid && !interior)
		{
			const uint2 src = BorderSource(t, n);
			g_ddgiDepthOut[tileBase + t.y * kDdgiDepthTile + t.x] = DdgiPackDepth(s_tile[src.y * kDdgiDepthTile + src.x]);
		}

		// Relocation + classification (RTXGI-style), one thread per probe.
		if (valid && gi == 0)
		{
			uint backfaces = 0;
			float closestBack = 1e27f;
			float3 closestBackDir = 0.0f.xxx;
			float closestFront = 1e27f;
			float3 closestFrontDir = 0.0f.xxx;
			float farthestFront = 0.0f;
			float3 farthestFrontDir = 0.0f.xxx;
			[loop]
			for (uint r = 0; r < kDdgiRaysPerProbe; ++r)
			{
				const float4 ray = s_rays[r];
				const float3 dir = DdgiRayDirection(r);
				if (ray.w < 0.0f)
				{
					++backfaces;
					const float d = -ray.w;
					if (d < closestBack)
					{
						closestBack = d;
						closestBackDir = dir;
					}
				}
				else
				{
					if (ray.w < closestFront)
					{
						closestFront = ray.w;
						closestFrontDir = dir;
					}
					if (ray.w > farthestFront)
					{
						farthestFront = ray.w;
						farthestFrontDir = dir;
					}
				}
			}

			float3 offset = fresh ? 0.0f.xxx : st.offsetState.xyz;
			const float backRatio = (float)backfaces / (float)kDdgiRaysPerProbe;
			if (g_ddgiParams3.y > 0.5f)
			{
				const float minFront = spacing * 0.25f;
				float3 candidate = offset;
				if (backRatio > 0.25f)
				{
					// Inside geometry: step through the nearest back face.
					candidate = offset + closestBackDir * (closestBack + minFront * 0.5f);
				}
				else if (closestFront < minFront && dot(closestFrontDir, farthestFrontDir) <= 0.0f)
				{
					// Pressed against a surface: move toward open space.
					candidate = offset + farthestFrontDir * min(farthestFront, spacing * 0.5f) * 0.5f;
				}
				// Stay inside the probe's own cell.
				if (all(abs(candidate) <= spacing * 0.45f))
					offset = candidate;
			}

			const bool inactive = (g_ddgiParams3.z > 0.5f) && (backRatio > 0.25f);
			DdgiProbeState outState;
			outState.offsetState = float4(offset, inactive ? 1.0f : 0.0f);
			outState.coord = int4(g, 1);
			g_ddgiStateOut[slot] = outState;
		}
	}
}
