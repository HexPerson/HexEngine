

#include "FreeTypeImporter.hpp"

const int32_t AtlasSize = 256;

#define ENABLE_OUTLINE 1

bool FreeTypeImporter::Create()
{
	g_pEnv->_resourceSystem->RegisterResourceLoader(this);

	auto error = FT_Init_FreeType(&_library);

	if (error)
	{
		LOG_CRIT("Failed to initialize FreeType font engine. Code: %d", error);
		return false;
	}

#if ENABLE_OUTLINE
	FT_Stroker_New(_library, &_stroker);
#endif

	return true;
}

void FreeTypeImporter::Destroy()
{
	g_pEnv->_resourceSystem->UnregisterResourceLoader(this);

	FT_Done_FreeType(_library);

#if ENABLE_OUTLINE
	FT_Stroker_Done(_stroker);
#endif
}

std::shared_ptr<IResource> FreeTypeImporter::LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options /*= nullptr*/)
{
	const FontImportOptions* importOptions = reinterpret_cast<const FontImportOptions*>(options);

	std::shared_ptr<FreeTypeFont> font = std::shared_ptr<FreeTypeFont>(new FreeTypeFont, ResourceDeleter());

	font->_face = absolutePath.filename().string();

	uint32_t width, height;
	g_pEnv->GetScreenSize(width, height);

	_maxCharHeight = 0;
	
	for (auto size : importOptions->sizes)
	{
		FT_Face face;

		auto error = FT_New_Face(_library,
			absolutePath.string().c_str(),
			0,
			&face);

		if (LoadFontInternal(font, face, size, importOptions) == false)
		{
			LOG_CRIT("Failed to import font '%s'", absolutePath.string().c_str());
			font.reset();
			return nullptr;
		}
	}

	return font;
}

std::shared_ptr<IResource> FreeTypeImporter::LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
{
	const FontImportOptions* importOptions = reinterpret_cast<const FontImportOptions*>(options);

	std::shared_ptr<FreeTypeFont> font = std::shared_ptr<FreeTypeFont>(new FreeTypeFont, ResourceDeleter());

	font->_face = relativePath.filename().string();

	uint32_t width, height;
	g_pEnv->GetScreenSize(width, height);

	FT_Int32 loadFlags = FT_LOAD_TARGET_NORMAL;

	if (!importOptions->antialias)
	{
		loadFlags &= ~FT_LOAD_TARGET_NORMAL;
		loadFlags |= (FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_AUTOHINT);
	}

	for (auto size : importOptions->sizes)
	{
		FT_Face face;

		auto error = FT_New_Memory_Face(
			_library,
			data.data(),
			data.size(),
			0,
			&face);

		if (LoadFontInternal(font, face, size, importOptions) == false)
		{
			LOG_CRIT("Failed to import font '%s'", relativePath.string().c_str());
			font.reset();
			return nullptr;
		}
	}
	return font;
}

