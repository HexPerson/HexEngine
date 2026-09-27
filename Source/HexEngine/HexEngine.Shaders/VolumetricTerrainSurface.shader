"Requirements"
{
	GBuffer
}
"InputLayout"
{
	None
}
"VertexShaderIncludes"
{
	Global
}
"PixelShaderIncludes"
{
	Global
	Utils
	PBRutils
}
"VertexShader"
{
	struct TerrainTriangleGpu
	{
		float4 p0;
		float4 p1;
		float4 p2;
		float4 n0;
		float4 n1;
		float4 n2;
	};

	StructuredBuffer<TerrainTriangleGpu> g_surfaceTriangles : register(t0);

	struct VSOut
	{
		float4 position : SV_Position;
		float3 worldPos : TEXCOORD0;
		float3 normal : TEXCOORD1;
		// Unjittered clip-space positions for the current and previous frame, used by the PS
		// to compute per-pixel motion vectors. Terrain is static, so the only motion comes
		// from the camera (encoded in the difference between g_viewProjectionMatrix and
		// g_viewProjectionMatrixPrev). Without these the velocity RT stays zero and TAA
		// reprojection cannot keep history aligned with the surface, producing visible
		// ghosting whenever the camera moves.
		float4 currentPositionUnjittered  : TEXCOORD2;
		float4 previousPositionUnjittered : TEXCOORD3;
	};

	VSOut ShaderMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
	{
		VSOut o;
		const TerrainTriangleGpu tri = g_surfaceTriangles[instanceId];
		float3 worldPos = tri.p0.xyz;
		float3 normal = tri.n0.xyz;
		if (vertexId == 1u)
		{
			worldPos = tri.p1.xyz;
			normal = tri.n1.xyz;
		}
		else if (vertexId == 2u)
		{
			worldPos = tri.p2.xyz;
			normal = tri.n2.xyz;
		}

		o.worldPos = worldPos;
		o.normal = normalize(normal);

		// Compute current/previous clip positions; these feed CalcVelocity in the pixel
		// shader so TAA can reproject this frame's terrain pixels onto where the same world
		// point appeared in last frame's history.
		const float4 currClip = mul(float4(worldPos, 1.0f), g_viewProjectionMatrix);
		const float4 prevClip = mul(float4(worldPos, 1.0f), g_viewProjectionMatrixPrev);
		o.currentPositionUnjittered  = currClip;
		o.previousPositionUnjittered = prevClip;

		o.position = currClip;
		o.position.xy += g_jitterOffsets * o.position.w;
		return o;
	}
}
"PixelShader"
{
	Texture3D<float4> g_materialWeights : register(t0);
	Texture2D g_grassAlbedoMap : register(t1);
	Texture2D g_rockAlbedoMap : register(t2);
	Texture2D g_snowAlbedoMap : register(t3);
	Texture2D g_dirtAlbedoMap : register(t4);
	Texture2D g_grassNormalMap : register(t5);
	Texture2D g_rockNormalMap : register(t6);
	Texture2D g_snowNormalMap : register(t7);
	Texture2D g_dirtNormalMap : register(t8);
	Texture2D g_grassAoMap : register(t9);
	Texture2D g_rockAoMap : register(t10);
	Texture2D g_snowAoMap : register(t11);
	Texture2D g_dirtAoMap : register(t12);
	Texture2D g_grassHeightMap : register(t13);
	Texture2D g_rockHeightMap : register(t14);
	Texture2D g_snowHeightMap : register(t15);
	Texture2D g_dirtHeightMap : register(t16);
	Texture2D g_grassRoughnessMap : register(t17);
	Texture2D g_rockRoughnessMap : register(t18);
	Texture2D g_snowRoughnessMap : register(t19);
	Texture2D g_dirtRoughnessMap : register(t20);
	Texture2D g_grassMetallicMap : register(t21);
	Texture2D g_rockMetallicMap : register(t22);
	Texture2D g_snowMetallicMap : register(t23);
	Texture2D g_dirtMetallicMap : register(t24);
	SamplerState g_textureSampler : register(s0);

	struct VSOut
	{
		float4 position : SV_Position;
		float3 worldPos : TEXCOORD0;
		float3 normal : TEXCOORD1;
		// Must match the VS output layout. PS reads these to compute motion vectors.
		float4 currentPositionUnjittered  : TEXCOORD2;
		float4 previousPositionUnjittered : TEXCOORD3;
	};

	cbuffer VolumetricTerrainChunkBuffer : register(b6)
	{
		float4 g_chunkOriginVoxel;
		float4 g_chunkWorldInfo;
		float4 g_chunkGrassColor;
		float4 g_chunkRockColor;
		float4 g_chunkSnowColor;
		float4 g_chunkDirtColor;
	};

	float4 SampleMaterialWeights(float3 worldPos)
	{
		const int baseResolution = (int)g_chunkWorldInfo.y;
		const float voxelSize = max(g_chunkOriginVoxel.w, 0.0001f);
		const float maxLocal = max((float)baseResolution, 1.0f);
		float3 local = (worldPos - g_chunkOriginVoxel.xyz) / voxelSize;
		float3 uvw = saturate(local / maxLocal);
		float4 weights = g_materialWeights.SampleLevel(g_textureSampler, uvw, 0);
		float weightSum = max(weights.x + weights.y + weights.z + weights.w, 0.0001f);
		return weights / weightSum;
	}

	float3 UnpackNormalMap(float4 sampleValue)
	{
		return normalize(sampleValue.xyz * 2.0f - 1.0f);
	}

	// Triplanar weights are computed ONCE per pixel (they used to be recomputed
	// inside every one of the 24 map samples). Axes below the threshold are
	// dropped and the rest renormalised, so each helper below fetches only
	// the axes that matter - most terrain pixels have one dominant axis, and
	// 3 fetches per map become 1. This is the single biggest part of taking
	// the terrain from 72 texture fetches per pixel to a typical 6-12.
	static const float kTriplanarAxisEps = 0.03f;

	float3 ComputeTriplanarWeights(float3 normal)
	{
		float3 weights = pow(abs(normal), 4.0f);
		weights /= max(weights.x + weights.y + weights.z, 0.0001f);
		// step() masking instead of a vector ternary: fxc (DXBC) accepts
		// vector ?: but DXC (DXIL/SM6) requires select() - step() compiles
		// identically on both.
		weights *= step(kTriplanarAxisEps.xxx, weights);
		return weights / max(weights.x + weights.y + weights.z, 0.0001f);
	}

	float3 SampleTriplanarAlbedo(Texture2D tex, float3 worldPos, float3 weights, float scale, float3 fallbackColor)
	{
		float3 sampled = 0.0f.xxx;
		[branch] if (weights.x > 0.0f) sampled += tex.Sample(g_textureSampler, worldPos.yz * scale).rgb * weights.x;
		[branch] if (weights.y > 0.0f) sampled += tex.Sample(g_textureSampler, worldPos.xz * scale).rgb * weights.y;
		[branch] if (weights.z > 0.0f) sampled += tex.Sample(g_textureSampler, worldPos.xy * scale).rgb * weights.z;
		return lerp(fallbackColor, sampled, 0.9f);
	}

	float SampleTriplanarScalar(Texture2D tex, float3 worldPos, float3 weights, float scale, float fallbackValue)
	{
		float sampled = 0.0f;
		[branch] if (weights.x > 0.0f) sampled += tex.Sample(g_textureSampler, worldPos.yz * scale).r * weights.x;
		[branch] if (weights.y > 0.0f) sampled += tex.Sample(g_textureSampler, worldPos.xz * scale).r * weights.y;
		[branch] if (weights.z > 0.0f) sampled += tex.Sample(g_textureSampler, worldPos.xy * scale).r * weights.z;
		return lerp(fallbackValue, sampled, 0.9f);
	}

	float3 SampleTriplanarDetailNormal(Texture2D tex, float3 worldPos, float3 baseNormal, float3 weights, float scale)
	{
		float3 sum = 0.0f.xxx;
		[branch] if (weights.x > 0.0f)
		{
			float3 nx = UnpackNormalMap(tex.Sample(g_textureSampler, worldPos.yz * scale));
			nx = normalize(float3(nx.z * sign(baseNormal.x == 0.0f ? 1.0f : baseNormal.x), nx.x, nx.y));
			sum += nx * weights.x;
		}
		[branch] if (weights.y > 0.0f)
		{
			float3 ny = UnpackNormalMap(tex.Sample(g_textureSampler, worldPos.xz * scale));
			ny = normalize(float3(ny.x, ny.z * sign(baseNormal.y == 0.0f ? 1.0f : baseNormal.y), ny.y));
			sum += ny * weights.y;
		}
		[branch] if (weights.z > 0.0f)
		{
			float3 nz = UnpackNormalMap(tex.Sample(g_textureSampler, worldPos.xy * scale));
			nz = normalize(float3(nz.x, nz.y, nz.z * sign(baseNormal.z == 0.0f ? 1.0f : baseNormal.z)));
			sum += nz * weights.z;
		}
		return dot(sum, sum) > 1e-8f ? normalize(sum) : baseNormal;
	}

	// Everything one terrain layer contributes, fetched only when the layer
	// actually has weight here (see ShaderMain) and with the detail maps
	// faded to their fallbacks with distance - at a couple of hundred metres
	// a triplanar detail normal or roughness map is sub-pixel, so its only
	// contribution is shimmer and fetch cost.
	struct TerrainLayerSample
	{
		float height;
		float3 albedo;
		float ao;
		float3 detailNormal;
		float roughness;
		float metallic;
	};

	TerrainLayerSample SampleTerrainLayer(
		Texture2D heightMap, Texture2D albedoMap, Texture2D aoMap, Texture2D normalMap, Texture2D roughnessMap, Texture2D metallicMap,
		float3 worldPos, float3 N, float3 triW, float scale, float3 fallbackColour, float detailFade)
	{
		TerrainLayerSample l;
		l.height = 0.5f;
		l.albedo = SampleTriplanarAlbedo(albedoMap, worldPos, triW, scale, fallbackColour);
		l.ao = 1.0f;
		l.detailNormal = N;
		l.roughness = 1.0f;
		l.metallic = 1.0f;
		[branch] if (detailFade > 0.001f)
		{
			l.height = lerp(0.5f, SampleTriplanarScalar(heightMap, worldPos, triW, scale, 0.5f), detailFade);
			l.ao = lerp(1.0f, SampleTriplanarScalar(aoMap, worldPos, triW, scale, 1.0f), detailFade);
			l.detailNormal = normalize(lerp(N, SampleTriplanarDetailNormal(normalMap, worldPos, N, triW, scale), detailFade));
			l.roughness = lerp(1.0f, SampleTriplanarScalar(roughnessMap, worldPos, triW, scale, 1.0f), detailFade);
			l.metallic = lerp(1.0f, SampleTriplanarScalar(metallicMap, worldPos, triW, scale, 1.0f), detailFade);
		}
		return l;
	}

	GBufferOut ShaderMain(VSOut input)
	{
		GBufferOut output;
		float3 interpolatedNormal = normalize(input.normal);
		float3 geometricNormal = normalize(cross(ddy(input.worldPos), ddx(input.worldPos)));
		if (dot(geometricNormal, interpolatedNormal) < 0.0f)
		{
			geometricNormal = -geometricNormal;
		}
		float3 N = normalize(lerp(geometricNormal, interpolatedNormal, 0.7f));
		float4 layerWeights = SampleMaterialWeights(input.worldPos);

		// Fetch budget (was 6 maps x 4 layers x 3 axes = 72 per pixel, always):
		//  - triplanar axes below threshold are skipped (ComputeTriplanarWeights)
		//  - a layer with ~zero blend weight fetches nothing
		//  - detail maps fade to fallbacks between 120 m and 220 m; beyond that
		//    only albedo is sampled (the detail is sub-pixel out there).
		const float3 triW = ComputeTriplanarWeights(N);
		const float viewDist = length(input.worldPos - g_eyePos.xyz);
		const float detailFade = 1.0f - smoothstep(120.0f, 220.0f, viewDist);
		const float kLayerEps = 0.004f;

		TerrainLayerSample grass, rock, snow, dirt;
		grass.height = 0.5f; grass.albedo = g_chunkGrassColor.rgb; grass.ao = 1.0f; grass.detailNormal = N; grass.roughness = 1.0f; grass.metallic = 1.0f;
		rock = grass;  rock.albedo = g_chunkRockColor.rgb;
		snow = grass;  snow.albedo = g_chunkSnowColor.rgb;
		dirt = grass;  dirt.albedo = g_chunkDirtColor.rgb;
		[branch] if (layerWeights.x > kLayerEps)
			grass = SampleTerrainLayer(g_grassHeightMap, g_grassAlbedoMap, g_grassAoMap, g_grassNormalMap, g_grassRoughnessMap, g_grassMetallicMap, input.worldPos, N, triW, 0.075f, g_chunkGrassColor.rgb, detailFade);
		[branch] if (layerWeights.y > kLayerEps)
			rock = SampleTerrainLayer(g_rockHeightMap, g_rockAlbedoMap, g_rockAoMap, g_rockNormalMap, g_rockRoughnessMap, g_rockMetallicMap, input.worldPos, N, triW, 0.045f, g_chunkRockColor.rgb, detailFade);
		[branch] if (layerWeights.z > kLayerEps)
			snow = SampleTerrainLayer(g_snowHeightMap, g_snowAlbedoMap, g_snowAoMap, g_snowNormalMap, g_snowRoughnessMap, g_snowMetallicMap, input.worldPos, N, triW, 0.02f, g_chunkSnowColor.rgb, detailFade);
		[branch] if (layerWeights.w > kLayerEps)
			dirt = SampleTerrainLayer(g_dirtHeightMap, g_dirtAlbedoMap, g_dirtAoMap, g_dirtNormalMap, g_dirtRoughnessMap, g_dirtMetallicMap, input.worldPos, N, triW, 0.06f, g_chunkDirtColor.rgb, detailFade);

		const float grassHeight = grass.height, rockHeight = rock.height, snowHeight = snow.height, dirtHeight = dirt.height;
		float4 heightWeighted = layerWeights * float4(
			lerp(0.75f, 1.25f, grassHeight),
			lerp(0.75f, 1.25f, rockHeight),
			lerp(0.75f, 1.25f, snowHeight),
			lerp(0.75f, 1.25f, dirtHeight));
		heightWeighted /= max(heightWeighted.x + heightWeighted.y + heightWeighted.z + heightWeighted.w, 0.0001f);

		const float3 grassAlbedo = grass.albedo, rockAlbedo = rock.albedo, snowAlbedo = snow.albedo, dirtAlbedo = dirt.albedo;
		const float grassAo = grass.ao, rockAo = rock.ao, snowAo = snow.ao, dirtAo = dirt.ao;
		const float3 grassDetailNormal = grass.detailNormal, rockDetailNormal = rock.detailNormal, snowDetailNormal = snow.detailNormal, dirtDetailNormal = dirt.detailNormal;
		const float grassRoughness = grass.roughness, rockRoughness = rock.roughness, snowRoughness = snow.roughness, dirtRoughness = dirt.roughness;
		const float grassMetallic = grass.metallic, rockMetallic = rock.metallic, snowMetallic = snow.metallic, dirtMetallic = dirt.metallic;

		float3 baseColor =
			(grassAlbedo * grassAo * heightWeighted.x) +
			(rockAlbedo * rockAo * heightWeighted.y) +
			(snowAlbedo * snowAo * heightWeighted.z) +
			(dirtAlbedo * dirtAo * heightWeighted.w);

		float3 detailNormal =
			(grassDetailNormal * heightWeighted.x) +
			(rockDetailNormal * heightWeighted.y) +
			(snowDetailNormal * heightWeighted.z) +
			(dirtDetailNormal * heightWeighted.w);
		// Macro (pre-detail) up-ness, used below to gate the wet SSR so only FLAT
		// wet ground reflects - sloped/bumpy wet terrain reflecting off its detail
		// normal scatters into dark geometry (the dark edges).
		const float macroUp = N.y;
		N = normalize(lerp(N, normalize(detailNormal), 0.55f));

		float metallic =
			(0.02f * grassMetallic * heightWeighted.x) +
			(0.03f * rockMetallic * heightWeighted.y) +
			(0.01f * snowMetallic * heightWeighted.z) +
			(0.02f * dirtMetallic * heightWeighted.w);
		float roughness =
			(0.90f * grassRoughness * heightWeighted.x) +
			(0.72f * rockRoughness * heightWeighted.y) +
			(0.52f * snowRoughness * heightWeighted.z) +
			(0.86f * dirtRoughness * heightWeighted.w);
		// Smoothness is the SSR enable gate (mat.b - read by SSR as both the
		// "should this pixel reflect?" test and the reflection-sharpness scale).
		// Previously the blend baked in per-layer smoothness contributions
		// (0.08..0.24) which looked matte at a glance but triggered noticeable
		// SSR pulls of nearby buildings / sky onto open ground - the "terrain
		// mistakenly reflecting" symptom. Terrain (grass / rock / dirt / dry
		// snow) has essentially zero macroscopic specular gloss in real life;
		// the high-frequency glare you do see on damp grass or fresh snow is
		// the weather/wetness layer's job, not the base terrain shader's.
		// Force smoothness to 0 so SSR is disabled for dry terrain pixels.
		float smoothness = 0.0f;

		// Universal wet response (the "weather/wetness layer" the comment
		// above defers to). Terrain darkens and glosses with rain like every
		// other opaque surface - see PBRutils::ApplyWetSurface. Wetness also
		// opens the SSR gate proportionally (w^2, matching the film curve):
		// dry terrain keeps smoothness 0 and never reflects, a rain-soaked
		// field mirrors sky/buildings the way a wet street does.
		// Shelter occlusion + melt-fed wetness - matches DefaultPixel.
		const float __shelter = SampleRainShelter(input.worldPos, g_textureSampler);
		const float __shelteredWetness = saturate(
			(g_weatherSurface.wetness
				+ g_weatherSurface.snowCoverage * g_weatherSurface.snowMelt * 0.6f) * __shelter);
		// Sea contact (underwater S4) - lockstep with DefaultPixel; see
		// PBRutils::OceanContactWetness. Terrain is where this matters most:
		// the beach and the seabed are terrain.
		float __seaSubmerged;
		const float __surfaceWetness = max(__shelteredWetness, OceanContactWetness(input.worldPos, __seaSubmerged));
		if (__surfaceWetness > 0.001f)
		{
			const float __wetFilm = ApplyWetSurfaceSea(baseColor, roughness, metallic,
				__surfaceWetness, __seaSubmerged, g_wetnessDarkening);
			// Open SSR only on FLAT wet ground (a puddle-like mirror the SSR's
			// puddle-flatten then handles cleanly). Sloped/bumpy wet terrain kept
			// at smoothness 0 - its detail-normal reflection scatters dark (the
			// dark edges the user saw; r_ssr 0 removed them). macroUp is the
			// pre-detail surface up-ness.
			const float __wetFlat = saturate((macroUp - 0.80f) / 0.20f);
			smoothness = __wetFilm * 0.9f * __wetFlat;
			N = ApplyRainRipples(N, input.worldPos, g_time,
				__shelteredWetness * saturate(g_weatherSurface.precipitationIntensity) * (1.0f - __seaSubmerged));
		}

		// Snow accumulation. Same global driver from g_weatherSurface.snowCoverage
		// that DefaultPixel / DefaultAnimated / graph-compiled PSs use - just
		// called explicitly here so terrain (which has its own surface shader,
		// not Default) also catches snow. See ApplySnowAccumulation in
		// PBRutils.shader for the full doc.
		// Dust then snow - matches DefaultPixel.
		const float __dustAmount = g_weatherSurface.dirtAmount * (0.5f + 0.5f * __shelter);
		if (__dustAmount > 0.001f)
		{
			const float4 __dustResult = ApplyDustAccumulation(baseColor, roughness, N, input.worldPos, __dustAmount, g_textureSampler);
			baseColor = __dustResult.rgb;
			roughness = __dustResult.w;
		}

		// Weather snow - terrain parity with the mesh snow SHELL. The mesh path
		// draws a second tessellated Crusted_snow2 layer (SnowShell.shader); the
		// terrain has no shell, so here we accumulate the terrain's OWN snow-layer
		// textures (the real crusted-snow albedo / normal / roughness sampled far
		// above) driven by the same weather coverage. That reads as textured,
		// relief-shaded snow matching the meshes - NOT the flat procedural white
		// ApplySnowAccumulation produced (whose POM self-occlusion also dark-edged
		// on slopes, the artifact the user flagged). Masking mirrors
		// ApplySnowAccumulation exactly (slope + patch noise + coverage + melt +
		// wall drift banks) so terrain snow COVERAGE tracks the meshes 1:1; only
		// the shaded surface comes from the terrain's textures instead of procedural
		// white. Slot note: the shell's Crusted_snow2 lives at t22/t23, occupied on
		// terrain by rock/snow metallic, so we can't sample the identical texture
		// without a plugin-side rebind - the terrain's own snow layer is the parity
		// source (point it at Crusted_snow2 art-side for a pixel-exact match).
		const float __shelteredSnow = g_weatherSurface.snowCoverage * __shelter;
		if (__shelteredSnow > 0.001f)
		{
			const float __melt = saturate(g_weatherSurface.snowMelt);
			const float __slopeMask = smoothstep(0.35f, 0.85f, macroUp);
			if (__slopeMask > 0.0f)
			{
				// Two-octave patch noise (same 0.45 m scale as ApplySnowAccumulation)
				// so cover reads as drifted snow, not a uniform coat. Melt erodes the
				// THRESHOLD - thin snow retreats first, drift cores survive longest.
				const float __kNoise = 0.45f;
				const float __n1 = ValueNoise3(float3(input.worldPos.x, 0.0f, input.worldPos.z) / __kNoise);
				const float __n2 = ValueNoise3(float3(input.worldPos.x, 0.0f, input.worldPos.z) / (__kNoise * 0.4f));
				const float __patch = saturate((__n1 * 0.65f + __n2 * 0.35f)
					- (1.0f - g_weatherSurface.snowCoverage) * 0.45f
					- __melt * 0.55f);
				// Coverage matches the SHELL, which covers FULLY wherever it is
				// drawn (albedo = solid snow-white, no patch-noise holes) and only
				// tapers at melt/shelter/slope/edge. ApplySnowAccumulation's
				// (0.2 + patch*1.4) term punched coverage holes even under a heavy
				// blanket - that is exactly why terrain read as "less snow" than the
				// meshes. Instead drive coverage from snowCoverage and let the patch
				// noise carve only the THIN dusting at low coverage, filling in
				// completely (fill -> 1) as the blanket deepens.
				const float __sc = saturate(g_weatherSurface.snowCoverage);
				const float __fill = saturate((0.25f + __patch * 1.4f) + __sc * __sc);
				float __snowMask = saturate(__slopeMask * __sc * __fill)
					* (1.0f - __melt * 0.7f);

				// Wall drift banks (mirror ApplySnowAccumulation): snow piles where
				// the rain-occlusion map shows a near wall/prop face to the side.
				float __drift = 0.0f;
				if (g_rainOcclusionParams.x > 0.5f)
				{
					const float4 __clip = mul(float4(input.worldPos, 1.0f), g_rainOcclusionVP);
					const float2 __uv = __clip.xy * float2(0.5f, -0.5f) + 0.5f;
					if (all(__uv >= 0.0f) && all(__uv <= 1.0f) && __clip.z > 0.0f && __clip.z < 1.0f)
					{
						const float2 __stepUv = g_rainOcclusionParams.z * 6.0f;
						const float __wallDelta = 1.5f / 160.0f;
						[unroll]
						for (int __i = 0; __i < 4; ++__i)
						{
							const float2 __o = float2((__i & 1) ? __stepUv.x : -__stepUv.x,
							                          (__i & 2) ? __stepUv.y : -__stepUv.y);
							const float __nd = g_rainOcclusionMap.SampleLevel(g_textureSampler, __uv + __o, 0);
							__drift += (__clip.z - __nd > __wallDelta) ? 0.25f : 0.0f;
						}
						__drift *= (1.0f - __melt);
					}
				}
				__snowMask = saturate(__snowMask + __drift * 0.5f * __slopeMask * g_weatherSurface.snowCoverage);

				if (__snowMask > 0.001f)
				{
					// Surface = the terrain's real crusted-snow albedo lifted toward
					// the shell's bright blue-white (Crusted_snow2 reads ~this) so it
					// matches the meshes regardless of the terrain snow layer's base
					// tint; the height-field crevice adds volume like the shell PS.
					const float __crevice = SnowHeightField(input.worldPos.xz);
					const float3 __snowTint = float3(0.92f, 0.94f, 0.98f) * (0.86f + 0.14f * __crevice);
					baseColor = lerp(baseColor, snowAlbedo * __snowTint, __snowMask);

					// Relief: bend toward world-up (the shell forces a world-up snow
					// normal so drifts lie flat) plus a height-field drift gradient so
					// clumps shade, then a touch of the snow layer's detail normal.
					const float __kd = 0.09f;
					const float __hC = SnowHeightField(input.worldPos.xz);
					const float __hX = SnowHeightField(input.worldPos.xz + float2(__kd, 0.0f));
					const float __hZ = SnowHeightField(input.worldPos.xz + float2(0.0f, __kd));
					const float __amp = 0.18f * __snowMask * (1.0f - __melt) * (1.0f + __drift * 1.5f);
					const float3 __relief = normalize(float3(
						-(__hX - __hC) / __kd * __amp,
						1.0f,
						-(__hZ - __hC) / __kd * __amp));
					const float3 __snowN = normalize(lerp(__relief, snowDetailNormal, 0.25f));
					N = normalize(lerp(N, __snowN, __snowMask * 0.85f));

					// Snow is matte and highly diffuse - roughness UP, and close the
					// SSR gate under snow (dry snow doesn't mirror; melt->wet is the
					// wet layer's job, and it already ran above).
					roughness = lerp(roughness, max(snowRoughness, 0.85f), __snowMask);
					smoothness = lerp(smoothness, 0.0f, __snowMask);
				}
			}
		}

		float4 viewPos = mul(float4(input.worldPos, 1.0f), g_viewMatrix);
		float pixelDepth = -viewPos.z;

		output.diff = float4(baseColor, 1.0f);
		// .a was the per-layer specularProbability blend - removed along with the
		// MaterialProperties::specularProbability field, since nothing in the
		// renderer actually sampled mat.a. Writes 0 to keep the channel clean.
		// .b = smoothness, the SSR gate - 0 when dry (the local above), rain
		// opens it. The literal 0 this used to write made the smoothness
		// local dead code.
		output.mat = float4(metallic, roughness, smoothness, 0.0f);
		output.norm = float4(N, pixelDepth);
		output.pos = float4(input.worldPos, 0.0f);
		// Per-pixel screen-space motion from the camera moving relative to this static surface.
		// CalcVelocity divides by .w and converts NDC -> UV so the result drops straight into
		// the velocity RT in the same units TAA expects.
		output.velocity = CalcVelocity(input.currentPositionUnjittered, input.previousPositionUnjittered, float2(g_screenWidth, g_screenHeight));
		// Terrain is standard PBR.
		output.feat = float4(0.0f, 0.0f, 0.0f, 0.0f);
		return output;
	}
}
