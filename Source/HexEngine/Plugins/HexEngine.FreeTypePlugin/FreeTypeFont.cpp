
#include "FreeTypeFont.hpp"

FreeTypeFont::FreeTypeFont()
{}

void FreeTypeFont::Destroy()
{
	for (auto& set : _sizedGlyphs)
	{
		auto& glyphSet = set.second;

		SAFE_DELETE(glyphSet.atlas);
		SAFE_DELETE(glyphSet.instance);

		FT_Done_Face(set.second.face);
	}

	_sizedGlyphs.clear();
}

bool FreeTypeFont::HasFontSize(int32_t size)
{
	/*uint32_t screenWidth, screenHeight;
	g_pEnv->GetScreenSize(screenWidth, screenHeight);
	size = RY(size, screenHeight);*/

	for (auto& set : _sizedGlyphs)
	{
		if (set.first == size)
			return true;
	}

	return false;
}

HexEngine::GlyphDesc* FreeTypeFont::GetGlyphFromChar(int32_t size, wchar_t ch)
{
	for (auto&& set : _sizedGlyphs)
	{
		if (set.first == size)
		{
			for (auto&& glyph : set.second.glyphs)
			{
				if (glyph.ch == ch)
					return &glyph;
			}
		}
	}

	return nullptr;
}

HexEngine::ITexture2D* FreeTypeFont::GetAtlas(int32_t size)
{
	if (!HasFontSize(size))
		return nullptr;

	/*uint32_t screenWidth, screenHeight;
	g_pEnv->GetScreenSize(screenWidth, screenHeight);
	size = RY(size, screenHeight);*/

	auto& set = _sizedGlyphs[size];

	return set.atlas;
}

void FreeTypeFont::MeasureText(int32_t fontSize, const std::string& text, int32_t& width, int32_t& height)
{
	MeasureText(fontSize, std::wstring(text.begin(), text.end()), width, height);
}

void FreeTypeFont::MeasureText(int32_t fontSize, const std::wstring& text, int32_t& width, int32_t& height)
{
	width = 0;
	height = 0;

	auto atlas = _sizedGlyphs.find(fontSize);
	if (atlas == _sizedGlyphs.end())
		return;

	const auto& glyphSet = atlas->second;
	const int32_t lineHeight = glyphSet.lineHeight > 0 ? glyphSet.lineHeight : glyphSet.baseLine;

	int32_t largestWidth = 0;
	int32_t currentWidth = 0;
	wchar_t lastCh = 0;
	height = lineHeight;

	for (auto ch : text)
	{
		if (ch == '\n')
		{
			if (currentWidth > largestWidth)
				largestWidth = currentWidth;

			currentWidth = 0;
			lastCh = 0;
			height += lineHeight;
			continue;
		}

		for (const auto& glyph : glyphSet.glyphs)
		{
			if (glyph.ch == ch)
			{
				currentWidth += (glyph.advanceX >> 6);
				lastCh = ch;
				break;
			}
		}
	}

	if (currentWidth > largestWidth)
		largestWidth = currentWidth;

	width = largestWidth;
}

int32_t FreeTypeFont::GetKerning(int32_t size, wchar_t first, wchar_t second)
{
	//return 0;
	int32_t kerning = 0;

	auto face = _sizedGlyphs[size].face;

	if (FT_HAS_KERNING(face) )
	{
		//FT_Set_Pixel_Sizes(_ftFace, 0, size);

		FT_Vector delta;
		FT_UInt prev_index = FT_Get_Char_Index(face, first);
		FT_UInt glyph_index = FT_Get_Char_Index(face, second);
		FT_Get_Kerning(face, prev_index, glyph_index,FT_KERNING_DEFAULT, &delta);

		kerning = delta.x >> 6;
	}

	return kerning;
}

void FreeTypeFont::CreateInstanceBuffer(int32_t size)
{
	auto atlas = _sizedGlyphs.find(size);

	if (atlas == _sizedGlyphs.end())
		return;

	if (atlas->second.instance != nullptr)
		return;

	atlas->second.instance = new HexEngine::UIInstance;
}

bool FreeTypeFont::HasInstanceBuffer(int32_t size) const
{
	return false;
}

HexEngine::UIInstance* FreeTypeFont::GetInstanceBuffer(int32_t size) const
{
	auto atlas = _sizedGlyphs.find(size);

	if (atlas == _sizedGlyphs.end())
		return nullptr;

	return atlas->second.instance;
}