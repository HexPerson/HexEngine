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


	SamplerState g_textureSampler : register(s0);
	SamplerComparisonState g_cmpSampler : register(s1);

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

		if (g_material.isInTransparencyPhase)
		{
			if (g_objectFlags & OBJECT_FLAGS_HAS_OPACITY)
			{
				opacity = g_opacityMap.Sample(g_textureSampler, input.texcoord);

				if (opacity < 1.0f)
				{
					clip(-1);
				}
			}
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
			roughness = g_roughnessMap.Sample(g_textureSampler, input.texcoord).r * g_material.roughnessFactor;
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

		// skip and pixels that have any transparency
		if (g_objectFlags & OBJECT_FLAGS_HAS_OPACITY)
		{
			opacity = g_opacityMap.Sample(g_textureSampler, input.texcoord);

			if (opacity <= 0.0f)
			{
				clip(-1);
			}
		}

		float2 velocity = CalcVelocity(input.currentPositionUnjittered, input.previousPositionUnjittered, float2(g_screenWidth, g_screenHeight));
		//velocity *= float2(g_screenWidth, g_screenHeight);

		output.diff = float4(finalRGB, input.instanceID);

		// material output is: metallic, roughness, smoothness, specularProbability
		output.mat = float4(metalness, roughness, g_material.smoothness, g_material.specularProbability);

		output.norm = float4(worldNormal.xyz, pixelDepth);

		output.pos = float4(input.positionWS.xyz, g_material.emissiveColour.a * emission);

		output.velocity = velocity;

		return output;
	}
}
