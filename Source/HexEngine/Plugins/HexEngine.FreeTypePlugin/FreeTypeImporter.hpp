
#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include "FreeTypeFont.hpp"



class FreeTypeImporter : public HexEngine::IFontImporter, public HexEngine::IResourceLoader
{
public:
	bool Create();

	virtual void Destroy() override;

	// Methods from IFontImporter
	//
	//virtual IFont* CreateFont(const fs::path& path) override;
	//virtual void DestroyFont(IFont* font) override;

protected:
	// Methods from IResourceLoader
	//
	virtual std::shared_ptr<HexEngine::IResource>	LoadResourceFromFile(const fs::path& absolutePath, HexEngine::FileSystem* fileSystem, const HexEngine::ResourceLoadOptions* options = nullptr) override;
	virtual std::shared_ptr<HexEngine::IResource>	LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, HexEngine::FileSystem* fileSystem, const HexEngine::ResourceLoadOptions* options = nullptr) override;
	virtual void						OnResourceChanged(std::shared_ptr<HexEngine::IResource> resource) override {}
	virtual void						UnloadResource(HexEngine::IResource* resource) override;
	virtual std::vector<std::string>	GetSupportedResourceExtensions() override;
	virtual std::wstring				GetResourceDirectory() const override;
	virtual void						SaveResource(HexEngine::IResource* resource, const fs::path& path) override { }

private:
	bool LoadFontInternal(std::shared_ptr<FreeTypeFont>& font, FT_Face face, int32_t size, const HexEngine::FontImportOptions* importOptions);
	void DrawGlyphToFontSheet(int32_t xoffset, int32_t yoffset, int32_t width, int32_t height, HexEngine::GlyphDesc& glyph, FT_Bitmap* bm, uint32_t* data, uint32_t atlasSize, const uint8_t* src, uint8_t r, uint8_t g, uint8_t b);

private:
	FT_Library _library;
	FT_Stroker _stroker;
	int32_t _maxCharHeight = 0;
};
