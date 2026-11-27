"ComputeShaderIncludes"
{
	Global
	DDGICommon
}
"ComputeShader"
{
	// Blend this dispatch's probe rays into the irradiance atlas of the
	// cascade being updated. One group = one probe tile (8x8 = 6x6 interior +
	// 1-texel octahedral border). Runs BEFORE DDGIBlendDepth, which is the
	// pass that rewrites probe state - so "fresh" (newly scrolled-in or never
	// initialised) is still detectable here and drops the history.

	StructuredBuffer<float4> g_ddgiRaysIn : register(t67);
	RWStructuredBuffer<uint2> g_ddgiIrradianceOut : register(u0);
	// Per-probe change-detector state: x = fast running mean of the probe's
	// luminance (~4 updates), y = last z score (debug).
	RWStructuredBuffer<float2> g_ddgiProbeStats : register(u1);

	groupshared float4 s_rays[kDdgiRaysPerProbe];
	groupshared float4 s_tile[kDdgiIrrTile * kDdgiIrrTile];
	// Per-texel luminance of this update's fresh estimate and of the history,
	// for the probe-level change detector (interior texels only, else 0).
	groupshared float2 s_luma[kDdgiIrrTile * kDdgiIrrTile];

	uint2 BorderSource(uint2 t, uint n)
	{
		// Octahedral border: each border texel copies the interior texel that
		// is its neighbour across the folded edge, so bilinear filtering at
		// tile edges wraps correctly.
		const uint last = n + 1;
		if ((t.x == 0 || t.x == last) && (t.y == 0 || t.y == last))
			return uint2(t.x == 0 ? n : 1, t.y == 0 ? n : 1);
		if (t.y == 0 || t.y == last)
			return uint2(last - t.x, t.y == 0 ? 1 : n);
		return uint2(t.x == 0 ? 1 : n, last - t.y);
	}

	[numthreads(8, 8, 1)]
	void ShaderMain(uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint gi : SV_GroupIndex)
	{
		const uint cascade = g_ddgiUpdate.x;
		const uint period = max(g_ddgiUpdate.y, 1u);
		const uint slot = gid.x * period + g_ddgiUpdate.z;
		const bool valid = slot < (uint)g_ddgiCascade[cascade].dims.w;

		// 64 threads, kDdgiRaysPerProbe rays: each thread loads its share.
		for (uint r0 = gi; r0 < kDdgiRaysPerProbe; r0 += 64u)
			s_rays[r0] = g_ddgiRaysIn[gid.x * kDdgiRaysPerProbe + r0];
		GroupMemoryBarrierWithGroupSync();

		const uint n = kDdgiIrrInterior;
		const uint2 t = gtid.xy;
		const bool interior = t.x >= 1 && t.x <= n && t.y >= 1 && t.y <= n;
		const uint tileBase = slot * kDdgiIrrTile * kDdgiIrrTile;

		// Pass 1: fresh estimate + history for this texel.
		const uint index = tileBase + t.y * kDdgiIrrTile + t.x;
		bool fresh = true;
		bool haveFresh = false;
		float4 fresh4 = 0.0f.xxxx;
		float4 previous = 0.0f.xxxx;
		if (valid && interior)
		{
			const int3 g = DdgiSlotToGrid(cascade, slot);
			const DdgiProbeState st = DdgiLoadState(cascade, slot);
			fresh = (st.coord.w == 0) || any(st.coord.xyz != g);

			const float2 e = ((float2(t - 1) + 0.5f) / (float)n) * 2.0f - 1.0f;
			const float3 texelDir = DdgiOctDecode(e);
			float3 sum = 0.0f.xxx;
			float sky = 0.0f;
			float sumW = 0.0f;
			float sumWAll = 0.0f;
			[loop]
			for (uint r = 0; r < kDdgiRaysPerProbe; ++r)
			{
				const float4 ray = s_rays[r];
				const float w = max(0.0f, dot(texelDir, DdgiRayDirection(r)));
				// Every direction counts toward SKY VISIBILITY, back-face hits
				// included: a one-sided roof seen from the room below (an
				// A-frame's outward-facing tiles) is hit from behind, and
				// skipping those rays left only the window rays in the
				// average - the room read as open to the sky and weather fog
				// fell in from the roof. Back faces still carry no LIGHT.
				sumWAll += w;
				if (ray.w < 0.0f)
					continue;
				sum += ray.rgb * w;
				sky += (ray.w > 1e26f) ? w : 0.0f; // miss = escaped to the sky
				sumW += w;
			}
			previous = DdgiUnpackIrr4(g_ddgiIrradianceOut[index]);
			haveFresh = sumW > 1e-4f;
			if (haveFresh)
				fresh4 = float4(sum / sumW, sky / max(sumWAll, 1e-4f));
		}
		const float3 kLuma = float3(0.2126f, 0.7152f, 0.0722f);
		s_luma[gi] = (valid && interior && haveFresh) ? float2(dot(fresh4.rgb, kLuma), dot(previous.rgb, kLuma)) : 0.0f.xx;
		GroupMemoryBarrierWithGroupSync();

		// Pass 2: blend, with a NOISE-AWARE, DUAL-RATE change detector.
		//
		// A real lighting change (sun crossing a window, a lamp switched on,
		// a door opening) should drop the history quickly; update-to-update
		// ray noise must not. The earlier detectors compared ONE update with
		// the history against fixed thresholds (probe mean 10-40%, texel
		// > 50%). A probe that sees a small bright window through a handful
		// of its 128 rays legitimately swings ~20% between updates, so they
		// fired on noise, cut the hysteresis, and those probes popped
		// bright/dark every update - the "disco" pulsing indoors.
		//
		// One update cannot tell a 35% lamp step from that noise (it is < 2
		// standard errors), so evidence is accumulated instead: each probe
		// keeps a FAST running mean of its luminance (~4 updates). Noise
		// does not move it consistently; a real change does. When the fast
		// mean sits several of its own standard errors (estimated from this
		// update's ray luminance spread) away from the slow history, the
		// hysteresis is cut and the history catches up.
		// g_ddgiGround.w: 0 = fixed hysteresis, 1 = this (default),
		// 2 = the old fixed-threshold detectors (A/B only).
		if (valid && interior)
		{
			float4 result = fresh ? 0.0f.xxxx : previous;
			if (haveFresh)
			{
				const float baseH = saturate(g_ddgiParams0.x);
				float hysteresis = fresh ? 0.0f : baseH;
				const uint mode = (uint)(g_ddgiGround.w + 0.5f);
				float2 mean = 0.0f.xx;
				[loop]
				for (uint k = 0; k < kDdgiIrrTile * kDdgiIrrTile; ++k)
					mean += s_luma[k];
				mean /= (float)(n * n);
				if (mode == 1u)
				{
					float rs = 0.0f, rs2 = 0.0f, rc = 0.0f;
					[loop]
					for (uint r = 0; r < kDdgiRaysPerProbe; ++r)
					{
						const float4 ray = s_rays[r];
						if (ray.w < 0.0f)
							continue;
						const float l = dot(ray.rgb, kLuma);
						rs += l; rs2 += l * l; rc += 1.0f;
					}
					const float rm = rs / max(rc, 1.0f);
					const float variance = max(rs2 / max(rc, 1.0f) - rm * rm, 0.0f);
					// Cosine-weighted texels average ~half the rays each.
					const float stdErr = sqrt(variance / max(rc * 0.5f, 1.0f));
					const float kFast = 0.75f;
					const float fastPrev = g_ddgiProbeStats[slot].x;
					const float fastNew = (fresh || !(fastPrev >= 0.0f)) ? mean.x : lerp(mean.x, fastPrev, kFast);
					// Std error of an EMA with weight kFast: SE * sqrt((1-k)/(1+k)).
					const float fastErr = stdErr * 0.378f;
					const float z = abs(fastNew - mean.y) / (fastErr + 0.02f * max(fastNew, mean.y) + 1e-4f);
					hysteresis *= 1.0f - 0.8f * smoothstep(3.5f, 7.0f, z);
					if (t.x == 1u && t.y == 1u)
						g_ddgiProbeStats[slot] = float2(fastNew, z);
				}
				else if (mode == 2u)
				{
					const float relProbe = abs(mean.x - mean.y) / max(max(mean.x, mean.y), 0.02f);
					hysteresis *= 1.0f - 0.6f * smoothstep(0.10f, 0.40f, relProbe);
					const float lPrev = dot(previous.rgb, kLuma);
					const float lNew = dot(fresh4.rgb, kLuma);
					const float rel = abs(lNew - lPrev) / max(max(lPrev, lNew), 0.02f);
					if (rel > 0.5f)
						hysteresis = min(hysteresis, baseH * 0.6f);
				}
				result = lerp(fresh4, previous, hysteresis);
			}
			result = float4(max(result.rgb, 0.0f.xxx), saturate(result.a));
			s_tile[t.y * kDdgiIrrTile + t.x] = result;
			g_ddgiIrradianceOut[index] = DdgiPackIrr4(result);
		}
		GroupMemoryBarrierWithGroupSync();
		if (valid && !interior)
		{
			const uint2 src = BorderSource(t, n);
			g_ddgiIrradianceOut[tileBase + t.y * kDdgiIrrTile + t.x] = DdgiPackIrr4(s_tile[src.y * kDdgiIrrTile + src.x]);
		}
	}
}
