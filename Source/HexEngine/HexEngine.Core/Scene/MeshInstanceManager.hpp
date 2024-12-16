
#pragma once

#include "Mesh.hpp"
#include "MeshInstance.hpp"

namespace HexEngine
{
	struct MeshInstanceStorage
	{
		bool IsMeshStored(Mesh* mesh)
		{
			for (auto&& m : originalMeshes)
			{
				if (m == mesh)
					return true;
			}
			return false;
		}

		Mesh* FindMeshThatIsNotMesh(Mesh* mesh)
		{
			for (auto&& m : originalMeshes)
			{
				if (m != mesh)
					return m;
			}

			return nullptr;
		}

		void RemoveMesh(Mesh* mesh)
		{
			/*originalMeshes.erase(std::remove_if(originalMeshes.begin(), originalMeshes.end(), 
				[](const Mesh* left, const Mesh* right) {
					return (left == right);
				}), originalMeshes.end()
			);*/

			originalMeshes.erase(std::remove(originalMeshes.begin(), originalMeshes.end(), mesh), originalMeshes.end());
		}

		

		MeshInstance* instance;		
		std::vector<Mesh*> originalMeshes;
	};

	class MeshInstanceManager
	{
	public:
		MeshInstance* CreateInstance(Mesh* mesh);
		bool DestroyInstance(MeshInstance* instance, Mesh* oldMesh);

		void NotifyMeshRendererRemoval(MeshRenderer* renderer, Mesh* mesh);

	private:
		std::map<std::string, MeshInstanceStorage> _instances;
		MeshInstanceId _idBase = 1;
	};

	inline MeshInstanceManager gMeshInstanceManager;
}
