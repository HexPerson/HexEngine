

#include "TerrainGenerator.hpp"
#include "../Scene/Mesh.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../FileSystem/FileSystem.hpp"
#include "../Math/FloatMath.hpp"

namespace HexEngine
{
	//void CalculateTangentBinormal(const math::Vector4& vertex1, const math::Vector4& vertex2, const math::Vector4& vertex3,
	//	math::Vector3& tangent, math::Vector3& binormal)
	//{
	//	float vector1[3], vector2[3];
	//	float tuVector[2], tvVector[2];
	//	float den;
	//	float length;


	//	// Calculate the two vectors for this face.
	//	vector1[0] = vertex2.x - vertex1.x;
	//	vector1[1] = vertex2.y - vertex1.y;
	//	vector1[2] = vertex2.z - vertex1.z;

	//	vector2[0] = vertex3.x - vertex1.x;
	//	vector2[1] = vertex3.y - vertex1.y;
	//	vector2[2] = vertex3.z - vertex1.z;

	//	// Calculate the tu and tv texture space vectors.
	//	//tuVector[0] = vertex2.tu - vertex1.tu;
	//	//tvVector[0] = vertex2.tv - vertex1.tv;

	//	//tuVector[1] = vertex3.tu - vertex1.tu;
	//	//tvVector[1] = vertex3.tv - vertex1.tv;

	//	// Calculate the denominator of the tangent/binormal equation.
	//	den = 1.0f;// / (tuVector[0] * tvVector[1] - tuVector[1] * tvVector[0]);

	//	// Calculate the cross products and multiply by the coefficient to get the tangent and binormal.
	//	tangent.x = (tvVector[1] * vector1[0] - tvVector[0] * vector2[0]) * den;
	//	tangent.y = (tvVector[1] * vector1[1] - tvVector[0] * vector2[1]) * den;
	//	tangent.z = (tvVector[1] * vector1[2] - tvVector[0] * vector2[2]) * den;

	//	binormal.x = (tuVector[0] * vector2[0] - tuVector[1] * vector1[0]) * den;
	//	binormal.y = (tuVector[0] * vector2[1] - tuVector[1] * vector1[1]) * den;
	//	binormal.z = (tuVector[0] * vector2[2] - tuVector[1] * vector1[2]) * den;

	//	// Calculate the length of this normal.
	//	length = sqrt((tangent.x * tangent.x) + (tangent.y * tangent.y) + (tangent.z * tangent.z));

	//	// Normalize the normal and then store it
	//	tangent.x = tangent.x / length;
	//	tangent.y = tangent.y / length;
	//	tangent.z = tangent.z / length;

	//	// Calculate the length of this normal.
	//	length = sqrt((binormal.x * binormal.x) + (binormal.y * binormal.y) + (binormal.z * binormal.z));

	//	// Normalize the normal and then store it
	//	binormal.x = binormal.x / length;
	//	binormal.y = binormal.y / length;
	//	binormal.z = binormal.z / length;

	//	return;
	//}

