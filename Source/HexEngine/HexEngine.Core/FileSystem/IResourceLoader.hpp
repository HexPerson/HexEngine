
#pragma once

#include "../Required.hpp"
#include "IResource.hpp"

namespace HexEngine
{
	class Dialog;

	class HEX_API IResourceLoader
	{
	public:
		virtual std::shared_ptr<IResource>	LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) = 0;
		virtual std::shared_ptr<IResource>	LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) = 0;
		virtual void						OnResourceChanged(std::shared_ptr<IResource> resource) = 0;
		virtual void						UnloadResource(IResource* resource) = 0;
		virtual std::vector<std::string>	GetSupportedResourceExtensions() = 0;
		virtual std::wstring				GetResourceDirectory() const = 0;
		virtual Dialog*						CreateEditorDialog(const std::vector<fs::path>& paths) {	return nullptr;	}
		virtual void						SaveResource(IResource* resource, const fs::path& path) = 0;
	};
}
