
#include "Mesh.hpp"
#include "../HexEngine.hpp"
#include "MeshInstanceManager.hpp"
#include "../Math/FloatMath.hpp"
#include "../Graphics/MaterialLoader.hpp"

//#include <noise.h>
#include <fastnoiselite/FastNoiseLite.h>
#include <GeometricPrimitive.h>

namespace HexEngine
{
	Mesh::Mesh()
	{
		_objectBuffer = new PerObjectBuffer;
	}
	Mesh::Mesh(const std::shared_ptr<Model>& model, const std::string& name)
	{
		_name = name;
		_model = model;

		if (auto lodStr = name.find("LOD"); lodStr != name.npos)
		{
			if (lodStr + 4 >= name.length())
			{
				auto lodString = name.substr(lodStr + 3, 1);

				_lodLevel = std::stoi(lodString);

				if (_model && _lodLevel > _model->GetMaxLOD())
				{
					_model->SetMaxLOD(_lodLevel);
				}
			}
			else if (auto lodEndStr = name.find_first_not_of("0123456789", lodStr+3); lodEndStr != name.npos)
			{
				if (lodEndStr - (lodStr + 3) > 0)
				{
					auto lodString = name.substr(lodStr + 3, lodEndStr - (lodStr + 3));

					_lodLevel = std::stoi(lodString);

					if (_model && _lodLevel > _model->GetMaxLOD())
					{
						_model->SetMaxLOD(_lodLevel);
					}
				}
			}
		}

		_objectBuffer = new PerObjectBuffer;
	}

	Mesh::Mesh(Mesh* other)
	{
		_name = other->_name;
		_model = other->_model;
		//_fileName = other->_fileName;

		_vertices = other->_vertices;
		_simpleVertices = other->_simpleVertices;
		_indices = other->_indices;

		_aabb = other->_aabb;
		_obb = other->_obb;

		_instance = CreateInstance();

		//CreateBuffers(other->_isDynamicMesh);

		_objectBuffer = new PerObjectBuffer;

		_lodLevel = other->_lodLevel;

		_faceCount = other->_faceCount;

		_modelTransform = other->_modelTransform;

		/*_boneInfo = other->_boneInfo;
		_boneMap = other->_boneMap;
		_bones = other->_bones;
		_rootTransformation = other->_rootTransformation;

		_terrainParams = other->_terrainParams;

		if(other->_animData)
			_animData = other->_animData;*/
	}

	Mesh::~Mesh()
	{
		Destroy();

		SAFE_DELETE(_objectBuffer);

		// Unload the resources safely
		//
		/*for (int i = 0; i < MeshMaxMaterials; ++i)
		{
			SAFE_UNLOAD(_materials[i]);
		}*/
	}

	std::shared_ptr<Mesh> Mesh::Create(const fs::path& path)
	{
		return dynamic_pointer_cast<Mesh>(g_pEnv->GetResourceSystem().LoadResource(path));
	}

	std::shared_ptr<Mesh> Mesh::CreateAsync(const fs::path& path, ResourceLoadedFn fn)
	{
		return dynamic_pointer_cast<Mesh>(g_pEnv->GetResourceSystem().LoadResourceAsync(path, fn));
	}

	const std::string& Mesh::GetName()
	{
		if (_cachedName.length() == 0)
		{
			std::string name;

			if (_relativePath.empty() == false)
			{
				fs::path modelPath = _relativePath;
				modelPath += fs::path::preferred_separator;

				name = modelPath.string();
			}

			name.append(_name);

			_cachedName = name;
		}

		return _cachedName;
	}

