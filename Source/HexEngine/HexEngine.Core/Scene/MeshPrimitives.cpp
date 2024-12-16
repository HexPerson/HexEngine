

#include "MeshPrimitives.hpp"
#include "../HexEngine.hpp"

namespace HexEngine
{
	void MeshPrimitives::Destroy()
	{
		SAFE_UNLOAD(_sphere);
	}

	//Model* MeshPrimitives::GetSphere()
	//{
	//	if (!_sphere)
	//	{
	//		_sphere = CreateSphere();
	//	}

	//	return _sphere;// ->Clone();
	//}

	//Model* MeshPrimitives::CreateSphere()
	//{
	//	Model* model = (Model*)g_pEnv->_resourceSystem->LoadResource("Models/Primitives/sphere.obj");

	//	if (!model)
	//	{
	//		LOG_CRIT("Failed to create primitive sphere!");
	//		return nullptr;
	//	}

	//	return model;
	//}

	void MeshPrimitives::GenerateBox(const dx::BoundingBox& box, std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices)
	{
		MeshVertex v[24];

		// Create the 8 points of the box

		// Fill in the front face vertex data.
		v[0] = MeshVertex::Create(math::Vector3(-box.Extents.x, -box.Extents.y, -box.Extents.z), math::Vector3(0.0f, 0.0f, -1.0f), math::Vector3(1.0f, 0.0f, 0.0f), 0.0f, 1.0f);
		v[1] = MeshVertex::Create(math::Vector3(-box.Extents.x, +box.Extents.y, -box.Extents.z), math::Vector3(0.0f, 0.0f, -1.0f), math::Vector3(1.0f, 0.0f, 0.0f), 0.0f, 0.0f);
		v[2] = MeshVertex::Create(math::Vector3(+box.Extents.x, +box.Extents.y, -box.Extents.z), math::Vector3(0.0f, 0.0f, -1.0f), math::Vector3(1.0f, 0.0f, 0.0f), 1.0f, 0.0f);
		v[3] = MeshVertex::Create(math::Vector3(+box.Extents.x, -box.Extents.y, -box.Extents.z), math::Vector3(0.0f, 0.0f, -1.0f), math::Vector3(1.0f, 0.0f, 0.0f), 1.0f, 1.0f);

		// Fill in the back face vertex data.
		v[4] = MeshVertex::Create(math::Vector3(-box.Extents.x, -box.Extents.y, +box.Extents.z), math::Vector3(0.0f, 0.0f, 1.0f), math::Vector3(-1.0f, 0.0f, 0.0f), 1.0f, 1.0f);
		v[5] = MeshVertex::Create(math::Vector3(+box.Extents.x, -box.Extents.y, +box.Extents.z), math::Vector3(0.0f, 0.0f, 1.0f), math::Vector3(-1.0f, 0.0f, 0.0f), 0.0f, 1.0f);
		v[6] = MeshVertex::Create(math::Vector3(+box.Extents.x, +box.Extents.y, +box.Extents.z), math::Vector3(0.0f, 0.0f, 1.0f), math::Vector3(-1.0f, 0.0f, 0.0f), 0.0f, 0.0f);
		v[7] = MeshVertex::Create(math::Vector3(-box.Extents.x, +box.Extents.y, +box.Extents.z), math::Vector3(0.0f, 0.0f, 1.0f), math::Vector3(-1.0f, 0.0f, 0.0f), 1.0f, 0.0f);

		// Fill in the top face vertex data.
		v[8] = MeshVertex::Create(math::Vector3(-box.Extents.x, +box.Extents.y, -box.Extents.z), math::Vector3(0.0f, 1.0f, 0.0f), math::Vector3(1.0f, 0.0f, 0.0f), 0.0f, 1.0f);
		v[9] = MeshVertex::Create(math::Vector3(-box.Extents.x, +box.Extents.y, +box.Extents.z), math::Vector3(0.0f, 1.0f, 0.0f), math::Vector3(1.0f, 0.0f, 0.0f), 0.0f, 0.0f);
		v[10] = MeshVertex::Create(math::Vector3(+box.Extents.x, +box.Extents.y, +box.Extents.z), math::Vector3(0.0f, 1.0f, 0.0f), math::Vector3(1.0f, 0.0f, 0.0f), 1.0f, 0.0f);
		v[11] = MeshVertex::Create(math::Vector3(+box.Extents.x, +box.Extents.y, -box.Extents.z), math::Vector3(0.0f, 1.0f, 0.0f), math::Vector3(1.0f, 0.0f, 0.0f), 1.0f, 1.0f);

		// Fill in the bottom face vertex data.
		v[12] = MeshVertex::Create(math::Vector3(-box.Extents.x, -box.Extents.y, -box.Extents.z), math::Vector3(0.0f, -1.0f, 0.0f), math::Vector3(-1.0f, 0.0f, 0.0f), 1.0f, 1.0f);
		v[13] = MeshVertex::Create(math::Vector3(+box.Extents.x, -box.Extents.y, -box.Extents.z), math::Vector3(0.0f, -1.0f, 0.0f), math::Vector3(-1.0f, 0.0f, 0.0f), 0.0f, 1.0f);
		v[14] = MeshVertex::Create(math::Vector3(+box.Extents.x, -box.Extents.y, +box.Extents.z), math::Vector3(0.0f, -1.0f, 0.0f), math::Vector3(-1.0f, 0.0f, 0.0f), 0.0f, 0.0f);
		v[15] = MeshVertex::Create(math::Vector3(-box.Extents.x, -box.Extents.y, +box.Extents.z), math::Vector3(0.0f, -1.0f, 0.0f), math::Vector3(-1.0f, 0.0f, 0.0f), 1.0f, 0.0f);

		// Fill in the left face vertex data.
		v[16] = MeshVertex::Create(math::Vector3(-box.Extents.x, -box.Extents.y, +box.Extents.z), math::Vector3(-1.0f, 0.0f, 0.0f), math::Vector3(0.0f, 0.0f, -1.0f), 0.0f, 1.0f);
		v[17] = MeshVertex::Create(math::Vector3(-box.Extents.x, +box.Extents.y, +box.Extents.z), math::Vector3(-1.0f, 0.0f, 0.0f), math::Vector3(0.0f, 0.0f, -1.0f), 0.0f, 0.0f);
		v[18] = MeshVertex::Create(math::Vector3(-box.Extents.x, +box.Extents.y, -box.Extents.z), math::Vector3(-1.0f, 0.0f, 0.0f), math::Vector3(0.0f, 0.0f, -1.0f), 1.0f, 0.0f);
		v[19] = MeshVertex::Create(math::Vector3(-box.Extents.x, -box.Extents.y, -box.Extents.z), math::Vector3(-1.0f, 0.0f, 0.0f), math::Vector3(0.0f, 0.0f, -1.0f), 1.0f, 1.0f);

		// Fill in the right face vertex data.
		v[20] = MeshVertex::Create(math::Vector3(+box.Extents.x, -box.Extents.y, -box.Extents.z), math::Vector3(1.0f, 0.0f, 0.0f), math::Vector3(0.0f, 0.0f, 1.0f), 0.0f, 1.0f);
		v[21] = MeshVertex::Create(math::Vector3(+box.Extents.x, +box.Extents.y, -box.Extents.z), math::Vector3(1.0f, 0.0f, 0.0f), math::Vector3(0.0f, 0.0f, 1.0f), 0.0f, 0.0f);
		v[22] = MeshVertex::Create(math::Vector3(+box.Extents.x, +box.Extents.y, +box.Extents.z), math::Vector3(1.0f, 0.0f, 0.0f), math::Vector3(0.0f, 0.0f, 1.0f), 1.0f, 0.0f);
		v[23] = MeshVertex::Create(math::Vector3(+box.Extents.x, -box.Extents.y, +box.Extents.z), math::Vector3(1.0f, 0.0f, 0.0f), math::Vector3(0.0f, 0.0f, 1.0f), 1.0f, 1.0f);

		vertices.assign(&v[0], &v[24]);

		//
		// Create the indices.
		//

		uint32_t i[36];

		// Fill in the front face index data
		i[0] = 0; i[1] = 1; i[2] = 2;
		i[3] = 0; i[4] = 2; i[5] = 3;

		// Fill in the back face index data
		i[6] = 4; i[7] = 5; i[8] = 6;
		i[9] = 4; i[10] = 6; i[11] = 7;

		// Fill in the top face index data
		i[12] = 8; i[13] = 9; i[14] = 10;
		i[15] = 8; i[16] = 10; i[17] = 11;

		// Fill in the bottom face index data
		i[18] = 12; i[19] = 13; i[20] = 14;
		i[21] = 12; i[22] = 14; i[23] = 15;

		// Fill in the left face index data
		i[24] = 16; i[25] = 17; i[26] = 18;
		i[27] = 16; i[28] = 18; i[29] = 19;

		// Fill in the right face index data
		i[30] = 20; i[31] = 21; i[32] = 22;
		i[33] = 20; i[34] = 22; i[35] = 23;

		indices.assign(&i[0], &i[36]);
	}
}