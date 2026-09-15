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
	Utils
}
"VertexShader"
{
	// Skinned silhouette transform. Mirrors DefaultAnimated's VS exactly so the
	// outline seed lines up with the ANIMATED mesh. The static OutlineSeed VS
	// ignored the bone matrices, so skinned meshes were seeded (and therefore
	// outlined) in their bind/T-pose while the lit mesh animated. The bone
	// palette (g_boneTransforms) and OBJECT_FLAGS_HAS_ANIMATION are already
	// bound by StaticMeshComponent::RenderMesh -> AnimatedMesh::UpdateConstantBuffer
	// before this shader is swapped in; we only need to consume them.
	MeshPixelInput ShaderMain(AnimatedMeshVertexInput input, MeshInstanceData instance, uint instanceID : SV_INSTANCEID)
	{
		MeshPixelInput output = (MeshPixelInput)0;

		input.position.w = 1.0f;
		output.cullDistance = 0.5f;

		matrix worldMatrix;

		if ((g_objectFlags & OBJECT_FLAGS_HAS_ANIMATION) != 0)
		{
			matrix boneTransform  = mul(input.boneWeights[0], g_boneTransforms[(int)input.boneIds[0]]);
			boneTransform        += mul(input.boneWeights[1], g_boneTransforms[(int)input.boneIds[1]]);
			boneTransform        += mul(input.boneWeights[2], g_boneTransforms[(int)input.boneIds[2]]);
			boneTransform        += mul(input.boneWeights[3], g_boneTransforms[(int)input.boneIds[3]]);

			worldMatrix = mul(boneTransform, instance.world);
		}
		else
		{
			worldMatrix = instance.world;
		}

		float4 worldPos = mul(input.position, worldMatrix);
		output.position = mul(worldPos, g_viewProjectionMatrix);

		return output;
	}
}
"PixelShader"
{
	// Jump-flood seed: every covered pixel writes its OWN absolute screen
	// coordinate (in pixels) as the seed. Identical to OutlineSeed's PS.
	float4 ShaderMain(MeshPixelInput input) : SV_Target
	{
		return float4(input.position.xy, 0.0f, 1.0f);
	}
}