	const std::string& Mesh::GetFullName()
	{
		if (_cachedFullName.length() == 0)
		{
			std::string name;

			if (_relativePath.empty() == false)
			{
				fs::path modelPath = _relativePath;
				modelPath += fs::path::preferred_separator;

				name = modelPath.string();
			}

			/*if (_fileName.empty() == false)
			{
				fs::path modelPath = _fileName;
				modelPath += fs::path::preferred_separator;

				name.append(modelPath.string());
			}*/

			if (_materialName.empty() == false)
			{
				name.append(_materialName);
			}
			//else
				name.append(_name);

			if (_lodLevel != -1)
				name.append("LOD" + std::to_string(_lodLevel));

			_cachedFullName = name;
		}

		return _cachedFullName;
	}

	void Mesh::Destroy()
	{
		// Delete the buffers
		//
		SAFE_DELETE(_vertexBuffer);
		SAFE_DELETE(_indexBuffer);
		SAFE_DELETE(_simpleVertexBuffer);		

		// And finally delete the mesh instance
		//
		/*if (_instance)
		{
			if (gMeshInstanceManager.DestroyInstance(_instance, this))
			{
				_model.reset();
			}
		}*/
		SAFE_DELETE(_instance);
	}

	void Mesh::Clear()
	{
		_vertices.clear();
		_indices.clear();
	}

	Mesh* Mesh::Clone()
	{
		return new Mesh(this);
	}

	MeshInstance* Mesh::CreateInstance()
	{
		if (_instance)
		{
			_instance->AddRef();
			return _instance;
		}

		//_instance = gMeshInstanceManager.CreateInstance(this);

		MeshInstance* meshInstance = new MeshInstance;

		static int32_t _idBase = 0;

		meshInstance->_instanceBufferNumElements = 0;
		meshInstance->_mesh = this;
		meshInstance->_meshName = GetFullName();
		meshInstance->_refCount = 1;
		meshInstance->_id = _idBase++;

		meshInstance->_simpleInstance->_instanceBufferNumElements = 0;
		meshInstance->_simpleInstance->_mesh = this;
		meshInstance->_simpleInstance->_meshName = GetFullName();
		meshInstance->_simpleInstance->_refCount = 1;
		meshInstance->_simpleInstance->_id = meshInstance->_id;

		_instance = meshInstance;

		return _instance;
	}

	const std::vector<MeshIndexFormat>& Mesh::GetIndices() const
	{
		return _indices;
	}

	const std::vector<MeshVertex>& Mesh::GetVertices() const
	{
		return _vertices;
	}

	uint32_t Mesh::GetNumFaces() const
	{
		return _faceCount;
	}

	//void Mesh::SetPaths(const fs::path& path, FileSystem* fileSystem)
	//{
	//	_relativePath = fs::relative(path, fileSystem->GetDataDirectory());
	//	//_fileName = path.stem().string();
	//	_fsRelativePath = fileSystem->GetRelativeResourcePath(_relativePath);
	//	_absolutePath = path;
	//}

	bool Mesh::CreateBuffers()
	{
		if (_vertexBuffer != nullptr || _indexBuffer != nullptr)
		{
			LOG_WARN("Trying to create vertex or index buffers for a Mesh when it already has a buffer is ok, but might indicate resources are being wasted");
			return false;
		}

		if (_vertexBuffer == nullptr)
		{
			_vertexBuffer = g_pEnv->_graphicsDevice->CreateVertexBuffer(
				(int32_t)_vertices.size() * sizeof(MeshVertex),
				sizeof(MeshVertex),
				D3D11_USAGE_DEFAULT,
				0,
				_vertices.data());

			if (!_vertexBuffer)
			{
				return false;
			}
		}

		if (_simpleVertexBuffer == nullptr)
		{
			_simpleVertexBuffer = g_pEnv->_graphicsDevice->CreateVertexBuffer(
				(int32_t)_simpleVertices.size() * sizeof(SimpleMeshVertex),
				sizeof(SimpleMeshVertex),
				D3D11_USAGE_DEFAULT,
				0,
				_simpleVertices.data());

			if (!_simpleVertexBuffer)
			{
				return false;
			}
		}

		if (_indexBuffer == nullptr)
		{
			_indexBuffer = g_pEnv->_graphicsDevice->CreateIndexBuffer(
				(int32_t)_indices.size() * sizeof(MeshIndexFormat),
				sizeof(MeshIndexFormat),
				D3D11_USAGE_DEFAULT,
				0,
				_indices.data());

			if (!_indexBuffer)
			{
				return false;
			}
		}

		return true;
	}

