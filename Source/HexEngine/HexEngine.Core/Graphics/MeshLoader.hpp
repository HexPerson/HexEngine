
#pragma once

#include "../Required.hpp"
#include "../FileSystem/ResourceSystem.hpp"

namespace HexEngine
{
	class MeshLoader : public IResourceLoader
	{
	public:
		MeshLoader();
		~MeshLoader();

		virtual std::shared_ptr<IResource>	LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
		virtual std::shared_ptr<IResource>	LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
		virtual void						UnloadResource(IResource* resource) override;
		virtual std::vector<std::string>	GetSupportedResourceExtensions() override;
		virtual std::wstring				GetResourceDirectory() const override;
		virtual Dialog*						CreateEditorDialog(const fs::path& path, FileSystem* fileSystem) override;
		virtual void						SaveResource(IResource* resource, const fs::path& path) override;
	};
}