#include "MarchingCubes.hpp"

#include <unordered_map>

namespace HexEngine::VolumetricTerrain
{
	namespace
	{
		struct WeldKey
		{
			int32_t x = 0;
			int32_t y = 0;
			int32_t z = 0;

			bool operator==(const WeldKey& other) const
			{
				return x == other.x && y == other.y && z == other.z;
			}
		};

		struct WeldKeyHash
		{
			size_t operator()(const WeldKey& key) const
			{
				const uint64_t h1 = static_cast<uint32_t>(key.x) * 73856093ull;
				const uint64_t h2 = static_cast<uint32_t>(key.y) * 19349663ull;
				const uint64_t h3 = static_cast<uint32_t>(key.z) * 83492791ull;
				return static_cast<size_t>(h1 ^ h2 ^ h3);
			}
		};

		struct Tet
		{
			int32_t v[4];
		};

		constexpr int32_t kTetEdges[6][2] =
		{
			{0, 1},
			{1, 2},
			{2, 0},
			{0, 3},
			{1, 3},
			{2, 3},
		};

		// Tetrahedron triangulation table (density < 0 is inside/solid).
		// Each row is a sequence of edge triplets; -1 terminates the row.
		constexpr int32_t kTetTriTable[16][7] =
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
			{-1, -1, -1, -1, -1, -1, -1},
		};

		constexpr Tet kTets[6] =
		{
			{{0, 5, 1, 6}},
			{{0, 1, 2, 6}},
			{{0, 2, 3, 6}},
			{{0, 3, 7, 6}},
			{{0, 7, 4, 6}},
			{{0, 4, 5, 6}},
		};

		WeldKey MakeWeldKey(const math::Vector3& p)
		{
			constexpr float kQuantize = 1024.0f;
			return WeldKey
			{
				static_cast<int32_t>(std::lround(p.x * kQuantize)),
				static_cast<int32_t>(std::lround(p.y * kQuantize)),
				static_cast<int32_t>(std::lround(p.z * kQuantize)),
			};
		}

