"ComputeShaderIncludes"
{
	Global
}
"ComputeShader"
{
	struct TerrainTriangleGpu
	{
		float4 p0;
		float4 p1;
		float4 p2;
		float4 n0;
		float4 n1;
		float4 n2;
	};

	AppendStructuredBuffer<TerrainTriangleGpu> g_surfaceTrianglesOut : register(u0);
	Texture3D<float> g_densityField : register(t0);

	cbuffer VolumetricTerrainChunkBuffer : register(b6)
	{
		float4 g_chunkOriginVoxel;
		float4 g_chunkWorldInfo;
		float4 g_chunkGrassColor;
		float4 g_chunkRockColor;
		float4 g_chunkSnowColor;
	};

	static const int2 kTetEdges[6] =
	{
		int2(0, 1), int2(1, 2), int2(2, 0),
		int2(0, 3), int2(1, 3), int2(2, 3)
	};

	static const int kTetTriTable[16][7] =
	{
		{-1, -1, -1, -1, -1, -1, -1},
		{0, 3, 2, -1, -1, -1, -1},
		{0, 1, 4, -1, -1, -1, -1},
		{1, 4, 2, 2, 4, 3, -1},
		{1, 2, 5, -1, -1, -1, -1},
		{0, 3, 5, 0, 5, 1, -1},
		{0, 2, 5, 0, 5, 4, -1},
		{5, 4, 3, -1, -1, -1, -1},
		{3, 4, 5, -1, -1, -1, -1},
		{4, 5, 0, 5, 2, 0, -1},
		{1, 5, 0, 5, 3, 0, -1},
		{5, 2, 1, -1, -1, -1, -1},
		{3, 4, 2, 2, 4, 1, -1},
		{4, 1, 0, -1, -1, -1, -1},
		{2, 3, 0, -1, -1, -1, -1},
		{-1, -1, -1, -1, -1, -1, -1}
	};

	static const int4 kTets[6] =
	{
		int4(0, 5, 1, 6),
		int4(0, 1, 2, 6),
		int4(0, 2, 3, 6),
		int4(0, 3, 7, 6),
		int4(0, 7, 4, 6),
		int4(0, 4, 5, 6)
	};

	static const int kCornerX[8] = { 0, 1, 1, 0, 0, 1, 1, 0 };
	static const int kCornerY[8] = { 0, 0, 1, 1, 0, 0, 1, 1 };
	static const int kCornerZ[8] = { 0, 0, 0, 0, 1, 1, 1, 1 };

	float DensityAt(int3 p)
	{
		return g_densityField.Load(int4(p, 0));
	}

	float3 CellCornerWorld(int3 cell, int corner, float voxelSize, float3 chunkOrigin)
	{
		int3 o = int3(kCornerX[corner], kCornerY[corner], kCornerZ[corner]);
		return chunkOrigin + float3(cell + o) * voxelSize;
	}

	float3 InterpolateIso(float3 a, float3 b, float da, float db)
	{
		float denom = da - db;
		if (abs(denom) <= 0.00001f)
		{
			return (a + b) * 0.5f;
		}

		float t = saturate(da / denom);
		return lerp(a, b, t);
	}

	float3 SampleDensityNormal(float3 worldPos, float voxelSize, float3 chunkOrigin, int pointsPerAxis)
	{
		float3 local = (worldPos - chunkOrigin) / max(voxelSize, 0.0001f);
		int3 p = int3(clamp(local, 1.0f, float(pointsPerAxis - 2)));
		float dx = DensityAt(p + int3(1, 0, 0)) - DensityAt(p - int3(1, 0, 0));
		float dy = DensityAt(p + int3(0, 1, 0)) - DensityAt(p - int3(0, 1, 0));
		float dz = DensityAt(p + int3(0, 0, 1)) - DensityAt(p - int3(0, 0, 1));
		float3 n = normalize(float3(dx, dy, dz));
		return all(isfinite(n)) ? n : float3(0.0f, 1.0f, 0.0f);
	}

	void EmitTriangle(float3 a, float3 b, float3 c, float voxelSize, float3 chunkOrigin, int pointsPerAxis)
	{
		TerrainTriangleGpu tri;
		tri.p0 = float4(a, 1.0f);
		tri.p1 = float4(c, 1.0f);
		tri.p2 = float4(b, 1.0f);
		float3 n0 = SampleDensityNormal(tri.p0.xyz, voxelSize, chunkOrigin, pointsPerAxis);
		float3 n1 = SampleDensityNormal(tri.p1.xyz, voxelSize, chunkOrigin, pointsPerAxis);
		float3 n2 = SampleDensityNormal(tri.p2.xyz, voxelSize, chunkOrigin, pointsPerAxis);
		float3 flatNormal = normalize(cross(tri.p1.xyz - tri.p0.xyz, tri.p2.xyz - tri.p0.xyz));
		if (dot(flatNormal, (n0 + n1 + n2) / 3.0f) < 0.0f)
		{
			n0 = -n0;
			n1 = -n1;
			n2 = -n2;
		}

		tri.n0 = float4(n0, 0.0f);
		tri.n1 = float4(n1, 0.0f);
		tri.n2 = float4(n2, 0.0f);
		g_surfaceTrianglesOut.Append(tri);
	}

	[numthreads(4, 4, 4)]
	void ShaderMain(uint3 id : SV_DispatchThreadID)
	{
		const int resolution = (int)g_chunkWorldInfo.x;
		if (id.x >= (uint)resolution || id.y >= (uint)resolution || id.z >= (uint)resolution)
		{
			return;
		}

		const int pointsPerAxis = resolution + 1;
		const float voxelSize = g_chunkOriginVoxel.w;
		const float3 chunkOrigin = g_chunkOriginVoxel.xyz;
		const int3 cell = int3(id);

		float3 corners[8];
		float densities[8];
		[unroll] for (int i = 0; i < 8; ++i)
		{
			corners[i] = CellCornerWorld(cell, i, voxelSize, chunkOrigin);
			int3 o = int3(kCornerX[i], kCornerY[i], kCornerZ[i]);
			densities[i] = DensityAt(cell + o);
		}

		[unroll] for (int t = 0; t < 6; ++t)
		{
			int4 tet = kTets[t];
			float td[4] = { densities[tet.x], densities[tet.y], densities[tet.z], densities[tet.w] };
			float3 tp[4] = { corners[tet.x], corners[tet.y], corners[tet.z], corners[tet.w] };
			int mask = 0;
			if (td[0] <= 0.0f) mask |= 1;
			if (td[1] <= 0.0f) mask |= 2;
			if (td[2] <= 0.0f) mask |= 4;
			if (td[3] <= 0.0f) mask |= 8;
			if (mask == 0 || mask == 15)
			{
				continue;
			}

			float3 edgeVerts[6];
			[unroll] for (int e = 0; e < 6; ++e)
			{
				int2 edge = kTetEdges[e];
				edgeVerts[e] = InterpolateIso(tp[edge.x], tp[edge.y], td[edge.x], td[edge.y]);
			}

			[unroll] for (int i = 0; i < 7; i += 3)
			{
				int e0 = kTetTriTable[mask][i];
				if (e0 < 0) break;
				int e1 = kTetTriTable[mask][i + 1];
				int e2 = kTetTriTable[mask][i + 2];
				EmitTriangle(edgeVerts[e0], edgeVerts[e1], edgeVerts[e2], voxelSize, chunkOrigin, pointsPerAxis);
			}
		}
	}
}
