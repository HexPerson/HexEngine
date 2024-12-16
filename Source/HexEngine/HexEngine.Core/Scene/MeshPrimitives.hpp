

#pragma once

#include "../Required.hpp"
#include "Mesh.hpp"
#include "Model.hpp"

namespace HexEngine
{
	class MeshPrimitives
	{
	public:
		void Destroy();

		//Model* GetSphere();

		void GenerateBox(const dx::BoundingBox& box, std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices);

	private:
		//Model* CreateSphere();

	private:
		Model* _sphere = nullptr;
	};
}
