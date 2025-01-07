
#include "Model.hpp"
#include "../HexEngine.hpp"

namespace HexEngine
{
	Model::~Model()
	{
		Destroy();
	}

	void Model::AddMesh(const std::shared_ptr<Mesh>& mesh)
	{
		_meshes.push_back(mesh);
	}

	/*void Model::AddRef()
	{
		IResource::AddRef();

		LOG_DEBUG("Model '%s' now has a refcount of %d", this->GetRelativePath().u8string().c_str(), GetRefCount());

		for (auto& mesh : _meshes)
		{
			mesh->AddRef();
			mesh->CreateInstance();

			LOG_DEBUG("Mesh '%s' [%p] now has a refcount of %d", mesh->GetName().c_str(), mesh, mesh->GetRefCount());
		}
	}*/

	std::shared_ptr<Model> Model::Create(const fs::path& path)
	{
		return dynamic_pointer_cast<Model>(g_pEnv->_resourceSystem->LoadResource(path));
	}

	std::shared_ptr<Model> Model::CreateAsync(const fs::path& path, ResourceLoadedFn callback)
	{
		return dynamic_pointer_cast<Model>(g_pEnv->_resourceSystem->LoadResourceAsync(path, callback));
	}

	void Model::Destroy()
	{
		/*for (auto&& mesh : _meshes)
		{
			SAFE_DELETE(mesh);
		}*/

		_meshes.clear();
	}

	void Model::SetPaths(const fs::path& path, FileSystem* fileSystem)
	{
		_name = path.stem();
		_relativePath = fs::relative(path, fileSystem->GetBaseDirectory()).parent_path();
	}

	fs::path Model::GetRelativePath()
	{
		return _relativePath;
	}	

	std::shared_ptr<Mesh> Model::GetMeshAtIndex(uint32_t index)
	{
		if (index < 0 || index >= _meshes.size())
			return nullptr;

		return _meshes.at(index);
	}

	uint32_t Model::GetNumMeshes()
	{
		return (uint32_t)_meshes.size();
	}

	const std::vector<std::shared_ptr<Mesh>>& Model::GetMeshes() const
	{
		return _meshes;
	}

	/*void Model::SetAnimIndex(uint32_t index)
	{
		_animIndex = index;
	}*/
}