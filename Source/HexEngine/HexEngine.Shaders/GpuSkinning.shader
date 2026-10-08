"ComputeShaderIncludes"
{
	Global
}
"ComputeShader"
{
	// Compute skinning (GpuSkinning.cpp). One thread per vertex: skin the bind-pose
	// vertex with the current and previous bone palettes and write it into the
	// entity's skinned vertex buffers, which every pass then draws rigidly
	// (OBJECT_FLAGS_PRESKINNED).
	//
	// Same maths as the DefaultAnimated vertex shader, so the result matches the
	// vertex-shader path:
	//   skin   = sum(w_i * B_i)
	//   pos'   = mul(pos, skin)             normal' = mul(n, (float3x3)skin)
	// The palettes are uploaded byte-for-byte as the PerAnimationBuffer cbuffer gets
	// them, and the engine compiles with column-major packing (HLSL.cpp), so float4
	// row k of a CPU matrix is COLUMN k of the HLSL matrix: mul(v, B).k = dot(v, row k).
	//
	// Outputs are raw views over the vertex buffers, already seeded with the bind
	// pose; only the skinned fields are rewritten. Last frame's skinned position goes
	// in the BLENDINDICES slot (unused once skinned) for motion vectors.

	ByteAddressBuffer			g_skinSource		: register(t0);	// AnimatedMeshVertex[]
	StructuredBuffer<float4>	g_skinBones			: register(t1);	// [boneCount] current, then [boneCount] previous; 4 rows each
	RWByteAddressBuffer			g_skinOut			: register(u0);	// AnimatedMeshVertex[]
	RWByteAddressBuffer			g_skinOutSimple		: register(u1);	// SimpleAnimatedMeshVertex[]

	cbuffer GpuSkinConstants : register(b5)
	{
		uint4 g_skinLayout0;	// x stride, y position, z normal, w tangent (byte offsets)
		uint4 g_skinLayout1;	// x bitangent, y boneIds, z boneWeights, w vertex count
		uint4 g_skinLayout2;	// x simple stride, y simple position, z bone count, w unused
	};

	struct SkinRows
	{
		float4 r0, r1, r2, r3;
	};

	// Weighted sum of the four bones' rows. Indices outside the palette contribute
	// nothing (a cbuffer read past the array would have returned zero too).
	SkinRows BlendBones(float4 ids, float4 weights, uint paletteBase, uint boneCount)
	{
		SkinRows s;
		s.r0 = 0; s.r1 = 0; s.r2 = 0; s.r3 = 0;

		[unroll]
		for (uint i = 0; i < 4; ++i)
		{
			const int bone = (int)ids[i];
			if (bone < 0 || (uint)bone >= boneCount)
				continue;

			const uint row = (paletteBase + (uint)bone) * 4;
			const float w = weights[i];
			s.r0 += w * g_skinBones[row + 0];
			s.r1 += w * g_skinBones[row + 1];
			s.r2 += w * g_skinBones[row + 2];
			s.r3 += w * g_skinBones[row + 3];
		}
		return s;
	}

	float4 SkinPoint(float4 p, SkinRows s)
	{
		return float4(dot(p, s.r0), dot(p, s.r1), dot(p, s.r2), dot(p, s.r3));
	}

	float3 SkinVector(float3 v, SkinRows s)
	{
		return float3(dot(v, s.r0.xyz), dot(v, s.r1.xyz), dot(v, s.r2.xyz));
	}

	[numthreads(64, 1, 1)]
	void ShaderMain(uint3 tid : SV_DispatchThreadID)
	{
		const uint vertex = tid.x;
		if (vertex >= g_skinLayout1.w)
			return;

		const uint stride = g_skinLayout0.x;
		const uint base = vertex * stride;
		const uint boneCount = g_skinLayout2.z;

		float4 position = asfloat(g_skinSource.Load4(base + g_skinLayout0.y));
		position.w = 1.0f;
		const float3 normal = asfloat(g_skinSource.Load3(base + g_skinLayout0.z));
		const float3 tangent = asfloat(g_skinSource.Load3(base + g_skinLayout0.w));
		const float3 bitangent = asfloat(g_skinSource.Load3(base + g_skinLayout1.x));
		const float4 boneIds = asfloat(g_skinSource.Load4(base + g_skinLayout1.y));
		const float4 boneWeights = asfloat(g_skinSource.Load4(base + g_skinLayout1.z));

		const SkinRows skin = BlendBones(boneIds, boneWeights, 0, boneCount);
		const SkinRows skinPrev = BlendBones(boneIds, boneWeights, boneCount, boneCount);

		const float4 skinned = float4(SkinPoint(position, skin).xyz, 1.0f);
		const float4 skinnedPrev = float4(SkinPoint(position, skinPrev).xyz, 1.0f);

		g_skinOut.Store4(base + g_skinLayout0.y, asuint(skinned));
		g_skinOut.Store3(base + g_skinLayout0.z, asuint(SkinVector(normal, skin)));
		g_skinOut.Store3(base + g_skinLayout0.w, asuint(SkinVector(tangent, skin)));
		g_skinOut.Store3(base + g_skinLayout1.x, asuint(SkinVector(bitangent, skin)));
		g_skinOut.Store4(base + g_skinLayout1.y, asuint(skinnedPrev));

		g_skinOutSimple.Store4(vertex * g_skinLayout2.x + g_skinLayout2.y, asuint(skinned));
	}
}
