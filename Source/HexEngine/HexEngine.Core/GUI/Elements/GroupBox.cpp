
#include "GroupBox.hpp"
#include "../GuiRenderer.hpp"

namespace HexEngine
{
	GroupBox::GroupBox(Element* parent, const Point& position, const Point& size, const std::wstring& label) :
		Element(parent, position, size),
		_label(label)
	{}

	void GroupBox::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		auto pos = Element::GetAbsolutePosition();

		renderer->Line(pos.x, pos.y, pos.x, pos.y + _size.y, renderer->_style.groupbox_border); // left
		renderer->Line(pos.x + _size.x, pos.y, pos.x + _size.x, pos.y + _size.y, renderer->_style.groupbox_border); // right
		renderer->Line(pos.x, pos.y + _size.y, pos.x + _size.x, pos.y + _size.y, renderer->_style.groupbox_border); // bottom

		if (_label.length() > 0)
		{
			renderer->Line(pos.x, pos.y, pos.x + 40, pos.y, renderer->_style.groupbox_border); // left

			int32_t width, height;
			renderer->_style.font->MeasureText((int32_t)Style::FontSize::Tiny, _label, width, height);

			renderer->PrintText(renderer->_style.font, (uint8_t)Style::FontSize::Tiny, pos.x + 42, pos.y, renderer->_style.text_regular, FontAlign::CentreUD, _label);

			renderer->Line(pos.x + width + 44, pos.y, pos.x + _size.x, pos.y, renderer->_style.groupbox_border);
		}
	}

	Point GroupBox::GetAbsolutePosition() const
	{
		auto pos = GetPosition();

		// add some offset for the label
		pos.y += 10;

		if (Element* parent = GetParent(); parent != nullptr)
		{
			pos += parent->GetAbsolutePosition();
		}

		return pos;
	}
}