		void WeldAndSmoothOutput(MarchingCubesOutput& output)
		{
			if (output.vertices.empty() || output.indices.empty())
			{
				return;
			}

			std::vector<MeshVertex> weldedVertices;
			std::vector<MeshIndexFormat> weldedIndices;
			std::unordered_map<WeldKey, MeshIndexFormat, WeldKeyHash> weldMap;
			weldedVertices.reserve(output.vertices.size());
			weldedIndices.reserve(output.indices.size());
			weldMap.reserve(output.vertices.size());

			for (MeshIndexFormat oldIndex : output.indices)
			{
				const MeshVertex& src = output.vertices[static_cast<size_t>(oldIndex)];
				const math::Vector3 pos(src._position.x, src._position.y, src._position.z);
				const WeldKey key = MakeWeldKey(pos);

				auto it = weldMap.find(key);
				if (it == weldMap.end())
				{
					MeshVertex dst = src;
					dst._normal = math::Vector3::Zero;
					dst._tangent = math::Vector3::Zero;
					dst._bitangent = math::Vector3::Zero;

					const MeshIndexFormat newIndex = static_cast<MeshIndexFormat>(weldedVertices.size());
					weldedVertices.push_back(dst);
					weldedIndices.push_back(newIndex);
					weldMap.emplace(key, newIndex);
				}
				else
				{
					weldedIndices.push_back(it->second);
				}
			}

			for (size_t tri = 0; tri + 2 < weldedIndices.size(); tri += 3)
			{
				MeshVertex& v0 = weldedVertices[static_cast<size_t>(weldedIndices[tri + 0])];
				MeshVertex& v1 = weldedVertices[static_cast<size_t>(weldedIndices[tri + 1])];
				MeshVertex& v2 = weldedVertices[static_cast<size_t>(weldedIndices[tri + 2])];

				const math::Vector3 p0(v0._position.x, v0._position.y, v0._position.z);
				const math::Vector3 p1(v1._position.x, v1._position.y, v1._position.z);
				const math::Vector3 p2(v2._position.x, v2._position.y, v2._position.z);
				math::Vector3 normal = (p1 - p0).Cross(p2 - p0);
				if (normal.LengthSquared() > 0.0f)
				{
					normal.Normalize();
				}
				else
				{
					normal = math::Vector3::Up;
				}

				v0._normal += normal;
				v1._normal += normal;
				v2._normal += normal;
			}

			for (MeshVertex& vertex : weldedVertices)
			{
				if (vertex._normal.LengthSquared() > 0.0f)
				{
					vertex._normal.Normalize();
				}
				else
				{
					vertex._normal = math::Vector3::Up;
				}

				math::Vector3 tangent = math::Vector3::Up.Cross(vertex._normal);
				if (tangent.LengthSquared() <= 0.0f)
				{
					tangent = math::Vector3::Right;
				}
				tangent.Normalize();
				math::Vector3 bitangent = vertex._normal.Cross(tangent);
				if (bitangent.LengthSquared() > 0.0f)
				{
					bitangent.Normalize();
				}
				else
				{
					bitangent = math::Vector3::Forward;
				}

				vertex._tangent = tangent;
				vertex._bitangent = bitangent;
			}

			output.vertices = std::move(weldedVertices);
			output.indices = std::move(weldedIndices);
		}
	}

	int32_t MarchingCubes::Index(int32_t x, int32_t y, int32_t z, int32_t pointsPerAxis) const
	{
		return (z * pointsPerAxis * pointsPerAxis) + (y * pointsPerAxis) + x;
	}

	math::Vector3 MarchingCubes::Interpolate(const math::Vector3& a, const math::Vector3& b, float da, float db) const
	{
		const float denom = da - db;
		if (fabsf(denom) <= 0.00001f)
		{
			return (a + b) * 0.5f;
		}

		const float t = std::clamp(da / denom, 0.0f, 1.0f);
		return a + (b - a) * t;
	}

	void MarchingCubes::EmitTriangle(MarchingCubesOutput& output, const math::Vector3& a, const math::Vector3& b, const math::Vector3& c, float uvScale) const
	{
		// Flip winding so terrain front faces point outward/upward for engine culling.
		const math::Vector3 p0 = a;
		const math::Vector3 p1 = c;
		const math::Vector3 p2 = b;

		const MeshIndexFormat base = static_cast<MeshIndexFormat>(output.vertices.size());

		math::Vector3 ab = p1 - p0;
		math::Vector3 ac = p2 - p0;
		math::Vector3 normal = ab.Cross(ac);
		if (normal.LengthSquared() > 0.0f)
		{
			normal.Normalize();
		}
		else
		{
			normal = math::Vector3::Up;
		}

		math::Vector3 tangent = math::Vector3::Up.Cross(normal);
		if (tangent.LengthSquared() <= 0.0f)
		{
			tangent = math::Vector3::Right;
		}
		tangent.Normalize();
		math::Vector3 bitangent = normal.Cross(tangent);
		bitangent.Normalize();

		auto append = [&](const math::Vector3& p)
		{
			MeshVertex v;
			v._position = math::Vector4(p.x, p.y, p.z, 1.0f);
			v._normal = normal;
			v._tangent = tangent;
			v._bitangent = bitangent;
			v._texcoord = math::Vector2(p.x * uvScale, p.z * uvScale);
			output.vertices.push_back(v);
		};

		append(p0);
		append(p1);
		append(p2);

		output.indices.push_back(base + 0);
		output.indices.push_back(base + 1);
		output.indices.push_back(base + 2);
	}

	MarchingCubesOutput MarchingCubes::Build(
		const std::vector<float>& densities,
		int32_t resolution,
		float voxelSize,
		const math::Vector3& chunkOrigin,
		float uvScale) const
	{
		MarchingCubesOutput output;
		if (resolution <= 0)
		{
			return output;
		}

		const int32_t pointsPerAxis = resolution + 1;
		auto getDensity = [&](int32_t x, int32_t y, int32_t z) -> float
		{
			return densities[Index(x, y, z, pointsPerAxis)];
		};

		for (int32_t z = 0; z < resolution; ++z)
		{
			for (int32_t y = 0; y < resolution; ++y)
			{
				for (int32_t x = 0; x < resolution; ++x)
				{
					math::Vector3 p[8];
					float d[8];

					// Corner order must match tetra table expectations:
					// 0:(0,0,0) 1:(1,0,0) 2:(1,1,0) 3:(0,1,0)
					// 4:(0,0,1) 5:(1,0,1) 6:(1,1,1) 7:(0,1,1)
					static constexpr int32_t kCornerX[8] = { 0, 1, 1, 0, 0, 1, 1, 0 };
					static constexpr int32_t kCornerY[8] = { 0, 0, 1, 1, 0, 0, 1, 1 };
					static constexpr int32_t kCornerZ[8] = { 0, 0, 0, 0, 1, 1, 1, 1 };

					for (int32_t i = 0; i < 8; ++i)
					{
						const int32_t ox = kCornerX[i];
						const int32_t oy = kCornerY[i];
						const int32_t oz = kCornerZ[i];

						const math::Vector3 localPos(
							static_cast<float>(x + ox) * voxelSize,
							static_cast<float>(y + oy) * voxelSize,
							static_cast<float>(z + oz) * voxelSize);

						// Mesh vertices are in chunk-local space; entity transform places them in world.
						p[i] = localPos;
						d[i] = getDensity(x + ox, y + oy, z + oz);
					}

					for (int32_t t = 0; t < 6; ++t)
					{
						const Tet& tet = kTets[t];
						int32_t caseMask = 0;
						for (int32_t i = 0; i < 4; ++i)
						{
							const int32_t idx = tet.v[i];
							if (d[idx] <= 0.0f)
							{
								caseMask |= (1 << i);
							}
						}

						if (caseMask == 0 || caseMask == 15)
						{
							continue;
						}

						math::Vector3 edgeVertices[6];
						for (int32_t edgeIndex = 0; edgeIndex < 6; ++edgeIndex)
						{
							const int32_t aLocal = kTetEdges[edgeIndex][0];
							const int32_t bLocal = kTetEdges[edgeIndex][1];
							const int32_t a = tet.v[aLocal];
							const int32_t b = tet.v[bLocal];
							edgeVertices[edgeIndex] = Interpolate(p[a], p[b], d[a], d[b]);
						}

						const int32_t* row = kTetTriTable[caseMask];
						for (int32_t tri = 0; tri < 7; tri += 3)
						{
							const int32_t e0 = row[tri + 0];
							if (e0 == -1)
							{
								break;
							}

							const int32_t e1 = row[tri + 1];
							const int32_t e2 = row[tri + 2];
							EmitTriangle(output, edgeVertices[e0], edgeVertices[e1], edgeVertices[e2], uvScale);
						}
					}
				}
			}
		}

		WeldAndSmoothOutput(output);

		if (!output.vertices.empty())
		{
			dx::BoundingBox::CreateFromPoints(output.aabb, output.vertices.size(), reinterpret_cast<const math::Vector3*>(output.vertices.data()), sizeof(MeshVertex));
			dx::BoundingOrientedBox::CreateFromBoundingBox(output.obb, output.aabb);
		}

		return output;
	}
}