	void Mesh::SetBuffers(bool isShadowMap)
	{
		auto graphicsDevice = g_pEnv->_graphicsDevice;

		// Set the vertex and index buffers
		graphicsDevice->SetIndexBuffer(_indexBuffer);
		graphicsDevice->SetVertexBuffer(0, isShadowMap ? _simpleVertexBuffer : _vertexBuffer);
		graphicsDevice->SetTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}	

	void Mesh::AddVertex(const MeshVertex& vertex)
	{
		_vertices.push_back(vertex);

		SimpleMeshVertex smv;
		smv._position = vertex._position;
		smv._texcoord = vertex._texcoord;

		_simpleVertices.push_back(smv);
	}

	void Mesh::AddIndex(const MeshIndexFormat index)
	{
		_indices.push_back(index);
	}

	void Mesh::AddVertices(const std::vector<MeshVertex>& vertices)
	{
		_vertices.insert(_vertices.end(), vertices.begin(), vertices.end());

		for (auto& vertex : vertices)
		{
			SimpleMeshVertex smv;
			smv._position = vertex._position;
			smv._texcoord = vertex._texcoord;

			_simpleVertices.push_back(smv);
		}
	}

	void Mesh::AddIndices(const std::vector<MeshIndexFormat>& indices)
	{
		_indices.insert(_indices.end(), indices.begin(), indices.end());
	}

	void Mesh::UpdateConstantBuffer(Entity* entity, const math::Matrix& localTM, Material* material, int32_t instanceId)
	{
		// Write the per-object constant buffer
		auto perObjectBuffer = g_pEnv->_graphicsDevice->GetEngineConstantBuffer(EngineConstantBuffer::PerObjectBuffer);


		if (!perObjectBuffer)
		{
			LOG_WARN("Invalid per-object constant buffer");
			return;
		}

		_objectBuffer->_worldMatrix = localTM.Transpose();
		_objectBuffer->_flags = 0;
		_objectBuffer->entityId = instanceId;
		_objectBuffer->cullDistance = material->GetCullDistance();

		if (HasAnimations()) {
			_objectBuffer->_flags |= OBJECT_FLAGS_HAS_ANIMATION;
		}

		if (material->GetTexture(MaterialTexture::Normal) != nullptr)
			_objectBuffer->_flags |= OBJECT_FLAGS_HAS_BUMP;

		if (material->GetTexture(MaterialTexture::Roughness) != nullptr)
			_objectBuffer->_flags |= OBJECT_FLAGS_HAS_ROUGHNESS;

		if (material->GetTexture(MaterialTexture::Metallic) != nullptr)
			_objectBuffer->_flags |= OBJECT_FLAGS_HAS_METALLIC;

		if (material->GetTexture(MaterialTexture::Height) != nullptr)
			_objectBuffer->_flags |= OBJECT_FLAGS_HAS_HEIGHT;

		if (material->GetTexture(MaterialTexture::Emission) != nullptr)
			_objectBuffer->_flags |= OBJECT_FLAGS_HAS_EMISSION;

		if (material->GetTexture(MaterialTexture::Opacity) != nullptr)
			_objectBuffer->_flags |= OBJECT_FLAGS_HAS_OPACITY;

		if (material->GetTexture(MaterialTexture::AmbientOcclusion) != nullptr)
			_objectBuffer->_flags |= OBJECT_FLAGS_HAS_AMBIENT_OCCLUSION;

		if (material->GetFormat() == MaterialFormat::ORM)
			_objectBuffer->_flags |= OBJECT_FLAGS_ORM_FORMAT;

		memcpy(&_objectBuffer->_material, &material->_properties, sizeof(MaterialProperties));

		material->_properties.isInTransparencyPhase = false;// isInTransparencyPhase;

		perObjectBuffer->Write(_objectBuffer, sizeof(PerObjectBuffer));

	}

