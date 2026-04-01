"ComputeShaderIncludes"
{
	Global
}
"ComputeShader"
{
	struct VoxelTriangleData
	{
		float4 p0;
		float4 p1;
		float4 p2;
		float4 radianceOpacity;
		float4 albedoWeight;
	};

	StructuredBuffer<VoxelTriangleData> g_voxelTrianglesIn : register(t0);
	AppendStructuredBuffer<VoxelTriangleData> g_voxelCandidatesOut : register(u0);

	cbuffer GIConstants : register(b4)
	{
		float4 g_clipCenterExtent[4];
		float4 g_clipVoxelInfo[4];
		float4 g_giParams0;
		float4 g_giParams1;
		float4 g_giParams2;
		float4 g_giParams3;
		float4 g_giParams4;
		float4 g_giParams5;
		float4 g_giParams6;
	};

	[numthreads(64, 1, 1)]
	void ShaderMain(uint3 tid : SV_DispatchThreadID)
	{
		uint triangleCount = 0u;
		uint triangleStride = 0u;
		g_voxelTrianglesIn.GetDimensions(triangleCount, triangleStride);
		if (tid.x >= triangleCount)
			return;

		const VoxelTriangleData tri = g_voxelTrianglesIn[tid.x];
		const uint clipIdx = min((uint)g_giParams0.w, 3u);
		const float3 clipCenter = g_clipCenterExtent[clipIdx].xyz;
		const float clipExtent = max(1e-3f, g_clipCenterExtent[clipIdx].w);
		const float3 clipMinWs = clipCenter - clipExtent;
		const float3 clipMaxWs = clipCenter + clipExtent;

		const float3 p0 = tri.p0.xyz;
		const float3 p1 = tri.p1.xyz;
		const float3 p2 = tri.p2.xyz;
		const float3 triMinWs = min(p0, min(p1, p2));
		const float3 triMaxWs = max(p0, max(p1, p2));

		if (any(triMaxWs < clipMinWs) || any(triMinWs > clipMaxWs))
			return;

		g_voxelCandidatesOut.Append(tri);
	}
}
