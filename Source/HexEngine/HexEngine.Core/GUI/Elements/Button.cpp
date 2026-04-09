
#include "Button.hpp"
#include "../GuiRenderer.hpp"

namespace HexEngine
{
	Button::Button(Element* parent, const Point& position, const Point& size, const std::wstring& label, std::function<bool(Button*)> action) :
		Element(parent, position, size),
		_label(label),
		_action(action)
	{}

	Button::Button(Element* parent, const Point& position, const Point& size, const std::shared_ptr<ITexture2D>& icon, std::function<bool(Button*)> action) :
		Element(parent, position, size),
		_icon(icon),
		_action(action)
	{
	}

	void Button::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		auto pos = GetAbsolutePosition();		

		bool hovering = IsMouseOver(true) && IsInputEnabled();

		if (hovering != _wasHovering)
		{
			_wasHovering = hovering;
			_canvas.Redraw();
		}

		if (_canvas.BeginDraw(renderer, _size.x, _size.y))
		{
			int32_t x = 0;
			int32_t y = 0;

			if (hovering)
				renderer->FillQuad(x, y, _size.x, _size.y, renderer->_style.button_back2);
			else
				renderer->FillQuad(x, y, _size.x, _size.y, renderer->_style.button_back);

			renderer->Frame(x, y, _size.x, _size.y, 1, math::Color(HEX_RGBA_TO_FLOAT4(4, 5, 6, 255)));

			if (hovering)
				renderer->Frame(x + 1, y + 1, _size.x - 2, _size.y - 2, 1, renderer->_style.button_border);

			auto centre = Point().GetCenter(_size);

			if (_icon)
			{
				renderer->FillTexturedQuad(_icon.get(),
					centre.x - (_size.y / 2) + 1,
					centre.y - (_size.y / 2) + 1,
					_size.y - 2,
					_size.y - 2,
					math::Color(0xFFFFFFFF));
			}
			else
			{
				renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Small, centre.x, centre.y, renderer->_style.text_regular, FontAlign::CentreLR | FontAlign::CentreUD, _label);
			}

			_canvas.EndDraw(renderer);
		}

		_canvas.Present(renderer, pos.x, pos.y, _size.x, _size.y);
	}

	void Button::SetIcon(std::shared_ptr<ITexture2D> icon)
	{
		_icon = icon;
	}

	bool Button::OnInputEvent(InputEvent event, InputData* data)
	{
		if (event == InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON)
		{
			if (IsMouseOver(true) && _action && IsInputEnabled())
			{
				if (_action(this))
					return true;
			}
		}
		return false;
	}

	void Button::SetHighlightOverride(const math::Color& colour)
	{
		_highlightOverride = colour;
		_hasHighlightOverride = true;
	}

	void Button::RemoveHighlightOverride()
	{
		_hasHighlightOverride = false;
	}
}