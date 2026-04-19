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
	StructuredBuffer<uint> g_frustumVisibility : register(t1);
	Texture2D g_hzbTexture : register(t2);
	RWStructuredBuffer<uint> g_finalVisibility : register(u0);

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

	float2 SafeNdcToUv(float2 ndc)
	{
		return float2(ndc.x * 0.5f + 0.5f, -ndc.y * 0.5f + 0.5f);
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
		const uint frustumVisible = g_frustumVisibility[tid.x] > 0u ? 1u : 0u;
		if (frustumVisible == 0u)
		{
			g_finalVisibility[tid.x] = 0u;
			return;
		}

		uint outputFlags = 0u;
		outputFlags |= 1u; // frustum visible

		if ((candidate.flags & 1u) != 0u)
		{
			outputFlags |= 2u;
			g_finalVisibility[tid.x] = outputFlags;
			return;
		}

		const bool occlusionEnabled = g_cullParams0.y > 0.5f;
		const bool hasHiz = g_cullHzbInfo.w > 0.5f;
		if (!occlusionEnabled || !hasHiz)
		{
			outputFlags |= 2u;
			g_finalVisibility[tid.x] = outputFlags;
			return;
		}

		float4 centerVs4 = mul(float4(candidate.sphereWs.xyz, 1.0f), g_cullView);
		const float centerViewDepth = abs(centerVs4.z);
		if (centerViewDepth <= 0.01f)
		{
			outputFlags |= 2u;
			g_finalVisibility[tid.x] = outputFlags;
			return;
		}

		const float4 centerClip = mul(float4(candidate.sphereWs.xyz, 1.0f), g_cullViewProjection);
		if (abs(centerClip.w) <= 1e-5f)
		{
			outputFlags |= 2u;
			g_finalVisibility[tid.x] = outputFlags;
			return;
		}

		const float2 centerNdc = centerClip.xy / centerClip.w;
		const float depthNdc = centerClip.z / centerClip.w;

		const float projScale = max(abs(g_cullProjection[0][0]), abs(g_cullProjection[1][1]));
		const float radiusNdc = (candidate.sphereWs.w * projScale) / max(centerViewDepth, 1e-4f);
		const float2 minUv = saturate(SafeNdcToUv(centerNdc - radiusNdc.xx));
		const float2 maxUv = saturate(SafeNdcToUv(centerNdc + radiusNdc.xx));

		const float2 boxSizePx = max((maxUv - minUv) * g_cullViewportSizeInvSize.xy, 1.0f.xx);
		const float boxSize = max(boxSizePx.x, boxSizePx.y);
		const float mip = clamp(floor(log2(max(boxSize, 1.0f))), 0.0f, max(g_cullHzbInfo.z - 1.0f, 0.0f));

		const float2 sampleUv = (minUv + maxUv) * 0.5f;
		const float mipScale = exp2(mip);
		const float2 mipSize = max(g_cullViewportSizeInvSize.xy / mipScale, 1.0f.xx);
		const float2 uvInset = min((maxUv - minUv) * 0.1f, 0.01f.xx);
		const float2 uvMinSafe = saturate(minUv + uvInset);
		const float2 uvMaxSafe = saturate(maxUv - uvInset);

		const int mipIndex = (int)mip;
		const int2 centerCoord = int2(clamp(sampleUv * mipSize, 0.0f.xx, mipSize - 1.0f.xx));
		const int2 c00 = int2(clamp(float2(uvMinSafe.x, uvMinSafe.y) * mipSize, 0.0f.xx, mipSize - 1.0f.xx));
		const int2 c10 = int2(clamp(float2(uvMaxSafe.x, uvMinSafe.y) * mipSize, 0.0f.xx, mipSize - 1.0f.xx));
		const int2 c01 = int2(clamp(float2(uvMinSafe.x, uvMaxSafe.y) * mipSize, 0.0f.xx, mipSize - 1.0f.xx));
		const int2 c11 = int2(clamp(float2(uvMaxSafe.x, uvMaxSafe.y) * mipSize, 0.0f.xx, mipSize - 1.0f.xx));

		const float centerDepth = g_hzbTexture.Load(int3(centerCoord, mipIndex)).r;
		float cornerMinDepth = g_hzbTexture.Load(int3(c00, mipIndex)).r;
		cornerMinDepth = min(cornerMinDepth, g_hzbTexture.Load(int3(c10, mipIndex)).r);
		cornerMinDepth = min(cornerMinDepth, g_hzbTexture.Load(int3(c01, mipIndex)).r);
		cornerMinDepth = min(cornerMinDepth, g_hzbTexture.Load(int3(c11, mipIndex)).r);
		const float depthBias = g_cullParams0.z;

		// Stability-biased policy: require center-depth agreement, then confirm with corner coverage.
		// This reduces camera-motion flicker from thin/partial occluders.
		const float sphereFrontDepthNdc = depthNdc - max(radiusNdc, 0.0f);
		const bool centerOccluded = sphereFrontDepthNdc > (centerDepth + depthBias);
		const bool cornerOccluded = sphereFrontDepthNdc > (cornerMinDepth + depthBias);
		const bool occluded = centerOccluded && cornerOccluded;
		if (!occluded)
			outputFlags |= 2u;

		g_finalVisibility[tid.x] = outputFlags;
	}
}
