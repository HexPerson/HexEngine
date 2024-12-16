

#include "MeshInstance.hpp"
#include "../HexEngine.hpp"

namespace HexEngine
{
	std::map<std::string, std::vector<std::pair<Mesh*, MeshInstance*>>> gMeshInstances;

	//MeshInstance* MeshInstance::Find(Mesh* mesh)
	//{
	//	auto it = gMeshInstances.find(mesh->GetFullName());

	//	if (it == gMeshInstances.end())
	//		return nullptr;

	//	// clone the mesh to the back of the list
	//	it->second.push_back({ mesh, it->second.front().second });

	//	return it->second.front().second;
	//}

	//MeshInstance* MeshInstance::Create(Mesh* mesh)
	//{
	//	MeshInstance* meshInstance = new MeshInstance;		

	//	meshInstance->_instanceBufferNumElements = 0;
	//	meshInstance->_mesh = mesh;

	//	gMeshInstances[mesh->GetFullName()].push_back({ mesh, meshInstance });

	//	return meshInstance;
	//}

	//void MeshInstance::Destroy(MeshInstance* instance)
	//{
	//	for (auto it = gMeshInstances.begin(); it != gMeshInstances.end(); it++)
	//	{
	//		for(auto it2 = it->second.begin(); it2 != it->second.end(); it2++)
	//		{
	//			if (it2->second == instance)
	//			{
	//				// is this the first mesh we need to move
	//				if (it2 == it->second.begin())
	//				{
	//					it->second.erase(it2);

	//					// switch all the meshes over to the new mesh
	//					for (auto& otherMeshes : it->second)
	//					{
	//						otherMeshes.first = it->second.begin()->first;
	//					}
	//				}
	//				else
	//					it->second.erase(it2);
	//				
	//				break;
	//			}
	//		}

	//		if (it->second.size() == 0)
	//		{
	//			gMeshInstances.erase(it);
	//			return;
	//		}
	//		/*if (it->second.second == instance && --it->second.first == 0)
	//		{
	//			gMeshInstances.erase(it);
	//			return;
	//		}*/
	//	}
	//}

	void MeshInstance::Start()
	{
		//_currentInstanceId = 0;

		//_data.clear();

		_poolIndex = 0;

		//if(_instanceBufferNumElements > 0)
		//	_data.reserve(_instanceBufferNumElements);
	}

	MeshInstance::~MeshInstance()
	{
		//MeshInstance::Destroy(this);

		SAFE_DELETE(_instanceBuffer);
	}

	MeshInstanceId MeshInstance::GetInstanceId()
	{
		return _id;
	}

	void MeshInstance::Render(const math::Matrix& worldMatrix, const math::Matrix& worldMatrixTranspose, const math::Matrix& worldMatrixPrev, const math::Vector4& colour, const math::Vector2& uvScale)
	{
		if ((_poolIndex + 1) > (int32_t)_data.size())
		{
			MeshInstanceData data;
			data.worldMatrix = worldMatrixTranspose;
			data.worldMatrixPrev = worldMatrixPrev;
			data.worldMatrixInverseTranspose = worldMatrix.Invert();// .Transpose();
			data.colour = colour;
			data.uvscale = uvScale;

			_data.push_back(data);
		}
		else
		{
			MeshInstanceData& data = _data.at(_poolIndex);

			data.worldMatrix = worldMatrixTranspose;
			data.worldMatrixPrev = worldMatrixPrev;
			data.worldMatrixInverseTranspose = worldMatrix.Invert();// .Transpose();
			data.colour = colour;
			data.uvscale = uvScale;
		}

		_poolIndex++;
	}

	void MeshInstance::Finish()
	{
		if (_poolIndex == 0)
			return;

		// if we have too many instances, we have to rebuild the buffer
		//
		if (_poolIndex > _instanceBufferNumElements || _instanceBuffer == nullptr)
		{
			SAFE_DELETE(_instanceBuffer);

			_instanceBuffer = g_pEnv->_graphicsDevice->CreateVertexBuffer(
				sizeof(MeshInstanceData) * _poolIndex,
				sizeof(MeshInstanceData),
				D3D11_USAGE_DYNAMIC,
				D3D11_CPU_ACCESS_WRITE);

			_instanceBufferNumElements = _poolIndex;
		}

		// finally set the buffer data
		//
		_instanceBuffer->SetVertexData((uint8_t*)_data.data(), _poolIndex * sizeof(MeshInstanceData));

		// then set the instance buffer to the pipeline
		//
		g_pEnv->_graphicsDevice->SetVertexBuffer(1, _instanceBuffer);
	}

	Mesh* MeshInstance::GetMesh()
	{
		return _mesh;
	}
}