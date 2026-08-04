"GlobalIncludes"
{
	MeshCommon
	Utils
	// SampleEnvAtlas, for the transparency path's environment reflection fallback.
	EnvMapCommon
	// CalculateShadows + ShadowInput + the b2 caster constants, for sun
	// cascade shadows on the transparency phase (Phase 2 slice 5).
	ShadowUtils
}
"Global"
{
	Texture2D g_albedoMap : register(t0);
	Texture2D g_normalMap : register(t1);
#ifdef SNOW_SHELL_NO_CLIP
	// Snow-shell material textures (M_SnowShell.hmat), bound once per frame
	// by SceneRenderer at t22/t23. Only declared for the shell so no other
	// DefaultPixel consumer reserves the slots. g_rainOcclusionParams.w == 1
	// signals they're bound (else the shell falls back to procedural white).
	Texture2D g_snowShellAlbedo : register(t22);
	Texture2D g_snowShellNormal : register(t23);
	// Snow footprint deformation map (Phase 3 Part B), bound at t30 by
	// SceneRenderer (t27-29 are the forward cluster lists). R8 top-down field of
	// foot depressions. Consumed PER-PIXEL here (albedo darken + normal dent):
	// the geometric/domain approach aliased the coarse tessellation into
	// streaks, so the crisp foot shape lives as a shading detail, not geometry.
	Texture2D<float> g_snowFootprintMap : register(t30);
	float SnowShellFootprint(float3 worldPos, SamplerState samp)
	{
		if (g_snowFootprintParams.x < 0.5f)
			return 0.0f;
		const float4 clip = mul(float4(worldPos, 1.0f), g_snowFootprintVP);
		const float2 uv = clip.xy * float2(0.5f, -0.5f) + 0.5f;
		if (any(uv < 0.0f) || any(uv > 1.0f))
			return 0.0f;
		return g_snowFootprintMap.SampleLevel(samp, uv, 0);
	}
#endif
	Texture2D g_roughnessMap : register(t2);
	Texture2D g_metallicMap : register(t3);
	Texture2D g_heightMap : register(t4);
	Texture2D g_emissionMap : register(t5);
	Texture2D g_opacityMap : register(t6);
	Texture2D g_ambientOcclusionMap : register(t7);

	// Screen-space inputs used by the transparency-phase PBR path. Bound by SceneRenderer
	// before the transparent pass; only sampled when g_material.isInTransparencyPhase != 0.
	Texture2D g_sceneColourTex   : register(t10); // opaque beauty (snapshot pre-transparency)
	Texture2D g_sceneDepthTex    : register(t11); // opaque depth buffer (linear-encoded raw depth)
	Texture2D g_sceneNormalTex   : register(t12); // opaque world normal (xyz) + viewspace depth (w)
	Texture2D g_scenePositionTex : register(t13); // opaque world position (xyz)
	// Prefiltered sky environment atlas (IBL). The transparency reflection falls
	// back to this wherever the screen-space march finds nothing - without it,
	// glass reflects black, which is why window panes read as dark holes.
	Texture2D g_iblSkyEnvFwd     : register(t14);

	SamplerState g_textureSampler : register(s0);
	SamplerComparisonState g_cmpSampler : register(s1);
	// Engine-global point sampler (same s2 every deferred pass uses);
	// CalculateShadows wants it alongside the comparison sampler.
	SamplerState g_pointSamplerFwd : register(s2);

	// Sun shadow cascades for the transparency phase, bound by
	// SceneRenderer::RenderTransparent when r_transparentShadows is on.
	// t15..t20 - the one free six-slot run in this shader's layout. Unbound
	// maps read as fully shadowed, which is why the shader gates on
	// g_taaParams.z rather than sampling unconditionally.
	// (A/B-measured 2026-07-30: this block + the PCSS include cost nothing
	// measurable when the gate is off - the fps<20 hunt ruled it out.)
	SHADOWMAPS_RESOURCE(15)

	// Up to 16 point + 16 spot lights gathered in SceneRenderer::SetupForwardLights().
	// Slot b7 is shared with the particle path which uses the same packing.
	cbuffer ForwardLightsBuffer : register(b7)
	{
		float4 g_fwdCountsAndParams; // x=pointCount, y=spotCount
		float4 g_fwdReserved;
		float4 g_fwdPointPosRadius[16];
		float4 g_fwdPointColorStrength[16];
		float4 g_fwdSpotPosRadius[16];
		float4 g_fwdSpotDirCone[16];           // (dir.xyz, cos(outerHalfAngle))
		float4 g_fwdSpotColorStrength[16];
		float4 g_fwdSpotInnerCone[16];         // .x = cos(innerHalfAngle)
	};

	// Clustered light lists (Phase 2 slice 4). When g_clusterForwardActive is
	// set, forward-lit surfaces (glass, alpha-blend) read ALL local lights
	// from these instead of the closest-16 arrays above. t27+ to stay clear
	// of every material/shadow/env slot; null binds read zero counts.
	struct ClFwdLight
	{
		float4 posRadius;
		float4 colorStrength;
		float4 dirCone;   // spot: xyz dir, w cos(outer)
		float4 params;    // x cos(inner), y type (0 point, 1 spot), z shadowed
	};
	StructuredBuffer<ClFwdLight> g_clfLights : register(t27);
	StructuredBuffer<uint>       g_clfCounts : register(t28);
	StructuredBuffer<uint>       g_clfLists  : register(t29);

	// Direct-only PBR shading for a single analytical light (no ambient, no lightning extras).
	// Mirrors the BRDF inside CalculatePBRSurface so glass / alpha-blended meshes get the same
	// energy-conserving response the deferred opaque pipeline produces.
	float3 PBRDirectLight(float3 worldPos, float3 worldNormal, float3 baseColour,
		float metalness, float perceptualRoughness, float3 L, float3 lightColour, float attenuation)
	{
		metalness = saturate(metalness);
		perceptualRoughness = clamp(perceptualRoughness, MinRoughness, 1.0f);
		perceptualRoughness = ApplySpecularAntiAliasing(worldNormal, perceptualRoughness);
		const float alphaRoughness = perceptualRoughness * perceptualRoughness;

		const float3 diffuseColor = (baseColour * (float3(1.0f, 1.0f, 1.0f) - f0)) * (1.0f - metalness);
		const float3 specularColor = lerp(f0, baseColour, metalness);
		const float reflectance = max(max(specularColor.r, specularColor.g), specularColor.b);
		const float reflectance90 = saturate(reflectance * 25.0f);
		const float3 R0 = specularColor;
		const float3 R90 = float3(1.0f, 1.0f, 1.0f) * reflectance90;

		const float3 V = normalize(g_eyePos.xyz - worldPos);
		const float3 H = normalize(L + V);
		const float NdotL = clamp(dot(worldNormal, L), 0.001f, 1.0f);
		const float NdotV = abs(dot(worldNormal, V)) + 0.001f;
		const float NdotH = saturate(dot(worldNormal, H));
		const float VdotH = saturate(dot(V, H));

		const float3 F = specularReflection(R0, R90, VdotH);
		const float G = geometricOcclusion(NdotL, NdotV, alphaRoughness);
		const float D = microfacetDistribution(NdotH, alphaRoughness);
		const float3 diffuseContrib = (1.0f - F) * diffuse(diffuseColor);
		const float3 specContrib = F * G * D / (4.0f * NdotL * NdotV);
		return NdotL * lightColour * attenuation * (diffuseContrib + specContrib);
	}

	// Accumulate forward point + spot light contributions. Direct-only (caller adds ambient
	// once for the whole surface to avoid the N-times-ambient bug).
	float3 AccumulateForwardLights_PBR(float3 worldPos, float3 worldNormal, float3 baseColour,
		float metalness, float roughness)
	{
		float3 accum = float3(0.0f, 0.0f, 0.0f);

		// Clustered path: every local light, uncapped, from the same lists the
		// deferred apply and froxel volume consume. Shadowed lights are NOT
		// skipped here - the forward path has never sampled local shadows, so
		// including them matches the old arrays' behaviour exactly, just
		// without the closest-16 cap. The 16+16 arrays are not read at all in
		// this branch, so there is nothing to double-count.
		if (g_clusterForwardActive > 0.5f)
		{
			float4 clip = mul(float4(worldPos, 1.0f), g_viewProjectionMatrix);
			if (clip.w > 0.0f)
			{
				const float2 ndc = clip.xy / clip.w;
				const float2 cuv = float2(ndc.x * 0.5f + 0.5f, 1.0f - (ndc.y * 0.5f + 0.5f));
				const float4 viewPos = mul(float4(worldPos, 1.0f), g_viewMatrix);
				const float viewDepth = -viewPos.z;
				// Same grid + exponential slicing as ClusterLightCull.
				const uint ccx = min((uint)(saturate(cuv.x) * 16.0f), 15u);
				const uint ccy = min((uint)(saturate(cuv.y) * 9.0f), 8u);
				const float cw = log(max(viewDepth, 0.1f) / 0.1f) / log(128.0f / 0.1f);
				const uint ccz = min((uint)(saturate(cw) * 32.0f), 31u);
				const uint clusterIdx = (ccz * 9u + ccy) * 16u + ccx;
				const uint cCount = min(g_clfCounts[clusterIdx], 64u);
				[loop]
				for (uint ci = 0u; ci < cCount; ++ci)
				{
					const ClFwdLight cl = g_clfLights[g_clfLists[clusterIdx * 64u + ci]];
					const float3 clToLight = cl.posRadius.xyz - worldPos;
					const float clDistSq = dot(clToLight, clToLight);
					const float clRadius = max(0.05f, cl.posRadius.w);
					if (clDistSq >= clRadius * clRadius)
						continue;
					const float clDist = sqrt(max(1e-6f, clDistSq));
					const float3 clL = clToLight / clDist;
					float coneAtten = 1.0f;
					if (cl.params.y > 0.5f)
					{
						const float cosOuter = cl.dirCone.w;
						const float cosInner = max(cl.params.x, cosOuter + 1e-4f);
						coneAtten = smoothstep(cosOuter, cosInner, dot(-clL, normalize(cl.dirCone.xyz)));
						if (coneAtten <= 0.0f)
							continue;
					}
					const float clMinDistSqr = 0.01f * 0.01f;
					float clFalloff = saturate(1.0f - pow(clDist / clRadius, 4.0f));
					clFalloff *= clFalloff;
					const float clAtten = (clFalloff / max(clDistSq, clMinDistSqr)) * coneAtten;
					accum += PBRDirectLight(worldPos, worldNormal, baseColour, metalness, roughness,
						clL, cl.colorStrength.rgb * cl.colorStrength.w, clAtten);
				}
			}
			return accum;
		}

		const uint pointCount = min((uint)g_fwdCountsAndParams.x, 16u);
		[loop]
		for (uint pi = 0u; pi < pointCount; ++pi)
		{
			const float3 lightPos = g_fwdPointPosRadius[pi].xyz;
			const float radius = max(0.05f, g_fwdPointPosRadius[pi].w);
			const float3 toLight = lightPos - worldPos;
			const float distSq = dot(toLight, toLight);
			const float radiusSq = radius * radius;
			if (distSq >= radiusSq)
				continue;
			const float dist = sqrt(max(1e-6f, distSq));
			const float3 L = toLight / dist;
			// Physical inverse-square + smooth-window attenuation, same form as the
			// deferred PointLight/SpotLight shaders so forward-transparent surfaces
			// (glass, alpha-blended decals) match the opaque pipeline's falloff.
			const float minDistSqr = 0.01f * 0.01f;
			float distanceFalloff = saturate(1.0f - pow(dist / radius, 4.0f));
			distanceFalloff *= distanceFalloff;
			const float atten = distanceFalloff / max(distSq, minDistSqr);
			const float3 colour = g_fwdPointColorStrength[pi].rgb * g_fwdPointColorStrength[pi].w;
			accum += PBRDirectLight(worldPos, worldNormal, baseColour, metalness, roughness,
				L, colour, atten);
		}

		const uint spotCount = min((uint)g_fwdCountsAndParams.y, 16u);
		[loop]
		for (uint si = 0u; si < spotCount; ++si)
		{
			const float3 lightPos = g_fwdSpotPosRadius[si].xyz;
			const float radius = max(0.05f, g_fwdSpotPosRadius[si].w);
			const float3 toLight = lightPos - worldPos;
			const float distSq = dot(toLight, toLight);
			const float radiusSq = radius * radius;
			if (distSq >= radiusSq)
				continue;
			const float dist = sqrt(max(1e-6f, distSq));
			const float3 L = toLight / dist;
			const float3 spotFwd = normalize(g_fwdSpotDirCone[si].xyz);
			const float cosOuter = g_fwdSpotDirCone[si].w;
			// Inner-cone cosine pulled from the parallel array. CPU clamps inner <= outer
			// so cosInner >= cosOuter; we still guard the smoothstep against equal cosines
			// to avoid a divide-by-zero shape at the boundary.
			const float cosInner = max(g_fwdSpotInnerCone[si].x, cosOuter + 1e-4f);
			const float cosAngle = dot(-L, spotFwd);
			const float coneAtten = smoothstep(cosOuter, cosInner, cosAngle);
			if (coneAtten <= 0.0f)
				continue;
			const float minDistSqr = 0.01f * 0.01f;
			float distanceFalloff = saturate(1.0f - pow(dist / radius, 4.0f));
			distanceFalloff *= distanceFalloff;
			const float atten = (distanceFalloff / max(distSq, minDistSqr)) * coneAtten;
			const float3 colour = g_fwdSpotColorStrength[si].rgb * g_fwdSpotColorStrength[si].w;
			accum += PBRDirectLight(worldPos, worldNormal, baseColour, metalness, roughness,
				L, colour, atten);
		}

		return accum;
	}

	// Cheap screen-space reflection used inline by the transparency PBR path. Marches a ray
	// derived from the surface's reflection vector against the opaque depth buffer and
	// samples the opaque beauty texture on hit. Returns black on miss (caller blends with
	// fallback / leaves base radiance intact). Roughness biases step count + early-out.
	bool TraceTransparentSSR(float3 surfaceWorldPos, float3 reflectDirWorld, float roughness,
		out float3 reflectedColour, out float hitConfidence)
	{
		reflectedColour = float3(0.0f, 0.0f, 0.0f);
		hitConfidence = 0.0f;

		// Skip for very rough surfaces — output is dominated by diffuse anyway and the trace
		// would just produce noise without temporal accumulation.
		if (roughness > 0.85f)
			return false;

		const int kMaxSteps = 48;
		const float kStrideWorld = 0.12f;       // world-space step length, scaled by distance below
		const float kThicknessWorld = 0.35f;    // depth-difference window for accepting a hit

		// Step length grows with distance from camera so distant rays don't take many steps.
		const float distFromEye = length(g_eyePos.xyz - surfaceWorldPos);
		const float strideWorld = kStrideWorld * max(0.5f, distFromEye * 0.08f);

		float3 rayPos = surfaceWorldPos + reflectDirWorld * (strideWorld * 0.5f);

		[loop]
		for (int step = 0; step < kMaxSteps; ++step)
		{
			rayPos += reflectDirWorld * strideWorld;

			// Project the ray sample into clip / screen space.
			const float4 clip = mul(float4(rayPos, 1.0f), g_viewProjectionMatrix);
			if (clip.w <= 0.0f)
				return false;
			const float2 ndc = clip.xy / clip.w;
			if (any(abs(ndc) > 1.0f))
				return false;

			const float2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);

			// Compare ray's view-space depth with the opaque scene at the same UV. The G-buffer
			// stores world-space-z in normal.w; ray's view depth is mul(rayPos, view).z negated.
			const float rayViewZ = -mul(float4(rayPos, 1.0f), g_viewMatrix).z;
			const float sceneViewZ = g_sceneNormalTex.SampleLevel(g_textureSampler, uv, 0).w;

			// Skip the sky / very far depth.
			if (sceneViewZ <= 0.0f || sceneViewZ >= g_frustumDepths[3] * 0.999f)
				continue;

			const float dz = rayViewZ - sceneViewZ;
			if (dz > 0.0f && dz < kThicknessWorld)
			{
				reflectedColour = g_sceneColourTex.SampleLevel(g_textureSampler, uv, 0).rgb;
				// Fade out near screen edges to hide the obvious missing-data band.
				const float2 edgeFade = smoothstep(0.0f, 0.1f, uv) * smoothstep(0.0f, 0.1f, 1.0f - uv);
				hitConfidence = saturate(edgeFade.x * edgeFade.y);
				return true;
			}
		}
		return false;
	}

	GBufferOut DefaultPixelShader(MeshPixelInput input)
	{
		GBufferOut output;

		// create some values we need first
		//
		float3 eyeVector = normalize(g_eyePos.xyz - input.positionWS.xyz);
		float3 lightVector = normalize(input.positionWS.xyz - g_lightPosition.xyz);
		float3 worldNormal = normalize(input.normal);
		float3 lightDir = -normalize(g_lightDirection.xyz);
		float opacity = 1.0f;
		

		// Calculate the pixel depth
		//
		float4 worldViewPosition = mul(input.positionWS, g_viewMatrix);
		float pixelDepth = -worldViewPosition.z;

		bool isInDetailRange = length(input.positionWS.xyz - g_eyePos.xyz) <= g_frustumDepths[3];

		if (g_objectFlags & OBJECT_FLAGS_HAS_OPACITY)
		{
			opacity = g_opacityMap.Sample(g_textureSampler, input.texcoord).r;
		}

		if (g_objectFlags & OBJECT_FLAGS_HAS_HEIGHT && isInDetailRange)
		{
			float3x3 tangentMatrix = float3x3(input.tangent, input.binormal, worldNormal);

			float3 viewDirTangent = mul(tangentMatrix, eyeVector);

			float heightMap = g_heightMap.Sample(g_textureSampler, input.texcoord).r;

			input.texcoord += ParallaxOffset(heightMap, 0.018, viewDirTangent);
		}

		// Skipped on the snow shell: the substrate's normal map + the mesh's
		// FACETED per-face tangents/binormals rebuild a per-triangle world
		// normal, which lit each tessellated facet differently even on a flat
		// sheet with a forced up normal (the user-diagnosed faceting). Snow is
		// its own surface - it keeps the clean world-up normal the domain
		// shader set, plus its own relief.
#ifndef SNOW_SHELL_NO_CLIP
		if (g_objectFlags & OBJECT_FLAGS_HAS_BUMP && isInDetailRange)
		{
			// Normalize the resulting bump normal.
			worldNormal = (ApplyNormalMap(worldNormal, input.tangent, input.binormal, g_normalMap, g_textureSampler, input.texcoord, true));
		}
#endif

		

		//float4 specular = float4(0.0f, 0.0f, 0.0f, 0.0f);
		float4 albedo = g_albedoMap.Sample(g_textureSampler, input.texcoord) * input.colour;

#ifdef SNOW_SHELL_NO_CLIP
		// The snow shell is a pure snow LAYER - it must not inherit the
		// substrate's albedo (the road graph material has none, so this reads
		// black). Force a snow-white base with a subtle height-field crevice
		// tint so it isn't a dead flat white (ApplySnowAccumulation is skipped
		// on the shell - see the guard further down - to avoid its POM +
		// mesh-normal dependence, so the tint lives here).
		if (g_rainOcclusionParams.w > 0.5f)
		{
			// Real snow albedo, world-tiled (the DS sets input.texcoord =
			// worldPos.xz * scale - tune that scale for tiling).
			albedo = float4(g_snowShellAlbedo.Sample(g_textureSampler, input.texcoord).rgb, 1.0f);
		}
		else
		{
			// Fallback (no snow material): snow-white + subtle crevice tint.
			albedo = float4(float3(0.90f, 0.92f, 0.96f)
				* (0.82f + 0.18f * SnowHeightField(input.positionWS.xz)), 1.0f);
		}
		// Footprints: compacted / self-shadowed snow inside a print reads darker.
		{
			const float fp = SnowShellFootprint(input.positionWS.xyz, g_textureSampler);
			albedo.rgb *= 1.0f - fp * saturate(g_snowFootprintParams.w) * 0.55f;
		}
#endif

		// The snow shell (SnowShell.shader) reuses this pixel shader for
		// identical snow shading but is drawn over materials whose albedo has
		// no/zero alpha - this clip would then discard every shell pixel. The
		// shell #defines SNOW_SHELL_NO_CLIP before including this file so it
		// keeps its own thickness clip instead; every other consumer clips as
		// before.
#ifndef SNOW_SHELL_NO_CLIP
		if(albedo.a == 0.0f && g_material.isInTransparencyPhase == 0)
			clip(-1);
#endif

		float metalness = g_material.metallicFactor;
		float roughness = g_material.roughnessFactor;

		// support ORM format, extract the data from the roughness map
		if(g_objectFlags & OBJECT_FLAGS_ORM_FORMAT)
		{
			if (g_objectFlags & OBJECT_FLAGS_HAS_ROUGHNESS)
			{
				float3 orm = g_roughnessMap.Sample(g_textureSampler, input.texcoord).rgb;

				roughness = lerp(1.0f, orm.g, g_material.roughnessFactor);		
				metalness = orm.b * g_material.metallicFactor;
				albedo.rgb *= orm.r;
			}
		}
		else if(g_objectFlags & OBJECT_FLAGS_RMA_FORMAT)
		{
			if (g_objectFlags & OBJECT_FLAGS_HAS_ROUGHNESS)
			{
				float3 rma = g_roughnessMap.Sample(g_textureSampler, input.texcoord).rgb;

				roughness = lerp(1.0f, rma.r, g_material.roughnessFactor);		
				metalness = rma.b * g_material.metallicFactor;
				albedo.rgb *= rma.g;
			}
		}
		else
		{
			// Get the roughness
			if (g_objectFlags & OBJECT_FLAGS_HAS_ROUGHNESS)
			{
				roughness = lerp(1.0f, g_roughnessMap.Sample(g_textureSampler, input.texcoord).r, g_material.roughnessFactor);
			}		

			// Get metallicness
			if (g_objectFlags & OBJECT_FLAGS_HAS_METALLIC)
			{
				metalness = lerp(1.0f, g_metallicMap.Sample(g_textureSampler, input.texcoord).r, g_material.metallicFactor);
			}			
			// ambient occlusion
			if (g_objectFlags & OBJECT_FLAGS_HAS_AMBIENT_OCCLUSION)
			{
				albedo.rgb *= g_ambientOcclusionMap.Sample(g_textureSampler, input.texcoord).r;
			}
		}

		// Universal wet response (Phase 3): EVERY opaque surface darkens and
		// gains the water-film gloss with weather wetness, exactly as snow
		// applies universally - rain wets the whole world, not just materials
		// that opted into drips. Porosity-from-roughness heuristic and the
		// darkening rationale live in PBRutils::ApplyWetSurface. The drip
		// block below stays per-material (rainDripIntensity) and no longer
		// darkens - that would double-apply.
		// Shelter occlusion (slice 2): surfaces under static cover receive
		// no rain or snow. 1 = exposed; scales every weather term below.
		// Melting snow (slice 4) feeds ground wetness - slush darkens and
		// glosses the surface it sits on.
		const float shelter = SampleRainShelter(input.positionWS.xyz, g_textureSampler);
		const float shelteredWetness = saturate(
			(g_weatherSurface.wetness
				+ g_weatherSurface.snowCoverage * g_weatherSurface.snowMelt * 0.6f) * shelter);

		float wetFilm = 0.0f;
#ifndef SNOW_SHELL_NO_CLIP
		// Skipped on the snow shell: snow is not wet asphalt, so the wet
		// darkening/gloss (which reads near-black at night) must not apply to
		// the snow layer.
		if (shelteredWetness > 0.001f)
		{
			wetFilm = ApplyWetSurface(albedo.rgb, roughness, metalness,
				shelteredWetness, g_wetnessDarkening);
			// Rain-impact ripples while precipitation is falling (slice 3).
			worldNormal = ApplyRainRipples(worldNormal, input.positionWS.xyz, g_time,
				shelteredWetness * saturate(g_weatherSurface.precipitationIntensity));
		}
#endif

		// Rain droplets: when the material opts in (rainDripIntensity > 0) and
		// it's actually raining (g_weatherSurface.wetness > 0), perturb the
		// normal + drop roughness via procedural droplet noise so the surface
		// reads as "wet with rain hitting it". Reuses the same TBN basis the
		// normal-map sampling above used. World-space noise so drops stay
		// anchored as the camera moves - fine for static geometry, would slide
		// on rotating meshes (acceptable v1 limit).
		const float rainStrength = g_material.rainDripIntensity * shelteredWetness;
		if (rainStrength > 0.001f)
		{
			// Surface up-facing-ness selects between "drops bead in place" (horizontal)
			// and "drops streak downward" (vertical). 0.5 splits a 60deg cone of
			// flat-ish surfaces from the rest.
			const float isHorizontal = step(0.5f, worldNormal.y);
			const float4 rainResult = ApplyRainDroplets(
				worldNormal,
				input.positionWS.xyz,
				input.tangent,
				input.binormal,
				rainStrength,
				g_time,
				isHorizontal);
			worldNormal = rainResult.xyz;
			// Roughness multiplier. Clamps to avoid over-smoothing pure black-mirror
			// (which would explode SSR sample radii).
			roughness   = max(roughness * rainResult.w, MinRoughness);
		}

		// Snow accumulation. Applied AFTER rain because in real life snow lays on
		// top of wet surfaces (the geometry that was wet is also where snow
		// accumulates, snow then dominates the visual). No per-material opt-in -
		// any upward-facing surface naturally catches snow when the weather
		// system reports snowCoverage > 0. See ApplySnowAccumulation in PBRutils.
		// Dust before snow: snow lays on top of dust, not under it. Dust is
		// wind-borne so shelter only halves it (see ApplyDustAccumulation).
		const float dustAmount = g_weatherSurface.dirtAmount * (0.5f + 0.5f * shelter);
		if (dustAmount > 0.001f)
		{
			const float4 dustResult = ApplyDustAccumulation(
				albedo.rgb, roughness, worldNormal, input.positionWS.xyz, dustAmount, g_textureSampler);
			albedo.rgb = dustResult.rgb;
			roughness  = dustResult.w;
		}

		// The snow shell does NOT use ApplySnowAccumulation: that path's POM
		// + per-vertex-friendly relief + mesh-normal use fought the shell (it
		// already carries real tessellated geometry and forces its own white
		// albedo). Instead the shell gets its own lightweight PER-PIXEL relief
		// normal from the height field + snow roughness, below.
#ifdef SNOW_SHELL_NO_CLIP
		if (g_rainOcclusionParams.w > 0.5f)
		{
			// Real snow normal map through the DS's WORLD-aligned tangent
			// basis (input.tangent = +X, binormal = +Z, worldNormal = up) so
			// it never touches the mesh's faceted tangents. Same world UV as
			// the albedo. Normal-ogl -> flip green for D3D.
			float3 nTS = g_snowShellNormal.Sample(g_textureSampler, input.texcoord).xyz * 2.0f - 1.0f;
			nTS.y = -nTS.y;
			worldNormal = normalize(nTS.x * input.tangent + nTS.y * input.binormal + nTS.z * worldNormal);
		}
		else
		{
			// Fallback relief, PER-PIXEL (the domain shader's per-vertex
			// version aliased coarse tessellation into dark fans).
			const float e = 0.07f;
			const float hC = SnowHeightField(input.positionWS.xz);
			const float hX = SnowHeightField(input.positionWS.xz + float2(e, 0.0f));
			const float hZ = SnowHeightField(input.positionWS.xz + float2(0.0f, e));
			const float amp = 0.05f;
			const float3 reliefN = normalize(float3(-(hX - hC) / e * amp, 1.0f, -(hZ - hC) / e * amp));
			worldNormal = normalize(lerp(worldNormal, reliefN, 0.6f));
		}
		roughness = 0.85f;
		// Footprint dent (PER-PIXEL, full-res): tilt the normal by the gradient
		// of the footprint depth so a print reads as a pressed hollow with lit
		// rims - the fine shape the coarse tessellation could not carry. The
		// horizontal gradient is added to the normal so the snow relief survives.
		{
			const float fC = SnowShellFootprint(input.positionWS.xyz, g_textureSampler);
			if (fC > 0.001f)
			{
				const float e = 0.04f;
				const float fX = SnowShellFootprint(input.positionWS.xyz + float3(e, 0.0f, 0.0f), g_textureSampler);
				const float fZ = SnowShellFootprint(input.positionWS.xyz + float3(0.0f, 0.0f, e), g_textureSampler);
				const float amp = 3.0f * saturate(g_snowFootprintParams.w);
				worldNormal = normalize(worldNormal
					+ float3((fX - fC) / e * amp, 0.0f, (fZ - fC) / e * amp) * saturate(fC));
			}
		}
#else
		const float shelteredSnow = g_weatherSurface.snowCoverage * shelter;
		if (shelteredSnow > 0.001f)
		{
			// worldNormal is inout since slice 5 - snow relief + drift banks.
			const float4 snowResult = ApplySnowAccumulation(
				albedo.rgb, roughness, worldNormal, input.positionWS.xyz,
				shelteredSnow, g_weatherSurface.snowMelt, g_textureSampler);
			albedo.rgb = snowResult.rgb;
			roughness  = snowResult.w;
		}
#endif

		float3 finalRGB = albedo.rgb;

		// Apply emission, if there was any and multiply it by the emission colours and factor
		// Emission mapping

		float3 emission = float3(0,0,0);//g_material.emissiveColour.rgb;

		if (g_objectFlags & OBJECT_FLAGS_HAS_EMISSION)
		{
			emission = g_emissionMap.Sample(g_textureSampler, input.texcoord).rgb * g_material.emissiveColour.rgb * g_material.emissiveColour.a;
		}

		finalRGB += emission;

		// In the opaque pass we cut out non-opaque pixels; in transparency phase we preserve fractional alpha.
		if (g_material.isInTransparencyPhase == 0)
		{
			if (opacity < 1.0f)
			{
				clip(-1);
			}
		}
		else if (opacity <= 0.0f)
		{
			clip(-1);
		}

		if (g_material.isInTransparencyPhase != 0)
		{
			// Sun cascade shadows (Phase 2 slice 5). This argument was a
			// hardcoded 1.0f - transparent surfaces received NO shadows at
			// all, glass in a shadowed interior lit as if outdoors. Same
			// ShadowInput/bias formulation as the deferred pass, so the glass
			// and the wall behind it agree about where the shadow falls.
			// Gated: the cascades + b2 caster constants are only valid when
			// SceneRenderer bound them for this pass.
			// Cheap PCF, not full PCSS: glass needs "am I in shadow", not
			// contact hardening, and PCSS at g_shadowConfig.samples cost
			// ~4 ms on window-heavy views (measured 2026-07-30).
			float sunShadow = 1.0f;
			if (g_taaParams.z > 0.5f)
			{
				const float ndl = dot(worldNormal, normalize(g_shadowCasterLightDir.xyz));
				const float shadowBias = g_shadowConfig.biasMultiplier * (1.0f - ndl);
				sunShadow = CalculateShadowsCheapPCF(input.positionWS.xyz, g_cmpSampler, SHADOWMAPS, shadowBias);
			}

			// Sun (analytical). CalculatePBRSurface already includes a single ambient term and
			// lightning flash contribution — don't add ambient again below.
			const float4 sunLit = CalculatePBRSurface(
				metalness,
				roughness,
				worldNormal,
				input.positionWS.xyz,
				-normalize(g_lightDirection.xyz),
				getSunColour(),
				albedo.rgb,
				sunShadow,
				g_globalLight[0]);

			// Direct-only contributions for forward point + spot lights so ambient isn't
			// scaled by the light count.
			const float3 forwardDirect = AccumulateForwardLights_PBR(input.positionWS.xyz,
				worldNormal, albedo.rgb, metalness, roughness);

			// Screen-space reflection from the opaque scene snapshot captured before this pass.
			const float3 V = normalize(g_eyePos.xyz - input.positionWS.xyz);
			const float3 R = reflect(-V, worldNormal);
			const float NdotV = saturate(dot(worldNormal, V));
			const float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo.rgb, metalness);
			const float fresnel = pow(1.0f - NdotV, 5.0f);
			const float3 F = F0 + (float3(1.0f, 1.0f, 1.0f) - F0) * fresnel;

			float3 reflection = float3(0.0f, 0.0f, 0.0f);
			float reflectionWeight = 0.0f;
			float3 ssrColour = float3(0.0f, 0.0f, 0.0f);
			float ssrConfidence = 0.0f;
			const float glossiness = saturate(1.0f - roughness);

			if (TraceTransparentSSR(input.positionWS.xyz, R, roughness, ssrColour, ssrConfidence))
			{
				// Gloss-only SSR (no blur), so fade out as roughness rises.
				reflectionWeight = ssrConfidence * glossiness;
				reflection = ssrColour;
			}

			// Environment fallback wherever the screen-space march found nothing.
			//
			// Without this, `reflection` stays BLACK on a miss - and for a window
			// the reflected ray usually leaves the screen on the first step, so it
			// misses almost always. The Fresnel term then multiplies black, and the
			// pane renders as a dark hole instead of glass. That is the "black
			// window panes" artifact; it was never the SSR march being wrong, just
			// nothing behind it.
			//
			// The prefiltered sky atlas is the same environment the deferred IBL
			// uses, so glass and opaque surfaces agree about what the sky looks
			// like. Weighted by (1 - ssrWeight) so a real screen-space hit always
			// wins - screen data is more accurate than a distant-environment
			// approximation when it exists.
			{
				const float3 envColour = SampleEnvAtlas(g_iblSkyEnvFwd, g_textureSampler, R, roughness);
				// Downward rays would otherwise pick up horizon sky and light the
				// undersides of glass; the atlas carries no ground radiance.
				const float envHorizon = saturate(R.y * 3.0f + 0.35f);
				const float envWeight = (1.0f - reflectionWeight) * glossiness * envHorizon * g_glassEnvStrength;

				reflection = reflection * reflectionWeight + envColour * envWeight;
				reflectionWeight = saturate(reflectionWeight + envWeight);
				// `reflection` is now premultiplied by its weight, so undo that -
				// the composition below multiplies by reflectionWeight again.
				reflection = reflectionWeight > 1e-4f ? reflection / reflectionWeight : 0.0f.xxx;
			}

			// Sum: analytical sun (incl. ambient) + forward direct + Fresnel-weighted reflection.
			const float3 specularReflectionTerm = F * reflection * reflectionWeight;
			finalRGB = sunLit.rgb + forwardDirect + specularReflectionTerm + emission;
		}

		float2 velocity = CalcVelocity(input.currentPositionUnjittered, input.previousPositionUnjittered, float2(g_screenWidth, g_screenHeight));
		//velocity *= float2(g_screenWidth, g_screenHeight);

		float transparencyAlpha = saturate(opacity * albedo.a);
		if (g_material.isInTransparencyPhase && (g_objectFlags & OBJECT_FLAGS_HAS_OPACITY) == 0)
		{
			const float minChannel = min(albedo.r, min(albedo.g, albedo.b));
			const float whiteMask = smoothstep(0.85f, 0.995f, minChannel);
			transparencyAlpha *= (1.0f - whiteMask);
		}
		const float outputAlpha = g_material.isInTransparencyPhase ? transparencyAlpha : input.instanceID;

		// Rain-drip diagnostic: when r_rainDripDebug is on, paint the cell-grid
		// pattern straight into the surface colour so we can see the basis and
		// streak direction visually. cellFrac.x in R, cellFrac.y in G, thin grid
		// line in B. Should look like proper 2D squares sliding down on walls;
		// any horizontal stripe artifact would be immediately obvious.
		if (g_rainDripDebug > 0.5f)
		{
			const float isHorizDebug = step(0.5f, worldNormal.y);
			finalRGB = RainDripsCellGridDebug(worldNormal, input.positionWS.xyz, g_time, isHorizDebug);
		}

		output.diff = float4(finalRGB, outputAlpha);

		// material output is: metallic, roughness, smoothness, reserved (0)
		// (specularProbability used to live in .a but nothing read it; channel is
		// kept zero so future repurposing of .a starts from a clean clear value).
		// Smoothness (the SSR gate) opens with the wet film - a rain-soaked
		// surface reflects even when its dry material never would.
		output.mat = float4(metalness, roughness, max(g_material.smoothness, wetFilm * 0.9f), 1.0f);

		output.norm = float4(worldNormal.xyz, pixelDepth);

		output.pos = float4(input.positionWS.xyz, length(emission));

		output.velocity = velocity;

		// Material-features RT. Encodes shading-model id + per-model params for the
		// post-process passes (SSS, clearcoat, anisotropic, sheen). Layout:
		//   .r = (modelId * 32 + modelParams.w_quant) / 255
		//        i.e. upper 3 bits of the byte = model id (range 0..7), lower 5
		//        bits = quantised modelParams.w (32 levels). This lets sheen
		//        (which needs all four modelParam channels for strength + RGB tint)
		//        carry tint.b through the otherwise-full features RT - we'd run
		//        out of channels packing modelId + 4 modelParams into 4 RT slots.
		//   .gba = modelParams.xyz (strength + the first two tint / shape params)
		// (0,0,0,0) = standard PBR which preserves existing behaviour for materials
		// that don't opt into a non-default model.
		const uint idByte = (uint)g_material.materialModel;
		const float wQuant = floor(saturate(g_material.modelParams.w) * 31.0f + 0.5f);
		const float packedR = ((float)idByte * 32.0f + wQuant) / 255.0f;
		output.feat = float4(
			packedR,
			g_material.modelParams.x,
			g_material.modelParams.y,
			g_material.modelParams.z);

		return output;
	}
}
