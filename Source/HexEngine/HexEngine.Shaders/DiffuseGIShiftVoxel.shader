"ComputeShaderIncludes"
{
	Global
}
"ComputeShader"
{
	Texture3D<float4> g_voxelRadianceSrc : register(t0);
	Texture3D<float4> g_voxelAlbedoSrc : register(t1);
	Texture3D<float4> g_voxelL1xSrc : register(t2);
	Texture3D<float4> g_voxelL1ySrc : register(t3);
	Texture3D<float4> g_voxelL1zSrc : register(t4);
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
			g_voxelAlbedoOut[tid] = 0.0f.xxxx;
			if (g_giParams13.w > 0.5f)
			{
				g_voxelL1xOut[tid] = 0.0f.xxxx;
				g_voxelL1yOut[tid] = 0.0f.xxxx;
				g_voxelL1zOut[tid] = 0.0f.xxxx;
			}
			return;
		}

		g_voxelRadianceOut[tid] = g_voxelRadianceSrc.Load(int4(src, 0));
		g_voxelAlbedoOut[tid] = g_voxelAlbedoSrc.Load(int4(src, 0));
		if (g_giParams13.w > 0.5f)
		{
			g_voxelL1xOut[tid] = g_voxelL1xSrc.Load(int4(src, 0));
			g_voxelL1yOut[tid] = g_voxelL1ySrc.Load(int4(src, 0));
			g_voxelL1zOut[tid] = g_voxelL1zSrc.Load(int4(src, 0));
		}
	}
}
