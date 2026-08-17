"ComputeShaderIncludes"
{
	Global
}
"ComputeShader"
{
	Texture3D<float4> g_voxelRadianceSrc : register(t0);
	// Directional (SH L1) moment sources - bound only when g_giParams13.w > 0.5.
	Texture3D<float4> g_voxelL1xSrc : register(t2);
	Texture3D<float4> g_voxelL1ySrc : register(t3);
	Texture3D<float4> g_voxelL1zSrc : register(t4);
	RWTexture3D<float4> g_voxelRadianceOut : register(u0);
	RWTexture3D<float4> g_voxelL1xOut : register(u1);
	RWTexture3D<float4> g_voxelL1yOut : register(u2);
	RWTexture3D<float4> g_voxelL1zOut : register(u3);

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

	// Dual cap: luminance AND peak channel both <= maxLuma. The 1.0x channel ratio prevents
	// pure single-channel values like (0, 8, 0) from slipping past the luma test (whose
	// weighted luma is only 5.7) and propagating outward to dominate downstream sampling.
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

		const int3 p = int3(tid);
		const int3 maxP = int3((int)voxelRes - 1, (int)voxelRes - 1, (int)voxelRes - 1);
		const float4 center = g_voxelRadianceSrc[p];
		const float3 sunDirWs = normalize(g_giParams3.xyz + float3(1e-6f, 1e-6f, 1e-6f));
		// Keep sun directionality as a subtle shaping hint during propagation.
		// If this gets too strong it turns directional GI into a broad white smear on
		// sun-facing buildings as the sun rotates.
		const float dirStrength = saturate(g_giParams3.w * 0.35f);

		const bool directionalActive = g_giParams13.w > 0.5f;
		float3 accum = center.rgb;
		float accumW = 1.0f;
		float maxOcc = center.a;
		float3 accumL1x = directionalActive ? g_voxelL1xSrc[p].rgb : 0.0f.xxx;
		float3 accumL1y = directionalActive ? g_voxelL1ySrc[p].rgb : 0.0f.xxx;
		float3 accumL1z = directionalActive ? g_voxelL1zSrc[p].rgb : 0.0f.xxx;

		static const int3 kOffsets[6] =
		{
			int3(1, 0, 0), int3(-1, 0, 0),
			int3(0, 1, 0), int3(0, -1, 0),
			int3(0, 0, 1), int3(0, 0, -1)
		};

		[unroll]
		for (uint i = 0; i < 6; ++i)
		{
			const int3 np = clamp(p + kOffsets[i], int3(0, 0, 0), maxP);
			const float4 n = g_voxelRadianceSrc[np];
			const float3 sampleDir = normalize((float3)kOffsets[i]);
			const float directionalWeight = 1.0f + saturate(dot(sampleDir, -sunDirWs)) * (0.18f * dirStrength);
			// Occlusion-aware diffusion: a solid neighbour (occupancy in .a)
			// should not push its radiance THROUGH itself into this voxel -
			// unoccluded isotropic blur was one of the mechanisms carrying
			// bounce around/through geometry (the silhouette light leak).
			const float neighbourTransmit = 1.0f - saturate(n.a) * 0.75f;
			const float w = 0.70f * directionalWeight * neighbourTransmit;
			accum += n.rgb * w;
			accumW += w;
			maxOcc = max(maxOcc, n.a);
			if (directionalActive)
			{
				// L1 diffuses with the SAME weights so direction stays
				// consistent with magnitude.
				accumL1x += g_voxelL1xSrc[np].rgb * w;
				accumL1y += g_voxelL1ySrc[np].rgb * w;
				accumL1z += g_voxelL1zSrc[np].rgb * w;
			}
		}

		const float3 blurred = accum / max(accumW, 1e-4f);

		// Extra directional gather from the up-sun side to preserve sun-driven GI gradients.
		float3 directionalSample = 0.0f.xxx;
		if (dirStrength > 0.001f)
		{
			const int3 sunStep = int3(round(-sunDirWs));
			if (any(sunStep != int3(0, 0, 0)))
			{
				const int3 sp = clamp(p + sunStep * 2, int3(0, 0, 0), maxP);
				directionalSample = g_voxelRadianceSrc[sp].rgb;
			}
		}

		const float propagation = lerp(0.40f, 0.46f, dirStrength);
		// Use symmetric temporal blending to avoid channel "white locking" from max-only propagation.
		float3 mixed = lerp(blurred, directionalSample, 0.12f * dirStrength);
		float3 outRgb = lerp(center.rgb, mixed, propagation);
		// Propagation cap matches voxel write cap (4) so propagation can't amplify a voxel
		// past what the write stage allowed. Both luma and peak channel are bounded.
		outRgb = LuminanceClamp(outRgb, 4.0f);

		const float outOcc = saturate(max(center.a * 0.985f, maxOcc * 0.95f));
		g_voxelRadianceOut[p] = float4(outRgb, outOcc);

		if (directionalActive)
		{
			const float invW = rcp(max(accumW, 1e-4f));
			g_voxelL1xOut[p] = float4(lerp(g_voxelL1xSrc[p].rgb, accumL1x * invW, propagation), 0.0f);
			g_voxelL1yOut[p] = float4(lerp(g_voxelL1ySrc[p].rgb, accumL1y * invW, propagation), 0.0f);
			g_voxelL1zOut[p] = float4(lerp(g_voxelL1zSrc[p].rgb, accumL1z * invW, propagation), 0.0f);
		}
	}
}
