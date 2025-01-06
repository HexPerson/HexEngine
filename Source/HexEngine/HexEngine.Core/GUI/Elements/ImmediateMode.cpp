
#include "ImmediateMode.hpp"

namespace HexEngine
{
	void IMGUI::FloatControl(int32_t x, int32_t y, int32_t w, int32_t h, Style* style, float* value)
	{
		auto renderer = g_pEnv->_uiManager->GetRenderer();

		renderer->FillQuad(x, y, w, h, style->win_back);
		renderer->Frame(x, y, w, h, 1, style->win_border);

		renderer->PrintText(style->font.get(), (uint8_t)Style::FontSize::Tiny, x + w / 2, y + h / 2, style->text_regular, FontAlign::CentreLR | FontAlign::CentreUD, std::format(L"{:.2f}", *value));
	}

	void IMGUI::VectorControl(int32_t x, int32_t y, int32_t w, int32_t h, Style* style, math::Vector3* value)
	{
		FloatControl(x, y, w, h, style, &value->x); x += w + 10;
		FloatControl(x, y, w, h, style, &value->y); x += w + 10;
		FloatControl(x, y, w, h, style, &value->z);
	}
}