"ComputeShaderIncludes"
{
	Global
}
"ComputeShader"
{
	Texture2D g_sourceDepth : register(t0);
	RWTexture2D<float> g_dstMip : register(u0);

	cbuffer GpuCullConstants : register(b5)
	{
		matrix g_cullView;
		matrix g_cullProjection;
		matrix g_cullViewProjection;
		float4 g_cullFrustumPlanes[6];
		float4 g_cullCameraPos;
		float4 g_cullViewportSizeInvSize;
		float4 g_cullHzbInfo;
		float4 g_cullParams0;
		float4 g_cullParams1;
	};

	[numthreads(8, 8, 1)]
	void ShaderMain(uint3 tid : SV_DispatchThreadID)
	{
		uint dstW = 0u;
		uint dstH = 0u;
		g_dstMip.GetDimensions(dstW, dstH);
		if (tid.x >= dstW || tid.y >= dstH)
			return;

		const uint srcW = max((uint)g_cullHzbInfo.x, 1u);
		const uint srcH = max((uint)g_cullHzbInfo.y, 1u);
		const uint mipIndex = (uint)g_cullHzbInfo.z;

		// mip0: full-resolution copy from depth source.
		// mip>0: 2x downsample from previous HZB mip.
		const uint2 srcBase = (mipIndex == 0u) ? uint2(tid.xy) : uint2(tid.xy * 2u);
		const uint2 p0 = min(srcBase + uint2(0u, 0u), uint2(srcW - 1u, srcH - 1u));
		const uint2 p1 = (mipIndex == 0u) ? p0 : min(srcBase + uint2(1u, 0u), uint2(srcW - 1u, srcH - 1u));
		const uint2 p2 = (mipIndex == 0u) ? p0 : min(srcBase + uint2(0u, 1u), uint2(srcW - 1u, srcH - 1u));
		const uint2 p3 = (mipIndex == 0u) ? p0 : min(srcBase + uint2(1u, 1u), uint2(srcW - 1u, srcH - 1u));

		const float d0 = g_sourceDepth.Load(int3(p0, 0)).r;
		const float d1 = g_sourceDepth.Load(int3(p1, 0)).r;
		const float d2 = g_sourceDepth.Load(int3(p2, 0)).r;
		const float d3 = g_sourceDepth.Load(int3(p3, 0)).r;

		g_dstMip[tid.xy] = min(min(d0, d1), min(d2, d3));
	}
}
