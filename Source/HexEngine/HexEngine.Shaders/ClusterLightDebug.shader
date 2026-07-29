"ComputeShaderIncludes"
{
	Global
}
"ComputeShader"
{
	// Cluster occupancy heatmap. One thread per output pixel: find the pixel's
	// cluster from its screen position + gbuffer depth, colour by light count.
	// Black 0, blue 1, green ~1/4 cap, yellow ~1/2, red at the 64 cap (cap hit
	// = lights are being dropped there). This is the verification tool for the
	// cull pass - if the heatmap doesn't match where lights sit in the world,
	// the cluster maths is wrong and nothing downstream can be trusted.

	static const uint  kClustersX = 16;
	static const uint  kClustersY = 9;
	static const uint  kClustersZ = 32;
	static const uint  kMaxLightsPerCluster = 64;
	static const float kNearPlaneM = 0.25f;
	static const float kFarDepthM  = 128.0f;

	Texture2D                g_normalDepth   : register(t0); // gbuffer normal, .w = view depth
	StructuredBuffer<uint>   g_clusterCounts : register(t1);
	RWTexture2D<float4>      g_output        : register(u0);

	cbuffer ClusterConstants : register(b5)
	{
		matrix g_clusterView;
		float4 g_clusterScreenParams;
	};

	uint DepthToSlice(float depth)
	{
		// Inverse of the froxel exponential mapping, clamped into range.
		const float w = log(max(depth, kNearPlaneM) / kNearPlaneM) / log(kFarDepthM / kNearPlaneM);
		return min((uint)(saturate(w) * (float)kClustersZ), kClustersZ - 1);
	}

	float3 Ramp(float t)
	{
		// black -> blue -> green -> yellow -> red
		const float3 c0 = float3(0.0f, 0.0f, 0.0f);
		const float3 c1 = float3(0.1f, 0.2f, 1.0f);
		const float3 c2 = float3(0.1f, 1.0f, 0.2f);
		const float3 c3 = float3(1.0f, 1.0f, 0.1f);
		const float3 c4 = float3(1.0f, 0.1f, 0.1f);
		if (t < 0.25f) return lerp(c0, c1, t * 4.0f);
		if (t < 0.50f) return lerp(c1, c2, (t - 0.25f) * 4.0f);
		if (t < 0.75f) return lerp(c2, c3, (t - 0.50f) * 4.0f);
		return lerp(c3, c4, (t - 0.75f) * 4.0f);
	}

	[numthreads(8, 8, 1)]
	void ShaderMain(uint3 tid : SV_DispatchThreadID)
	{
		uint outW = 0, outH = 0;
		g_output.GetDimensions(outW, outH);
		if (tid.x >= outW || tid.y >= outH)
			return;

		// Output is screen-sized / N; sample the gbuffer at the matching UV.
		uint srcW = 0, srcH = 0;
		g_normalDepth.GetDimensions(srcW, srcH);
		const uint2 srcPx = uint2(
			(uint)(((float)tid.x + 0.5f) / (float)outW * (float)srcW),
			(uint)(((float)tid.y + 0.5f) / (float)outH * (float)srcH));

		const float depth = g_normalDepth.Load(int3(srcPx, 0)).w;

		const uint cx = min(tid.x * kClustersX / outW, kClustersX - 1);
		const uint cy = min(tid.y * kClustersY / outH, kClustersY - 1);
		const uint cz = DepthToSlice(depth);
		const uint clusterIdx = (cz * kClustersY + cy) * kClustersX + cx;

		const uint count = g_clusterCounts[clusterIdx];
		const float t = saturate((float)count / (float)kMaxLightsPerCluster);
		g_output[tid.xy] = float4(Ramp(t), count > 0 ? 0.85f : 0.35f);
	}
}
