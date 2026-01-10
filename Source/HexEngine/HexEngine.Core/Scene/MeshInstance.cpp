

#include "MeshInstance.hpp"
#include "../HexEngine.hpp"

namespace HexEngine
{
	std::map<std::string, std::vector<std::pair<Mesh*, MeshInstance*>>> gMeshInstances;

	//void BaseMeshInstance::Start()
	//{
	//	_poolIndex = 0;
	//}

	//BaseMeshInstance::~MeshInstance()
	//{
	//	//MeshInstance::Destroy(this);

	//	SAFE_DELETE(_instanceBuffer);
	//}

	//MeshInstanceId BaseMeshInstance::GetInstanceId()
	//{
	//	return _id;
	//}

	void MeshInstance::Render(
		const math::Matrix& worldMatrix,
		const math::Matrix& worldMatrixTranspose,
		const math::Matrix& worldMatrixPrev,
		const math::Matrix& worldMatrixInvert,
		const math::Vector4& colour,
		const math::Vector2& uvScale)
	{
		if ((_poolIndex + 1) > (int32_t)_data.size())
		{
			MeshInstanceData data;
			data.worldMatrix = worldMatrixTranspose;
			data.worldMatrixPrev = worldMatrixPrev;
			data.worldMatrixInverseTranspose = worldMatrixInvert;
			data.colour = colour;
			data.uvscale = uvScale;

			_data.push_back(data);
		}
		else
		{
			MeshInstanceData& data = _data.at(_poolIndex);

			data.worldMatrix = worldMatrixTranspose;
			data.worldMatrixPrev = worldMatrixPrev;
			data.worldMatrixInverseTranspose = worldMatrixInvert;
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

	void SimpleMeshInstance::Render(const math::Matrix& worldMatrixTranspose)
	{
		if ((_poolIndex + 1) > (int32_t)_data.size())
		{
			SimpleMeshInstanceData data;
			data.worldMatrix = worldMatrixTranspose;
			_data.push_back(data);
		}
		else
		{
			SimpleMeshInstanceData& data = _data.at(_poolIndex);

			data.worldMatrix = worldMatrixTranspose;
		}

		_poolIndex++;
	}

	void SimpleMeshInstance::Finish()
	{
		if (_poolIndex == 0)
			return;

		// if we have too many instances, we have to rebuild the buffer
		//
		if (_poolIndex > _instanceBufferNumElements || _instanceBuffer == nullptr)
		{
			SAFE_DELETE(_instanceBuffer);

			_instanceBuffer = g_pEnv->_graphicsDevice->CreateVertexBuffer(
				sizeof(SimpleMeshInstanceData) * _poolIndex,
				sizeof(SimpleMeshInstanceData),
				D3D11_USAGE_DYNAMIC,
				D3D11_CPU_ACCESS_WRITE);

			_instanceBufferNumElements = _poolIndex;
		}

		// finally set the buffer data
		//
		_instanceBuffer->SetVertexData((uint8_t*)_data.data(), _poolIndex * sizeof(SimpleMeshInstanceData));

		// then set the instance buffer to the pipeline
		//
		g_pEnv->_graphicsDevice->SetVertexBuffer(1, _instanceBuffer);
	}

	//Mesh* BaseMeshInstance::GetMesh()
	//{
	//	return _mesh;
	//}
}