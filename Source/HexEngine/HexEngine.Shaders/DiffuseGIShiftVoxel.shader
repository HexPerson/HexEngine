"ComputeShaderIncludes"
{
	Global
}
"ComputeShader"
{
	Texture3D<float4> g_voxelRadianceSrc : register(t0);
	RWTexture3D<float4> g_voxelRadianceOut : register(u0);

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
	};

	cbuffer VoxelShiftConstants : register(b5)
	{
		int4 g_voxelShift;
	};

	[numthreads(8, 8, 8)]
	void ShaderMain(uint3 tid : SV_DispatchThreadID)
	{
		const uint clipIdx = min((uint)g_giParams0.w, 3u);
		const uint voxelRes = max(1u, (uint)g_clipVoxelInfo[clipIdx].z);
		if (any(tid >= voxelRes))
			return;

		const int3 dst = int3(tid);
		const int3 src = dst + g_voxelShift.xyz;
		const int maxCoord = (int)voxelRes - 1;

		if (src.x < 0 || src.y < 0 || src.z < 0 ||
			src.x > maxCoord || src.y > maxCoord || src.z > maxCoord)
		{
			g_voxelRadianceOut[tid] = 0.0f.xxxx;
			return;
		}

		g_voxelRadianceOut[tid] = g_voxelRadianceSrc.Load(int4(src, 0));
	}
}
