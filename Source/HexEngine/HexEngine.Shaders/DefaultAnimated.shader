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

			worldMatrix = mul(boneTransform, instance.world);
			normalMatrix = mul(boneTransform, instance.worldInverseTranspose);
			worldPrev = mul(boneTransform, instance.worldPrev);
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

			// Normalize the resulting bump normal.
			worldNormal = normalize(ApplyNormalMap(worldNormal, input.tangent, input.binormal, g_normalMap, g_textureSampler, input.texcoord));
		}

		// Emission mapping
		if (g_objectFlags & OBJECT_FLAGS_HAS_EMISSION)
		{
			emission = g_emissionMap.Sample(g_textureSampler, input.texcoord).r;
		}

		//float4 specular = float4(0.0f, 0.0f, 0.0f, 0.0f);
		float4 albedo = g_albedoMap.Sample(g_textureSampler, input.texcoord) * input.colour;
		
		
		float metalness = 0.0f;
		float roughness = 0.0f;

		// Get the roughness
		if ((g_objectFlags & OBJECT_FLAGS_HAS_ROUGHNESS) != 0)
		{
			roughness = lerp(1.0f, g_roughnessMap.Sample(g_textureSampler, input.texcoord).r, g_material.roughnessFactor);
		}		

		// Get metallicness
		if (g_objectFlags & OBJECT_FLAGS_HAS_METALLIC)
		{
			metalness = g_metallicMap.Sample(g_textureSampler, input.texcoord).r * g_material.metallicFactor;
		}	
		
		// ambient occlusion
		if (g_objectFlags & OBJECT_FLAGS_HAS_AMBIENT_OCCLUSION)
		{
			albedo.rgb *= g_ambientOcclusionMap.Sample(g_textureSampler, input.texcoord).r;
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
			const float4 sunLit = CalculatePBRSurface(
				metalness,
				roughness,
				worldNormal,
				input.positionWS.xyz,
				-normalize(g_lightDirection.xyz),
				getSunColour(),
				albedo.rgb,
				1.0f,
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

		// material output is: metallic, roughness, smoothness, specularProbability
		output.mat = float4(metalness, roughness, g_material.smoothness, g_material.specularProbability);

		output.norm = float4(worldNormal.xyz, pixelDepth);

		output.pos = float4(input.positionWS.xyz, g_material.emissiveColour.a * emission);

		output.velocity = velocity;

		// Material-features RT, standard PBR default. See DefaultPixel.shader.
		output.feat = float4(0.0f, 0.0f, 0.0f, 0.0f);

		return output;
	}
}
