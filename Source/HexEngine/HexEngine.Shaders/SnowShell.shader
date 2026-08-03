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
	// Snow SHELL (Phase 3, tess slice 4). Unlike DefaultSnowTess this does
	// NOT replace the surface - the concrete draws rigidly in its own pass,
	// and this shader draws a SECOND, extruded copy on top that clips to
	// nothing at its edges, so snow reads as a layer sitting ON the ground
	// instead of the ground itself deforming. Same world-space control
	// point transform as the standard tess VS.
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
	// Extrude the shell UP off the concrete by the snow height. The concrete
	// underneath is untouched (drawn in its own rigid pass); this raised
	// copy is what carries the snow volume + silhouette.
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

		// Extrude up: thin base (clears the concrete, no z-fight) + a SMOOTH,
		// large-scale height for gentle volume. Only a single ~90cm octave -
		// the full SnowHeightField has 13cm detail that the coarse far-LOD
		// tessellation (triangles metres wide) can't represent, so it aliased
		// into big tilted facets = the radiating fans. Fine snow texture lives
		// in the per-pixel relief normal instead. Static -> prev same lift (TAA).
		const float hSmooth = SnowHeight_Noise3(float3(worldPos.x, 0.0f, worldPos.z) / 0.9f);
		const float snowAmt = saturate((0.3f + hSmooth * 0.7f) * g_weatherSurface.snowCoverage)
			* (1.0f - saturate(g_weatherSurface.snowMelt));
		const float disp = 0.02f + snowAmt * 0.10f;
		worldPos.y  += disp;
		worldPrev.y += disp;

		// Clean world-up shading normal - NOT the mesh normals, and NOT a
		// per-VERTEX height-field gradient (that aliased the coarse far-LOD
		// tessellation into radiating dark fans). The snow's surface relief
		// is added PER-PIXEL in the pixel shader instead, where it's smooth
		// regardless of tessellation density.
		normal = float3(0.0f, 1.0f, 0.0f);

		o.positionWS = float4(worldPos, 1.0f);
		o.position   = mul(float4(worldPos, 1.0f), g_viewProjectionMatrix);

		const float4 prevClip = mul(float4(worldPrev, 1.0f), g_viewProjectionMatrixPrev);
		o.previousPositionUnjittered = prevClip;
		o.currentPositionUnjittered  = o.position;
		o.position.xy += g_jitterOffsets * o.position.w;

		// Snow textures tile in WORLD space (the road UVs are asphalt-scaled),
		// with a world-aligned tangent basis so the snow normal map applies
		// cleanly (not via the mesh's faceted tangents). ~0.35 = ~3m tile.
		o.texcoord = worldPos.xz * 0.15f;
		o.normal   = normal;
		o.tangent  = float3(1.0f, 0.0f, 0.0f);
		o.binormal = float3(0.0f, 0.0f, 1.0f);
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
	// Reuse DefaultPixel for identical snow shading/lighting as the flat
	// ground - but suppress its albedo-alpha clip (which discards every
	// shell pixel over an alpha-less graph material). The shell keeps its
	// OWN thickness clip below.
	#define SNOW_SHELL_NO_CLIP
	#include "DefaultPixel.shader"

	GBufferOut ShaderMain(MeshPixelInput input)
	{
		// Feather the shell to where snow accumulates: the height field +
		// slope + coverage give a natural broken edge; clipping below a
		// threshold thins the layer to nothing so the rigid concrete shows
		// through at the margins. This is what reads as "snow ON the
		// surface" rather than a solid slab.
		// Shelter: no snow under static cover (awnings, bridges, indoors).
		// SampleRainShelter reads the top-down occlusion map at t26; under
		// cover thickness -> 0 -> clipped -> bare concrete shows through.
		const float shelter = SampleRainShelter(input.positionWS.xyz, g_textureSampler);
		const float slope = smoothstep(0.35f, 0.85f, input.normal.y);
		const float h = SnowHeightField(input.positionWS.xz);
		const float thickness = saturate(slope * (0.2f + h * 1.4f) * g_weatherSurface.snowCoverage * shelter)
			* (1.0f - saturate(g_weatherSurface.snowMelt));
		clip(thickness - 0.06f);

		return DefaultPixelShader(input);
	}
}
