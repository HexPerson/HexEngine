

#pragma once

#include "../FileSystem/IResource.hpp"
#include "../FileSystem/ResourceSystem.hpp"
#include "UIInstance.hpp"

namespace HexEngine
{
	/** @brief Font import/build options used by the font importer plugin. */
	struct FontImportOptions : ResourceLoadOptions
	{
		/** Default Latin/extended punctuation range. */
		static inline std::vector<std::pair<uint32_t, uint32_t>> EnglishCharacterSets = { { 0x20, 0x7E }, {0xA1, 0xAF} };
		/** Default simplified Chinese unicode range. */
		static inline std::vector<std::pair<uint32_t, uint32_t>> SimplifiedChineseCharacterSets = { { 0x4E00, 0x9FFF} };

		/**
		 * @brief Appends a unicode range set to import.
		 * @param set Inclusive unicode ranges in the form {start, end}.
		 */
		void AddCharacterSet(const std::vector<std::pair<uint32_t, uint32_t>>& set)
		{
			charRanges.insert(charRanges.end(), set.begin(), set.end());
		}

		/** Requested pixel font sizes to bake. */
		std::vector<uint8_t> sizes;
		/** Target DPI used during glyph rasterization. */
		uint32_t dpi = 72;
		/** Enables antialiasing during glyph rasterization. */
		bool antialias = true;
		/** Unicode ranges to include in the baked atlas. */
		std::vector<std::pair<uint32_t, uint32_t>> charRanges;
	};

	/** @brief Glyph metadata and atlas placement information for a single character. */
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

	/** @brief Text alignment flags used by UI text rendering helpers. */
	enum FontAlign : uint8_t
	{
		None,
		CentreLR = HEX_BITSET(0),
		CentreUD = HEX_BITSET(1),
		Right	 = HEX_BITSET(2)
	};

	/** @brief Optional effects that can be layered when rendering text. */
	enum class TextEffectFlags : uint8_t
	{
		None = 0,
		Shadow = HEX_BITSET(0),
		Outline = HEX_BITSET(1),
		Glow = HEX_BITSET(2)
	};

	/** @brief Configurable parameters for text shadow/outline/glow rendering. */
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

	/** @brief Font resource abstraction used by UI and text rendering systems. */
	class HEX_API IFontResource : public IResource
	{
	public:
		/**
		 * @brief Checks whether a baked atlas exists for the requested size.
		 * @param size Font size in pixels.
		 * @return True if this size is available.
		 */
		virtual bool HasFontSize(int32_t size) { return false; }

		/**
		 * @brief Returns glyph metrics/atlas data for a character at the given size.
		 * @param size Font size in pixels.
		 * @param ch Unicode character.
		 */
		virtual GlyphDesc* GetGlyphFromChar(int32_t size, wchar_t ch) = 0;

		/**
		 * @brief Returns the texture atlas that stores glyph bitmaps for a size.
		 * @param size Font size in pixels.
		 */
		virtual ITexture2D* GetAtlas(int32_t size) = 0;

		/**
		 * @brief Measures the pixel bounds of an ASCII/UTF-8 string.
		 * @param fontSize Font size in pixels.
		 * @param text Input text.
		 * @param width Output width in pixels.
		 * @param height Output height in pixels.
		 */
		virtual void MeasureText(int32_t fontSize, const std::string& text, int32_t& width, int32_t& height) = 0;

		/**
		 * @brief Measures the pixel bounds of a wide-character string.
		 * @param fontSize Font size in pixels.
		 * @param text Input text.
		 * @param width Output width in pixels.
		 * @param height Output height in pixels.
		 */
		virtual void MeasureText(int32_t fontSize, const std::wstring& text, int32_t& width, int32_t& height) = 0;

		/**
		 * @brief Returns horizontal kerning offset between two glyphs.
		 * @param size Font size in pixels.
		 * @param first Left character.
		 * @param second Right character.
		 */
		virtual int32_t GetKerning(int32_t size, wchar_t first, wchar_t second) = 0;

		/** @brief Allocates per-instance UI draw data for the given font size. */
		virtual void CreateInstanceBuffer(int32_t size) = 0;

		/** @brief Checks whether an instance buffer already exists for this size. */
		virtual bool HasInstanceBuffer(int32_t size) const = 0;

		/** @brief Returns the UI instance buffer used to submit glyph quads for this size. */
		virtual UIInstance* GetInstanceBuffer(int32_t size) const = 0;

		/**
		 * @brief Loads/creates a font resource.
		 * @param path Source font path.
		 * @param options Optional import options.
		 */
		static std::shared_ptr<IFontResource> Create(const fs::path& path, FontImportOptions* options);
	};
}