	void Mesh::SetNumFaces(uint32_t numFaces)
	{
		_faceCount = numFaces;
	}

	const dx::BoundingBox& Mesh::GetAABB() const
	{
		return _aabb;
	}

	const dx::BoundingOrientedBox& Mesh::GetOBB() const
	{
		return _obb;
	}

	void Mesh::SetAABB(const dx::BoundingBox& aabb)
	{
		_aabb = aabb;
	}

	void Mesh::SetOBB(const dx::BoundingOrientedBox& obb)
	{
		_obb = obb;
	}

	uint32_t Mesh::GetNumIndices() const
	{
		return (uint32_t)_indices.size();
	}

	uint32_t Mesh::GetNumVertices() const
	{
		return (uint32_t)_vertices.size();
	}

	MeshInstance* Mesh::GetInstance()
	{
		return _instance;
	}

	/*MeshRenderer* Mesh::GetMeshRenderer()
	{
		return _meshRenderer;
	}*/

#if 0
	Mesh* Mesh::CreatePlane(uint32_t resolution, float uvScale)
	{
		resolution += 1;

		Mesh* mesh = new Mesh(nullptr, "Plane");

		std::vector<MeshVertex> vertices;
		std::vector<uint32_t> indices;

		uint32_t vertexCount = resolution * resolution;
		uint32_t faceCount = (resolution - 1) * (resolution - 1) * 2;

		vertices.resize(vertexCount);

		const float width = 1.0f;
		const float halfWidth = width * 0.5f;

		float dx = width / ((float)resolution - 1);
		float dz = width / ((float)resolution - 1);

		float du = uvScale / ((float)resolution - 1);
		float dv = uvScale / ((float)resolution - 1);

		for (uint32_t i = 0; i < resolution; ++i)
		{
			float z = halfWidth - i * dz;

			for (uint32_t j = 0; j < resolution; ++j)
			{
				float x = -halfWidth + j * dx;

				//height = GetHeight(x, z);

				vertices[i * resolution + j]._position = DirectX::XMFLOAT4(x, 0.0f, z, 1.0f);
				vertices[i * resolution + j]._texcoord = DirectX::XMFLOAT2((float)i * du, (float)j * dv);
				vertices[i * resolution + j]._normal = math::Vector3(0, 1, 0);
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

		mesh->AddVertices(vertices);
		mesh->AddIndices(indices);
		mesh->CreateBuffers(false);

		dx::BoundingBox::CreateFromPoints(mesh->GetAABB(), vertices.size(), (const math::Vector3*)vertices.data(), sizeof(MeshVertex));
		dx::BoundingOrientedBox::CreateFromBoundingBox(mesh->GetOBB(), mesh->GetAABB());

		return mesh;
	}
#endif

	int32_t Mesh::GetLodLevel() const
	{
		return _lodLevel;
	}

	void Mesh::SetLodLevel(int32_t level)
	{
		_lodLevel = level;
	}

	int32_t Mesh::GetMaxLodLevel() const
	{
		return _maxLodLevel;
	}

	void Mesh::SetMaxLodLevel(int32_t level)
	{
		_maxLodLevel = level;
	}

	std::shared_ptr<Material> Mesh::GetMaterial() const
	{
		return _material;
	}

	void Mesh::SetMaterial(std::shared_ptr<Material> material)
	{
		if (material)
		{
			_material = material;
		}
		else
		{
			_material.reset();
		}
	}

	void Mesh::SetMaterialName(const std::string& matName)
	{
		_materialName = matName;
	}

	const std::string& Mesh::GetMaterialName() const
	{
		return _materialName;
	}
}