"InputLayout"
{
	PosNormTanBinTex_INSTANCED
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
	MeshPixelInput ShaderMain(MeshVertexInput input, MeshInstanceData instance)
	{
		MeshPixelInput output;
		
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
			worldMatrix = instance.world;

		output.position = mul(input.position, worldMatrix);
		output.positionWS = output.position;

		output.position = mul(output.position, g_viewProjectionMatrix);

#if 0
		// Calculate velocity
		float4x4 prevFrame_modelMatrix = instance.worldPrev;
		float4 prevFrame_worldPos = mul(input.position, worldMatrix);
		float4 prevFrame_clipPos = mul(prevFrame_worldPos, g_viewProjectionMatrixPrev);

		output.velocity = CalcVelocity(prevFrame_clipPos, output.position, float2(g_screenWidth, g_screenHeight));

		// Apply TAA jitter
		output.position.xy += g_jitterOffsets * output.position.w;		
#endif

		output.texcoord = input.texcoord;
		
		return output;
	}
}
"PixelShader"
{
	Texture2D g_opacityMap : register(t7);
	SamplerState g_textureSampler : register(s0);

	float4 ShaderMain(MeshPixelInput input) : SV_Target
	{
		// Calculate the pixel depth
		//
		float4 worldViewPosition = mul(input.positionWS, g_viewMatrix);
		float pixelDepth = -worldViewPosition.z;

		pixelDepth = pixelDepth / g_frustumDepths[3];

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

		return float4(pixelDepth, pixelDepth, pixelDepth, opacity);
	}
}