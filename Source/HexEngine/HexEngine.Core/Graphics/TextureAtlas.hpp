

#pragma once

#include "ITexture2D.hpp"

namespace HexEngine
{
	class TextureAtlas
	{
	public:
		struct Item
		{
			ITexture2D* texture;
			int32_t x;
			int32_t y;
			int32_t width;
			int32_t height;
			math::Vector2 uv[2];
		};

		TextureAtlas();

		void AddTexture(ITexture2D* texture);

		void Pack();

		void Clear();

		bool IsInAtlas(ITexture2D* texture) const;

		const Item* GetTexureItemFromAtlas(ITexture2D* texture) const;

		ITexture2D* GetAtlasTexture() const;

		int32_t GetTotalWidth() const;
		int32_t GetTotalHeight() const;

	private:
		std::vector<Item> _items;
		int32_t _totalWidth;
		int32_t _totalHeight;
		ITexture2D* _atlas;
	};
}
