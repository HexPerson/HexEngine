"InputLayout"
{
	PosTex_INSTANCED_SIMPLE
}
"VertexShaderIncludes"
{
	MeshCommon
	Utils
}
"PixelShaderIncludes"
{
	MeshCommon
}
"VertexShader"
{
	SimpleMeshPixelInput ShaderMain(SimpleMeshVertexInput input, SimpleMeshInstanceData instance)
	{
		SimpleMeshPixelInput output;
		
		input.position.w = 1.0f;

		matrix worldMatrix;

		/*if (g_objectFlags & OBJECT_FLAGS_HAS_ANIMATION)
		{
			matrix	boneTransform = mul(input.boneWeights[0], g_boneTransforms[(int)input.boneIds[0]]);
			boneTransform += mul(input.boneWeights[1], g_boneTransforms[(int)input.boneIds[1]]);
			boneTransform += mul(input.boneWeights[2], g_boneTransforms[(int)input.boneIds[2]]);
			boneTransform += mul(input.boneWeights[3], g_boneTransforms[(int)input.boneIds[3]]);

			worldMatrix = mul(boneTransform, instance.world);
		}
		else*/
			worldMatrix = mul(instance.world, g_worldMatrix);

		output.position = mul(input.position, worldMatrix);
		//output.positionWS = output.position;

		output.position = mul(output.position, g_viewProjectionMatrix);


		output.texcoord = input.texcoord;
		
		return output;
	}
}
"PixelShader"
{
	Texture2D g_albedoMap : register(t0);
	Texture2D g_opacityMap : register(t7);
	SamplerState g_textureSampler : register(s0);

	float4 ShaderMain(SimpleMeshPixelInput input) : SV_Target
	{
		// Calculate the pixel depth
		//
		//float4 worldViewPosition = mul(input.positionWS, g_viewMatrix);
		//float pixelDepth = -worldViewPosition.z;

		//pixelDepth = pixelDepth / g_frustumDepths[3];

		float opacity = 1.0f;

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

		if(g_albedoMap.Sample(g_textureSampler, input.texcoord).a == 0.0f && g_material.isInTransparencyPhase == 0)
			clip(-1);
		/*else
		{
			if (g_objectFlags & OBJECT_FLAGS_HAS_OPACITY)
			{
				opacity = g_opacityMap.Sample(g_textureSampler, input.texcoord);

				if (opacity < 1.0f)
				{
					clip(-1);
				}
			}
		}*/

		return float4(1.0f.rrr, opacity);
	}
}