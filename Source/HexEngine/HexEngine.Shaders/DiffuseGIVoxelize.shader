"ComputeShaderIncludes"
{
	Global
}
"ComputeShader"
{
	struct VoxelTriangleData
	{
		float4 p0;
		float4 p1;
		float4 p2;
		float4 radianceOpacity;
		float4 albedoWeight;
		float4 uv0uv1;
		float4 uv2Pad;
	};

	StructuredBuffer<VoxelTriangleData> g_voxelTriangles : register(t0);
	Texture3D<float4> g_prevVoxelRadiance : register(t1);
	Texture3D<float4> g_prevVoxelAlbedo : register(t2);
	SHADOWMAPS_RESOURCE(3);
	RWTexture3D<float4> g_voxelRadianceOut : register(u0);
	RWTexture3D<float4> g_voxelAlbedoOut : register(u1);

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
		float4 g_giParams6;
	};

	bool IsPointInTriangle(float3 p, float3 a, float3 b, float3 c, float3 n)
	{
		const float eps = -0.0001f;
		const float3 c0 = cross(b - a, p - a);
		const float3 c1 = cross(c - b, p - b);
		const float3 c2 = cross(a - c, p - c);
		return dot(c0, n) >= eps && dot(c1, n) >= eps && dot(c2, n) >= eps;
	}

	bool ProjectToShadowCascade(uint cascadeIdx, float3 positionWs, out float2 uv, out float depth)
	{
		const float4 clipPos = mul(float4(positionWs, 1.0f), g_lightViewProjectionMatrix[cascadeIdx]);
		if (abs(clipPos.w) < 1e-6f)
		{
			uv = 0.0f.xx;
			depth = 0.0f;
			return false;
		}

		const float invW = rcp(clipPos.w);
		uv.x = clipPos.x * invW * 0.5f + 0.5f;
		uv.y = -clipPos.y * invW * 0.5f + 0.5f;
		depth = clipPos.z * invW;
		return all(uv >= 0.0f.xx) && all(uv <= 1.0f.xx) && depth > 0.0f && depth < 1.0f;
	}

	void GetShadowMapDimensions(uint cascadeIdx, out uint width, out uint height)
	{
		width = 0u;
		height = 0u;
		switch (cascadeIdx)
		{
		case 0u: SHADOWMAPS[0].GetDimensions(width, height); break;
		case 1u: SHADOWMAPS[1].GetDimensions(width, height); break;
		case 2u: SHADOWMAPS[2].GetDimensions(width, height); break;
		case 3u: SHADOWMAPS[3].GetDimensions(width, height); break;
		case 4u: SHADOWMAPS[4].GetDimensions(width, height); break;
		default: SHADOWMAPS[5].GetDimensions(width, height); break;
		}
	}

	float LoadShadowDepth(uint cascadeIdx, int2 coord)
	{
		switch (cascadeIdx)
		{
		case 0u: return SHADOWMAPS[0].Load(int3(coord, 0)).r;
		case 1u: return SHADOWMAPS[1].Load(int3(coord, 0)).r;
		case 2u: return SHADOWMAPS[2].Load(int3(coord, 0)).r;
		case 3u: return SHADOWMAPS[3].Load(int3(coord, 0)).r;
		case 4u: return SHADOWMAPS[4].Load(int3(coord, 0)).r;
		default: return SHADOWMAPS[5].Load(int3(coord, 0)).r;
		}
	}

	float SampleCascadeShadowVisibility(uint cascadeIdx, float2 uv, float compareDepth)
	{
		uint width = 0u;
		uint height = 0u;
		GetShadowMapDimensions(cascadeIdx, width, height);
		if (width == 0u || height == 0u)
			return 1.0f;

		const int2 maxCoord = int2((int)width - 1, (int)height - 1);
		const float2 texel = 1.0f / float2((float)width, (float)height);
		float visibility = 0.0f;

		[unroll]
		for (int oy = -1; oy <= 1; ++oy)
		{
			[unroll]
			for (int ox = -1; ox <= 1; ++ox)
			{
				const float2 sampleUv = saturate(uv + float2((float)ox, (float)oy) * texel);
				const int2 coord = clamp((int2)(sampleUv * float2((float)width, (float)height)), int2(0, 0), maxCoord);
				const float depth = LoadShadowDepth(cascadeIdx, coord);
				visibility += (compareDepth <= depth) ? 1.0f : 0.0f;
			}
		}

		return visibility / 9.0f;
	}

	float EvaluateSunShadowVisibility(float3 positionWs, float3 normalWs, float voxelSize)
	{
		const float3 sunDirWs = normalize(g_giParams3.xyz + float3(1e-6f, 1e-6f, 1e-6f));
		const float ndotl = saturate(dot(normalWs, -sunDirWs));
		const float normalBiasWs = voxelSize * (0.35f + (1.0f - ndotl) * 1.1f);
		const float3 shadowPosWs = positionWs + normalWs * normalBiasWs;
		const float compareBias = max(0.00005f, voxelSize * (0.0009f + (1.0f - ndotl) * 0.0015f));

		[unroll]
		for (uint cascade = 0u; cascade < 6u; ++cascade)
		{
			float2 uv = 0.0f.xx;
			float depth = 0.0f;
			if (!ProjectToShadowCascade(cascade, shadowPosWs, uv, depth))
				continue;

			return SampleCascadeShadowVisibility(cascade, uv, depth - compareBias);
		}

		// Outside all cascades: keep GI stable and avoid over-occluding by defaulting to lit.
		return 1.0f;
	}

	[numthreads(64, 1, 1)]
	void ShaderMain(uint3 tid : SV_DispatchThreadID)
	{
		uint triangleCount = 0;
		uint triangleStride = 0;
		g_voxelTriangles.GetDimensions(triangleCount, triangleStride);
		if (tid.x >= triangleCount)
			return;

		const VoxelTriangleData tri = g_voxelTriangles[tid.x];

		const uint clipIdx = min((uint)g_giParams0.w, 3u);
		const float3 clipCenter = g_clipCenterExtent[clipIdx].xyz;
		const float clipExtent = max(1e-3f, g_clipCenterExtent[clipIdx].w);
		const uint voxelRes = max(1u, (uint)g_clipVoxelInfo[clipIdx].z);
		if (voxelRes <= 1u)
			return;
		const float voxelSize = max(1e-4f, g_clipVoxelInfo[clipIdx].x);

		const float3 p0 = tri.p0.xyz;
		const float3 p1 = tri.p1.xyz;
		const float3 p2 = tri.p2.xyz;

		const float3 triMinWs = min(p0, min(p1, p2));
		const float3 triMaxWs = max(p0, max(p1, p2));
		const float3 clipMinWs = clipCenter - clipExtent;
		const float3 clipMaxWs = clipCenter + clipExtent;
		if (any(triMaxWs < clipMinWs) || any(triMinWs > clipMaxWs))
			return;

		const float3 uvwMin = ((triMinWs - clipCenter) / (clipExtent * 2.0f)) + 0.5f;
		const float3 uvwMax = ((triMaxWs - clipCenter) / (clipExtent * 2.0f)) + 0.5f;
		const float3 uvwLo = saturate(min(uvwMin, uvwMax));
		const float3 uvwHi = saturate(max(uvwMin, uvwMax));

		const uint3 voxelMin = min((uint3)(uvwLo * (float)(voxelRes - 1u)), voxelRes - 1u);
		const uint3 voxelMax = min((uint3)(uvwHi * (float)(voxelRes - 1u)), voxelRes - 1u);

		const uint spanX = max(1u, voxelMax.x - voxelMin.x + 1u);
		const uint spanY = max(1u, voxelMax.y - voxelMin.y + 1u);
		const uint spanZ = max(1u, voxelMax.z - voxelMin.z + 1u);
		uint step = 1u;
		while (((spanX + step - 1u) / step) * ((spanY + step - 1u) / step) * ((spanZ + step - 1u) / step) > 128u)
		{
			++step;
		}

		const float3 nRaw = cross(p1 - p0, p2 - p0);
		const float nLen = length(nRaw);
		if (nLen < 1e-6f)
			return;
		const float3 n = nRaw / nLen;
		const float3 sunDirWs = normalize(g_giParams3.xyz + float3(1e-6f, 1e-6f, 1e-6f));
		const float3 toSunWs = -sunDirWs;
		const float sunDirectionality = saturate(g_giParams3.w);
		const float planeThickness = voxelSize * 1.5f;

		for (uint z = voxelMin.z; z <= voxelMax.z; z += step)
		{
			for (uint y = voxelMin.y; y <= voxelMax.y; y += step)
			{
				for (uint x = voxelMin.x; x <= voxelMax.x; x += step)
				{
					const uint3 coord = uint3(x, y, z);
					const float3 uvw = (float3(coord) + 0.5f) / (float)voxelRes;
					const float3 voxelCenterWs = (uvw - 0.5f.xxx) * (clipExtent * 2.0f) + clipCenter;
					const float signedDist = dot(n, voxelCenterWs - p0);
					if (abs(signedDist) > planeThickness)
						continue;

					const float3 projected = voxelCenterWs - n * signedDist;
					if (!IsPointInTriangle(projected, p0, p1, p2, n))
						continue;

					// Evaluate direct sun visibility from the real shadow cascades first.
					float sunVisibility = 1.0f;
					if (sunDirectionality > 0.001f)
					{
						sunVisibility = EvaluateSunShadowVisibility(voxelCenterWs, n, voxelSize);

						// Add short-range occupancy occlusion from last-frame voxels to retain local thickness.
						float localVoxelOcclusion = 1.0f;
						[unroll]
						for (uint s = 1u; s <= 2u; ++s)
						{
							const float rayDist = voxelSize * (2.0f * (float)s);
							const float3 sampleWs = voxelCenterWs + toSunWs * rayDist;
							const float3 sampleUVW = ((sampleWs - clipCenter) / (clipExtent * 2.0f)) + 0.5f;
							if (any(sampleUVW < 0.0f.xxx) || any(sampleUVW > 1.0f.xxx))
								continue;

							const uint3 sampleCoord = min((uint3)(sampleUVW * (float)(voxelRes - 1u)), voxelRes - 1u);
							const float occ = saturate(g_prevVoxelRadiance.Load(int4(sampleCoord, 0)).a);
							localVoxelOcclusion *= (1.0f - occ * (0.30f + 0.15f * sunDirectionality));
						}

						sunVisibility *= lerp(1.0f, localVoxelOcclusion, 0.35f);
					}

					const float4 previous = g_voxelRadianceOut[coord];
					const float4 previousAlbedo = g_prevVoxelAlbedo[coord];
					// Keep a non-directional floor so directional shadowing never wipes out all indirect bounce.
					const float visibilityFactor = max(
						lerp(1.0f, sunVisibility, 0.90f * sunDirectionality),
						lerp(1.0f, 0.08f, sunDirectionality));
					const float3 triAlbedo = saturate(tri.albedoWeight.rgb);

					// Temporal damping at the voxel-injection stage to reduce frame-to-frame GI shimmer
					// from triangle-budget/coverage changes while moving clipmaps.
					const float warmStabilize = saturate((g_giParams1.x - 0.84f) * 8.0f);
					const float shiftSettle = saturate(g_giParams6.y);
					const float temporalKeepBase = lerp(0.80f, 0.94f, warmStabilize);
					const float temporalKeep = lerp(temporalKeepBase, 0.95f, shiftSettle * 0.45f);
					const float albedoKeepBase = lerp(0.75f, 0.93f, warmStabilize);
					const float albedoKeep = lerp(albedoKeepBase, 0.96f, shiftSettle * 0.55f);
					const float prevAlbedoW = saturate(previousAlbedo.a) * albedoKeep;
					const float triAlbedoW = saturate(tri.albedoWeight.a);
					const float albedoW = max(prevAlbedoW + triAlbedoW, 1e-4f);
					const float3 voxelAlbedo = saturate((previousAlbedo.rgb * prevAlbedoW + triAlbedo * triAlbedoW) / albedoW);
					const float albedoConfidence = saturate(max(previousAlbedo.a * albedoKeep, triAlbedoW));
					const float albedoInfluence = saturate(g_giParams6.z) * saturate(albedoConfidence * 1.25f);
					const float3 albedoTint = lerp(1.0f.xxx, voxelAlbedo, albedoInfluence);
					const float tintLuma = max(dot(albedoTint, float3(0.2126f, 0.7152f, 0.0722f)), 0.35f);
					const float motionInjectScale = lerp(1.0f, 0.82f, shiftSettle);
					const float3 injected = tri.radianceOpacity.rgb * visibilityFactor * (albedoTint / tintLuma) * motionInjectScale;
					const float injectedLum = dot(injected, float3(0.2126f, 0.7152f, 0.0722f));
					// If there's little/no new injection, reduce history retention so stale neutral energy fades out.
					const float injectionPresence = saturate(injectedLum * 2.5f);
					const float minKeep = lerp(0.20f, 0.28f, shiftSettle);
					const float effectiveKeep = lerp(minKeep, temporalKeep, injectionPresence);
					float3 radiance = previous.rgb * effectiveKeep + injected * (1.0f - effectiveKeep);

					// Cap per-frame voxel radiance change to suppress visible bright/dark flicker.
					const float3 baseDeltaLimit = 0.08f.xxx + previous.rgb * 0.30f;
					const float3 settleDeltaLimit = 0.055f.xxx + previous.rgb * 0.22f;
					const float3 deltaLimit = lerp(baseDeltaLimit, settleDeltaLimit, shiftSettle * 0.85f);
					radiance = clamp(radiance, previous.rgb - deltaLimit, previous.rgb + deltaLimit);
					radiance = min(radiance, 32.0f.xxx);

					const float opacity = max(previous.a, tri.radianceOpacity.a);
					g_voxelRadianceOut[coord] = float4(radiance, opacity);
					g_voxelAlbedoOut[coord] = float4(voxelAlbedo, albedoConfidence);
				}
			}
		}
	}
}
