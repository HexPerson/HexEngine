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
	SnowCommon
}
"HullShaderIncludes"
{
	MeshCommon
	SnowCommon
}
"DomainShaderIncludes"
{
	MeshCommon
	SnowCommon
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
	// Tessellation VS: no projection here - transform each control point to
	// WORLD space and hand it to the hull shader. The displacement + clip
	// transform happen in the domain shader after subdivision.
	SnowCP ShaderMain(MeshVertexInput input, MeshInstanceData instance, uint instanceID : SV_INSTANCEID)
	{
		SnowCP o;
		input.position.w = 1.0f;

		matrix worldMatrix  = mul(instance.world, g_worldMatrix);
		matrix normalMatrix = mul(instance.worldInverseTranspose, g_worldMatrix);
		matrix worldPrev    = mul(instance.worldPrev, g_worldMatrix);

		o.worldPos  = mul(input.position, worldMatrix).xyz;
		o.worldPrev = mul(input.position, worldPrev).xyz;
		o.normal    = normalize(mul(input.normal,   (float3x3)normalMatrix));
		o.tangent   = normalize(mul(input.tangent,  (float3x3)normalMatrix));
		o.binormal  = normalize(mul(input.binormal, (float3x3)normalMatrix));
		o.texcoord  = input.texcoord * instance.uvScale;
		o.colour    = instance.colour;
		o.instanceID = instanceID + entityId;
		return o;
	}
}
"HullShader"
{
	// Distance-based LOD: dense subdivision near the camera where snow
	// relief reads, collapsing to 1 (no added geometry) by ~44 m. Coverage
	// gates the whole thing off when it isn't snowing, so the patch cost is
	// only paid while snow is on the ground.
	float SnowTessFactor(float3 worldMid)
	{
		const float d = distance(worldMid, g_eyePos.xyz);
		const float lod = lerp(16.0f, 1.0f, saturate((d - 4.0f) / 40.0f));
		const float covGate = step(0.01f, g_weatherSurface.snowCoverage);
		return max(1.0f, lod * covGate);
	}

	SnowHSConst ConstantHS(InputPatch<SnowCP, 3> ip, uint pid : SV_PrimitiveID)
	{
		SnowHSConst o;
		const float3 c0 = ip[0].worldPos;
		const float3 c1 = ip[1].worldPos;
		const float3 c2 = ip[2].worldPos;
		// SV_TessFactor[i] governs the edge OPPOSITE control point i.
		o.edges[0] = SnowTessFactor((c1 + c2) * 0.5f);
		o.edges[1] = SnowTessFactor((c2 + c0) * 0.5f);
		o.edges[2] = SnowTessFactor((c0 + c1) * 0.5f);
		o.inside   = (o.edges[0] + o.edges[1] + o.edges[2]) / 3.0f;
		return o;
	}

	[domain("tri")]
	[partitioning("fractional_odd")]
	[outputtopology("triangle_cw")]
	[outputcontrolpoints(3)]
	[patchconstantfunc("ConstantHS")]
	SnowCP ShaderMain(InputPatch<SnowCP, 3> ip, uint i : SV_OutputControlPointID, uint pid : SV_PrimitiveID)
	{
		return ip[i];
	}
}
"DomainShader"
{
	// Interpolate the subdivided vertex, lift it by the snow height field,
	// then project. The pixel shader still owns albedo/roughness/relief
	// normals/POM - this stage only supplies the real geometry POM cannot:
	// a raised, self-occluding, silhouette-breaking surface.
	[domain("tri")]
	MeshPixelInput ShaderMain(SnowHSConst patchConst, float3 bary : SV_DomainLocation, const OutputPatch<SnowCP, 3> patch)
	{
		MeshPixelInput o;

		float3 worldPos  = patch[0].worldPos  * bary.x + patch[1].worldPos  * bary.y + patch[2].worldPos  * bary.z;
		float3 worldPrev = patch[0].worldPrev * bary.x + patch[1].worldPrev * bary.y + patch[2].worldPrev * bary.z;
		float3 normal    = normalize(patch[0].normal   * bary.x + patch[1].normal   * bary.y + patch[2].normal   * bary.z);
		float3 tangent   = normalize(patch[0].tangent  * bary.x + patch[1].tangent  * bary.y + patch[2].tangent  * bary.z);
		float3 binormal  = normalize(patch[0].binormal * bary.x + patch[1].binormal * bary.y + patch[2].binormal * bary.z);
		float2 uv        = patch[0].texcoord * bary.x + patch[1].texcoord * bary.y + patch[2].texcoord * bary.z;
		float4 colour    = patch[0].colour   * bary.x + patch[1].colour   * bary.y + patch[2].colour   * bary.z;

		// Lift along world up. Displacement is static (view/time independent)
		// so the previous-frame position gets the SAME lift - camera-motion
		// velocity stays correct, TAA doesn't smear the snow.
		const float disp = SnowDisplacement(worldPos, normal, g_weatherSurface.snowCoverage, g_weatherSurface.snowMelt);
		worldPos.y  += disp;
		worldPrev.y += disp;

		o.positionWS = float4(worldPos, 1.0f);
		o.position   = mul(float4(worldPos, 1.0f), g_viewProjectionMatrix);

		const float4 prevClip = mul(float4(worldPrev, 1.0f), g_viewProjectionMatrixPrev);
		o.previousPositionUnjittered = prevClip;
		o.currentPositionUnjittered  = o.position;

		// TAA jitter, matching the standard VS.
		o.position.xy += g_jitterOffsets * o.position.w;

		o.texcoord = uv;
		o.normal   = normal;
		o.tangent  = tangent;
		o.binormal = binormal;
		o.viewDirection = float4(normalize(g_eyePos.xyz - worldPos), 0.0f);
		o.colour = colour;
		o.instanceID = patch[0].instanceID;
		o.cullDistance = 1.0f;

		o.lightViewPosition1 = float4(0, 0, 0, 0);
		o.lightViewPosition2 = float4(0, 0, 0, 0);
		o.lightViewPosition3 = float4(0, 0, 0, 0);
		o.lightViewPosition4 = float4(0, 0, 0, 0);
		return o;
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
