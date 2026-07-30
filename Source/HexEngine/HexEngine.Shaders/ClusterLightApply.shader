"InputLayout"
{
	PosTexColour
}
"VertexShaderIncludes"
{
	UICommon
}
"PixelShaderIncludes"
{
	UICommon
	ShadowUtils
	LightingUtils
	Atmosphere
	PBRutils
}
"VertexShader"
{
	UIPixelInput ShaderMain(UIVertexInput input)
	{
		UIPixelInput output;

		output.position = input.position;
		output.texcoord = input.texcoord;
		output.positionSS = output.position;

		return output;
	}
}
"PixelShader"
{
	// Clustered light apply (Phase 2, slice 2).
	//
	// One fullscreen pass shading every UNSHADOWED point and spot light from
	// the cluster lists, additively into the light accumulation buffer -
	// replacing hundreds of per-light sphere draws with one. Shadowed lights
	// stay on the per-light path (they need per-light shadow maps bound), so
	// lights flagged shadowed in the buffer are skipped here and unshadowed
	// ones are skipped there.
	//
	// Every model choice mirrors PointLight.shader / SpotLight.shader exactly,
	// so a light moving between the paths must not change appearance:
	//   attenuation  = saturate(1-(d/r)^4)^2 / max(d^2, 0.01^2)   (UE4/Frostbite window)
	//   spot cone    = smoothstep(cosOuter, max(cosInner, cosOuter+1e-4), coneDot)
	//   surface      = CalculatePBRPointLighting(..., depthValue = 1)
	// The one legitimate difference: no per-pixel volumetric march here - the
	// froxel volume owns fog, and the plan retires the inline march anyway.

	GBUFFER_RESOURCE(0, 1, 2, 3, 4);

	struct GpuLight
	{
		float4 posRadius;
		float4 colorStrength;
		float4 dirCone;
		float4 params; // x cos(inner), y type (0 point, 1 spot), z shadowed
	};

	// Raw-bound by SceneRenderer at explicit slots (the engine API has no PS
	// structured-buffer bind).
	StructuredBuffer<GpuLight> g_clLights : register(t21);
	StructuredBuffer<uint>     g_clCounts : register(t22);
	StructuredBuffer<uint>     g_clLists  : register(t23);

	// Slice 7: the shared local-light shadow atlas + per-tile view-proj
	// matrices (CAPTURED at tile render time - a cached tile samples with
	// the matrices its depth was drawn with, not the light's current ones).
	// Null when r_shadowAtlas is off; the params.w gate never reads them.
	Texture2D                 g_shadowAtlas : register(t24);
	StructuredBuffer<matrix>  g_atlasTileVP : register(t25);

	static const float kAtlasTilesPerRow = 4.0f;
	static const float kAtlasTileUvSize = 1.0f / kAtlasTilesPerRow;

	SamplerState g_textureSampler : register(s0);
	SamplerComparisonState g_cmpSampler : register(s1);
	SamplerState g_pointSampler : register(s2);

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

	cbuffer ClusterConstants : register(b5)
	{
		matrix g_clusterViewMatrix;
		float4 g_clusterScreenParams;
	};

	uint DepthToSlice(float depth)
	{
		const float w = log(max(depth, kNearPlaneM) / kNearPlaneM) / log(kFarDepthM / kNearPlaneM);
		return min((uint)(saturate(w) * (float)kClustersZ), kClustersZ - 1);
	}

	float4 ShaderMain(UIPixelInput input) : SV_TARGET
	{
		const float2 screenPos = float2(
			input.position.x / (float)g_screenWidth,
			input.position.y / (float)g_screenHeight);

		// Flood test (r_clusterApplyDebug): unconditional magenta proves the
		// draw itself - render target, blend, viewport, shader bind - before
		// any cluster or light logic gets a chance to zero the output.
		if (g_clusterScreenParams.w > 0.5f && g_clusterScreenParams.w < 1.5f)
			return float4(10.0f, 0.0f, 10.0f, 0.0f);

		const float4 pixelNormal = GBUFFER_NORMAL.Sample(g_pointSampler, screenPos);
		const float4 pixelPosWS  = GBUFFER_POSITION.Sample(g_pointSampler, screenPos);

		// Sky: both spellings, matching the deferred pass and SSR.
		if (pixelPosWS.a > 0.0f || pixelNormal.w == g_frustumDepths[3])
			return float4(0.0f.xxx, 0.0f);

		const float4 pixelColour = GBUFFER_DIFFUSE.Sample(g_pointSampler, screenPos);
		const float3 normalWS = normalize(pixelNormal.xyz);

		const uint cx = min((uint)(screenPos.x * (float)kClustersX), kClustersX - 1);
		const uint cy = min((uint)(screenPos.y * (float)kClustersY), kClustersY - 1);
		const uint cz = DepthToSlice(pixelNormal.w);
		const uint clusterIdx = (cz * kClustersY + cy) * kClustersX + cx;

		const uint count = min(g_clCounts[clusterIdx], kMaxLightsPerCluster);

		// Mode 2: light count per pixel. Black = 0 (list empty HERE - cull or
		// lookup fault), green ramp = populated (fault is later in the loop).
		if (g_clusterScreenParams.w > 1.5f && g_clusterScreenParams.w < 2.5f)
			return float4(0.0f, (float)count * 0.5f, count == 0 ? 0.05f : 0.0f, 0.0f);


		float3 accum = 0.0f.xxx;

		[loop]
		for (uint i = 0; i < count; ++i)
		{
			const GpuLight light = g_clLights[g_clLists[clusterIdx * kMaxLightsPerCluster + i]];

			// Mode 3: first light raw - blue if the shadowed flag would skip it
			// (pointing at CollectShadowCasters marking everything), red-ramped
			// by attenuation otherwise (pointing at range/falloff).
			if (g_clusterScreenParams.w > 2.5f)
			{
				if (light.params.z > 0.5f)
					return float4(0.0f, 0.0f, 5.0f, 0.0f);
				const float dd = length(pixelPosWS.xyz - light.posRadius.xyz);
				return float4(dd < light.posRadius.w ? 5.0f : 0.0f, 0.2f, 0.0f, 0.0f);
			}

			// Shadowed lights: spots with an atlas tile (params.w >= 0) shade
			// HERE with an atlas shadow term; everything else shadowed stays
			// on the per-light path exactly as before.
			float shadowTerm = 1.0f;
			if (light.params.z > 0.5f)
			{
				const int tileIndex = (int)light.params.w;
				if (tileIndex < 0 || light.params.y < 0.5f)
					continue; // per-light path's job

				// Project into the tile's light space (captured matrices).
				const float4 lightClip = mul(float4(pixelPosWS.xyz, 1.0f), g_atlasTileVP[tileIndex]);
				if (lightClip.w <= 0.0f)
					continue;
				float2 shadowUv = float2(
					lightClip.x / lightClip.w * 0.5f + 0.5f,
					-lightClip.y / lightClip.w * 0.5f + 0.5f);
				const float lightDepth = lightClip.z / lightClip.w;

				if (saturate(shadowUv.x) != shadowUv.x || saturate(shadowUv.y) != shadowUv.y ||
					lightDepth >= 1.0f)
				{
					shadowTerm = 1.0f; // outside the map = unshadowed
				}
				else
				{
					// Tile-local UV, clamped half a texel inside the tile so
					// bilinear comparison taps never bleed into a neighbour.
					const float tileX = (float)(tileIndex % (int)kAtlasTilesPerRow);
					const float tileY = (float)(tileIndex / (int)kAtlasTilesPerRow);
					const float halfTexel = 0.5f / 4096.0f;
					shadowUv = clamp(shadowUv, halfTexel / kAtlasTileUvSize, 1.0f - halfTexel / kAtlasTileUvSize);
					const float2 atlasUv = (float2(tileX, tileY) + shadowUv) * kAtlasTileUvSize;

					// Same bias the per-light spot path settled on (0.0005 -
					// see the spotShadowBias note in SceneRenderer).
					shadowTerm = g_shadowAtlas.SampleCmpLevelZero(
						g_cmpSampler, atlasUv, lightDepth - 0.0005f);
				}

				if (shadowTerm <= 0.001f)
					continue;
			}

			// PIXEL -> LIGHT. The old shaders name this exact vector
			// "lightToPixelVec" while constructing lightPos - pixelPos; the name
			// is a lie and trusting it inverted NdotL here, which made every
			// clustered light shade to a clamped near-zero - present in the
			// debug views, invisible in the frame. CalculatePBRPointLighting
			// documents its LightDirection as surface-to-light; this is that.
			float3 pixelToLight = light.posRadius.xyz - pixelPosWS.xyz;
			const float d = length(pixelToLight);
			const float lightRange = light.posRadius.w;
			if (d >= lightRange || d <= 0.0f)
				continue;
			pixelToLight /= d;

			const float minDistSqr = 0.01f * 0.01f;
			float distanceFalloff = saturate(1.0f - pow(d / lightRange, 4.0f));
			distanceFalloff *= distanceFalloff;
			float attenuation = distanceFalloff / max(d * d, minDistSqr);

			// Spot cone, same smoothstep as SpotLight.shader.
			if (light.params.y > 0.5f)
			{
				// Inside the cone when the light->pixel direction aligns with
				// the spot forward: that is minus the pixel->light vector.
				const float coneDot = dot(-pixelToLight, light.dirCone.xyz);
				attenuation *= smoothstep(
					light.dirCone.w,
					max(light.params.x, light.dirCone.w + 1e-4f),
					coneDot);
			}

			if (attenuation <= 0.0f)
				continue;

			const float4 pbr = CalculatePBRPointLighting(
				GBUFFER_SPECULAR,
				g_pointSampler,
				screenPos,
				normalWS,
				pixelPosWS.xyz,
				pixelToLight,
				light.colorStrength.rgb * light.colorStrength.w,
				pixelColour.rgb,
				shadowTerm, // 1 for unshadowed; atlas term for tiled spots
				attenuation);

			accum += pbr.rgb;
		}

		return float4(accum, 0.0f);
	}
}
