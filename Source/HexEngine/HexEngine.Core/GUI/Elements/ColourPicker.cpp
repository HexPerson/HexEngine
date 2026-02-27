
#include "ColourPicker.hpp"

namespace HexEngine
{
	ColourPicker::ColourPicker(Element* parent, const Point& position, const Point& size, const std::wstring& label, math::Color* col) :
		Dialog(parent, position, Point(300, 300), L"Colour Picker"),
		_colour(col)
	{}

	void ColourPicker::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		Dialog::Render(renderer, w, h);

		const auto& pos = GetAbsolutePosition();

		if (_colour)
		{
			renderer->FillQuad(pos.x + 20, pos.y + 20, _size.x - 40, _size.y - 40, *_colour);
		}
	}
}