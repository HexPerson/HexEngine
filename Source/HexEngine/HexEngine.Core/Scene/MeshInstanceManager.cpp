

#include "MeshInstanceManager.hpp"
#include "../Environment/LogFile.hpp"
#include "../Environment/IEnvironment.hpp"

namespace HexEngine
{
	MeshInstance* MeshInstanceManager::CreateInstance(Mesh* mesh)
	{
		std::unique_lock lock(_lock);

		for (auto&& it : _instances)
		{
			if (it.first == mesh->GetFullName() /*&&
				mesh->GetNumIndices() == it.second.originalMeshes[0]->GetNumIndices() &&
				mesh->GetNumFaces() == it.second.originalMeshes[0]->GetNumFaces()*/)
			{
				auto& storage = it.second;

				storage.instance->_refCount++;
				
				if (storage.IsMeshStored(mesh) == false)
					storage.originalMeshes.push_back(mesh);

				return storage.instance;
			}
		}

		// if we got here we need to create a new instance for this mesh
		//
		MeshInstance* meshInstance = new MeshInstance;

		meshInstance->_instanceBufferNumElements = 0;
		meshInstance->_mesh = mesh;
		meshInstance->_meshName = mesh->GetFullName();
		meshInstance->_refCount = 1;
		meshInstance->_id = _idBase++;

		meshInstance->_simpleInstance->_instanceBufferNumElements = 0;
		meshInstance->_simpleInstance->_mesh = mesh;
		meshInstance->_simpleInstance->_meshName = mesh->GetFullName();
		meshInstance->_simpleInstance->_refCount = 1;
		meshInstance->_simpleInstance->_id = meshInstance->_id;

		MeshInstanceStorage storage;
		storage.instance = meshInstance;
		storage.originalMeshes.push_back(mesh);

		_instances[meshInstance->_meshName] = storage;

		

		LOG_DEBUG("Created new MeshInstance for %s", meshInstance->_meshName.c_str());

		return meshInstance;
	}

	/*void MeshInstanceManager::NotifyMeshRendererRemoval(MeshRenderer* renderer, Mesh* mesh)
	{
		for (auto&& it : _instances)
		{
			if (it.first == mesh->GetFullName())
			{
				if (it.second.
				{
					LOG_DEBUG("MeshInstance for '%s' has no more references, deleting", instance->_meshName.c_str());

					delete instance;

					_instances.erase(meshName);
					return;
				}
			}
		}
	}*/

	bool MeshInstanceManager::DestroyInstance(MeshInstance* instance, Mesh* oldMesh)
	{
		std::unique_lock lock(_lock);

		LOG_DEBUG("Destroying MeshInstance for '%s'", instance->_meshName.c_str());

		std::string meshName = instance->_meshName;
		//Mesh* oldMesh = instance->_mesh;

		// swap out the deleted meshes in the instances for onces we know are still ok to use
		//
		for (auto&& it : _instances)
		{
			if (it.first == meshName)
			{
				Mesh* targetMesh = it.second.FindMeshThatIsNotMesh(oldMesh);

				if (targetMesh && targetMesh != it.second.instance->_mesh)
				{
					it.second.instance->_mesh = targetMesh;
				}
				else if (!targetMesh)
				{
					bool a = false;
				}

				it.second.RemoveMesh(oldMesh);
			}
		}

		LOG_DEBUG("MeshInstance '%s' now has a reference count of %d", instance->_meshName.c_str(), instance->_refCount);

		// then free up the instance if its no longer needed
		//
		for (auto&& it : _instances)
		{
			if (it.first == meshName)
			{
				if (it.second.instance->RemRef() == 0)
				{
					LOG_DEBUG("MeshInstance for '%s' has no more references, deleting", instance->_meshName.c_str());

					delete instance;

					_instances.erase(meshName);
					return true;
				}
			}
		}	

		return false;
	}
}