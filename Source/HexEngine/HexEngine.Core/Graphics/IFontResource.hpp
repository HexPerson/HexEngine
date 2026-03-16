

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

	enum class TextEffectFlags : uint8_t
	{
		None = 0,
		Shadow = HEX_BITSET(0),
		Outline = HEX_BITSET(1),
		Glow = HEX_BITSET(2)
	};

	struct TextEffectSettings
	{
		TextEffectFlags flags = TextEffectFlags::None;
		math::Color shadowColour = math::Color(0.0f, 0.0f, 0.0f, 0.75f);
		int32_t shadowOffsetX = 1;
		int32_t shadowOffsetY = 1;
		math::Color outlineColour = math::Color(0.0f, 0.0f, 0.0f, 1.0f);
		int32_t outlineThickness = 1;
		math::Color glowColour = math::Color(1.0f, 0.0f, 0.0f, 0.2f);
		int32_t glowRadius = 2;

		bool HasEffects() const
		{
			return flags != TextEffectFlags::None;
		}

		static TextEffectSettings Shadow()
		{
			TextEffectSettings shadow;
			shadow.flags = TextEffectFlags::Shadow;
			shadow.shadowOffsetX = shadow.shadowOffsetY = 2;
			return shadow;
		}

		static TextEffectSettings Glow()
		{
			TextEffectSettings glow;
			glow.flags = TextEffectFlags::Glow;
			glow.glowRadius = 3;
			return glow;
		}
	};

	DEFINE_ENUM_FLAG_OPERATORS(FontAlign);
	DEFINE_ENUM_FLAG_OPERATORS(TextEffectFlags);

	class HEX_API IFontResource : public IResource
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

		static std::shared_ptr<IFontResource> Create(const fs::path& path, FontImportOptions* options);
	};
}
