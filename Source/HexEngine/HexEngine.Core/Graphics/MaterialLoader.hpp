
#pragma once

#include "../Required.hpp"
#include "../FileSystem/ResourceSystem.hpp"
#include "../FileSystem/KeyValues.hpp"

namespace HexEngine
{
	class Material;
	class JsonFile;

	class HEX_API MaterialLoader : public IResourceLoader
	{
	public:
		MaterialLoader();
		~MaterialLoader();

		virtual std::shared_ptr<IResource>	LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
		virtual std::shared_ptr<IResource>	LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
		virtual void						OnResourceChanged(std::shared_ptr<IResource> resource) override;
		virtual void						UnloadResource(IResource* resource) override;
		virtual std::vector<std::string>	GetSupportedResourceExtensions() override;
		virtual std::wstring				GetResourceDirectory() const override;
		virtual Dialog*						CreateEditorDialog(const std::vector<fs::path>& paths) override;
		virtual void						SaveResource(IResource* resource, const fs::path& path) override;

		void AddMaterial(const std::shared_ptr<Material>& material);
		void RemoveMaterial(const std::shared_ptr<Material> material);
		std::shared_ptr<Material> FindMaterialByName(const std::string& name) const;

	private:
		void ParseJson(JsonFile* file, json& kv, std::shared_ptr<Material>& material);

		std::map<fs::path, std::weak_ptr<Material>> _loadedMaterials;
	};
}
