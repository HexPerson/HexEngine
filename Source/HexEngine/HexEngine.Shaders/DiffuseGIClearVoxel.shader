"ComputeShaderIncludes"
{
	Global
}
"ComputeShader"
{
	RWTexture3D<float4> g_voxelRadianceOut : register(u0);
	RWTexture3D<float4> g_voxelAlbedoOut : register(u1);

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
	};

	[numthreads(8, 8, 8)]
	void ShaderMain(uint3 tid : SV_DispatchThreadID)
	{
		const uint clipIdx = (uint)g_giParams0.w;
		const uint voxelRes = max(1u, (uint)g_clipVoxelInfo[clipIdx].z);
		if (any(tid >= voxelRes))
			return;

		// This pass is a true clear. Prior voxel state is preserved explicitly in the scratch
		// volumes and consumed by voxel injection/trace where needed; keeping additional decayed
		// state here causes stale bright energy to survive and be re-propagated indefinitely.
		g_voxelRadianceOut[tid] = 0.0f.xxxx;
		g_voxelAlbedoOut[tid] = 0.0f.xxxx;
	}
}
