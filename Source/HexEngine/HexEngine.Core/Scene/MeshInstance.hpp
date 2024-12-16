

#pragma once

#include "../Required.hpp"
#include "../Graphics/IVertexBuffer.hpp"

namespace HexEngine
{
	class Mesh;

	struct MeshInstanceData
	{
		math::Matrix worldMatrix;
		math::Matrix worldMatrixInverseTranspose;
		math::Matrix worldMatrixPrev;
		math::Vector4 colour;
		math::Vector2 uvscale;		
	};

	typedef uint32_t MeshInstanceId;

	class MeshInstance
	{
	public:
		friend class MeshInstanceManager;

		//static MeshInstance* Find(Mesh* mesh);
		//static MeshInstance* Create(Mesh* mesh);
		//static void Destroy(MeshInstance* instance);

		~MeshInstance();

		void AddRef() { ++_refCount; }
		uint32_t RemRef() {	return --_refCount; }

		void Start();
		void Finish();
		void Render(const math::Matrix& worldMatrix, const math::Matrix& worldMatrixTranspose, const math::Matrix& worldMatrixPrev, const math::Vector4& colour, const math::Vector2& uvScale = math::Vector2(1.0f, 1.0f));
		Mesh* GetMesh();
		MeshInstanceId GetInstanceId();
		int32_t GetSize() { return _poolIndex; }

		//void AddRef();
		//uint32_t GetRefCount();

	private:
		std::string _meshName;
		uint32_t _refCount = 0;
		Mesh* _mesh = nullptr;
		IVertexBuffer* _instanceBuffer = nullptr;
		std::vector<MeshInstanceData> _data;
		uint32_t _instanceBufferNumElements = 0;
		MeshInstanceId _id;
		int32_t _poolIndex = 0;
	};
}
