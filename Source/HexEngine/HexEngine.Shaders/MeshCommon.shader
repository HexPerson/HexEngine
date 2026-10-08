"GlobalIncludes"
{
	Global
}
"Global"
{	
#ifndef MESHCOMMON_SHADER
#define MESHCOMMON_SHADER
	// This is the structure sent to the vertex shader from the gpu
	struct MeshVertexInput
	{
		float4 position 	: POSITION;
		float3 normal 		: NORMAL;
		float3 tangent 		: TANGENT;
		float3 binormal 	: BINORMAL;
		float2 texcoord		: TEXCOORD0;	

		// Bone stuff
		//float4 boneIds		: BLENDINDICES;
		//float4 boneWeights	: BLENDWEIGHT;
	};

	struct SimpleMeshVertexInput
	{
		float4 position 	: POSITION;
		float2 texcoord		: TEXCOORD0;	
	};

	// Pre-skinned characters (GpuSkinning.cpp). The vertex buffer being drawn is also
	// bound here as raw bytes, so ANY mesh shader - not just DefaultAnimated - can read
	// the skinned vertex's LAST-frame position (parked in the AnimatedMeshVertex
	// BLENDINDICES slot) by SV_VertexID and give deformation real motion vectors.
	// Read only under OBJECT_FLAGS_PRESKINNED. t40 sits inside the D3D12 root
	// signature's SRV range (t0-t63), so declaring it is harmless there.
	ByteAddressBuffer g_preSkinnedVertices : register(t40);
	static const uint PRESKINNED_VERTEX_STRIDE = 92;			// sizeof(AnimatedMeshVertex)
	static const uint PRESKINNED_PREV_POSITION_OFFSET = 60;	// AnimatedMeshVertex::_boneIds

	// Object-space position this vertex had last frame (for previousPositionUnjittered).
	float4 PreviousLocalPosition(float4 currentLocalPosition, uint vertexId)
	{
		[branch]
		if ((g_objectFlags & OBJECT_FLAGS_PRESKINNED) != 0)
			return float4(asfloat(g_preSkinnedVertices.Load3(vertexId * PRESKINNED_VERTEX_STRIDE + PRESKINNED_PREV_POSITION_OFFSET)), 1.0f);
		return currentLocalPosition;
	}

	struct AnimatedMeshVertexInput
	{
		float4 position 	: POSITION;
		float3 normal 		: NORMAL;
		float3 tangent 		: TANGENT;
		float3 binormal 	: BINORMAL;
		float2 texcoord		: TEXCOORD0;

		// Bone stuff
		float4 boneIds		: BLENDINDICES;
		float4 boneWeights	: BLENDWEIGHT;
	};

	struct SimpleAnimatedMeshVertexInput
	{
		float4 position 	: POSITION;
		float2 texcoord		: TEXCOORD0;

		// Bone stuff
		float4 boneIds		: BLENDINDICES;
		float4 boneWeights	: BLENDWEIGHT;
	};


	struct MeshInstanceData
	{
		matrix world : WORLD;
		matrix worldInverseTranspose : WORLDIT;
		matrix worldPrev : WORLDPREV;
		float4 colour : INSTANCECOLOR;
		float2 uvScale : UVSCALE;
		//uint instanceID : SV_INSTANCEID;
	};

	struct SimpleMeshInstanceData
	{
		matrix world : WORLD;
	};
	
	// This is the structure sent to the pixel shader from the vertex shader
	struct MeshPixelInput
	{
		float4 position : SV_POSITION;
		float2 texcoord : TEXCOORD0;
		float3 normal : NORMAL;
		float3 tangent : TANGENT;
		float3 binormal : BINORMAL;
		float4 lightViewPosition1 : TEXCOORD1;
		float4 lightViewPosition2 : TEXCOORD2;
		float4 lightViewPosition3 : TEXCOORD3;
		float4 lightViewPosition4 : TEXCOORD4;
		float4 positionWS : TEXCOORD5;
		float4 viewDirection : TEXCOORD6;
		float4 colour : TEXCOORD7;

		// taa stuff
		float4 previousPositionUnjittered : TEXCOORD8;
		float4 currentPositionUnjittered : TEXCOORD9;

		uint instanceID : SV_INSTANCEID;
		float cullDistance : SV_CullDistance;
	};

	struct SimpleMeshPixelInput
	{
		float4 position : SV_POSITION;
		float2 texcoord : TEXCOORD0;
	};

	// ===== Vegetation wind sway (Phase 3) ===================================
	// PURE function of its parameters - deliberately nothing else - so the
	// SHADOW vertex shader (input = position+UV only, instance = bare world
	// matrix) computes the exact same offset as the main VS and shadows track
	// the canopy. The main VS calls it TWICE: at g_time for the current
	// position and at g_timePrev for the previous-frame position, so the
	// displacement delta lands in previousPositionUnjittered and TAA/DLSS get
	// real motion vectors for the sway instead of smearing it.
	//
	// sway = g_material.windSwayParams:
	//   x = trunk bend strength, y = flutter strength,
	//   z = characteristic height (m), w = mode (0 off / 1 tree / 2 grass)
	// windDirSpeed = g_weatherSurface.windDirectionAndSpeed
	//   (normalized dir xyz, speed w in m/s; presets author 5 calm .. 29 storm)
	float3 WindSwayOffset(
		float3 worldPos,
		float3 origin,
		float4 sway,
		float4 windDirSpeed,
		float time)
	{
		if (sway.w < 0.5f)
			return float3(0.0f, 0.0f, 0.0f);

		const float speed = windDirSpeed.w;
		// Response curve: calm presets (~5 m/s) barely move, storms (24-29 m/s)
		// are violent. Slightly superlinear so the mid presets read distinct.
		const float windF = pow(saturate(speed / 30.0f), 1.4f);
		if (windF <= 0.001f)
			return float3(0.0f, 0.0f, 0.0f);

		float2 windDir = windDirSpeed.xz;
		const float dirLen = length(windDir);
		windDir = (dirLen > 0.001f) ? windDir / dirLen : float2(1.0f, 0.0f);

		// Height above the object's origin normalised by the plant's
		// characteristic height. The base does not move (roots stay planted,
		// and vertices shared with the ground can't crack); the canopy takes
		// the full ride. h^2 approximates a cantilever for trees; grass
		// (mode 2) leans linearly instead - a blade is not a trunk.
		const float charH = max(sway.z, 0.1f);
		const float h = saturate((worldPos.y - origin.y) / charH);
		const bool isGrass = sway.w > 1.5f;
		const float bendW = isGrass ? h : h * h;

		// Per-plant phase from the object origin so a row of trees desyncs.
		const float phase = dot(origin, float3(12.9898f, 78.233f, 37.719f));

		// Gusts: three unsynchronised bands. Frequency scales gently with wind
		// speed (storm gusts are faster, not just larger). Biased forward -
		// wind pushes much more than it springs back.
		const float fScale = 1.0f + speed * 0.04f;
		const float gust =
			  0.55f * sin(time * 0.9f * fScale + phase)
			+ 0.30f * sin(time * 2.3f * fScale + phase * 1.7f)
			+ 0.15f * sin(time * 5.1f * fScale + phase * 2.9f);
		const float lean = 0.575f + 0.425f * gust;

		// Trunk bend along the wind. The canopy also drops slightly as it
		// bends (an arc, not a shear) so tall trees don't read as skewed
		// rectangles.
		float3 offset;
		offset.xz = windDir * (sway.x * bendW * windF * lean);
		offset.y = -0.3f * bendW * length(offset.xz);

		// Flutter: high-frequency spatial sines keyed on the vertex's own
		// world position - neighbouring leaves get different phases. Grass
		// keeps only a shimmer of it. Amplitude stays in centimetres or the
		// motion aliases under TAA.
		const float flutterW = isGrass ? 0.35f : 1.0f;
		const float lateral = saturate(length(worldPos.xz - origin.xz) / max(charH * 0.5f, 0.1f));
		const float flutterAmp = sway.y * windF * flutterW
			* (0.3f + 0.7f * max(h, lateral)) * 0.05f;
		const float ft = time * (6.0f + speed * 0.35f);
		offset.x += flutterAmp * sin(ft * 1.00f + worldPos.y * 7.3f + worldPos.z * 5.1f + phase);
		offset.z += flutterAmp * sin(ft * 1.13f + worldPos.x * 6.1f + worldPos.y * 8.7f + phase * 1.3f);
		offset.y += flutterAmp * 0.5f * sin(ft * 0.91f + worldPos.x * 4.9f + worldPos.z * 6.7f);

		return offset;
	}

#endif
}