	std::shared_ptr<Mesh> HEX_API CreateTerrain(const TerrainGenerationParams& params)
	{
		uint32_t resolution = params.resolution + 1;

		Mesh* meshp = nullptr;

		if (!params.createInstance)
			meshp = new Mesh(nullptr, params.ident + std::to_string(params.position.x) + std::to_string(params.position.y) + std::to_string(params.position.z));
		else
			meshp = new Mesh(nullptr, params.ident);

		std::shared_ptr<Mesh> mesh = std::shared_ptr<Mesh>(meshp);

		std::vector<MeshVertex> vertices;
		std::vector<MeshIndexFormat> indices;

		uint32_t vertexCount = resolution * resolution;
		uint32_t faceCount = (resolution - 1) * (resolution - 1) * 2;

		vertices.resize(vertexCount);

		//const float width = 1.0f;
		const float halfWidth = params.width * 0.5f;

		float dx = params.width / ((float)resolution - 1);
		float dz = params.width / ((float)resolution - 1);

		float du = params.uvScale / ((float)resolution - 1);
		float dv = params.uvScale / ((float)resolution - 1);

		bool useHeightMap = params.heightMap.size() > 0;

		for (uint32_t i = 0; i < resolution; ++i)
		{
			float z = halfWidth - i * dz;

			for (uint32_t j = 0; j < resolution; ++j)
			{
				float x = -halfWidth + j * dx;

				float y = useHeightMap ? params.heightMap.at(i * resolution + j) : 0.0f;// ? useHeight ? GenerateTerrainHeight(math::Vector3(x + position.x, 0.0f, z + position.z), noiseScale) : position.y;

				// can be useful for creating a low-poly aesthetic
				if (params.modulo != 0 && useHeightMap)
				{
					y = RoundToNearest(y, params.modulo);
				}

				vertices[i * resolution + j]._position = DirectX::XMFLOAT4(x, y, z, 1.0f);
				vertices[i * resolution + j]._texcoord = DirectX::XMFLOAT2((float)i * du, (float)j * dv);
			}
		}

		indices.resize(faceCount * 3);

		uint32_t k = 0;

		for (uint32_t i = 0; i < resolution - 1; ++i)
		{
			for (uint32_t j = 0; j < resolution - 1; ++j)
			{
				indices[k + 0] = i * resolution + j;
				indices[k + 1] = i * resolution + j + 1;
				indices[k + 2] = (i + 1) * resolution + j;
				indices[k + 3] = (i + 1) * resolution + j;
				indices[k + 4] = i * resolution + j + 1;
				indices[k + 5] = (i + 1) * resolution + j + 1;

				k += 6;
			}
		}

		// Calculate the normals
		//
		for (uint32_t i = 0; i < faceCount; ++i)
		{
			uint32_t i0 = indices[i * 3 + 0];
			uint32_t i1 = indices[i * 3 + 1];
			uint32_t i2 = indices[i * 3 + 2];

			math::Vector4 v0 = vertices[i0]._position;// +position;
			math::Vector4 v1 = vertices[i1]._position;// +position;
			math::Vector4 v2 = vertices[i2]._position;// +position;			

			math::Vector2 uv0 = vertices[i0]._texcoord;
			math::Vector2 uv1 = vertices[i1]._texcoord;
			math::Vector2 uv2 = vertices[i2]._texcoord;

			math::Vector3 deltaPos1 = (math::Vector3)v1 - (math::Vector3)v0;
			math::Vector3 deltaPos2 = (math::Vector3)v2 - (math::Vector3)v0;
			//math::Vector3 d2 = (math::Vector3)v2 - (math::Vector3)v1;

			math::Vector2 deltaUV1 = uv1 - uv0;
			math::Vector2 deltaUV2 = uv2 - uv0;
			float r = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x);

			

			math::Vector3 tangent = (deltaPos1 * deltaUV2.y - deltaPos2 * deltaUV1.y) * r;
			math::Vector3 bitangent = (deltaPos2 * deltaUV1.x - deltaPos1 * deltaUV2.x) * r;

			//math::Vector3 bitangent = normal.Cross(d2);
			bitangent.Normalize();

			//math::Vector3 tangent = normal.Cross(bitangent);
			tangent.Normalize();

			math::Vector3 normal = bitangent.Cross(tangent);
			normal.Normalize();

			vertices[i0]._normal += normal;
			vertices[i1]._normal += normal;
			vertices[i2]._normal += normal;

			//math::Vector3 bitangent, tangent;
			//CalculateTangentBinormal(v0, v1, v2, tangent, bitangent);

			vertices[i0]._tangent += tangent;
			vertices[i1]._tangent += tangent;
			vertices[i2]._tangent += tangent;

			vertices[i0]._bitangent += bitangent;
			vertices[i1]._bitangent += bitangent;
			vertices[i2]._bitangent += bitangent;
		}

		// finally normalize the vertex
		for (auto& vertex : vertices)
		{
			vertex._normal.Normalize();
			vertex._tangent.Normalize();
			vertex._bitangent.Normalize();
		}

		mesh->AddVertices(vertices);
		mesh->AddIndices(indices);
		mesh->SetNumFaces(faceCount);

		mesh->CreateBuffers();

		dx::BoundingBox aabb;
		dx::BoundingBox::CreateFromPoints(aabb, vertices.size(), (const math::Vector3*)vertices.data(), sizeof(MeshVertex));

		dx::BoundingOrientedBox obb;
		dx::BoundingOrientedBox::CreateFromBoundingBox(obb , mesh->GetAABB());

		mesh->SetAABB(aabb);
		mesh->SetOBB(obb);

		// set the paths for saving/loading
		fs::path terrainPath = g_pEnv->_fileSystem->GetLocalAbsoluteDataPath("TERRAIN");

		mesh->SetPaths(terrainPath, g_pEnv->_fileSystem);
		//mesh->_terrainParams = params;

		return mesh;
	}

	//void FinalizeTerrainSet(const std::vector<Mesh*>& terrain, int32_t resolution)
	//{
	//	for (auto& mesh : terrain)
	//	{
	//		auto& vertices = mesh->GetVertices();

	//		
	//		mesh->CreateBuffers(false);
	//		

	//		dx::BoundingBox::CreateFromPoints(mesh->GetAABB(), vertices.size(), (const math::Vector3*)vertices.data(), sizeof(MeshVertex));
	//		dx::BoundingOrientedBox::CreateFromBoundingBox(mesh->GetOBB(), mesh->GetAABB());

	//		// set the paths for saving/loading
	//		fs::path terrainPath = g_pEnv->_fileSystem->GetLocalAbsoluteDataPath("TERRAIN");

	//		mesh->SetPaths(terrainPath, g_pEnv->_fileSystem);
	//	}
	//}
}