bool FreeTypeImporter::LoadFontInternal(std::shared_ptr<FreeTypeFont>& font, FT_Face face, int32_t size, const FontImportOptions* importOptions)
{
	FT_Error error = FT_Select_Charmap(face, FT_ENCODING_UNICODE);

	if (error != 0)
	{
		FT_Done_Face(face);
		return false;
	}

	if (error)
	{
		//LOG_CRIT("Failed to import font face: %S. Code: %d", absolutePath.c_str(), error);
		FT_Done_Face(face);
		return false;
	}
	
#if ENABLE_OUTLINE
	//  2 * 64 result in 2px outline
	FT_Stroker_Set(_stroker, 1 * 64, FT_STROKER_LINECAP_ROUND, FT_STROKER_LINEJOIN_ROUND, 0);
#endif

	//size = RY(size, height);

	uint32_t currentAtlasSize = AtlasSize;

	FT_Set_Pixel_Sizes(face, 0, size);

	FT_Int32 loadFlags = /*FT_LOAD_RENDER |*/ FT_LOAD_TARGET_NORMAL;

	if (!importOptions->antialias)
	{
		loadFlags &= ~FT_LOAD_TARGET_NORMAL;
		loadFlags |= (FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_AUTOHINT);
	}

	int32_t totalWidth = 0;
	int32_t totalHeight = 0;

	std::vector<GlyphDescFT> glyphs;
	uint32_t totalSizeToAlloc = 0;

	int32_t x = 0;
	int32_t y = 0;

	int yMax = face->bbox.yMax;
	int yMin = face->bbox.yMin;

	int32_t baseline = size * yMax / (yMax - yMin);

	// for each character set
	//
	for (auto& charRanges : importOptions->charRanges)
	{

		// for each character in the set
		//
		uint32_t lastCh = 0;
		for (uint32_t ch = charRanges.first; ch <= charRanges.second; ch++)
		{
			if (ch == lastCh && lastCh != 0)
				break;

			lastCh = ch;

			uint32_t glyph_index = FT_Get_Char_Index(face, ch);
			if (glyph_index == 0)
				continue;

			FT_Error error = FT_Load_Glyph(face, glyph_index, loadFlags);
			if (error)
				continue;

#if ENABLE_OUTLINE
			FT_Glyph glyph;
			FT_Get_Glyph(face->glyph, &glyph);
			//FT_Glyph_StrokeBorder(&effectGlyph, _stroker, false, true);
			FT_Glyph_Stroke(&glyph, _stroker, /*false,*/ true);
			FT_Glyph_To_Bitmap(&glyph, FT_RENDER_MODE_NORMAL, nullptr, true);

			FT_BitmapGlyph bitmapGlyph = reinterpret_cast<FT_BitmapGlyph>(glyph);			

			auto& slot = face->glyph;

			GlyphDescFT desc;
			desc.ch = ch;
			desc.offsetX = slot->bitmap_left;
			desc.offsetY = -slot->bitmap_top;
			desc.width = bitmapGlyph->bitmap.width;
			desc.height = bitmapGlyph->bitmap.rows;

			desc.pitch = bitmapGlyph->bitmap.pitch;
			desc.advanceX = slot->advance.x;
			desc.advanceY = slot->advance.y;
			memcpy(&desc.effectBitmap, &bitmapGlyph->bitmap, sizeof(FT_Bitmap));

			desc.baseline = baseline;
			desc.totalHeight = baseline + desc.offsetY + desc.height;

			desc.effectData.insert(desc.effectData.end(), bitmapGlyph->bitmap.buffer, bitmapGlyph->bitmap.buffer + (bitmapGlyph->bitmap.width * bitmapGlyph->bitmap.rows));

			//FT_Done_Glyph(glyph);

			FT_Glyph effectGlyph;
			FT_Get_Glyph(face->glyph, &effectGlyph);
			//FT_Glyph_StrokeBorder(&effectGlyph, _stroker, false, true);
			//FT_Glyph_Stroke(&effectGlyph, _stroker, /*false,*/ true);
			FT_Glyph_To_Bitmap(&effectGlyph, FT_RENDER_MODE_NORMAL, nullptr, true);			

			bitmapGlyph = reinterpret_cast<FT_BitmapGlyph>(effectGlyph);
			memcpy(&desc.bitmap, &bitmapGlyph->bitmap, sizeof(FT_Bitmap));

			desc.innerWidth = bitmapGlyph->bitmap.width;
			desc.innerHeight = bitmapGlyph->bitmap.rows;

			desc.pixelData.insert(desc.pixelData.end(), bitmapGlyph->bitmap.buffer, bitmapGlyph->bitmap.buffer + (bitmapGlyph->bitmap.width * bitmapGlyph->bitmap.rows));

			//FT_Done_Glyph(effectGlyph);
#else
			error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
			if (error != 0)
				continue;

			auto& slot = face->glyph;

			GlyphDescFT desc;
			desc.ch = ch;
			desc.offsetX = slot->bitmap_left;
			desc.offsetY = -slot->bitmap_top;
			desc.width = slot->bitmap.width;
			desc.height = slot->bitmap.rows;

			desc.pitch = slot->bitmap.pitch;
			desc.advanceX = slot->advance.x;
			desc.advanceY = slot->advance.y;
			memcpy(&desc.bitmap, &slot->bitmap, sizeof(FT_Bitmap));

			desc.baseline = baseline;
			desc.totalHeight = baseline + desc.offsetY + desc.height;

			desc.pixelData.insert(desc.pixelData.end(), slot->bitmap.buffer, slot->bitmap.buffer + (slot->bitmap.width * slot->bitmap.rows));
#endif

			glyphs.push_back(desc);



			//p += (desc.width * desc.height);
		}
	}

	const int32_t ExtraSize = 0;

	// now work out the atlas size that will fit
	bool didAtlasFit = false;
	while (!didAtlasFit)
	{
		bool didFit = true;
		_maxCharHeight = 0;

		for (auto& glyph : glyphs)
		{
			// calculate the max char height				
			if (glyph.totalHeight > _maxCharHeight)
				_maxCharHeight = glyph.totalHeight;

			if (x + glyph.width + ExtraSize >= currentAtlasSize)
			{
				uint32_t delta = currentAtlasSize - x;

				y += _maxCharHeight + 1 /*+ ExtraSize*/;
				x = 0;

				// we filled up the atlas, so double the size and start again
				if (y >= currentAtlasSize)
				{
					currentAtlasSize += AtlasSize;
					didFit = false;
					x = 0;
					y = 0;
					break;
				}
			}

			// we filled up the atlas, so double the size and start again
			if (y + _maxCharHeight >= currentAtlasSize)
			{
				currentAtlasSize += AtlasSize;
				didFit = false;
				x = 0;
				y = 0;
				break;
			}

			//int32_t effectOffsetX = (glyph.width - glyph.innerWidth) / 2;
			//int32_t effectOffsetY = (glyph.height - glyph.innerHeight) / 2;

			x += glyph.width + 1 + ExtraSize;
		}

		if (didFit)
			didAtlasFit = true;
	}

	_maxCharHeight = 0;

	uint32_t* pixelData = new uint32_t[currentAtlasSize * currentAtlasSize];
	memset(pixelData, 0, sizeof(uint32_t) * (currentAtlasSize * currentAtlasSize));

	uint32_t* p = pixelData;
	//uint32_t* lastOffset = p;

	x = 0;
	y = 0;

	// now for each character, render it into the bitmap

	for (auto& glyph : glyphs)
	{
		// calculate the max char height				
		if (glyph.totalHeight > _maxCharHeight)
			_maxCharHeight = glyph.totalHeight;

		if (x + glyph.width + ExtraSize >= currentAtlasSize)
		{
			uint32_t delta = currentAtlasSize - x;

			p += delta;
			p += currentAtlasSize * (_maxCharHeight);

			y += _maxCharHeight + 1 /*+ ExtraSize*/;
			x = 0;

			//_maxCharHeight = 0;
		}

		// calculate the rect
		glyph.atlasRect.left = x;
		glyph.atlasRect.right = glyph.atlasRect.left + glyph.width;
		glyph.atlasRect.top = y;// +(glyph.baseline + glyph.offsetY);
		glyph.atlasRect.bottom = glyph.atlasRect.top + glyph.totalHeight;

		// calculate UV
		glyph.uv0[0] = (float)glyph.atlasRect.left / currentAtlasSize;
		glyph.uv0[1] = (float)glyph.atlasRect.top / currentAtlasSize;
		glyph.uv1[0] = (float)glyph.atlasRect.right / currentAtlasSize;
		glyph.uv1[1] = (float)glyph.atlasRect.bottom / currentAtlasSize;

		/*if (size == 19 && (glyph.ch == 'e' || glyph.ch == 'o'))
		{
			LOG_DEBUG("")
		}*/

		//DrawGlyphToFontSheet(glyph.offsetX, baseline + glyph.offsetY, glyph.width, glyph.height, glyph, &glyph.effectBitmap, p, currentAtlasSize, glyph.effectData.data(), 0, 0, 0);

		int32_t effectOffsetX = (glyph.width - glyph.innerWidth) / 2;
		int32_t effectOffsetY = (glyph.height - glyph.innerHeight) / 2;

		//DrawGlyphToFontSheet(glyph.offsetX + 1, baseline + glyph.offsetY + 1, glyph.innerWidth, glyph.innerHeight, glyph, &glyph.bitmap, p, currentAtlasSize, glyph.effectData.data(), 0, 0, 0);

		DrawGlyphToFontSheet(glyph.offsetX /*+ effectOffsetX*/, baseline + glyph.offsetY /*+ effectOffsetY*/, glyph.innerWidth, glyph.innerHeight, glyph, &glyph.bitmap, p, currentAtlasSize, glyph.pixelData.data(), 255, 255, 255);

		auto w = glyph.width + 1 + ExtraSize;
		p += w;
		x += w;
	}



	D3D11_SUBRESOURCE_DATA initialData;
	initialData.pSysMem = pixelData;
	initialData.SysMemPitch = currentAtlasSize * 4;
	initialData.SysMemSlicePitch = 0;

	// Create the texture atlas with initial pixel data
	auto atlas = g_pEnv->_graphicsDevice->CreateTexture2D(
		currentAtlasSize,
		currentAtlasSize,
		DXGI_FORMAT_R8G8B8A8_UNORM,
		1,
		D3D11_BIND_SHADER_RESOURCE,
		0,
		1,
		0,
		&initialData,
		(D3D11_CPU_ACCESS_FLAG)0,
		D3D11_RTV_DIMENSION_UNKNOWN,
		D3D11_UAV_DIMENSION_UNKNOWN,
		D3D11_SRV_DIMENSION_TEXTURE2D);

	if (!atlas)
		return false;

	FreeTypeFont::SizedAtlas sizedAtlas;

	sizedAtlas.atlas = atlas;
	sizedAtlas.glyphs = glyphs;
	sizedAtlas.face = face;
	sizedAtlas.baseLine = baseline;

	font->_sizedGlyphs.insert({ size, sizedAtlas });

#if 0//def _DEBUG
	std::string atlasPath = "FontAtlas_";
	atlasPath += font->_face;
	atlasPath += "_";
	atlasPath += std::to_string(size);
	atlasPath += ".png";
	atlas->SaveToFile(atlasPath);
#endif	

	SAFE_DELETE_ARRAY(pixelData);

	font->CreateInstanceBuffer(size);

	return true;
}

