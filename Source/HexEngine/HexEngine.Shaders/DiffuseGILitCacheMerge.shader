"ComputeShaderIncludes"
{
	Global
}
"ComputeShader"
{
	// World-stable lit-radiance cache merge. The screen scatter only knows
	// what is on camera THIS frame; this pass folds it into a persistent
	// volume (rgb = remembered lit radiance, a = seen confidence) that
	// survives the camera looking away and scrolls with the clipmap. The
	// inject resolve consumes the CACHE, so feedback energy no longer pumps
	// with view direction. The snap signal (params12.z - armed by light-set
	// and settings changes) ages the cache hard so stale lighting clears in
	// a few updates instead of lingering.

	Texture3D<float4> g_litCachePrev : register(t0);
	StructuredBuffer<uint4> g_litFeedbackFrame : register(t1);
	RWTexture3D<float4> g_litCacheOut : register(u0);

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
		float4 g_giParams14;
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
		const float4 prev = g_litCachePrev[tid];
		const uint4 packed = g_litFeedbackFrame[idx];
		const float snapBoost = saturate(g_giParams12.z);
		// Snap-driven aging: light/settings changes clear remembered lighting
		// fast; steady state forgets very slowly (the cache is a memory, not
		// a filter).
		const float radianceAge = lerp(1.0f, 0.55f, snapBoost);
		const float confidenceAge = lerp(0.9995f, 0.55f, snapBoost);

		float3 cacheRgb = prev.rgb * radianceAge;
		float cacheConf = prev.a * confidenceAge;

		if (packed.w > 0u)
		{
			const float3 scattered = LuminanceClamp(
				float3(packed.xyz) / max((float)packed.w, 1.0f),
				max(g_giParams13.y, 0.1f));
			// Seen this frame: blend toward the fresh observation and grow
			// confidence.
			cacheRgb = lerp(cacheRgb, scattered, 0.35f);
			cacheConf = min(1.0f, cacheConf + 0.25f);
		}

		g_litCacheOut[tid] = float4(cacheRgb, saturate(cacheConf));
	}
}
