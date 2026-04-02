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

	struct GpuGiLight
	{
		float4 positionRadius;
		float4 directionCone;
		float4 colourType;
	};

	struct GpuGiMaterial
	{
		float4 diffuse;
		float4 emissive;
		uint texelOffset;
		uint textureWidth;
		uint textureHeight;
		uint flags;
	};

	StructuredBuffer<VoxelTriangleData> g_voxelTriangles : register(t0);
	Texture3D<float4> g_prevVoxelRadiance : register(t1);
	Texture3D<float4> g_prevVoxelAlbedo : register(t2);
	StructuredBuffer<GpuGiLight> g_giLights : register(t3);
	StructuredBuffer<GpuGiMaterial> g_giMaterials : register(t4);
	StructuredBuffer<uint> g_giMaterialTexels : register(t5);
	SHADOWMAPS_RESOURCE(6);
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
		float4 g_giParams7;
		float4 g_giParams8;
		float4 g_giParams9;
	};

	bool IsPointInTriangle(float3 p, float3 a, float3 b, float3 c, float3 n)
	{
		const float eps = -0.0001f;
		const float3 c0 = cross(b - a, p - a);
		const float3 c1 = cross(c - b, p - b);
		const float3 c2 = cross(a - c, p - c);
		return dot(c0, n) >= eps && dot(c1, n) >= eps && dot(c2, n) >= eps;
	}

	float3 DecodeTexelToLinearFast(uint packedTexel, bool isBgra)
	{
		const float rSrgb = ((packedTexel >> (isBgra ? 16u : 0u)) & 0xffu) / 255.0f;
		const float gSrgb = ((packedTexel >> 8u) & 0xffu) / 255.0f;
		const float bSrgb = ((packedTexel >> (isBgra ? 0u : 16u)) & 0xffu) / 255.0f;
		const float3 srgb = max(float3(rSrgb, gSrgb, bSrgb), 1e-6f.xxx);
		// Fast approximation is enough for GI tinting; avoids expensive pow() in hot loops.
		return srgb * srgb;
	}

	float3 SampleMaterialAlbedo(in GpuGiMaterial material, float2 uv)
	{
		const bool hasTexture = (material.flags & 1u) != 0u;
		const bool isBgra = (material.flags & 2u) != 0u;
		float3 sampled = saturate(material.diffuse.rgb);
		if (!hasTexture || material.textureWidth == 0u || material.textureHeight == 0u)
			return sampled;

		const float2 wrappedUv = frac(uv);
		const uint x = min((uint)(wrappedUv.x * (float)material.textureWidth), material.textureWidth - 1u);
		const uint y = min((uint)(wrappedUv.y * (float)material.textureHeight), material.textureHeight - 1u);
		const uint localIdx = y * material.textureWidth + x;
		const uint texelIdx = material.texelOffset + localIdx;
		const uint packedTexel = g_giMaterialTexels[texelIdx];
		sampled = saturate(DecodeTexelToLinearFast(packedTexel, isBgra) * material.diffuse.rgb);
		return sampled;
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
		const uint shadowMode = (uint)max(g_giParams9.w, 0.0f);
		if (shadowMode == 0u)
			return 1.0f;

		uint width = 0u;
		uint height = 0u;
		GetShadowMapDimensions(cascadeIdx, width, height);
		if (width == 0u || height == 0u)
			return 1.0f;

		const int2 maxCoord = int2((int)width - 1, (int)height - 1);
		const int2 centerCoord = clamp((int2)(saturate(uv) * float2((float)width, (float)height)), int2(0, 0), maxCoord);
		if (shadowMode == 1u)
		{
			const float depth = LoadShadowDepth(cascadeIdx, centerCoord);
			return (compareDepth <= depth) ? 1.0f : 0.0f;
		}

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

	float3 EvaluateLocalLights(float3 positionWs, float3 normalWs, float3 albedo, float voxelSize)
	{
		const uint lightCount = (uint)max(g_giParams6.w, 0.0f);
		if (lightCount == 0u)
			return 0.0f.xxx;

		float3 accum = 0.0f.xxx;
		const uint maxLights = min(lightCount, 64u);
		[loop]
		for (uint i = 0u; i < maxLights; ++i)
		{
			const GpuGiLight light = g_giLights[i];
			const float3 toLight = light.positionRadius.xyz - positionWs;
			const float radius = max(light.positionRadius.w, 0.01f);
			const float dist2 = dot(toLight, toLight);
			const float radius2 = radius * radius;
			if (dist2 <= 1e-8f || dist2 >= radius2)
				continue;

			const float dist = sqrt(dist2);
			const float3 l = toLight / dist;
			const float ndotl = saturate(dot(normalWs, l));
			if (ndotl <= 0.0f)
				continue;

			float attenuation = saturate(1.0f - saturate(dist / radius));
			attenuation *= attenuation;

			const bool isSpot = light.colourType.w > 0.5f;
			if (isSpot)
			{
				const float3 spotDir = normalize(light.directionCone.xyz + 1e-6f.xxx);
				const float spotCos = saturate(dot(spotDir, -l));
				if (spotCos <= 0.0f)
					continue;
				const float cone = max(light.directionCone.w, 1.0f);
				const float coneAtten = pow(spotCos, cone);
				attenuation *= coneAtten * coneAtten;
			}

			const float influence = attenuation * ndotl;
			accum += light.colourType.rgb * influence;
		}

		const float lum = dot(accum, float3(0.2126f, 0.7152f, 0.0722f));
		accum = accum / (1.0f + lum * 0.5f);
		const float localInject = 2.5f;
		const float3 bounced = accum * (localInject * 0.55f) * saturate(albedo);
		return min(bounced, 32.0f.xxx);
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
		uint materialIndex = (uint)max(0.0f, tri.p0.w);
		uint materialCount = 0u;
		uint materialStride = 0u;
		g_giMaterials.GetDimensions(materialCount, materialStride);

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
		const uint maxVoxelTestsPerTri = max(1u, (uint)g_giParams9.z);
		uint step = 1u;
		while (((spanX + step - 1u) / step) * ((spanY + step - 1u) / step) * ((spanZ + step - 1u) / step) > maxVoxelTestsPerTri)
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
		const float triSunFacing = saturate(dot(n, toSunWs));
		const float sunDirectionality = saturate(g_giParams3.w);
		const bool gpuComputeBaseSunMode = g_giParams7.y > 0.5f;
		const bool sunShadowPerVoxel = g_giParams7.z > 0.5f;
		const float planeThickness = voxelSize * 1.5f;
		const float2 uv0 = tri.uv0uv1.xy;
		const float2 uv1 = tri.uv0uv1.zw;
		const float2 uv2 = tri.uv2Pad.xy;
		const float2 triUv = (uv0 + uv1 + uv2) * (1.0f / 3.0f);
		float3 triAlbedoBase = saturate(tri.albedoWeight.rgb);
		float3 emissiveProxy = 0.0f.xxx;
		if (materialIndex < materialCount)
		{
			const GpuGiMaterial materialProxy = g_giMaterials[materialIndex];
			// In GPU base/sun mode we must trust sampled material albedo fully; otherwise fallback proxy tint
			// (often near-white) washes out GI color bleed.
			const float proxyBlend = gpuComputeBaseSunMode ? 1.0f : saturate(g_giParams7.x);
			const float3 materialAlbedo = SampleMaterialAlbedo(materialProxy, triUv);
			triAlbedoBase = saturate(lerp(triAlbedoBase, materialAlbedo, proxyBlend));
			const float emissiveStrength = max(0.0f, materialProxy.emissive.a);
			emissiveProxy = saturate(materialProxy.emissive.rgb) * emissiveStrength;
		}

		// Shadow-map evaluation is expensive. Default to one per-triangle visibility sample
		// and only evaluate per-voxel when explicitly requested via cvar.
		const float3 triCenterWs = (p0 + p1 + p2) * (1.0f / 3.0f);
		float triSunVisibility = 1.0f;
		if (sunDirectionality > 0.001f && triSunFacing > 0.02f)
		{
			triSunVisibility = EvaluateSunShadowVisibility(triCenterWs, n, voxelSize);
		}

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
					float sunVisibility = triSunVisibility;
					if (sunShadowPerVoxel && sunDirectionality > 0.001f && triSunFacing > 0.02f)
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
					const float3 triAlbedo = triAlbedoBase;

					// Temporal damping at the voxel-injection stage to reduce frame-to-frame GI shimmer
					// from triangle-budget/coverage changes while moving clipmaps.
					const float warmStabilize = saturate((g_giParams1.x - 0.84f) * 8.0f);
					const float temporalKeep = lerp(0.80f, 0.94f, warmStabilize);
					const float albedoKeep = lerp(0.75f, 0.93f, warmStabilize);
					const float prevAlbedoW = saturate(previousAlbedo.a) * albedoKeep;
					const float triAlbedoW = saturate(tri.albedoWeight.a);
					const float albedoW = max(prevAlbedoW + triAlbedoW, 1e-4f);
					const float3 voxelAlbedo = saturate((previousAlbedo.rgb * prevAlbedoW + triAlbedo * triAlbedoW) / albedoW);
					const float albedoConfidence = saturate(max(previousAlbedo.a * albedoKeep, triAlbedoW));
					const float albedoInfluence = saturate(g_giParams6.z) * saturate(albedoConfidence * 1.25f);
					const float3 albedoTint = lerp(1.0f.xxx, voxelAlbedo, albedoInfluence);
					const float tintLuma = max(dot(albedoTint, float3(0.2126f, 0.7152f, 0.0722f)), 0.35f);
					float3 injected = tri.radianceOpacity.rgb * visibilityFactor * (albedoTint / tintLuma);
					const bool gpuComputeBaseSun = gpuComputeBaseSunMode;
					if (gpuComputeBaseSun)
					{
						const float diffuseInject = max(0.0f, g_giParams8.x);
						const float sunInject = max(0.0f, g_giParams8.y);
						const float sunBoost = max(0.0f, g_giParams8.z);
						const float emissiveInject = max(0.0f, g_giParams8.w);
						const float sunStrength = max(0.0f, g_giParams9.x);
						const float unlitBase = max(0.0f, g_giParams9.y);
						const float clipAttenuation = rcp(1.0f + (float)clipIdx * 0.5f);
						const float sunFacing = triSunFacing;
						const float triLuma = saturate(dot(triAlbedo, float3(0.2126f, 0.7152f, 0.0722f)));
						const float3 baseDiffuse = triAlbedo * (triLuma * diffuseInject * unlitBase * 0.60f * clipAttenuation);
						const float sunDirectional = sunFacing * sunStrength * sunInject * (0.45f + 0.55f * sunBoost);
						const float3 sunBounce = triAlbedo * (triLuma * sunDirectional * visibilityFactor * clipAttenuation);
						const float3 emissiveBounce = emissiveProxy * emissiveInject * clipAttenuation;
						injected = baseDiffuse + sunBounce + emissiveBounce;
					}
					else
					{
						injected += emissiveProxy * 0.25f;
					}
					injected += EvaluateLocalLights(voxelCenterWs, n, voxelAlbedo, voxelSize);
					const float injectedLum = dot(injected, float3(0.2126f, 0.7152f, 0.0722f));
					// If there's little/no new injection, reduce history retention so stale neutral energy fades out.
					const float injectionPresence = saturate(injectedLum * 2.5f);
					const float effectiveKeep = lerp(0.20f, temporalKeep, injectionPresence);
					float3 radiance = previous.rgb * effectiveKeep + injected * (1.0f - effectiveKeep);

					// Cap per-frame voxel radiance change to suppress visible bright/dark flicker.
					const float3 deltaLimit = 0.08f.xxx + previous.rgb * 0.30f;
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
