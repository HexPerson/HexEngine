
#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_STROKER_H

struct GlyphDescFT : GlyphDesc
{
	FT_Bitmap bitmap;
	FT_Bitmap effectBitmap;
};

class FreeTypeFont : public IFontResource
{
public:
	struct SizedAtlas
	{
		int32_t baseLine = 0;
		FT_Face face = 0;
		ITexture2D* atlas;
		std::vector<GlyphDescFT> glyphs;
		IVertexBuffer* vbuffer = nullptr;
		IIndexBuffer* ibuffer = nullptr;
		UIInstance* instance = nullptr;
	};
	FreeTypeFont();

	virtual void Destroy() override;

	virtual bool HasFontSize(int32_t size) override;

	virtual GlyphDesc* GetGlyphFromChar(int32_t size, wchar_t ch) override;

	virtual ITexture2D* GetAtlas(int32_t size) override;

	virtual void MeasureText(int32_t fontSize, const std::string& text, int32_t& width, int32_t& height) override;

	virtual void MeasureText(int32_t fontSize, const std::wstring& text, int32_t& width, int32_t& height) override;

	virtual int32_t GetKerning(int32_t size, wchar_t first, wchar_t second) override;

	virtual void CreateInstanceBuffer(int32_t size) override;

	virtual bool HasInstanceBuffer(int32_t size) const override;

	virtual UIInstance* GetInstanceBuffer(int32_t size) const override;

public:	
	std::unordered_map<int32_t, SizedAtlas> _sizedGlyphs;
	std::string _face;
	
	
};
