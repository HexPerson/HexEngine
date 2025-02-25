"Requirements"
{
	//ShadowMaps
}
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
	Atmosphere
	ShadowUtils
	Utils
	LightingUtils
}
"VertexShader"
{
	MeshPixelInput ShaderMain(MeshVertexInput input, MeshInstanceData instance, uint instanceID : SV_INSTANCEID)
	{
		MeshPixelInput output;
		
		input.position.w = 1.0f;

		matrix worldMatrix, normalMatrix, worldPrev;

		/*if ((g_objectFlags & OBJECT_FLAGS_HAS_ANIMATION) != 0)
		{
			matrix	boneTransform = mul(input.boneWeights[0], g_boneTransforms[(int)input.boneIds[0]]);

			boneTransform += mul(input.boneWeights[1], g_boneTransforms[(int)input.boneIds[1]]);
			boneTransform += mul(input.boneWeights[2], g_boneTransforms[(int)input.boneIds[2]]);
			boneTransform += mul(input.boneWeights[3], g_boneTransforms[(int)input.boneIds[3]]);

			worldMatrix = mul(boneTransform, instance.world);
			normalMatrix = mul(boneTransform, instance.worldInverseTranspose);
			worldPrev = mul(boneTransform, instance.worldPrev);
		}
		else*/
		{
			worldMatrix = instance.world;//mul(instance.world, g_worldMatrix);
			normalMatrix = instance.worldInverseTranspose;//mul(instance.worldInverseTranspose, g_worldMatrix);
			worldPrev = instance.worldPrev;//mul(instance.worldPrev, g_worldMatrix);;
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

		// Apply TAA jitter
		output.position.xy += g_jitterOffsets * output.position.w;
					
		output.texcoord = input.texcoord * instance.uvScale;	
		
		output.normal = mul(input.normal, (float3x3)normalMatrix);
		output.normal = normalize(output.normal);
		
		output.tangent = mul(input.tangent, (float3x3)normalMatrix);
		output.tangent = normalize(output.tangent);
		
		output.binormal = mul(input.binormal, (float3x3)normalMatrix);
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
	

	#include "DefaultPixel.shader"

	GBufferOut ShaderMain(MeshPixelInput input)
	{
		return DefaultPixelShader(input);
	}
}