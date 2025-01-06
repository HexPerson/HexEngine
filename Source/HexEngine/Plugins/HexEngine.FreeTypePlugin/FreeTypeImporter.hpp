
#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include "FreeTypeFont.hpp"



class FreeTypeImporter : public IFontImporter, public IResourceLoader
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
	virtual std::shared_ptr<IResource>	LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
	virtual std::shared_ptr<IResource>	LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
	virtual void						UnloadResource(IResource* resource) override;
	virtual std::vector<std::string>	GetSupportedResourceExtensions() override;
	virtual std::wstring				GetResourceDirectory() const override;
	virtual void						SaveResource(IResource* resource, const fs::path& path) override { }

private:
	bool LoadFontInternal(std::shared_ptr<FreeTypeFont>& font, FT_Face face, int32_t size, const FontImportOptions* importOptions);
	void DrawGlyphToFontSheet(int32_t xoffset, int32_t yoffset, GlyphDesc& glyph, FT_Bitmap* bm, uint32_t* data, uint32_t atlasSize);

private:
	FT_Library _library;
	FT_Stroker _stroker;
	int32_t _maxCharHeight = 0;
};
