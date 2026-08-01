"Requirements"
{
	//ShadowMaps
}
"InputLayout"
{
	PosNormTanBinTexBoned_INSTANCED
}
"VertexShaderIncludes"
{
	MeshCommon
		Utils
}
"PixelShaderIncludes"
{
	MeshCommon
		Atmosphere
		ShadowUtils
		Utils
		LightingUtils
		PBRutils
}
"VertexShader"
{
	MeshPixelInput ShaderMain(AnimatedMeshVertexInput input, MeshInstanceData instance, uint instanceID : SV_INSTANCEID)
	{
		MeshPixelInput output;

		input.position.w = 1.0f;

		output.cullDistance = 0.5f;

		matrix worldMatrix, normalMatrix, worldPrev;

		if ((g_objectFlags & OBJECT_FLAGS_HAS_ANIMATION) != 0)
		{
			matrix	boneTransform = mul(input.boneWeights[0], g_boneTransforms[(int)input.boneIds[0]]);

			boneTransform += mul(input.boneWeights[1], g_boneTransforms[(int)input.boneIds[1]]);
			boneTransform += mul(input.boneWeights[2], g_boneTransforms[(int)input.boneIds[2]]);
			boneTransform += mul(input.boneWeights[3], g_boneTransforms[(int)input.boneIds[3]]);

			// Skin the previous-frame position with the PREVIOUS frame's pose. Reusing
			// boneTransform here meant the only motion a skinned mesh could report was its
			// rigid object transform plus camera movement - the deformation itself produced
			// zero velocity, so animated characters ghosted badly under TAA and DLSS.
			matrix	boneTransformPrev = mul(input.boneWeights[0], g_boneTransformsPrev[(int)input.boneIds[0]]);

			boneTransformPrev += mul(input.boneWeights[1], g_boneTransformsPrev[(int)input.boneIds[1]]);
			boneTransformPrev += mul(input.boneWeights[2], g_boneTransformsPrev[(int)input.boneIds[2]]);
			boneTransformPrev += mul(input.boneWeights[3], g_boneTransformsPrev[(int)input.boneIds[3]]);

			worldMatrix = mul(boneTransform, instance.world);
			normalMatrix = mul(boneTransform, instance.worldInverseTranspose);
			worldPrev = mul(boneTransformPrev, instance.worldPrev);
		}
		else
		{
			worldMatrix = instance.world;
			normalMatrix = instance.worldInverseTranspose;
			worldPrev = instance.worldPrev;
		}

		output.position = mul(input.position, worldMatrix);
		output.positionWS = output.position;

		output.position = mul(output.position, g_viewProjectionMatrix);

		// Calculate velocity
		float4x4 prevFrame_modelMatrix = worldPrev;
		float4 prevFrame_worldPos = mul(input.position, prevFrame_modelMatrix);
		float4 prevFrame_clipPos = mul(prevFrame_worldPos, g_viewProjectionMatrixPrev);

		output.previousPositionUnjittered = prevFrame_clipPos;
		output.currentPositionUnjittered = output.position;

		//output.velocity = CalcVelocity(prevFrame_clipPos, output.position, float2(g_screenWidth, g_screenHeight));

		// Apply TAA jitter
		output.position.xy += g_jitterOffsets * output.position.w;

		output.texcoord = input.texcoord;

		output.normal = mul(input.normal, (float3x3)normalMatrix/*instance.worldInverseTranspose*/);
		output.normal = normalize(output.normal);

		output.tangent = mul(input.tangent, (float3x3)normalMatrix/*instance.worldInverseTranspose*/);
		output.tangent = normalize(output.tangent);

		output.binormal = mul(input.binormal, (float3x3)normalMatrix/*instance.worldInverseTranspose*/);
		output.binormal = normalize(output.binormal);

		// Determine the viewing direction based on the position of the camera and the position of the vertex in the world.
		output.viewDirection.xyz = g_eyePos.xyz - output.positionWS.xyz;

		// Normalize the viewing direction vector.
		output.viewDirection.xyz = normalize(output.viewDirection.xyz);

		output.colour = instance.colour;

		output.instanceID = instanceID + entityId;

		return output;
	}
}
"PixelShader"
{
	Texture2D g_albedoMap : register(t0);
	Texture2D g_normalMap : register(t1);
	Texture2D g_roughnessMap : register(t2);
	Texture2D g_metallicMap : register(t3);
	Texture2D g_heightMap : register(t4);
	Texture2D g_emissionMap : register(t5);
	Texture2D g_opacityMap : register(t6);
	Texture2D g_ambientOcclusionMap : register(t7);

	// Screen-space inputs for transparency-phase PBR (bound by SceneRenderer::RenderTransparent).
	Texture2D g_sceneColourTex   : register(t10);
	Texture2D g_sceneDepthTex    : register(t11);
	Texture2D g_sceneNormalTex   : register(t12);
	Texture2D g_scenePositionTex : register(t13);

	SamplerState g_textureSampler : register(s0);
	SamplerComparisonState g_cmpSampler : register(s1);
	// Engine-global point sampler (same s2 every deferred pass uses);
	// CalculateShadows wants it alongside the comparison sampler.
	SamplerState g_pointSamplerFwd : register(s2);

	// Sun shadow cascades for the transparency phase, bound by
	// SceneRenderer::RenderTransparent when r_transparentShadows is on.
	// Same t15..t20 run as DefaultPixel so one C++ bind serves both shaders.
	SHADOWMAPS_RESOURCE(15)

	cbuffer ForwardLightsBuffer : register(b7)
	{
		float4 g_fwdCountsAndParams;
		float4 g_fwdReserved;
		float4 g_fwdPointPosRadius[16];
		float4 g_fwdPointColorStrength[16];
		float4 g_fwdSpotPosRadius[16];
		float4 g_fwdSpotDirCone[16];           // (dir.xyz, cos(outerHalfAngle))
		float4 g_fwdSpotColorStrength[16];
		float4 g_fwdSpotInnerCone[16];         // .x = cos(innerHalfAngle)
	};

	// Clustered light lists (Phase 2 slice 4) - see DefaultPixel for the doc.
	struct ClFwdLight
	{
		float4 posRadius;
		float4 colorStrength;
		float4 dirCone;
		float4 params;
	};
	StructuredBuffer<ClFwdLight> g_clfLights : register(t27);
	StructuredBuffer<uint>       g_clfCounts : register(t28);
	StructuredBuffer<uint>       g_clfLists  : register(t29);

	// Direct-only PBR (no ambient). See DefaultPixel.shader for full notes; same body.
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

	float3 AccumulateForwardLights_PBR(float3 worldPos, float3 worldNormal, float3 baseColour,
		float metalness, float roughness)
	{
		float3 accum = float3(0.0f, 0.0f, 0.0f);

		// Clustered path - see DefaultPixel's copy for the full doc. Uncapped
		// local lights from the shared lists; the 16+16 arrays go unread.
		if (g_clusterForwardActive > 0.5f)
		{
			float4 clip = mul(float4(worldPos, 1.0f), g_viewProjectionMatrix);
			if (clip.w > 0.0f)
			{
				const float2 ndc = clip.xy / clip.w;
				const float2 cuv = float2(ndc.x * 0.5f + 0.5f, 1.0f - (ndc.y * 0.5f + 0.5f));
				const float4 viewPos = mul(float4(worldPos, 1.0f), g_viewMatrix);
				const float viewDepth = -viewPos.z;
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
			// deferred shaders and DefaultPixel.
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

	bool TraceTransparentSSR(float3 surfaceWorldPos, float3 reflectDirWorld, float roughness,
		out float3 reflectedColour, out float hitConfidence)
	{
		reflectedColour = float3(0.0f, 0.0f, 0.0f);
		hitConfidence = 0.0f;
		if (roughness > 0.85f)
			return false;

		const int kMaxSteps = 48;
		const float kStrideWorld = 0.12f;
		const float kThicknessWorld = 0.35f;
		const float distFromEye = length(g_eyePos.xyz - surfaceWorldPos);
		const float strideWorld = kStrideWorld * max(0.5f, distFromEye * 0.08f);

		float3 rayPos = surfaceWorldPos + reflectDirWorld * (strideWorld * 0.5f);
		[loop]
		for (int step = 0; step < kMaxSteps; ++step)
		{
			rayPos += reflectDirWorld * strideWorld;
			const float4 clip = mul(float4(rayPos, 1.0f), g_viewProjectionMatrix);
			if (clip.w <= 0.0f)
				return false;
			const float2 ndc = clip.xy / clip.w;
			if (any(abs(ndc) > 1.0f))
				return false;
			const float2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
			const float rayViewZ = -mul(float4(rayPos, 1.0f), g_viewMatrix).z;
			const float sceneViewZ = g_sceneNormalTex.SampleLevel(g_textureSampler, uv, 0).w;
			if (sceneViewZ <= 0.0f || sceneViewZ >= g_frustumDepths[3] * 0.999f)
				continue;
			const float dz = rayViewZ - sceneViewZ;
			if (dz > 0.0f && dz < kThicknessWorld)
			{
				reflectedColour = g_sceneColourTex.SampleLevel(g_textureSampler, uv, 0).rgb;
				const float2 edgeFade = smoothstep(0.0f, 0.1f, uv) * smoothstep(0.0f, 0.1f, 1.0f - uv);
				hitConfidence = saturate(edgeFade.x * edgeFade.y);
				return true;
			}
		}
		return false;
	}

	GBufferOut ShaderMain(MeshPixelInput input)
	{
		GBufferOut output;

		// create some values we need first
		//
		float3 eyeVector = normalize(g_eyePos.xyz - input.positionWS.xyz);
		float3 lightVector = normalize(input.positionWS.xyz - g_lightPosition.xyz);
		float3 worldNormal = normalize(input.normal);
		float3 lightDir = -normalize(g_lightDirection.xyz);
		float opacity = 1.0f;
		float emission = 0.0f;

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

			input.texcoord += ParallaxOffset(heightMap, 0.03, viewDirTangent);
		}

		// BUMP MAPPING
		if (g_objectFlags & OBJECT_FLAGS_HAS_BUMP && isInDetailRange)
		{
			// Sample the pixel in the bump map.
			//float4 bumpMap = g_normalMap.Sample(g_textureSampler, input.texcoord);

			//// Expand the range of the normal value from (0, +1) to (-1, +1).
			//bumpMap = (bumpMap * 2.0f) - 1.0f;

			//// Calculate the normal from the data in the bump map.
			//float3 bumpNormal = (bumpMap.x * normalize(input.tangent)) + (bumpMap.y * normalize(input.binormal)) + (/*bumpMap.z **/ worldNormal);

			// flipY must match DefaultPixel.shader: the static and skinned paths sample the
			// same normal maps, and omitting it here rendered every map green-inverted on
			// skinned meshes only.
			worldNormal = ApplyNormalMap(worldNormal, input.tangent, input.binormal, g_normalMap, g_textureSampler, input.texcoord, true);
		}

		// Emission mapping
		if (g_objectFlags & OBJECT_FLAGS_HAS_EMISSION)
		{
			emission = g_emissionMap.Sample(g_textureSampler, input.texcoord).r;
		}

		//float4 specular = float4(0.0f, 0.0f, 0.0f, 0.0f);
		float4 albedo = g_albedoMap.Sample(g_textureSampler, input.texcoord) * input.colour;

		// Initialise from the material factors so a material with no PBR
		// textures bound but artist-set metallic/roughness sliders still works.
		// (Previously these were init to 0, silently dropping the slider
		// values whenever the material lacked maps - matte-plastic look on
		// every animated mesh missing a roughness/metallic texture.) Mirrors
		// DefaultPixel.shader exactly so static + animated meshes shade
		// identically given the same material.
		float metalness = g_material.metallicFactor;
		float roughness = g_material.roughnessFactor;

		// Packed PBR formats. ORM (R=AO, G=roughness, B=metallic) and RMA
		// (R=roughness, G=AO, B=metallic) are the two glTF-style conventions
		// the engine supports. The static path branches on these flags - the
		// animated path used to skip them entirely, so any ORM/RMA material
		// applied to a skinned mesh sampled nothing and ended up at default
		// zero metalness/roughness regardless of authoring.
		if (g_objectFlags & OBJECT_FLAGS_ORM_FORMAT)
		{
			if (g_objectFlags & OBJECT_FLAGS_HAS_ROUGHNESS)
			{
				float3 orm = g_roughnessMap.Sample(g_textureSampler, input.texcoord).rgb;
				roughness  = lerp(1.0f, orm.g, g_material.roughnessFactor);
				metalness  = orm.b * g_material.metallicFactor;
				albedo.rgb *= orm.r;
			}
		}
		else if (g_objectFlags & OBJECT_FLAGS_RMA_FORMAT)
		{
			if (g_objectFlags & OBJECT_FLAGS_HAS_ROUGHNESS)
			{
				float3 rma = g_roughnessMap.Sample(g_textureSampler, input.texcoord).rgb;
				roughness  = lerp(1.0f, rma.r, g_material.roughnessFactor);
				metalness  = rma.b * g_material.metallicFactor;
				albedo.rgb *= rma.g;
			}
		}
		else
		{
			// Get the roughness
			if ((g_objectFlags & OBJECT_FLAGS_HAS_ROUGHNESS) != 0)
			{
				roughness = lerp(1.0f, g_roughnessMap.Sample(g_textureSampler, input.texcoord).r, g_material.roughnessFactor);
			}

			// Get metallicness. lerp(1, map, factor) instead of map*factor
			// so factor=0 means "fully use map" the same way roughness does
			// and the way the static path always has - the editor slider
			// expects this convention.
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

		// Universal wet response - mirrors DefaultPixel exactly (see the
		// comment there). Skinned surfaces get wet like everything else;
		// only the drip perturbation below stays per-material.
		// Shelter occlusion - matches DefaultPixel.
		const float shelter = SampleRainShelter(input.positionWS.xyz, g_textureSampler);
		const float shelteredWetness = g_weatherSurface.wetness * shelter;

		float wetFilm = 0.0f;
		if (shelteredWetness > 0.001f)
		{
			wetFilm = ApplyWetSurface(albedo.rgb, roughness, metalness,
				shelteredWetness, g_wetnessDarkening);
		}

		// Rain droplets - same procedural perturbation DefaultPixel uses. See
		// ApplyRainDroplets in PBRutils.shader for the full doc. Animated meshes
		// hit this less often (skinned characters in rain), but if the user opts
		// in via the material slider they get the same wet-surface look.
		const float rainStrength = g_material.rainDripIntensity * shelteredWetness;
		if (rainStrength > 0.001f)
		{
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
			roughness   = max(roughness * rainResult.w, MinRoughness);
		}

		// Snow accumulation. Same global-no-opt-in semantics as DefaultPixel -
		// any upward-facing animated surface catches snow. See PBRutils.
		const float shelteredSnow = g_weatherSurface.snowCoverage * shelter;
		if (shelteredSnow > 0.001f)
		{
			const float4 snowResult = ApplySnowAccumulation(
				albedo.rgb, roughness, worldNormal, input.positionWS.xyz,
				shelteredSnow);
			albedo.rgb = snowResult.rgb;
			roughness  = snowResult.w;
		}

		float3 finalRGB = albedo.rgb;

		// Apply emission, if there was any and multiply it by the emission colours and factor
		if (emission > 0.0f)
		{
			float3 emissiveColour = g_material.emissiveColour.rgb * g_material.emissiveColour.a * emission;
			finalRGB = emissiveColour;

			if (length(albedo.rgb) > 0.0f)
				finalRGB += albedo.rgb;
		}

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
			// Sun cascade shadows (Phase 2 slice 5) - was a hardcoded 1.0f, so
			// transparent surfaces never received sun shadows. Same
			// ShadowInput/bias formulation as the deferred pass; gated because
			// the cascades + b2 caster constants are only valid when
			// SceneRenderer bound them for this pass.
			// Cheap PCF, not full PCSS - see DefaultPixel; ~4 ms measured on
			// window-heavy views with the full path.
			float sunShadow = 1.0f;
			if (g_taaParams.z > 0.5f)
			{
				const float ndl = dot(worldNormal, normalize(g_shadowCasterLightDir.xyz));
				const float shadowBias = g_shadowConfig.biasMultiplier * (1.0f - ndl);
				sunShadow = CalculateShadowsCheapPCF(input.positionWS.xyz, g_cmpSampler, SHADOWMAPS, shadowBias);
			}

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

			const float3 forwardDirect = AccumulateForwardLights_PBR(input.positionWS.xyz,
				worldNormal, albedo.rgb, metalness, roughness);

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
			if (TraceTransparentSSR(input.positionWS.xyz, R, roughness, ssrColour, ssrConfidence))
			{
				const float glossiness = saturate(1.0f - roughness);
				reflectionWeight = ssrConfidence * glossiness;
				reflection = ssrColour;
			}

			const float3 specularReflectionTerm = F * reflection * reflectionWeight;
			const float3 emissiveTerm = g_material.emissiveColour.rgb * g_material.emissiveColour.a * emission;
			finalRGB = sunLit.rgb + forwardDirect + specularReflectionTerm + emissiveTerm;
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
		output.diff = float4(finalRGB, outputAlpha);

		// material output is: metallic, roughness, smoothness, geometry-mask
		// .a = 1 marks this as a real geometry pixel - downstream sky-guard
		// paths (deferred lighting, GI sky-skip, etc) treat .a == 0 as sky.
		// Previously this shader wrote 0 here (with a misleading "see
		// DefaultPixel for rationale" comment - DefaultPixel actually writes
		// 1), which meant animated meshes were being classified as sky.
		// Smoothness (the SSR gate) opens with the wet film - matches DefaultPixel.
		output.mat = float4(metalness, roughness, max(g_material.smoothness, wetFilm * 0.9f), 1.0f);

		output.norm = float4(worldNormal.xyz, pixelDepth);

		output.pos = float4(input.positionWS.xyz, g_material.emissiveColour.a * emission);

		output.velocity = velocity;

		// Mirror DefaultPixel: encode (modelId, modelParams.w_quant) packed into
		// .r so sheen can use all four modelParam channels for strength + RGB tint.
		// See DefaultPixel.shader for the full layout description.
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
