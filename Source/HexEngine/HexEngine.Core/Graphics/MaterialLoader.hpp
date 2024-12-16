
#pragma once

#include "../Required.hpp"
#include "../FileSystem/ResourceSystem.hpp"
#include "../FileSystem/KeyValues.hpp"

namespace HexEngine
{
	class Material;
	class JsonFile;

	class MaterialLoader : public IResourceLoader
	{
	public:
		MaterialLoader();
		~MaterialLoader();

		virtual IResource*					LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
		virtual IResource*					LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
		virtual void						UnloadResource(IResource* resource) override;
		virtual std::vector<std::string>	GetSupportedResourceExtensions() override;
		virtual std::wstring				GetResourceDirectory() const override;
		virtual Dialog*						CreateEditorDialog(const fs::path& path, FileSystem* fileSystem) override;
		virtual void						SaveResource(IResource* resource, const fs::path& path) override;

		void AddMaterial(Material* material);
		void RemoveMaterial(Material* material);
		Material* FindMaterialByName(const std::string& name) const;

	private:
		void ParseJson(JsonFile* file, json& kv, Material* material);

		std::vector<Material*> _loadedMaterials;
	};
}
