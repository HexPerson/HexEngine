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
		float4 albedo = g_albedoMap.Sample(g_textureSampler, input.texcoord);

		if (g_material.isInTransparencyPhase)
		{
			if (g_objectFlags & OBJECT_FLAGS_HAS_OPACITY)
			{
				opacity = g_opacityMap.Sample(g_textureSampler, input.texcoord).r;
			}

			if ((g_objectFlags & OBJECT_FLAGS_HAS_OPACITY) == 0)
			{
				float minChannel = min(albedo.r, min(albedo.g, albedo.b));
				float whiteMask = smoothstep(0.85f, 0.995f, minChannel);
				opacity *= (1.0f - whiteMask);
			}

			if (opacity <= 0.0f)
				clip(-1);
		}

		if(albedo.a == 0.0f && g_material.isInTransparencyPhase == 0)
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

		// Write the post-rasterizer NDC.z (input.position.z is already divided by w by
		// the rasterizer) into the R32_FLOAT colour RT. This gives the volumetric's point-
		// shadow cubemap-array path a CLEAN, RT-bound source texture to CopySubresourceRegion
		// from. The previous "return 1.0" only let the volumetric copy the underlying DSV-
		// bound R32_TYPELESS depth texture, and some D3D11 drivers silently zero that copy
		// when the source still carries DEPTH_STENCIL bind state - the symptom was every
		// cube face reading 0 (occluder at near plane), collapsing point-light volumetric
		// contribution to a tiny 2m cube-shape around the light. Writing depth into the
		// colour RT makes the cube-array copy source a plain R32_FLOAT shader resource
		// with no implicit depth-state baggage, which works on all drivers we've tested.
		// Alpha keeps `opacity` so callers that look at coverage still get it.
		return float4(input.position.z.rrr, opacity);
	}
}
