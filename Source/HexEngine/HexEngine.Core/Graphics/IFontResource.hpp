

#pragma once

#include "../FileSystem/IResource.hpp"
#include "../FileSystem/ResourceSystem.hpp"
#include "UIInstance.hpp"

namespace HexEngine
{
	struct FontImportOptions : ResourceLoadOptions
	{
		static inline std::vector<std::pair<uint32_t, uint32_t>> EnglishCharacterSets = { { 0x20, 0x7E }, {0xA1, 0xAF} };
		static inline std::vector<std::pair<uint32_t, uint32_t>> SimplifiedChineseCharacterSets = { { 0x4E00, 0x9FFF} };

		void AddCharacterSet(const std::vector<std::pair<uint32_t, uint32_t>>& set)
		{
			charRanges.insert(charRanges.end(), set.begin(), set.end());
		}

		std::vector<uint8_t> sizes;
		uint32_t dpi = 72;
		bool antialias = true;
		std::vector<std::pair<uint32_t, uint32_t>> charRanges;
	};

	struct GlyphDesc
	{
		int32_t offsetX;
		int32_t offsetY;
		int32_t width;
		int32_t height;
		int32_t innerWidth;
		int32_t innerHeight;
		int32_t totalHeight;
		int32_t pitch;
		int32_t advanceX;
		int32_t advanceY;
		wchar_t ch;
		std::vector<uint8_t> pixelData;
		std::vector<uint8_t> effectData;
		RECT atlasRect;
		float uv0[2];
		float uv1[2];
		int32_t baseline;
	};

	enum FontAlign : uint8_t
	{
		None,
		CentreLR = HEX_BITSET(0),
		CentreUD = HEX_BITSET(1),
		Right	 = HEX_BITSET(2)
	};

	DEFINE_ENUM_FLAG_OPERATORS(FontAlign);

	class IFontResource : public IResource
	{
	public:
		virtual bool HasFontSize(int32_t size) { return false; }

		virtual GlyphDesc* GetGlyphFromChar(int32_t size, wchar_t ch) = 0;

		virtual ITexture2D* GetAtlas(int32_t size) = 0;

		virtual void MeasureText(int32_t fontSize, const std::string& text, int32_t& width, int32_t& height) = 0;

		virtual void MeasureText(int32_t fontSize, const std::wstring& text, int32_t& width, int32_t& height) = 0;

		virtual int32_t GetKerning(int32_t size, wchar_t first, wchar_t second) = 0;

		virtual void CreateInstanceBuffer(int32_t size) = 0;

		virtual bool HasInstanceBuffer(int32_t size) const = 0;

		virtual UIInstance* GetInstanceBuffer(int32_t size) const = 0;
	};
}
