
#include "MeshLoader.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Environment/LogFile.hpp"
#include "../Scene/Mesh.hpp"
#include "../FileSystem/FileSystem.hpp"
#include "Material.hpp"

namespace HexEngine
{
	MeshLoader::MeshLoader()
	{
		g_pEnv->_resourceSystem->RegisterResourceLoader(this);
	}

	MeshLoader::~MeshLoader()
	{
		g_pEnv->_resourceSystem->UnregisterResourceLoader(this);
	}

	std::shared_ptr<IResource> MeshLoader::LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		DiskFile file(absolutePath, std::ios::in | std::ios::binary);

		if (file.Open() == false)
		{
			LOG_CRIT("Unable to open file '%s' for reading", absolutePath.string().c_str());
			return nullptr;
		}

		std::shared_ptr<Mesh> mesh = std::shared_ptr<Mesh>(new Mesh(nullptr, fs::relative(absolutePath, fileSystem->GetDataDirectory()).string()), ResourceDeleter());

		// read the material
		bool hasMaterial = file.Read<bool>();

		if (hasMaterial)
		{
			std::string matName = file.ReadString();
			mesh->SetMaterial(Material::Create(matName));
		}
		else
		{
			mesh->SetMaterial(Material::GetDefaultMaterial());
		}

		// read the number of faces
		uint32_t numFaces;
		file.Read<uint32_t>(&numFaces);

		mesh->SetNumFaces(numFaces);

		// Read the vertices
		uint32_t numVertices = file.Read<uint32_t>();
		std::vector<MeshVertex> vertices(numVertices);
		file.Read((uint8_t*)vertices.data(), numVertices * sizeof(MeshVertex));

		mesh->AddVertices(vertices);

		// read the index data
		uint32_t numIndices = file.Read<uint32_t>();
		std::vector<MeshIndexFormat> indices(numIndices);
		file.Read((uint8_t*)indices.data(), (uint32_t)indices.size() * sizeof(MeshIndexFormat));

		mesh->AddIndices(indices);

		// write the aabb + obb
		mesh->SetAABB(file.Read<dx::BoundingBox>());
		mesh->SetOBB(file.Read<dx::BoundingOrientedBox>());

		// finally close the file
		file.Close();

		return mesh;
	}

	std::shared_ptr<IResource> MeshLoader::LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		return nullptr;
	}

	void MeshLoader::UnloadResource(IResource* resource)
	{
		SAFE_DELETE(resource);
	}

	std::vector<std::string> MeshLoader::GetSupportedResourceExtensions()
	{
		return { ".hmesh" };
	}

	std::wstring MeshLoader::GetResourceDirectory() const
	{
		return L"Meshes";
	}

	Dialog* MeshLoader::CreateEditorDialog(const fs::path& path, FileSystem* fileSystem)
	{
		return nullptr;
	}

	void MeshLoader::SaveResource(IResource* resource, const fs::path& path)
	{
		DiskFile file(path, std::ios::out | std::ios::binary | std::ios::trunc);

		if (file.Open() == false)
		{
			LOG_CRIT("Unable to open file '%S' for saving", path.wstring().c_str());
			return;
		}

		Mesh* mesh = dynamic_cast<Mesh*>(resource);

		const auto& vertices = mesh->GetVertices();
		const auto& indices = mesh->GetIndices();

		// write the material
		auto material = mesh->GetMaterial();
		if (material)
		{
			file.Write<bool>(true);
			file.WriteString(mesh->GetMaterial()->GetFileSystemPath().string());
		}
		else
		{
			file.Write<bool>(false);
		}

		// write the number of faces
		file.Write<uint32_t>(mesh->GetNumFaces());

		// write the vertex data
		file.Write<uint32_t>((uint32_t)vertices.size());
		file.Write((uint8_t*)vertices.data(), (uint32_t)vertices.size() * sizeof(MeshVertex));

		// write the index data
		file.Write<uint32_t>((uint32_t)indices.size());
		file.Write((uint8_t*)indices.data(), (uint32_t)indices.size() * sizeof(MeshIndexFormat));

		// write the aabb + obb
		file.Write<dx::BoundingBox>(mesh->GetAABB());
		file.Write<dx::BoundingOrientedBox>(mesh->GetOBB());

		// finally close the file
		file.Close();
	}
}