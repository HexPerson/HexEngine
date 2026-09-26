"ComputeShaderIncludes"
{
	Global
}
"ComputeShader"
{
	// Must match GpuVisibilityCulling::GpuCullCandidate (80 bytes).
	struct GpuCullCandidate
	{
		float4 sphereWs;
		float4 obbCenter;       // xyz world centre
		float4 obbExtents;      // xyz half-extents along the box's own axes
		float4 obbOrientation;  // quaternion (x, y, z, w)
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
		// The view-projection the HZB's depth was RENDERED with (last frame).
		// Bounds are projected with this, not the current camera: testing a
		// box through this frame's projection against last frame's depth
		// compares two different images, and a turning camera then culled
		// objects against whatever happened to be at the same pixel a frame ago.
		matrix g_cullHzbViewProjection;
		// x,y = A,B of ndcZ = A + B/viewZ for the HZB's camera; z = relative bias.
		float4 g_cullHzbDepthParams;
	};

	float NdcToViewDepth(float ndcZ)
	{
		return g_cullHzbDepthParams.y / min(ndcZ - g_cullHzbDepthParams.x, -1e-7f);
	}

	float3 QuatRotate(float4 q, float3 v)
	{
		const float3 t = 2.0f * cross(q.xyz, v);
		return v + q.w * t + cross(q.xyz, t);
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
		if (g_frustumVisibility[tid.x] == 0u)
		{
			g_finalVisibility[tid.x] = 0u;
			return;
		}

		const uint kFrustumVisible = 1u;
		const uint kVisible = 3u;   // frustum visible + final visible

		const bool occlusionEnabled = g_cullParams0.y > 0.5f && g_cullHzbInfo.w > 0.5f;
		if ((candidate.flags & 1u) != 0u || !occlusionEnabled)
		{
			g_finalVisibility[tid.x] = kVisible;
			return;
		}

		// Project the 8 corners of the ORIENTED box: the tightest bound we
		// have, both for the screen rectangle (fewer HZB texels to beat) and
		// for the nearest depth. The bounding sphere this replaced is ~1.7x
		// the box on each axis and made nearly every thin object unoccludable.
		// The nearest point of a box is always one of its corners, and NDC z
		// is monotonic in view depth, so min(corner z) is the exact nearest
		// depth - the old "centre depth minus a SCREEN-space radius" mixed
		// units and was wrong in both directions.
		float2 ndcMin = float2(1e30f, 1e30f);
		float2 ndcMax = float2(-1e30f, -1e30f);
		float nearestZ = 1.0f;
		[unroll]
		for (uint i = 0u; i < 8u; ++i)
		{
			const float3 corner = float3((i & 1u) ? 1.0f : -1.0f, (i & 2u) ? 1.0f : -1.0f, (i & 4u) ? 1.0f : -1.0f);
			const float3 p = candidate.obbCenter.xyz + QuatRotate(candidate.obbOrientation, corner * candidate.obbExtents.xyz);
			const float4 clip = mul(float4(p, 1.0f), g_cullHzbViewProjection);
			// A corner at or behind the camera plane: the rectangle is
			// unbounded - nothing to test against.
			if (clip.w <= 1e-3f)
			{
				g_finalVisibility[tid.x] = kVisible;
				return;
			}
			const float3 ndc = clip.xyz / clip.w;
			ndcMin = min(ndcMin, ndc.xy);
			ndcMax = max(ndcMax, ndc.xy);
			nearestZ = min(nearestZ, ndc.z);
		}

		// Crosses the near plane, or reaches outside the frame the HZB was
		// rendered from: there is no depth for that part, so no evidence it
		// is hidden. (Previously the rectangle was CLAMPED to the screen and
		// the object tested against the edge texels - things turning into
		// view were culled by whatever sat at the screen border last frame.)
		if (nearestZ <= 0.0f || any(ndcMin < -1.0f) || any(ndcMax > 1.0f))
		{
			g_finalVisibility[tid.x] = kVisible;
			return;
		}

		const float2 viewportSize = g_cullViewportSizeInvSize.xy;
		const float2 pxMin = float2(ndcMin.x * 0.5f + 0.5f, 0.5f - ndcMax.y * 0.5f) * viewportSize;
		const float2 pxMax = float2(ndcMax.x * 0.5f + 0.5f, 0.5f - ndcMin.y * 0.5f) * viewportSize;

		// Pick the mip where one texel is at least as large as the rectangle:
		// the rectangle then straddles at most 2x2 texels, and those four
		// together cover it completely - no inset, no centre-only sample.
		const float extentPx = max(max(pxMax.x - pxMin.x, pxMax.y - pxMin.y), 1.0f);
		const uint mipCount = max((uint)g_cullHzbInfo.z, 1u);
		const uint mip = min((uint)ceil(log2(extentPx)), mipCount - 1u);
		const float texelPx = exp2((float)mip);

		const uint2 hzbSize = uint2(max(g_cullHzbInfo.xy, 1.0f.xx));
		const int2 mipLast = int2(max(hzbSize >> mip, uint2(1u, 1u))) - 1;
		// The pyramid folds each level's odd leftover texel into the last
		// row/column, so a coordinate past the end belongs to the last texel.
		const int2 tMin = min(int2(floor(pxMin / texelPx)), mipLast);
		const int2 tMax = min(int2(floor(pxMax / texelPx)), mipLast);
		if (any(tMax - tMin > 1))
		{
			// Only reachable when the mip was clamped - too big to judge.
			g_finalVisibility[tid.x] = kVisible;
			return;
		}

		float farthest = g_hzbTexture.Load(int3(tMin.x, tMin.y, mip)).r;
		farthest = max(farthest, g_hzbTexture.Load(int3(tMax.x, tMin.y, mip)).r);
		farthest = max(farthest, g_hzbTexture.Load(int3(tMin.x, tMax.y, mip)).r);
		farthest = max(farthest, g_hzbTexture.Load(int3(tMax.x, tMax.y, mip)).r);

		// Occluded only if the box's NEAREST point lies behind the FARTHEST
		// depth anywhere under it - compared in LINEAR depth with a relative
		// margin (an NDC offset is huge at range and nothing at close range).
		const bool occluded = NdcToViewDepth(nearestZ) > NdcToViewDepth(farthest) * (1.0f + g_cullHzbDepthParams.z);
		g_finalVisibility[tid.x] = occluded ? kFrustumVisible : kVisible;
	}
}
