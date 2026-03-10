

#pragma once

#include <HexEngine.Core/FileSystem/ResourceSystem.hpp>
#include <HexEngine.Core/Graphics/ITexture2D.hpp>

class TextureImporter : public HexEngine::IResourceLoader
{
public:
	TextureImporter();
	virtual ~TextureImporter();

	// IResourceLoader
	//
	virtual std::shared_ptr<HexEngine::IResource>	LoadResourceFromFile(const fs::path& absolutePath, HexEngine::FileSystem* fileSystem, const HexEngine::ResourceLoadOptions* options = nullptr) override;
	virtual std::shared_ptr<HexEngine::IResource>	LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, HexEngine::FileSystem* fileSystem, const HexEngine::ResourceLoadOptions* options = nullptr) override;
	virtual void						OnResourceChanged(std::shared_ptr<HexEngine::IResource> resource) override {}
	virtual void						UnloadResource(HexEngine::IResource* resource) override;
	virtual std::vector<std::string>	GetSupportedResourceExtensions() override;
	virtual std::wstring				GetResourceDirectory() const override;
	virtual void						SaveResource(HexEngine::IResource* resource, const fs::path& path) override {}
};