void FreeTypeImporter::DrawGlyphToFontSheet(int32_t xoffset, int32_t yoffset, int32_t width, int32_t height, GlyphDesc& glyph, FT_Bitmap* bm, uint32_t* data, uint32_t atlasSize, const uint8_t* src, uint8_t r, uint8_t g, uint8_t b)
{
	//const uint8_t* src = glyph.pixelData.data();
	const uint32_t src_pitch = bm->pitch;

	if (xoffset > 0)
		data += xoffset;

	if (yoffset > 0)
		data += atlasSize * yoffset;

	switch (bm->pixel_mode)
	{
	case FT_PIXEL_MODE_GRAY: // Grayscale image, 1 byte per pixel.
	{
		for (int32_t y = 0; y < height; y++, data += atlasSize, src += src_pitch)
		{
			for (int32_t x = 0; x < width; x++)
			{
				//if ((BYTE)(data[x] >> 24) == 0)
				/*{
					float t = (float)src[x] / 255.0f;
					data[x] = (uint32_t)std::lerp((float)data[x], (float)(HEX_RGBA(r, g, b, src[x])), 1.0f - t);
				}
				else*/
				{
					data[x] = HEX_RGBA(r, g, b, src[x]);
				}
			}
		}
		break;
	}
	default:
		LOG_CRIT("NOT SUPPORTED PIXEL MODE");
		break;
	}

	if (yoffset > 0)
		data -= atlasSize * yoffset;

}

void FreeTypeImporter::UnloadResource(IResource* resource)
{
	SAFE_DELETE(resource);
}

std::vector<std::string> FreeTypeImporter::GetSupportedResourceExtensions()
{
	return { ".ttf", ".otf", ".ttc"};
}

std::wstring FreeTypeImporter::GetResourceDirectory() const
{
	return L"Fonts";
}