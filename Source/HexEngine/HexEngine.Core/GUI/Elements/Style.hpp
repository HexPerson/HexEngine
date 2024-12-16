
#pragma once

#include "../../Required.hpp"

namespace HexEngine
{
	class ITexture2D;
	class IFontResource;

	class Style
	{
	public:
		enum class FontSize : int8_t
		{
			Microscopic = 5,
			Minute = 8,
			Titchy = 12,
			Tiny = 15,
			Small = 16,
			Regular = 18,
			Large = 21,
			VeryLarge = 26,
			Huge = 32
		};

		enum class FillStyle : int8_t
		{
			BlockFill,
			VerticalGradient,
			HorizontalGradient,
		};

		~Style();

		static void CreateDefaultStyle(Style& style);

		// Window style params
		math::Color win_back;
		math::Color win_back2;
		math::Color win_render_area;
		math::Color win_border;
		math::Color win_title;
		math::Color win_title_colour1; // if using blockfill, this will be used
		math::Color win_highlight;

		math::Color text_regular;
		math::Color text_highlight;

		math::Color lineedit_back;

		math::Color button_back;
		math::Color button_back2;
		math::Color button_border;
		math::Color button_hover;
		math::Color button_hover_text;

		math::Color listbox_back;
		math::Color listbox_border;
		math::Color listbox_alternate_colour;
		math::Color listbox_highlight;

		math::Color groupbox_border;

		math::Color tabview_back;
		math::Color tabview_tab_highlight;
		math::Color tabview_tab_back;
		math::Color tabview_border;
		math::Color tabview_text_highlight;

		math::Color inspector_widget_back;

		math::Color context_back;
		math::Color context_highlight;

		ITexture2D* img_win_close;
		int32_t win_title_height;
		FillStyle win_title_fillstyle;
		IFontResource* font;
		ITexture2D* img_folder_closed;
		ITexture2D* img_folder_open;

		int32_t tab_height;
		
	};

	DEFINE_ENUM_FLAG_OPERATORS(Style::FontSize);
}
