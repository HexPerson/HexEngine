"ComputeShaderIncludes"
{
	Global
}
"ComputeShader"
{
	// Deterministic injection resolve. The per-triangle voxelize pass no
	// longer writes voxels directly (last-writer-wins races were the noise
	// the whole heavy temporal chain existed to hide) - it atomically
	// accumulates fixed-point contributions into g_injectAccum, and this
	// per-voxel pass computes the weighted mean, folds in the per-voxel
	// energy sources (lit-scene feedback, second bounce), and temporal-blends
	// with a LIGHT keep: the injection target is now identical every update
	// for a stable triangle list, so there is no race noise to average away.
	// Delta brakes and emissive fast-paths are retired.

	struct VoxelAccum
	{
		uint radR; uint radG; uint radB; uint radW;
		uint albR; uint albG; uint albB; uint albW;
		uint opacityMax;
		int l1xR; int l1xG; int l1xB;
		int l1yR; int l1yG; int l1yB;
		int l1zR; int l1zG; int l1zB;
		uint emiR; uint emiG; uint emiB; // max-accumulated emissive source (undiluted)
	};

	Texture3D<float4> g_prevVoxelRadiance : register(t0);
	Texture3D<float4> g_prevVoxelAlbedo : register(t1);
	Texture3D<float4> g_prevVoxelL1x : register(t2);
	Texture3D<float4> g_prevVoxelL1y : register(t3);
	Texture3D<float4> g_prevVoxelL1z : register(t4);
	StructuredBuffer<VoxelAccum> g_injectAccum : register(t5);
	// World-stable lit-radiance cache (rgb = remembered lit radiance,
	// a = seen confidence). Replaces the raw per-frame scatter here: the
	// cache persists when the camera looks away, so feedback energy no
	// longer pumps with view direction.
	Texture3D<float4> g_litCache : register(t6);
	RWTexture3D<float4> g_voxelRadianceOut : register(u0);
	RWTexture3D<float4> g_voxelAlbedoOut : register(u1);
	RWTexture3D<float4> g_voxelL1xOut : register(u2);
	RWTexture3D<float4> g_voxelL1yOut : register(u3);
	RWTexture3D<float4> g_voxelL1zOut : register(u4);

	cbuffer GIConstants : register(b4)
	{
		float4 g_clipCenterExtent[4];
		float4 g_clipPreviousCenterExtent[4];
		float4 g_clipVoxelInfo[4];
		float4 g_giParams0;
		float4 g_giParams1;
		float4 g_giParams2;
		float4 g_giParams3;
		float4 g_giParams4;
		float4 g_giParams5;
		float4 g_giParams6;
		float4 g_giParams7;
		float4 g_giParams8;
		float4 g_giParams9;
		float4 g_giParams10;
		float4 g_giParams11;
		float4 g_giParams12;
		float4 g_giParams13;
	};

	float3 LuminanceClamp(float3 rgb, float maxLuma)
	{
		const float lum = dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
		const float channelMax = max(max(rgb.r, rgb.g), rgb.b);
		float scale = 1.0f;
		if (lum > maxLuma && lum > 1e-6f)
			scale = min(scale, maxLuma / lum);
		if (channelMax > maxLuma && channelMax > 1e-6f)
			scale = min(scale, maxLuma / channelMax);
		return rgb * scale;
	}

	[numthreads(8, 8, 8)]
	void ShaderMain(uint3 tid : SV_DispatchThreadID)
	{
		const uint clipIdx = min((uint)g_giParams0.w, 3u);
		const uint voxelRes = max(1u, (uint)g_clipVoxelInfo[clipIdx].z);
		if (any(tid >= voxelRes))
			return;

		const uint idx = (tid.z * voxelRes + tid.y) * voxelRes + tid.x;
		const VoxelAccum a = g_injectAccum[idx];
		const float kInvScale = 1.0f / 1024.0f;
		const float w = (float)a.radW * kInvScale;
		const bool covered = w > 1e-3f;
		const bool directionalActive = g_giParams13.w > 0.5f;

		if (!covered)
		{
			// Uncovered voxels FADE over ~3 updates rather than snapping to
			// zero. Genuinely vacated geometry (a moved object) still vanishes
			// in ~100ms, but transient coverage changes - budget-stride subset
			// shifts, gather latency during edits - soften instead of blacking
			// the voxel out for a frame.
			const float4 prevRad = g_prevVoxelRadiance[tid];
			const float4 prevAlb = g_prevVoxelAlbedo[tid];
			const float fade = 0.35f;
			const float lum = dot(prevRad.rgb, float3(0.2126f, 0.7152f, 0.0722f));
			if (lum < 0.002f && prevRad.a < 0.01f)
			{
				g_voxelRadianceOut[tid] = 0.0f.xxxx;
				g_voxelAlbedoOut[tid] = 0.0f.xxxx;
				if (directionalActive)
				{
					g_voxelL1xOut[tid] = 0.0f.xxxx;
					g_voxelL1yOut[tid] = 0.0f.xxxx;
					g_voxelL1zOut[tid] = 0.0f.xxxx;
				}
				return;
			}
			g_voxelRadianceOut[tid] = prevRad * fade;
			g_voxelAlbedoOut[tid] = prevAlb * fade;
			if (directionalActive)
			{
				g_voxelL1xOut[tid] = float4(g_prevVoxelL1x[tid].rgb * fade, 0.0f);
				g_voxelL1yOut[tid] = float4(g_prevVoxelL1y[tid].rgb * fade, 0.0f);
				g_voxelL1zOut[tid] = float4(g_prevVoxelL1z[tid].rgb * fade, 0.0f);
			}
			return;
		}

		const float invW = 1.0f / w;
		float3 injected = float3((float)a.radR, (float)a.radG, (float)a.radB) * kInvScale * invW;
		// Emissive source: max-accumulated, added UNDILUTED on top of the
		// coverage-weighted mean - a thin neon strip lights its voxel at
		// full strength no matter how much non-emissive geometry shares it.
		injected += float3((float)a.emiR, (float)a.emiG, (float)a.emiB) * kInvScale;
		float3 l1xInj = float3((float)a.l1xR, (float)a.l1xG, (float)a.l1xB) * kInvScale * invW;
		float3 l1yInj = float3((float)a.l1yR, (float)a.l1yG, (float)a.l1yB) * kInvScale * invW;
		float3 l1zInj = float3((float)a.l1zR, (float)a.l1zG, (float)a.l1zB) * kInvScale * invW;
		const float albW = (float)a.albW * kInvScale;
		const float3 injAlbedo = (albW > 1e-3f)
			? saturate(float3((float)a.albR, (float)a.albG, (float)a.albB) * kInvScale / albW)
			: 0.0f.xxx;
		const float accumOpacity = saturate((float)a.opacityMax * kInvScale);

		const float4 previous = g_prevVoxelRadiance[tid];
		const float4 previousAlbedo = g_prevVoxelAlbedo[tid];

		// Lit-scene feedback via the world-stable cache: confidence-weighted
		// remembered radiance, persistent regardless of view direction.
		if (g_giParams13.z > 0.5f)
		{
			const float4 cache = g_litCache[tid];
			if (cache.a > 0.001f)
			{
				injected += LuminanceClamp(cache.rgb, g_giParams13.y) * (g_giParams13.x * 0.6f * saturate(cache.a));
			}
		}

		// Second bounce: previous-frame radiance from the 6 neighbours,
		// re-emitted through this voxel's albedo.
		if (g_giParams6.z > 0.0001f)
		{
			const int3 p = int3(tid);
			const int3 maxP = int3((int)voxelRes - 1, (int)voxelRes - 1, (int)voxelRes - 1);
			float3 neighbourRadiance = 0.0f.xxx;
			neighbourRadiance += g_prevVoxelRadiance[clamp(p + int3(1, 0, 0), int3(0, 0, 0), maxP)].rgb;
			neighbourRadiance += g_prevVoxelRadiance[clamp(p + int3(-1, 0, 0), int3(0, 0, 0), maxP)].rgb;
			neighbourRadiance += g_prevVoxelRadiance[clamp(p + int3(0, 1, 0), int3(0, 0, 0), maxP)].rgb;
			neighbourRadiance += g_prevVoxelRadiance[clamp(p + int3(0, -1, 0), int3(0, 0, 0), maxP)].rgb;
			neighbourRadiance += g_prevVoxelRadiance[clamp(p + int3(0, 0, 1), int3(0, 0, 0), maxP)].rgb;
			neighbourRadiance += g_prevVoxelRadiance[clamp(p + int3(0, 0, -1), int3(0, 0, 0), maxP)].rgb;
			neighbourRadiance *= (1.0f / 6.0f);
			injected += neighbourRadiance * injAlbedo * (g_giParams6.z * 1.5f);
		}

		// Light temporal blend. The injection target is deterministic per
		// update, so retention exists only to soften list-change steps and
		// the far-clip refresh cadence - not to hide per-frame race noise.
		const float snapBoost = saturate(g_giParams12.z);
		float keep = 0.5f;
		keep = lerp(keep, 0.2f, snapBoost);

		float3 radiance = previous.rgb * keep + injected * (1.0f - keep);
		// Voxel-radiance cap follows r_giEnergyClamp (4.0 at the historical
		// clamp of 3) so brighter GI configs are not silently flattened here.
		radiance = LuminanceClamp(max(radiance, 0.0f.xxx), max(4.0f, g_giParams0.y * 1.33f));
		const float opacity = saturate(max(accumOpacity, previous.a * 0.95f));
		g_voxelRadianceOut[tid] = float4(radiance, opacity);

		const float albedoKeep = 0.5f;
		const float3 albedoOut = saturate(previousAlbedo.rgb * albedoKeep + injAlbedo * (1.0f - albedoKeep));
		const float albedoConfidence = saturate(max(previousAlbedo.a * 0.9f, saturate(albW)));
		g_voxelAlbedoOut[tid] = float4(albedoOut, albedoConfidence);

		if (directionalActive)
		{
			const float3 prevL1x = g_prevVoxelL1x[tid].rgb;
			const float3 prevL1y = g_prevVoxelL1y[tid].rgb;
			const float3 prevL1z = g_prevVoxelL1z[tid].rgb;
			g_voxelL1xOut[tid] = float4(prevL1x * keep + l1xInj * (1.0f - keep), 0.0f);
			g_voxelL1yOut[tid] = float4(prevL1y * keep + l1yInj * (1.0f - keep), 0.0f);
			g_voxelL1zOut[tid] = float4(prevL1z * keep + l1zInj * (1.0f - keep), 0.0f);
		}
	}
}
