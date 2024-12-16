

#pragma once

#include <HexEngine.Core/FileSystem/ResourceSystem.hpp>
#include <HexEngine.Core/Graphics/ITexture2D.hpp>

namespace HexEngine
{
	class TextureLoader : public IResourceLoader
	{
	public:
		TextureLoader();
		~TextureLoader();

		// IResourceLoader
		//
		virtual IResource*					LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
		virtual IResource*					LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
		virtual void						UnloadResource(IResource* resource) override;
		virtual std::vector<std::string>	GetSupportedResourceExtensions() override;
		virtual std::wstring				GetResourceDirectory() const override;
		virtual void						SaveResource(IResource* resource, const fs::path& path) override { }
	};
}
