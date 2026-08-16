"ComputeShaderIncludes"
{
	Global
}
"ComputeShader"
{
	// Lit-scene radiance feedback: scatters the CURRENT frame's rendered
	// lighting (every light type, shadowed, including GI's own bounce) into
	// per-clip voxel accumulation buffers. The next voxelize update folds the
	// accumulated radiance into its injection target, so the injection EMA
	// damps the feedback loop (steady state converges as long as the fed-back
	// fraction stays below 1 - guaranteed by the luma cap + strength scale).
	//
	// Layout per voxel: uint4 { R*1024, G*1024, B*1024, weight*1024 },
	// atomically accumulated; the host clears the buffers before each scatter.
	Texture2D<float4> g_sceneLighting : register(t0);
	Texture2D<float4> g_gbufferPosition : register(t1);
	Texture2D<float4> g_gbufferNormal : register(t2);
	RWStructuredBuffer<uint4> g_accumClip0 : register(u0);
	RWStructuredBuffer<uint4> g_accumClip1 : register(u1);

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

	[numthreads(8, 8, 1)]
	void ShaderMain(uint3 tid : SV_DispatchThreadID)
	{
		const uint2 halfDims = uint2(max(1u, (uint)g_screenWidth / 2u), max(1u, (uint)g_screenHeight / 2u));
		if (any(tid.xy >= halfDims))
			return;

		const uint2 pixel = min(tid.xy * 2u, uint2((uint)g_screenWidth - 1u, (uint)g_screenHeight - 1u));
		const float4 normalDepth = g_gbufferNormal[pixel];
		if (normalDepth.w <= 0.0f)
			return; // sky

		const float3 positionWs = g_gbufferPosition[pixel].xyz;
		float3 radiance = max(g_sceneLighting[pixel].rgb, 0.0f.xxx);
		radiance = LuminanceClamp(radiance, max(g_giParams13.y, 0.1f));
		if (dot(radiance, 1.0f.xxx) <= 1e-5f)
			return;

		const uint3 quantized = uint3(round(saturate(radiance / 32.0f) * 32.0f * 1024.0f));

		// Clip 0
		{
			const float3 center = g_clipCenterExtent[0].xyz;
			const float extent = max(g_clipCenterExtent[0].w, 1e-3f);
			const uint res = max(1u, (uint)g_clipVoxelInfo[0].z);
			const float3 uvw = ((positionWs - center) / (extent * 2.0f)) + 0.5f;
			if (all(uvw > 0.0f) && all(uvw < 1.0f))
			{
				const uint3 coord = min((uint3)(uvw * (float)res), res - 1u);
				const uint idx = (coord.z * res + coord.y) * res + coord.x;
				InterlockedAdd(g_accumClip0[idx].x, quantized.x);
				InterlockedAdd(g_accumClip0[idx].y, quantized.y);
				InterlockedAdd(g_accumClip0[idx].z, quantized.z);
				InterlockedAdd(g_accumClip0[idx].w, 1024u);
			}
		}

		// Clip 1
		{
			const float3 center = g_clipCenterExtent[1].xyz;
			const float extent = max(g_clipCenterExtent[1].w, 1e-3f);
			const uint res = max(1u, (uint)g_clipVoxelInfo[1].z);
			const float3 uvw = ((positionWs - center) / (extent * 2.0f)) + 0.5f;
			if (all(uvw > 0.0f) && all(uvw < 1.0f))
			{
				const uint3 coord = min((uint3)(uvw * (float)res), res - 1u);
				const uint idx = (coord.z * res + coord.y) * res + coord.x;
				InterlockedAdd(g_accumClip1[idx].x, quantized.x);
				InterlockedAdd(g_accumClip1[idx].y, quantized.y);
				InterlockedAdd(g_accumClip1[idx].z, quantized.z);
				InterlockedAdd(g_accumClip1[idx].w, 1024u);
			}
		}
	}
}
