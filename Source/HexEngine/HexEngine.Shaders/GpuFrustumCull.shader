"ComputeShaderIncludes"
{
	Global
}
"ComputeShader"
{
	struct GpuCullCandidate
	{
		float4 sphereWs;
		float4 occlusionCenterExtent;
		uint stableIndex;
		uint entityKeyLo;
		uint entityKeyHi;
		uint flags;
	};

	StructuredBuffer<GpuCullCandidate> g_candidates : register(t0);
	RWStructuredBuffer<uint> g_frustumVisibility : register(u0);

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

	bool SphereIntersectsFrustum(float3 center, float radius)
	{
		[unroll]
		for (uint i = 0; i < 6; ++i)
		{
			const float4 p = g_cullFrustumPlanes[i];
			const float dist = dot(p.xyz, center) + p.w;
			if (dist < -radius)
				return false;
		}
		return true;
	}

	[numthreads(64, 1, 1)]
	void ShaderMain(uint3 tid : SV_DispatchThreadID)
	{
		uint count = 0u;
		uint stride = 0u;
		g_candidates.GetDimensions(count, stride);
		if (tid.x >= count)
			return;

		const GpuCullCandidate candidate = g_candidates[tid.x];
		if ((candidate.flags & 1u) != 0u)
		{
			g_frustumVisibility[tid.x] = 1u;
			return;
		}

		if (g_cullParams0.x <= 0.5f)
		{
			g_frustumVisibility[tid.x] = 1u;
			return;
		}

		const bool visible = SphereIntersectsFrustum(candidate.sphereWs.xyz, max(candidate.sphereWs.w, 0.01f));
		g_frustumVisibility[tid.x] = visible ? 1u : 0u;
	}
}

