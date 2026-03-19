
#include "Style.hpp"
#include "../../Graphics/ITexture2D.hpp"
#include "../../Graphics/IFontResource.hpp"
#include "../../Environment/IEnvironment.hpp"

namespace HexEngine
{
	void Style::CreateDefaultStyle(Style& style)
	{
		style.win_back = math::Color(HEX_RGBA_TO_FLOAT4(44, 44, 45, 255));
		style.win_back2 = math::Color(HEX_RGBA_TO_FLOAT4(34, 34, 35, 255));
		style.win_render_area = math::Color(HEX_RGBA_TO_FLOAT4(64, 64, 65, 255));
		style.win_border = math::Color(HEX_RGBA_TO_FLOAT4(1, 2, 3, 255));
		style.win_title = math::Color(1, 1, 1, 1);
		style.win_title_height = 24;
		style.win_title_fillstyle = FillStyle::VerticalGradient;
		style.win_title_colour1 = math::Color(HEX_RGBA_TO_FLOAT4(35, 35, 36, 255));
		style.win_highlight = math::Color(HEX_RGBA_TO_FLOAT4(200, 200, 200, 255));

		style.text_regular = math::Color(HEX_RGBA_TO_FLOAT4(210, 210, 210, 255));
		style.text_highlight = math::Color(HEX_RGBA_TO_FLOAT4(1, 85, 219, 255));

		style.lineedit_back = math::Color(HEX_RGBA_TO_FLOAT4(25, 25, 26, 255));

		style.button_back = math::Color(HEX_RGBA_TO_FLOAT4(54, 54, 55, 255));
		style.button_back2 = math::Color(HEX_RGBA_TO_FLOAT4(44, 44, 45, 255));
		style.button_border = math::Color(HEX_RGBA_TO_FLOAT4(1, 2, 3, 255));
		style.button_hover = math::Color(HEX_RGBA_TO_FLOAT4(1, 85, 219, 255));
		style.button_hover_text = math::Color(HEX_RGBA_TO_FLOAT4(1, 2, 3, 255));

		style.listbox_back = math::Color(HEX_RGBA_TO_FLOAT4(35, 35, 36, 255));
		style.listbox_border = math::Color(HEX_RGBA_TO_FLOAT4(1, 2, 3, 255));
		style.listbox_alternate_colour = math::Color(HEX_RGBA_TO_FLOAT4(40, 41, 42, 255));
		style.listbox_highlight = math::Color(HEX_RGBA_TO_FLOAT4(66, 66, 66, 255));

		style.groupbox_border = math::Color(HEX_RGBA_TO_FLOAT4(21, 22, 23, 255));

		style.tabview_back = math::Color(HEX_RGBA_TO_FLOAT4(35, 35, 36, 255));
		style.tabview_tab_highlight = math::Color(HEX_RGBA_TO_FLOAT4(1, 85, 219, 255));
		style.tabview_tab_back = math::Color(HEX_RGBA_TO_FLOAT4(44, 44, 45, 255));
		style.tabview_border = math::Color(HEX_RGBA_TO_FLOAT4(1, 2, 3, 255));
		style.tabview_text_highlight = math::Color(HEX_RGBA_TO_FLOAT4(1, 2, 3, 255));

		style.img_win_close = ITexture2D::Create("EngineData.Textures/UI/Close.png");
		style.img_folder_closed = ITexture2D::Create("EngineData.Textures/UI/folder_closed.png");
		style.img_folder_open = ITexture2D::Create("EngineData.Textures/UI/folder_open.png");

		style.inspector_widget_back = math::Color(HEX_RGBA_TO_FLOAT4(65, 65, 66, 255));

		style.context_back = math::Color(HEX_RGBA_TO_FLOAT4(55, 56, 57, 255));
		style.context_highlight = math::Color(HEX_RGBA_TO_FLOAT4(1, 85, 219, 255));

		style.tab_height = 20;

#if 1
		FontImportOptions fontOpts;
		fontOpts.antialias = true;
		fontOpts.dpi = 72;
		fontOpts.sizes.insert(fontOpts.sizes.end(), {
			(uint8_t)FontSize::Microscopic,
			(uint8_t)FontSize::Minute,
			(uint8_t)FontSize::Titchy,
			(uint8_t)FontSize::Tiny,
			(uint8_t)FontSize::Small,
			(uint8_t)FontSize::Regular,
			(uint8_t)FontSize::Large,
			(uint8_t)FontSize::VeryLarge,
			(uint8_t)FontSize::Huge
		});

		fontOpts.AddCharacterSet(FontImportOptions::EnglishCharacterSets);

		style.font = IFontResource::Create("EngineData.Fonts/Arial/arial.ttf", &fontOpts);

#else

		FontImportOptions fontOpts;
		fontOpts.antialias = true;
		fontOpts.dpi = 72;
		fontOpts.sizes.insert(fontOpts.sizes.end(), {
			(uint8_t)FontSize::Microscopic,
			(uint8_t)FontSize::Minute,
			(uint8_t)FontSize::Titchy,
			(uint8_t)FontSize::Tiny,
			(uint8_t)FontSize::Small,
			(uint8_t)FontSize::Regular,
			(uint8_t)FontSize::Large,
			(uint8_t)FontSize::VeryLarge,
			(uint8_t)FontSize::Huge
			});

		fontOpts.AddCharacterSet(FontImportOptions::SimplifiedChineseCharacterSets);

		//style.font = (IFontResource*)g_pEnv->_resourceSystem->LoadResource("Fonts/Inter/Inter-Regular.ttf", &fontOpts);

		style.font = (IFontResource*)g_pEnv->_resourceSystem->LoadResource("EngineData.Fonts/YaHei/msyh.ttc", &fontOpts);
#endif
	}

	Style::~Style()
	{

	}
}