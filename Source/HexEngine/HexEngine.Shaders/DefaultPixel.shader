"GlobalIncludes"
{
	MeshCommon
	Utils
	// Transparency-phase forward lighting (sun shadows, local lights, SSR + env
	// reflection, per-fragment fog); pulls in EnvMapCommon and ShadowUtils.
	TransparentForward
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

	SamplerState g_textureSampler : register(s0);

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

		// Not on the snow shell: its texcoord is WORLD XZ (set by the domain
		// shader), so the substrate's height map would offset the snow by an
		// unrelated texture sampled at an unrelated scale.
#ifndef SNOW_SHELL_NO_CLIP
		if (g_objectFlags & OBJECT_FLAGS_HAS_HEIGHT && isInDetailRange)
#else
		if (false)
#endif
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
		// The accumulation shell renders EITHER snow or wind-blown sand - the
		// geometry is identical, only the texture set swaps here on the dominant
		// weather (sand during a sandstorm, snow otherwise).
		if (g_weatherSurface.dirtAmount > g_weatherSurface.snowCoverage && g_dustParams.x > 0.5f)
		{
			// Real sand albedo, world-tiled at the dust tiling scale.
			albedo = float4(g_sandAlbedo.Sample(g_textureSampler, input.positionWS.xz * g_dustParams.y).rgb, 1.0f);
		}
		else if (g_rainOcclusionParams.w > 0.5f)
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

#ifdef SNOW_SHELL_NO_CLIP
		// The snow shell is drawn with the SUBSTRATE's material bound (it
		// reuses the base draw's per-object state), so everything below would
		// read the concrete's surface maps at the shell's world-XZ texcoord.
		// Roughness gets overwritten with the snow value further down, but
		// metalness never was: a substrate with a metallic map (or the
		// lerp(1, map, factor) convention at a small factor, which is ~96%
		// metal) turned its snow into chrome - sky-blue sheen with black
		// streaks where the snow normal map reflected dark directions. Snow is
		// a dielectric, full stop; the ORM/AO maps don't belong to it either.
		metalness = 0.0f;
#else
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
#endif

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
		const float shelter = SampleRainShelterN(input.positionWS.xyz, normalize(input.normal), g_textureSampler);
		const float shelteredWetness = saturate(
			(g_weatherSurface.wetness
				+ g_weatherSurface.snowCoverage * g_weatherSurface.snowMelt * 0.6f) * shelter);

		float wetFilm = 0.0f;
#ifndef SNOW_SHELL_NO_CLIP
		// Skipped on the snow shell: snow is not wet asphalt, so the wet
		// darkening/gloss (which reads near-black at night) must not apply to
		// the snow layer.
		// Sea contact (underwater S4): shoreline wet band + submerged response
		// - see PBRutils::OceanContactWetness. Rain and sea share ONE wet
		// response via max(), so a rain-soaked rock at the tide line is not
		// darkened twice. Lockstep with the other three sites.
		float seaSubmerged;
		const float surfaceWetness = max(shelteredWetness, OceanContactWetness(input.positionWS.xyz, seaSubmerged));
		if (surfaceWetness > 0.001f)
		{
			wetFilm = ApplyWetSurfaceSea(albedo.rgb, roughness, metalness,
				surfaceWetness, seaSubmerged, g_wetnessDarkening);
			// Rain-impact ripples while precipitation is falling (slice 3) -
			// rain only, and not on a surface that is under water.
			worldNormal = ApplyRainRipples(worldNormal, input.positionWS.xyz, g_time,
				shelteredWetness * saturate(g_weatherSurface.precipitationIntensity) * (1.0f - seaSubmerged));
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
		// Flat dust overlay - skipped on the accumulation shell, which renders
		// sand as real geometry with its own texture set below.
#ifndef SNOW_SHELL_NO_CLIP
		const float dustAmount = g_weatherSurface.dirtAmount * (0.5f + 0.5f * shelter);
		if (dustAmount > 0.001f)
		{
			const float4 dustResult = ApplyDustAccumulation(
				albedo.rgb, roughness, worldNormal, input.positionWS.xyz, dustAmount, g_textureSampler);
			albedo.rgb = dustResult.rgb;
			roughness  = dustResult.w;
		}
#endif

		// The snow shell does NOT use ApplySnowAccumulation: that path's POM
		// + per-vertex-friendly relief + mesh-normal use fought the shell (it
		// already carries real tessellated geometry and forces its own white
		// albedo). Instead the shell gets its own lightweight PER-PIXEL relief
		// normal from the height field + snow roughness, below.
#ifdef SNOW_SHELL_NO_CLIP
		const bool shellUseSand = g_weatherSurface.dirtAmount > g_weatherSurface.snowCoverage && g_dustParams.x > 0.5f;
		if (shellUseSand)
		{
			// LARGE-SCALE dune relief first: the shell forces a world-up shading
			// normal (to avoid faceting), so the extruded mounds would otherwise
			// read as a flat sheet - this bends the normal along the dune-height
			// gradient so the mounds actually self-shade. Then the fine sand grain
			// from the normal map rides on top. Normal-ogl -> flip green.
			const float e = 0.18f;                       // metres - coarse, dune scale
			const float hC = SnowHeightField(input.positionWS.xz);
			const float hX = SnowHeightField(input.positionWS.xz + float2(e, 0.0f));
			const float hZ = SnowHeightField(input.positionWS.xz + float2(0.0f, e));
			const float amp = 0.35f;
			const float3 dune = normalize(float3(-(hX - hC) / e * amp, 1.0f, -(hZ - hC) / e * amp));

			float3 nTS = g_sandNormal.Sample(g_textureSampler, input.positionWS.xz * g_dustParams.y).xyz * 2.0f - 1.0f;
			nTS.y = -nTS.y;
			const float3 grain = normalize(nTS.x * input.tangent + nTS.y * input.binormal + nTS.z * dune);
			worldNormal = normalize(lerp(dune, grain, 0.6f));
		}
		else if (g_rainOcclusionParams.w > 0.5f)
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
		roughness = shellUseSand ? 0.92f : 0.85f;
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

		// (Snow doesn't glow with the substrate's emission map either.)
#ifndef SNOW_SHELL_NO_CLIP
		if (g_objectFlags & OBJECT_FLAGS_HAS_EMISSION)
#else
		if (false)
#endif
		{
			emission = g_emissionMap.Sample(g_textureSampler, input.texcoord).rgb * g_material.emissiveColour.rgb * g_material.emissiveColour.a;
		}

		//finalRGB += emission;

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

		float transparencyAlpha = saturate(opacity * albedo.a);
		if (g_material.isInTransparencyPhase && (g_objectFlags & OBJECT_FLAGS_HAS_OPACITY) == 0)
		{
			const float minChannel = min(albedo.r, min(albedo.g, albedo.b));
			const float whiteMask = smoothstep(0.85f, 0.995f, minChannel);
			transparencyAlpha *= (1.0f - whiteMask);
		}

		if (g_material.isInTransparencyPhase != 0)
		{
			// rgb/alpha composed for SRC_ALPHA / INV_SRC_ALPHA blending, with the
			// Fresnel reflection raising alpha so it isn't faded by the opacity.
			const float4 glass = ShadeTransparentSurface(input.positionWS.xyz, worldNormal, albedo.rgb, metalness, roughness, transparencyAlpha, g_textureSampler);
			finalRGB = glass.rgb;
			transparencyAlpha = glass.a;
		}

		float2 velocity = CalcVelocity(input.currentPositionUnjittered, input.previousPositionUnjittered, float2(g_screenWidth, g_screenHeight));
		//velocity *= float2(g_screenWidth, g_screenHeight);

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

		// Transparents are drawn AFTER the fog / aerial-perspective applies
		// (which only know the opaque depth), so fog this fragment at its own
		// depth. Opaque gbuffer writes are untouched - the applies handle them.
		if (g_material.isInTransparencyPhase != 0)
			finalRGB = ApplyTransparentAtmosphere(finalRGB, input.positionWS.xyz, input.position.xy, g_transFogVolume, g_transApVolume, g_transLinearSampler);

		// Emissive REPLACE (matches the graph compiler and the deferred pass):
		// lerp the surface toward the emission by an emissive mask rather than
		// adding emission on top (which double-counted and washed out). The
		// mask goes to pos.w; the deferred lerps the lit result toward this
		// unlit diffuse by it. emission = g_emissionMap x emissiveColour.rgb x
		// strength, computed above.
		const float emissiveMask = saturate(dot(emission, float3(0.2126f, 0.7152f, 0.0722f)));
		finalRGB = lerp(finalRGB, emission, emissiveMask);

		output.diff = float4(finalRGB, outputAlpha);

		// material output is: metallic, roughness, smoothness, reserved (0)
		// (specularProbability used to live in .a but nothing read it; channel is
		// kept zero so future repurposing of .a starts from a clean clear value).
		// Smoothness (the SSR gate) opens with the wet film - a rain-soaked
		// surface reflects even when its dry material never would.
#ifdef SNOW_SHELL_NO_CLIP
		// Snow: no SSR gate from the substrate's smoothness.
		output.mat = float4(metalness, roughness, 0.0f, 1.0f);
#else
		output.mat = float4(metalness, roughness, max(g_material.smoothness, wetFilm * 0.9f), 1.0f);
#endif

		output.norm = float4(worldNormal.xyz, pixelDepth);

		output.pos = float4(input.positionWS.xyz, emissiveMask);

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
#ifdef SNOW_SHELL_NO_CLIP
		// Standard PBR for snow, whatever shading model the substrate uses.
		output.feat = float4(0.0f, 0.0f, 0.0f, 0.0f);
#else
		const uint idByte = (uint)g_material.materialModel;
		const float wQuant = floor(saturate(g_material.modelParams.w) * 31.0f + 0.5f);
		const float packedR = ((float)idByte * 32.0f + wQuant) / 255.0f;
		output.feat = float4(
			packedR,
			g_material.modelParams.x,
			g_material.modelParams.y,
			g_material.modelParams.z);
#endif

		return output;
	}
}
