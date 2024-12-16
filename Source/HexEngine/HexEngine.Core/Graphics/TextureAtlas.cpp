
#include "TextureAtlas.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Environment/LogFile.hpp"
#include "IGraphicsDevice.hpp"

#undef min
#undef max
#include <retpack2d\finders_interface.h>

using namespace rectpack2D;

namespace HexEngine
{
	TextureAtlas::TextureAtlas() :
		_totalWidth(0),
		_totalHeight(0),
		_atlas(nullptr)
	{}

	void TextureAtlas::AddTexture(ITexture2D* texture)
	{
		if (!texture)
			return;

		if (IsInAtlas(texture))
			return;

		Item item;
		item.texture = texture;
		item.x = 0;
		item.y = 0;
		item.uv[0] = math::Vector2(0.0f);
		item.uv[1] = math::Vector2(0.0f);
		item.width = texture->GetWidth();
		item.height = texture->GetHeight();

		_items.push_back(item);
	}

	void TextureAtlas::Clear()
	{
		_items.clear();
		SAFE_DELETE(_atlas);
	}

	bool TextureAtlas::IsInAtlas(ITexture2D* texture) const
	{
		for (auto& item : _items)
		{
			if (item.texture == texture)
				return true;
		}
		return false;
	}

	const TextureAtlas::Item* TextureAtlas::GetTexureItemFromAtlas(ITexture2D* texture) const
	{
		for (auto& item : _items)
		{
			if (item.texture == texture)
				return &item;;
		}
		return nullptr;
	}

	int32_t TextureAtlas::GetTotalWidth() const
	{
		return _totalWidth;
	}

	int32_t TextureAtlas::GetTotalHeight() const
	{
		return _totalHeight;
	}

	ITexture2D* TextureAtlas::GetAtlasTexture() const
	{
		return _atlas;
	}

	void TextureAtlas::Pack()
	{
		if (_items.size() == 0)
			return;

		constexpr bool allow_flip = true;
		const auto runtime_flipping_mode = flipping_option::ENABLED;

		/*
			Here, we choose the "empty_spaces" class that the algorithm will use from now on.

			The first template argument is a bool which determines
			if the algorithm will try to flip rectangles to better fit them.

			The second argument is optional and specifies an allocator for the empty spaces.
			The default one just uses a vector to store the spaces.
			You can also pass a "static_empty_spaces<10000>" which will allocate 10000 spaces on the stack,
			possibly improving performance.
		*/

		using spaces_type = rectpack2D::empty_spaces<allow_flip, default_empty_spaces>;

		/*
			rect_xywh or rect_xywhf (see src/rect_structs.h),
			depending on the value of allow_flip.
		*/

		using rect_type = output_rect_t<spaces_type>;

		/*
			Note:

			The multiple-bin functionality was removed.
			This means that it is now up to you what is to be done with unsuccessful insertions.
			You may initialize another search when this happens.
		*/

		auto report_successful = [](rect_type&) {
			return callback_result::CONTINUE_PACKING;
		};

		auto report_unsuccessful = [](rect_type&) {
			return callback_result::ABORT_PACKING;
		};

		/*
			Initial size for the bin, from which the search begins.
			The result can only be smaller - if it cannot, the algorithm will gracefully fail.
		*/

		const auto max_side = 8192;

		/*
			The search stops when the bin was successfully inserted into,
			AND the next candidate bin size differs from the last successful one by *less* then discard_step.

			The best possible granuarity is achieved with discard_step = 1.
			If you pass a negative discard_step, the algoritm will search with even more granularity -
			E.g. with discard_step = -4, the algoritm will behave as if you passed discard_step = 1,
			but it will make as many as 4 attempts to optimize bins down to the single pixel.

			Since discard_step = 0 does not make sense, the algoritm will automatically treat this case
			as if it were passed a discard_step = 1.

			For common applications, a discard_step = 1 or even discard_step = 128
			should yield really good packings while being very performant.
			If you are dealing with very small rectangles specifically,
			it might be a good idea to make this value negative.

			See the algorithm section of README for more information.
		*/

		const auto discard_step = 128;// -4;

		/*
			Create some arbitrary rectangles.
			Every subsequent call to the packer library will only read the widths and heights that we now specify,
			and always overwrite the x and y coordinates with calculated results.
		*/

		std::vector<rect_type> rectangles;

		for (auto& item : _items)
		{
			rectangles.emplace_back(rect_xywh(0, 0, item.width, item.height));
		}

		auto report_result = [&rectangles,this](const rect_wh& result_size)
		{
			//LOG_DEBUG("Resultant bin: %dx%d", result_size.w, result_size.h);

			_totalWidth = result_size.w;
			_totalHeight = result_size.h;

			for(auto i = 0ul; i < rectangles.size(); ++i)
			{
				auto& r = rectangles[i];

				std::cout << r.x << " " << r.y << " " << r.w << " " << r.h << std::endl;

				auto& item = _items[i];

				item.x = r.x;
				item.y = r.y;

				item.uv[0] = math::Vector2((float)item.x / (float)result_size.w, (float)item.y / (float)result_size.h);
				item.uv[1] = math::Vector2(((float)item.x + (float)item.width) / (float)result_size.w, ((float)item.y + (float)item.height) / (float)result_size.h);
			}
		};

		{
			/*
				Example 1: Find best packing with default orders.

				If you pass no comparators whatsoever,
				the standard collection of 6 orders:
				by area, by perimeter, by bigger side, by width, by height and by "pathological multiplier"
				- will be passed by default.
			*/

			const auto result_size = find_best_packing_dont_sort<spaces_type>(
				rectangles,
				make_finder_input(
					max_side,
					discard_step,
					report_successful,
					report_unsuccessful,
					runtime_flipping_mode
				)
				);

			report_result(result_size);
		}

		if (_atlas == nullptr || (_atlas != nullptr && (_totalWidth > _atlas->GetWidth() || _totalHeight > _atlas->GetHeight())))
		{
			SAFE_DELETE(_atlas);

			// Create the texture atlas with initial pixel data
			_atlas = g_pEnv->_graphicsDevice->CreateTexture2D(
				_totalWidth,
				_totalHeight,
				DXGI_FORMAT_R8G8B8A8_UNORM,
				1,
				D3D11_BIND_SHADER_RESOURCE,
				0,
				1,
				0,
				D3D11_RTV_DIMENSION_UNKNOWN,
				D3D11_UAV_DIMENSION_UNKNOWN,
				D3D11_SRV_DIMENSION_TEXTURE2D);
		}

		for (auto& item : _items)
		{
			//LOG_DEBUG("Copying %s to atlas", item.texture->GetPath().string().c_str());

			RECT src;
			src.left = 0;
			src.top = 0;
			src.right = item.width;
			src.bottom = item.height;

			RECT dst;
			dst.left = item.x;
			dst.top = item.y;
			dst.right = item.x + item.width;
			dst.bottom = item.y + item.height;

			item.texture->CopyTo(_atlas, src, dst);
		}
		}
}