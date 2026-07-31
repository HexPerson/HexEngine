"ComputeShaderIncludes"
{
	Global
}
"ComputeShader"
{
	// Clustered light culling (Phase 2, first slice).
	//
	// One thread per cluster on a 16 x 9 x 32 view-frustum grid. The depth
	// slicing reuses the froxel volumetric system's exponential mapping
	// (depth = near * pow(far/near, w), 128 m far) so a later step can share
	// cluster assignment between surface lighting and fog - see the plan's
	// Phase 2 notes. Lights beyond the far plane clamp into the last slice.
	//
	// Output is a fixed-cap index list per cluster plus a count. Spots pass
	// the bounding-sphere test first, then a cone-vs-sphere refinement
	// against the cluster AABB's bounding sphere (slice 6): still
	// conservative - the bounding sphere over-covers the AABB and the spot's
	// smoothstep is exactly zero outside cos(outer) - but a narrow cone
	// stops paying for its whole sphere. Every consumer (deferred apply,
	// froxel fog, forward transparents) applies the same cone falloff, so a
	// cone-culled cluster only ever loses zero-contribution entries.

	static const uint  kClustersX = 16;
	static const uint  kClustersY = 9;
	static const uint  kClustersZ = 32;
	static const uint  kMaxLightsPerCluster = 64;
	// 0.1 to MATCH THE FROXEL VOLUME EXACTLY (VolumetricScatterDensity's
	// NEAR_PLANE_M). Same exponential formula with a different near plane is a
	// different slicing - the original 0.25 here only rhymed with the froxel
	// mapping, it did not equal it. With 0.1, froxel (128x72x64) to cluster
	// (16x9x32) is exact integer division: fx/8, fy/8, fz/2 - which is what
	// lets fog and surface lighting share cluster assignment with no seam.
	static const float kNearPlaneM = 0.1f;
	static const float kFarDepthM  = 128.0f;

	struct GpuLight
	{
		float4 posRadius;      // xyz world, w radius
		float4 colorStrength;  // rgb colour, w strength
		float4 dirCone;        // spot: xyz dir, w cos(outer). point: unused
		float4 params;         // x cos(inner), y type (0 point, 1 spot), z shadowed, w unused
	};

	StructuredBuffer<GpuLight>  g_lights        : register(t0);
	RWStructuredBuffer<uint>    g_clusterCounts : register(u0);
	RWStructuredBuffer<uint>    g_clusterLists  : register(u1);

	cbuffer ClusterConstants : register(b5)
	{
		matrix g_clusterView;         // world -> view
		float4 g_clusterScreenParams; // x tanHalfFovX, y tanHalfFovY, z lightCount, w unused
	};

	// Slice boundary in view depth, froxel exponential mapping.
	float SliceDepth(uint z)
	{
		const float w = (float)z / (float)kClustersZ;
		return kNearPlaneM * pow(kFarDepthM / kNearPlaneM, w);
	}

	[numthreads(64, 1, 1)]
	void ShaderMain(uint3 tid : SV_DispatchThreadID)
	{
		const uint clusterIdx = tid.x;
		if (clusterIdx >= kClustersX * kClustersY * kClustersZ)
			return;

		const uint cz = clusterIdx / (kClustersX * kClustersY);
		const uint rem = clusterIdx - cz * (kClustersX * kClustersY);
		const uint cy = rem / kClustersX;
		const uint cx = rem - cy * kClustersX;

		// Cluster AABB in view space (right-handed view: forward is -Z in the
		// engine's view matrix, but we work in positive view DEPTH, matching
		// the froxel shaders). X right, Y up, extents from the frustum at each
		// depth plane.
		const float zNear = SliceDepth(cz);
		const float zFar  = SliceDepth(cz + 1);

		// NDC extents of this cluster column: x in [-1,1] left->right, y in
		// [-1,1] bottom->top. Cluster (0,0) is top-left to match screen UV.
		const float x0 = ((float)cx      / (float)kClustersX) * 2.0f - 1.0f;
		const float x1 = ((float)(cx + 1) / (float)kClustersX) * 2.0f - 1.0f;
		const float y1 = 1.0f - ((float)cy      / (float)kClustersY) * 2.0f;
		const float y0 = 1.0f - ((float)(cy + 1) / (float)kClustersY) * 2.0f;

		// Frustum widens with depth: take the union of the near and far
		// footprints so the AABB bounds the whole frustum cell.
		const float txn = g_clusterScreenParams.x * zNear;
		const float txf = g_clusterScreenParams.x * zFar;
		const float tyn = g_clusterScreenParams.y * zNear;
		const float tyf = g_clusterScreenParams.y * zFar;

		float3 aabbMin, aabbMax;
		aabbMin.x = min(x0 * txn, x0 * txf);
		aabbMax.x = max(x1 * txn, x1 * txf);
		aabbMin.y = min(y0 * tyn, y0 * tyf);
		aabbMax.y = max(y1 * tyn, y1 * tyf);
		aabbMin.z = zNear;
		aabbMax.z = zFar;

		// The LAST slice owns everything beyond the 128 m grid: pixel depths
		// clamp into it (DepthToSlice), so lights must too, or a lamp whose
		// sphere doesn't reach 128 m of the camera silently vanishes - seen
		// as lights popping with camera distance and a pitch/yaw depth
		// cut-off. The cone refinement's bounding sphere stays FINITE (from
		// zFar) - and the refinement is skipped for far-slice entries, since
		// a distant spot fails its apex-distance test against near geometry
		// while legitimately lighting distant pixels binned into this slice.
		const bool farSlice = (cz == kClustersZ - 1u);
		if (farSlice)
			aabbMax.z = 1e30f;

		uint count = 0;
		const uint lightCount = (uint)g_clusterScreenParams.z;

		[loop]
		for (uint i = 0; i < lightCount && count < kMaxLightsPerCluster; ++i)
		{
			const GpuLight light = g_lights[i];

			// World -> view. The engine's view matrix looks down -Z; flip to
			// the positive-depth convention the AABB uses.
			float4 viewPos = mul(float4(light.posRadius.xyz, 1.0f), g_clusterView);
			viewPos.z = -viewPos.z;

			// Sphere vs AABB: distance from centre to closest AABB point.
			const float3 closest = clamp(viewPos.xyz, aabbMin, aabbMax);
			const float3 d = viewPos.xyz - closest;
			if (dot(d, d) > light.posRadius.w * light.posRadius.w)
				continue;

			// Spot refinement: cone vs the cluster AABB's bounding sphere
			// (Lengyel's test). The bounding sphere over-covers the AABB, so
			// this can only keep extra clusters, never drop a lit one.
			// Skipped for the unbounded far slice (see above).
			if (!farSlice && light.params.y > 0.5f)
			{
				// Direction into the same flipped view space as the position:
				// rotate (w=0), then mirror z. Both flipped together keeps the
				// geometry consistent.
				float3 axis = mul(float4(light.dirCone.xyz, 0.0f), g_clusterView).xyz;
				axis.z = -axis.z;
				axis = normalize(axis);

				const float3 sphereC = (aabbMin + aabbMax) * 0.5f;
				const float  sphereR = length(aabbMax - sphereC);

				const float cosOuter = light.dirCone.w;
				const float sinOuter = sqrt(saturate(1.0f - cosOuter * cosOuter));

				const float3 v = sphereC - viewPos.xyz;
				const float  along = dot(v, axis);
				// Signed distance from the sphere centre to the nearest point
				// on the cone's surface (negative inside the cone).
				const float distToCone =
					cosOuter * sqrt(max(dot(v, v) - along * along, 0.0f)) - along * sinOuter;

				if (distToCone > sphereR ||                       // beside the cone
				    along < -sphereR ||                           // behind the apex
				    along > light.posRadius.w + sphereR)          // past the range cap
					continue;
			}

			g_clusterLists[clusterIdx * kMaxLightsPerCluster + count] = i;
			++count;
		}

		g_clusterCounts[clusterIdx] = count;
	}
}
