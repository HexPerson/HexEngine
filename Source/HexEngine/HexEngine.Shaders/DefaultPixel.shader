"GlobalIncludes"
{
	MeshCommon	
	Utils
}
"Global"
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

			input.texcoord += ParallaxOffset(heightMap, 0.018, viewDirTangent);
		}

		if (g_objectFlags & OBJECT_FLAGS_HAS_BUMP && isInDetailRange)
		{
			// Normalize the resulting bump normal.
			worldNormal = (ApplyNormalMap(worldNormal, input.tangent, input.binormal, g_normalMap, g_textureSampler, input.texcoord, true));
		}

		

		//float4 specular = float4(0.0f, 0.0f, 0.0f, 0.0f);
		float4 albedo = g_albedoMap.Sample(g_textureSampler, input.texcoord) * input.colour;

		if(albedo.a == 0.0f && g_material.isInTransparencyPhase == 0)
			clip(-1);

		float metalness = g_material.metallicFactor;
		float roughness = g_material.roughnessFactor;

		// support ORM format, extract the data from the roughness map
		if(g_objectFlags & OBJECT_FLAGS_ORM_FORMAT)
		{
			if (g_objectFlags & OBJECT_FLAGS_HAS_ROUGHNESS)
			{
				float3 orm = g_roughnessMap.Sample(g_textureSampler, input.texcoord).rgb;

				roughness = orm.g * g_material.roughnessFactor;		
				metalness = orm.b * g_material.metallicFactor;
				albedo.rgb *= orm.r;
			}
		}
		else
		{
			// Get the roughness
			if (g_objectFlags & OBJECT_FLAGS_HAS_ROUGHNESS)
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
		}

		float3 finalRGB = albedo.rgb;

		// Apply emission, if there was any and multiply it by the emission colours and factor
		// Emission mapping

		float3 emission = float3(0,0,0);//g_material.emissiveColour.rgb;

		if (g_objectFlags & OBJECT_FLAGS_HAS_EMISSION)
		{
			emission = g_emissionMap.Sample(g_textureSampler, input.texcoord).rgb * g_material.emissiveColour.rgb * g_material.emissiveColour.a;
		}

		finalRGB += emission;

		// skip and pixels that have any transparency
		if (g_objectFlags & OBJECT_FLAGS_HAS_OPACITY)
		{
			opacity = g_opacityMap.Sample(g_textureSampler, input.texcoord).r;

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

		output.pos = float4(input.positionWS.xyz, length(emission));

		output.velocity = velocity;

		return output;
	}
}