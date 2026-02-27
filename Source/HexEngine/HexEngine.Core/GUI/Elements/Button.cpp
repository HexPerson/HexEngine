
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
		auto centre = pos.GetCenter(_size);

		bool hovering = IsMouseOver(true) && IsInputEnabled();

		if(hovering)
			renderer->FillQuadVerticalGradient(pos.x, pos.y, _size.x, _size.y, _hasHighlightOverride ? _highlightOverride : renderer->_style.button_hover, renderer->_style.button_back2);
		else
			renderer->FillQuadVerticalGradient(pos.x, pos.y, _size.x, _size.y, renderer->_style.button_back, renderer->_style.button_back2);

		renderer->Frame(pos.x, pos.y, _size.x, _size.y, 1, renderer->_style.button_border);

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
			renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Small, centre.x, centre.y, hovering ? renderer->_style.button_hover_text : renderer->_style.text_regular, FontAlign::CentreLR | FontAlign::CentreUD, _label);
		}
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