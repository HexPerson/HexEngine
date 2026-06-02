
#pragma once

#include <HexEngine.Core/FileSystem/ResourceSystem.hpp>
#include <HexEngine.Core/Graphics/ITexture2D.hpp>

/**
 * @brief D3D12 texture file loader (png / jpg / dds / tga / etc).
 *
 * Registers itself with the engine's ResourceSystem for the supported
 * extensions on construction (mirrors D3D11's TextureImporter). Decodes the
 * file bytes via DirectXTex and uploads them into a Texture2DD3D12 created
 * through GraphicsDeviceD3D12::CreateTexture2D.
 *
 * B3 scope: top mip only. Mip-chain support folds in alongside the rest of
 * the texturing work in B5.
 */
class TextureImporterD3D12 : public HexEngine::IResourceLoader
{
public:
	TextureImporterD3D12();
	virtual ~TextureImporterD3D12();

	virtual std::shared_ptr<HexEngine::IResource> LoadResourceFromFile(const fs::path& absolutePath, HexEngine::FileSystem* fileSystem, const HexEngine::ResourceLoadOptions* options = nullptr) override;
	virtual std::shared_ptr<HexEngine::IResource> LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, HexEngine::FileSystem* fileSystem, const HexEngine::ResourceLoadOptions* options = nullptr) override;
	virtual void                                  OnResourceChanged(std::shared_ptr<HexEngine::IResource> resource) override {}
	virtual void                                  UnloadResource(HexEngine::IResource* resource) override;
	virtual std::vector<std::string>              GetSupportedResourceExtensions() override;
	virtual std::wstring                          GetResourceDirectory() const override;
	virtual void                                  SaveResource(HexEngine::IResource* resource, const fs::path& path) override {}
};
