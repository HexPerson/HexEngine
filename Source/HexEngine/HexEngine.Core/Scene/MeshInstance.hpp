

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

	struct SimpleMeshInstanceData
	{
		math::Matrix worldMatrix;
	};

	typedef uint32_t MeshInstanceId;

	template <typename T>
	class BaseMeshInstance
	{
	public:
		friend class MeshInstanceManager;

		virtual ~BaseMeshInstance()
		{
			SAFE_DELETE(_instanceBuffer);
		}

		void AddRef() { ++_refCount; }
		uint32_t RemRef() {	return --_refCount; }

		

		Mesh* GetMesh() const
		{
			return _mesh;
		}

		MeshInstanceId GetInstanceId() const
		{
			return _id;
		}

		int32_t GetSize() const 
		{
			return _poolIndex;
		}

		//void AddRef();
		//uint32_t GetRefCount();

	protected:
		std::string _meshName;
		uint32_t _refCount = 0;
		Mesh* _mesh = nullptr;
		IVertexBuffer* _instanceBuffer = nullptr;
		std::vector<T> _data;
		uint32_t _instanceBufferNumElements = 0;
		MeshInstanceId _id;
		int32_t _poolIndex = 0;
	};

	class SimpleMeshInstance : public BaseMeshInstance<SimpleMeshInstanceData>
	{
	public:
		friend class Mesh;
		void Start()
		{
			_poolIndex = 0;
		}

		void Finish();
		void Render(const math::Matrix& worldMatrixTranspose);
	};

	class MeshInstance : public BaseMeshInstance<MeshInstanceData>
	{
	public:
		friend class MeshInstanceManager;
		friend class Mesh;

		MeshInstance()
		{
			_simpleInstance = new SimpleMeshInstance;
		}

		virtual ~MeshInstance()
		{
			SAFE_DELETE(_simpleInstance);
		}
		void Start()
		{
			_poolIndex = 0;
		}


		void Finish();
		void Render(
			const math::Matrix& worldMatrix,
			const math::Matrix& worldMatrixTranspose,
			const math::Matrix& worldMatrixPrev,
			const math::Matrix& worldMatrixInvert,
			const math::Vector4& colour,
			const math::Vector2& uvScale = math::Vector2(1.0f, 1.0f));

		SimpleMeshInstance* GetSimpleInstance() const
		{
			return _simpleInstance;
		}

	private:
		SimpleMeshInstance* _simpleInstance = nullptr;
	};

	
}
