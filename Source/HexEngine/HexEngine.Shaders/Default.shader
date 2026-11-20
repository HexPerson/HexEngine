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
	PBRutils
}
"VertexShader"
{
	MeshPixelInput ShaderMain(MeshVertexInput input, MeshInstanceData instance, uint instanceID : SV_INSTANCEID, uint vertexID : SV_VertexID)
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
			worldMatrix = mul(instance.world, g_worldMatrix);
			normalMatrix = mul(instance.worldInverseTranspose, g_worldMatrix);
			worldPrev = mul(instance.worldPrev, g_worldMatrix);
		}

		output.position = mul(input.position, worldMatrix);
		output.positionWS = output.position;

		// Vegetation wind sway (Phase 3). Evaluated at BOTH g_time (current
		// position) and g_timePrev (previous-frame position, below) so the
		// displacement delta reaches previousPositionUnjittered - TAA/DLSS
		// motion vectors track the sway instead of smearing it (the
		// DefaultSnowTess worldPos/worldPrev precedent extended to a
		// time-dependent offset). Zero-cost for materials that don't opt in.
		[branch]
		if (g_material.windSwayParams.w > 0.5f)
		{
			output.position.xyz += WindSwayOffset(
				output.position.xyz, worldMatrix[3].xyz,
				g_material.windSwayParams,
				g_weatherSurface.windDirectionAndSpeed, g_time);
			output.positionWS = output.position;
		}

		if(g_cullDistance > 0.0f)
		{
			output.cullDistance = length(output.positionWS.xyz - g_eyePos.xyz) >= g_cullDistance ? -1.0f : 1.0f;
		}

		output.position = mul(output.position, g_viewProjectionMatrix);

		// Calculate velocity
		float4x4 prevFrame_modelMatrix = worldPrev;
		// Skinned meshes drawn from GpuSkinning's output read last frame's pose here.
		float4 prevFrame_worldPos = mul(PreviousLocalPosition(input.position, vertexID), prevFrame_modelMatrix);
		// Previous-frame sway: same function at g_timePrev against the
		// previous world transform (static vegetation: identical matrix, the
		// TIME term is the whole delta). Current wind is used for both - wind
		// changes far slower than a frame.
		[branch]
		if (g_material.windSwayParams.w > 0.5f)
		{
			prevFrame_worldPos.xyz += WindSwayOffset(
				prevFrame_worldPos.xyz, prevFrame_modelMatrix[3].xyz,
				g_material.windSwayParams,
				g_weatherSurface.windDirectionAndSpeed, g_timePrev);
		}
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
