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
		const uint2 srcLast = uint2(srcW - 1u, srcH - 1u);

		// mip0: full-resolution copy from the depth source.
		if (mipIndex == 0u)
		{
			g_dstMip[tid.xy] = g_sourceDepth.Load(int3(min(tid.xy, srcLast), 0)).r;
			return;
		}

		// mip>0: MAX reduction of the previous mip. A texel holds the FARTHEST
		// depth under its footprint, so "the object's nearest point is behind
		// it" means behind EVERYTHING the texel covers. This used to be a MIN
		// reduction, which let a single near pixel speak for the whole cell:
		// stand behind a lamp-post and its few columns of pixels made every
		// coarse texel they touched read as a solid wall at lamp-post depth,
		// and the occlusion pass culled the plainly visible buildings behind.
		//
		// The LAST row/column also folds in the source's leftover odd texel
		// (srcW = 2*dstW + 1). Floor-halved mips otherwise drop a strip of
		// depth at every level, and whatever sat under it could be culled by
		// its neighbour's value.
		const uint2 srcBase = min(tid.xy * 2u, srcLast);
		const uint xEnd = (tid.x == dstW - 1u) ? srcLast.x : min(srcBase.x + 1u, srcLast.x);
		const uint yEnd = (tid.y == dstH - 1u) ? srcLast.y : min(srcBase.y + 1u, srcLast.y);

		float farthest = 0.0f;
		[loop]
		for (uint y = srcBase.y; y <= yEnd; ++y)
		{
			[loop]
			for (uint x = srcBase.x; x <= xEnd; ++x)
				farthest = max(farthest, g_sourceDepth.Load(int3(x, y, 0)).r);
		}
		g_dstMip[tid.xy] = farthest;
	}
}
