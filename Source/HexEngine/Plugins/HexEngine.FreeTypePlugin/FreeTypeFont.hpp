
#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_STROKER_H

struct GlyphDescFT : HexEngine::GlyphDesc
{
	FT_Bitmap bitmap;
	FT_Bitmap effectBitmap;
};

class FreeTypeFont : public HexEngine::IFontResource
{
public:
	struct SizedAtlas
	{
		int32_t baseLine = 0;
		FT_Face face = 0;
		HexEngine::ITexture2D* atlas;
		std::vector<GlyphDescFT> glyphs;
		HexEngine::IVertexBuffer* vbuffer = nullptr;
		HexEngine::IIndexBuffer* ibuffer = nullptr;
		HexEngine::UIInstance* instance = nullptr;
	};
	FreeTypeFont();

	virtual void Destroy() override;

	virtual bool HasFontSize(int32_t size) override;

	virtual HexEngine::GlyphDesc* GetGlyphFromChar(int32_t size, wchar_t ch) override;

	virtual HexEngine::ITexture2D* GetAtlas(int32_t size) override;

	virtual void MeasureText(int32_t fontSize, const std::string& text, int32_t& width, int32_t& height) override;

	virtual void MeasureText(int32_t fontSize, const std::wstring& text, int32_t& width, int32_t& height) override;

	virtual int32_t GetKerning(int32_t size, wchar_t first, wchar_t second) override;

	virtual void CreateInstanceBuffer(int32_t size) override;

	virtual bool HasInstanceBuffer(int32_t size) const override;

	virtual HexEngine::UIInstance* GetInstanceBuffer(int32_t size) const override;

public:	
	std::unordered_map<int32_t, SizedAtlas> _sizedGlyphs;
	std::string _face;
	
	
};
