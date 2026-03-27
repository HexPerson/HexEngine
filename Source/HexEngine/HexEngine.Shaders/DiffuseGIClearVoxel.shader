"ComputeShaderIncludes"
{
	Global
}
"ComputeShader"
{
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

	[numthreads(8, 8, 8)]
	void ShaderMain(uint3 tid : SV_DispatchThreadID)
	{
		const uint clipIdx = (uint)g_giParams0.w;
		const uint voxelRes = max(1u, (uint)g_clipVoxelInfo[clipIdx].z);
		if (any(tid >= voxelRes))
			return;

		const float decay = saturate(g_giParams2.z);
		float4 value = g_voxelRadianceOut[tid];
		value.rgb *= decay;
		value.a *= decay;
		if (value.a < 0.01f)
		{
			value = 0.0f.xxxx;
		}
		g_voxelRadianceOut[tid] = value;
	}
}
