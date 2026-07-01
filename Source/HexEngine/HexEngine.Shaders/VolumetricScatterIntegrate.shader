"ComputeShaderIncludes"
{
	Global
	AtmosphereCommon
}
"ComputeShader"
{
	// Phase D integration pass. Reads per-froxel (scatter, extinction) from
	// the scatter volume and produces per-froxel (accumulated_inscatter,
	// accumulated_transmittance) in the integration volume, then blends
	// with the previous frame via world-position reprojection.
	//
	// One thread per (uv) column - the thread walks all W slices from
	// near to far in a serial loop. This is the right shape because each
	// W slice depends on the cumulative state from prior slices. Parallel
	// scan would work but adds complexity for negligible win at 64 slices.
	//
	// Per slice:
	//   stepLength      = depth(w+1) - depth(w)            (exp distribution)
	//   stepExtinction  = scatter.a * stepLength
	//   stepTransmittance = exp(-stepExtinction)
	//   stepInscatter   = transmittance * scatter.rgb * stepLength *
	//                     (1 - stepTransmittance) / stepExtinction
	//   transmittance  *= stepTransmittance
	//   inscatter      += stepInscatter
	// Then EMA-blend the slice's output with the reprojected history
	// sample - the internal prefix-sum state stays pure (physics-correct
	// for the next slice's accumulation) so only the OUTPUT smooths.

	Texture3D<float4>   g_scatterVolume       : register(t0);
	// Previous-frame integration volume - bound for temporal EMA blend
	// via world-position reprojection (not same-index lookup).
	Texture3D<float4>   g_historyVolume       : register(t1);
	RWTexture3D<float4> g_integrationVolume   : register(u0);

	// Linear-clamp sampler for sub-froxel-accurate history sampling.
	// Reprojected UVW rarely lands exactly on a texel centre so linear
	// filter is needed to avoid sub-froxel motion aliasing.
	SamplerState g_linearSampler : register(s4);

	cbuffer VolumetricIntegrateParams : register(b5)
	{
		float4 g_volumeDimsAndFar;
		// .x = base history blend alpha (current weight) when the froxel's
		// reprojected position matches its current cell (camera still).
		// .y = valid-history flag (0/1). First frame after init has
		// no previous viewProj - we skip the blend entirely on that
		// frame so the EMA doesn't pull in whatever was in the
		// cleared volume's initial state.
		// .z = motion alpha - the blend weight ramped TO as the froxel's
		// reprojected cell diverges from its current cell. Not all scatter
		// is world-stable (the screen-space emissive injection moves with
		// the camera), so a long blend window under motion drags ghost
		// trails; raising alpha with motion clears them within a frame or
		// two while keeping the long smooth window when static.
		float4 g_temporalParams;
		// World->prev-frame-clip matrix. Used to project this frame's
		// reconstructed world position back into where it WAS in the
		// previous frame's volume, so we sample the correct fog cell
		// regardless of how far the camera moved between frames.
		matrix g_prevViewProj;
		// Previous-frame camera world position (xyz). Used to compute
		// distance-along-prev-ray for the W axis mapping - the volume's
		// W encodes distance from eye along the ray, not view-space z.
		float4 g_prevEyePos;
	};

	static const float NEAR_PLANE_M = 0.1f;

	float VolumeWToDepth(float w, float farDepth)
	{
		return NEAR_PLANE_M * pow(farDepth / NEAR_PLANE_M, w);
	}

	// Inverse exp distribution: depth in [NEAR_PLANE, farDepth] -> w in [0,1].
	// Used during history reprojection: take previous-frame view-space
	// depth, find which W slice that corresponds to in the same exp
	// distribution the scatter pass used.
	float DepthToVolumeW(float depthM, float farDepth)
	{
		const float ratio = farDepth / NEAR_PLANE_M;
		return log(max(depthM, NEAR_PLANE_M) / NEAR_PLANE_M) / log(ratio);
	}

	[numthreads(8, 8, 1)]
	void ShaderMain(uint3 dtid : SV_DispatchThreadID)
	{
		const uint3 dims = uint3(g_volumeDimsAndFar.xyz);
		if (any(dtid.xy >= dims.xy))
			return;

		const float farDepth = g_volumeDimsAndFar.w;
		const uint  zSlices  = dims.z;

		// Reconstruct view ray for THIS column once, reused per slice.
		// Use the cell-centre UV (no jitter) so reprojection lookups
		// hit stable points - the per-frame jitter only applies inside
		// the scatter pass; here we want a deterministic world-pos for
		// each (x,y) column.
		const float2 uvCol = (float2(dtid.xy) + 0.5f) / float2(dims.xy);
		const float2 ndcXY = float2(uvCol.x * 2.0f - 1.0f, 1.0f - uvCol.y * 2.0f);
		const float4 farH  = mul(float4(ndcXY, 1.0f, 1.0f), g_viewProjectionMatrixInverse);
		const float3 worldFar = farH.xyz / max(farH.w, 1e-6f);
		const float3 rayDir   = normalize(worldFar - g_eyePos.xyz);

		float3 inscatter     = float3(0.0f, 0.0f, 0.0f);
		float  transmittance = 1.0f;

		float prevDepth = NEAR_PLANE_M;
		const float baseAlpha   = g_temporalParams.x;
		const float validHist   = g_temporalParams.y;

		[loop]
		for (uint z = 0u; z < zSlices; ++z)
		{
			const float4 sd = g_scatterVolume.Load(int4((int)dtid.x, (int)dtid.y, (int)z, 0));
			const float3 scatter   = sd.rgb;
			const float  extinction = sd.a;

			const float wCentre = ((float)z + 0.5f) / (float)zSlices;
			const float depthAtSlice = VolumeWToDepth(wCentre, farDepth);
			const float stepLength   = max(depthAtSlice - prevDepth, 1e-3f);

			const float stepExt   = extinction * stepLength;
			const float stepTrans = exp(-stepExt);
			const float stepGain  = (1.0f - stepTrans) / max(extinction, 1e-6f);

			inscatter    += transmittance * scatter * stepGain;
			transmittance *= stepTrans;

			// Reproject this slice's world position into the previous
			// frame's volume. World position = eye + rayDir * depthAtSlice
			// (the eye/rayDir are this frame's; we project the result
			// through last frame's viewProj to find prev-frame clip
			// space, then derive UVW from clip xy and view-space depth).
			const float3 worldPos = g_eyePos.xyz + rayDir * depthAtSlice;
			const float4 prevClip = mul(float4(worldPos, 1.0f), g_prevViewProj);

			// Behind-camera reject: prev view-space depth comes from
			// the clip-space w (positive forward for standard perspective).
			// If w<=0 the point was behind the prev camera, no valid history.
			const float prevViewDepthM = prevClip.w;
			const bool inFront = prevViewDepthM > NEAR_PLANE_M;

			// Compute prev UVW only when in front (else divide by ~0).
			float3 prevUVW = float3(0.5f, 0.5f, 0.0f);
			bool valid = false;
			if (inFront)
			{
				const float2 prevNdcXY = prevClip.xy / prevViewDepthM;
				prevUVW.x = prevNdcXY.x * 0.5f + 0.5f;
				prevUVW.y = 0.5f - prevNdcXY.y * 0.5f;
				// W axis uses distance-from-prev-eye, NOT view-space z.
				// The scatter pass's W maps "distance along ray" via
				// VolumeWToDepth, so reprojection must use the same
				// metric. View-space z (prevClip.w) and ray distance
				// differ by 1/cos(theta) where theta is the off-axis
				// angle - up to 15-30% error at frustum corners with
				// typical FOVs. Using length() keeps reprojection
				// pixel-perfect across the whole frustum.
				const float prevDistM = length(worldPos - g_prevEyePos.xyz);
				prevUVW.z = DepthToVolumeW(prevDistM, farDepth);
				valid = prevUVW.x >= 0.0f && prevUVW.x <= 1.0f &&
				        prevUVW.y >= 0.0f && prevUVW.y <= 1.0f &&
				        prevUVW.z >= 0.0f && prevUVW.z <= 1.0f;
			}

			// Effective blend alpha. When history is invalid (off-screen
			// pixel, behind camera, or first frame after init) we use
			// alpha=1 so output is pure current - no smear from invalid
			// history. When valid, ramp from the long-window base alpha
			// (static camera) toward the snappier motion alpha as the
			// reprojected cell diverges from the current cell. The ramp
			// saturates at ~3 cells of displacement - by then any screen-
			// space-derived scatter (emissive glow) in the history is
			// describing a meaningfully different view and holding onto it
			// reads as a ghost trail.
			float a = 1.0f;
			if (valid && validHist > 0.5f)
			{
				const float3 currUVW = float3(uvCol, wCentre);
				const float motionCells = length((prevUVW - currUVW) * float3(dims));
				const float motionAlpha = max(g_temporalParams.z, baseAlpha);
				a = lerp(baseAlpha, motionAlpha, saturate(motionCells / 3.0f));
			}

			float4 hist = float4(0.0f, 0.0f, 0.0f, 1.0f);
			if (a < 1.0f)
				hist = g_historyVolume.SampleLevel(g_linearSampler, prevUVW, 0);

			const float3 outInscatter = lerp(hist.rgb, inscatter, a);
			const float  outTrans     = lerp(hist.a,   transmittance, a);

			g_integrationVolume[int3((int)dtid.x, (int)dtid.y, (int)z)] =
				float4(outInscatter, outTrans);

			prevDepth = depthAtSlice;
		}
	}
}
