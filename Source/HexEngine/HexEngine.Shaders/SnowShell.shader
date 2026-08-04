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
		// Min factor 2 (not 1) and a longer 55 m reach keep mid-distance
		// triangles small enough that the edge taper spans several of them -
		// otherwise the ramp collapses into one tilted facet per triangle and
		// the snow edge reads as a triangular staircase.
		const float lod = lerp(16.0f, 5.0f, saturate((d - 4.0f) / 25.0f));
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
	// Rain/shelter occlusion map at DOMAIN-stage t0 (bound per frame by
	// SceneRenderer). Top-down depth of the highest surface - used here to
	// find nearby walls/props so snow banks UP against them (drift banks).
	Texture2D<float> g_dsOcclusionMap : register(t0);

	// How much a wall stands over this ground point, 0..1, from the occlusion
	// map neighbourhood. Load() texel fetches (no sampler needed on the DS).
	float SnowDriftBank(float3 worldPos)
	{
		if (g_rainOcclusionParams.x < 0.5f)
			return 0.0f;
		const float4 clip = mul(float4(worldPos, 1.0f), g_rainOcclusionVP);
		const float2 uv = clip.xy * float2(0.5f, -0.5f) + 0.5f;
		if (any(uv < 0.0f) || any(uv > 1.0f) || clip.z < 0.0f || clip.z > 1.0f)
			return 0.0f;

		uint mw, mh;
		g_dsOcclusionMap.GetDimensions(mw, mh);
		const int2 tc = int2(uv * float2(mw, mh));
		const float here = clip.z;
		const float kWallDelta = 0.6f / 160.0f; // a neighbour >=0.8m taller = wall

		// 8 directions, two rings (~0.5m and ~1.1m out at 192m/2048). Closer
		// ring counts double so the bank ramps UP toward the wall.
		const float2 dirs[8] = {
			float2(1,0), float2(-1,0), float2(0,1), float2(0,-1),
			float2(0.7f,0.7f), float2(-0.7f,0.7f), float2(0.7f,-0.7f), float2(-0.7f,-0.7f) };
		float bank = 0.0f;
		[unroll]
		for (int i = 0; i < 8; ++i)
		{
			const int2 tNear = clamp(tc + int2(dirs[i] * 5.0f),  int2(0,0), int2(mw - 1, mh - 1));
			const int2 tFar  = clamp(tc + int2(dirs[i] * 11.0f), int2(0,0), int2(mw - 1, mh - 1));
			bank += (here - g_dsOcclusionMap.Load(int3(tNear, 0)) > kWallDelta) ? 2.0f : 0.0f;
			bank += (here - g_dsOcclusionMap.Load(int3(tFar,  0)) > kWallDelta) ? 1.0f : 0.0f;
		}
		return saturate(bank / 12.0f);
	}

	// Shelter (1 = exposed to sky, 0 = covered) at the DOMAIN stage, from the
	// same top-down occlusion map. Lets the shell's GEOMETRY taper to ground
	// height under cover, instead of the pixel shader clipping a full-height
	// slab and leaving a vertical snow cliff at every occluder/awning edge.
	// Load taps + the same soft depth band as the pixel-side SampleRainShelter;
	// these per-vertex values interpolate across the tessellated triangles into
	// a smooth ramp.
	float SnowShelterDS(float3 worldPos)
	{
		if (g_rainOcclusionParams.x < 0.5f)
			return 1.0f;
		const float4 clip = mul(float4(worldPos, 1.0f), g_rainOcclusionVP);
		const float2 uv = clip.xy * float2(0.5f, -0.5f) + 0.5f;
		if (any(uv < 0.0f) || any(uv > 1.0f) || clip.z < 0.0f || clip.z > 1.0f)
			return 1.0f;

		uint mw, mh;
		g_dsOcclusionMap.GetDimensions(mw, mh);
		const float2 tc = uv * float2(mw, mh);
		const float bias = g_rainOcclusionParams.y;
		const float band = 3.0f / 160.0f; // match SampleRainShelter's soft depth band
		const float2 dirs[8] = {
			float2(1,0), float2(-1,0), float2(0,1), float2(0,-1),
			float2(0.7f,0.7f), float2(-0.7f,0.7f), float2(0.7f,-0.7f), float2(-0.7f,-0.7f) };
		// Centre + two rings (~0.6 m and ~1.2 m at 2048/192 m) give a ~1.2 m
		// wide shelter gradient, so the geometric ramp is long enough to cross
		// several tessellated triangles and read as a slope, not a staircase.
		const int2 tHere = clamp(int2(tc), int2(0,0), int2(mw - 1, mh - 1));
		float exposed = 1.0f - smoothstep(bias, bias + band, clip.z - g_dsOcclusionMap.Load(int3(tHere, 0)));
		[unroll]
		for (int i = 0; i < 8; ++i)
		{
			const int2 t0 = clamp(int2(tc + dirs[i] *  6.0f), int2(0,0), int2(mw - 1, mh - 1));
			const int2 t1 = clamp(int2(tc + dirs[i] * 13.0f), int2(0,0), int2(mw - 1, mh - 1));
			exposed += 1.0f - smoothstep(bias, bias + band, clip.z - g_dsOcclusionMap.Load(int3(t0, 0)));
			exposed += 1.0f - smoothstep(bias, bias + band, clip.z - g_dsOcclusionMap.Load(int3(t1, 0)));
		}
		return exposed * (1.0f / 17.0f);
	}

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
		// Geometric up-ness of the UNDERLYING surface (mesh normal), captured
		// before we overwrite the shading normal with world-up. The pixel
		// shader clips on this so snow only lands on up-facing faces - vertical
		// walls of a shared mesh get none - while shading still uses the clean
		// up + per-pixel relief (no faceting).
		const float geoUp = normal.y;
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
		// Drift banks: snow piles UP against nearby walls/objects. Detected
		// from the occlusion map; scaled by snow amount so it fades with
		// coverage/melt. Up to ~22cm of extra lift right against a wall.
		const float drift = SnowDriftBank(worldPos) * snowAmt;
		// Height taper: ramp the extrusion DOWN to ground at the shell's edges
		// so it lerps into the bare surface instead of clipping a full-height
		// slab (the hard snow cliff the user saw at occlusion-map edges). Uses
		// the SAME low-frequency gates the pixel shader clips on - shelter
		// (occlusion map), up-slope, coverage/melt - so the geometry height and
		// the clip line move together and the shell tapers exactly where it's
		// about to be cut. The per-pixel height-field breakup still clips fine
		// detail in the pixel shader; this only kills the big geometric cliffs.
		const float shelterDS = SnowShelterDS(worldPos);
		const float slopeGate = smoothstep(0.35f, 0.85f, geoUp);
		const float presence  = shelterDS * slopeGate
			* saturate(g_weatherSurface.snowCoverage) * (1.0f - saturate(g_weatherSurface.snowMelt));
		const float taper = smoothstep(0.05f, 0.85f, presence);
		// Taper the WHOLE lift (thin base included) to ~0 at the edges so the
		// shell feathers all the way down to meet the ground. The pixel shader
		// is handed this exact thickness and clips the fringe where it's only a
		// few mm tall - so the snow is cut while it's flush with the ground (no
		// vertical lip / seam) and, because it's clipped before it reaches the
		// concrete plane, can't z-fight the rigid base draw either.
		// Footprints are NOT applied here: a print (~25 cm) is smaller than a
		// tessellation triangle at any distance, so per-vertex compression
		// aliased into streaks. The foot shape is a PER-PIXEL shading detail in
		// DefaultPixel (albedo darken + normal dent) instead.
		const float disp = (0.008f + snowAmt * 0.10f + drift * 0.42f) * taper;
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
		// .w carries the tapered snow THICKNESS (metres). The pixel shader
		// clips the fringe on it, so the cut lands exactly where the layer is
		// thin - a seamless feathered edge. Wall exclusion rides along for
		// free: the slope gate zeroes presence -> taper -> thickness on
		// vertical faces, so they clip out with no separate test.
		o.viewDirection = float4(normalize(g_eyePos.xyz - worldPos), disp);
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
		// The domain shader already tapered the snow thickness (metres) to ~0
		// at every edge - shelter (occlusion map), coverage, melt and wall
		// slope all funnel through it - and handed it over in viewDirection.w.
		// Clip the fringe where the layer thins below a height-field-perturbed
		// ~1 cm line: the boundary meanders into a natural broken edge AND lands
		// exactly where the snow is only millimetres tall, so it feathers into
		// the ground with no vertical seam and can't z-fight the concrete.
		// Because the interior thickness is centimetres it never crosses the
		// cut line, so no holes speckle the sheet.
		const float thick = input.viewDirection.w;
		const float h = SnowHeightField(input.positionWS.xz);   // 0..1
		clip(thick - (0.010f + (0.5f - h) * 0.010f));           // ~0..2 cm wavy cut

		return DefaultPixelShader(input);
	}
}
