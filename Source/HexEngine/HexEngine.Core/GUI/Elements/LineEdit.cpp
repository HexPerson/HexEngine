
#include "LineEdit.hpp"
#include "../UIManager.hpp"
#include "../../Graphics/IFontResource.hpp"
#include "../GuiRenderer.hpp"
#include <algorithm>

namespace HexEngine
{
	LineEdit::LineEdit(Element* parent, const Point& position, const Point& size, const std::wstring& label) :
		_label(label),
		Element(parent, position, size)
	{}

	void LineEdit::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		auto position = GetAbsolutePosition();

		int32_t labelWidth = 0, labelHeight = 0;
		if (_label.length() > 0)
			renderer->_style.font->MeasureText((uint8_t)Style::FontSize::Tiny, _label, labelWidth, labelHeight);

		int32_t prefixWidth = 0, prefixHeight = 0;
		if (_uneditableText.length() > 0)
			renderer->_style.font->MeasureText((uint8_t)Style::FontSize::Tiny, _uneditableText, prefixWidth, prefixHeight);

		int32_t minSize = _labelMinSize > 0 ? _labelMinSize : labelWidth + 20;
		_cachedLabelWidth = minSize;

		if (_label.length() == 0)
			minSize = 0;

		if ((IsMouseOver(true) || _hasInputFocus) && _label.length() > 0)
			renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, position.x, position.y + _size.y / 2, renderer->_style.text_highlight, FontAlign::CentreUD, _label);
		else
			renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, position.x, position.y + _size.y / 2, renderer->_style.text_regular, FontAlign::CentreUD, _label);

		int32_t boxX = position.x + minSize;
		int32_t boxWidth = std::max(0, _size.x - minSize);
		renderer->FillQuad(boxX, position.y, boxWidth, _size.y, renderer->_style.lineedit_back);
		renderer->Frame(boxX, position.y, boxWidth, _size.y, 1, renderer->_style.win_border);

		_hovering = IsMouseOver(boxX, position.y, boxWidth, _size.y);

		int32_t textX = boxX + 4;
		int32_t textAvailableWidth = std::max(0, boxWidth - 8);

		if (_icon)
		{
			int32_t iconSize = _size.y - 3;
			renderer->FillTexturedQuad(_icon.get(), textX, position.y + 3, iconSize, iconSize, _iconColour);
			textX += iconSize + 4;
			textAvailableWidth = std::max(0, textAvailableWidth - (iconSize + 4));
		}

		std::wstring visibleValue = _value;
		int32_t valueWidth = 0, valueHeight = 0;
		const int32_t prefixGap = _uneditableText.empty() ? 0 : 6;
		const int32_t valueAvailableWidth = std::max(0, textAvailableWidth - prefixWidth - prefixGap);

		while (!visibleValue.empty())
		{
			renderer->_style.font->MeasureText((uint8_t)Style::FontSize::Tiny, visibleValue, valueWidth, valueHeight);
			if (valueWidth + 2 < valueAvailableWidth)
				break;

			visibleValue.erase(visibleValue.begin());
		}

		if (visibleValue.empty())
		{
			valueWidth = 0;
		}

		if (_uneditableText.length() > 0)
		{
			renderer->PrintText(
				renderer->_style.font.get(),
				(uint8_t)Style::FontSize::Tiny,
				textX,
				position.y + _size.y / 2,
				math::Color(HEX_RGBA_TO_FLOAT4(118, 118, 120, 255)),
				FontAlign::CentreUD,
				_uneditableText);
		}

		renderer->PrintText(
			renderer->_style.font.get(),
			(uint8_t)Style::FontSize::Tiny,
			textX + prefixWidth + prefixGap,
			position.y + _size.y / 2,
			renderer->_style.text_regular,
			FontAlign::CentreUD,
			visibleValue);

		if (_hasInputFocus)
		{
			renderer->FillQuad(textX + prefixWidth + prefixGap + valueWidth, position.y + 2, 2, _size.y - 4, renderer->_style.text_regular);
		}

	}

	void LineEdit::SetValue(const std::wstring& value)
	{
		_value = value;
	}

	const std::wstring& LineEdit::GetValue() const
	{
		return _value;
	}

	/*int32_t LineEdit::GetLabelWidth() const
	{
		return _labelWidth;
	}*/

	void LineEdit::SetDoesCallbackWaitForReturn(bool doesWait)
	{
		_doesCallbackWaitForReturn = doesWait;
	}

	void LineEdit::SetUneditableText(const std::wstring& text)
	{
		_uneditableText = text;
	}

	void LineEdit::SetOnDoubleClickFn(OnDoubleClickFn fn)
	{
		_onDoubleClickFn = fn;
	}

	bool LineEdit::OnInputEvent(InputEvent event, InputData* data)
	{
		if (Element::OnInputEvent(event, data))
			return true;

		if (event == InputEvent::MouseDown)
		{
			if (IsMouseOver(true) && data->MouseDown.button == VK_LBUTTON)
			{
				g_pEnv->GetUIManager().SetInputFocus(this);
				return true;
			}
			else
				SetHasInputFocus(false);
			
		}
		if (event == InputEvent::MouseDoubleClick && data->MouseDown.button == VK_LBUTTON)
		{
			if (IsMouseOver(true))
			{
				if (_onDoubleClickFn)
					_onDoubleClickFn(this, GetValue());

				return true;
			}
		}
		else if (event == InputEvent::KeyDown && _hasInputFocus)
		{
			if (data->KeyDown.key == VK_RETURN)
			{
				_hasInputFocus = false;

				if (_onInputFn)
				{
					_onInputFn(this, _value);
				}

				return true;
			}
		}
		else if (event == InputEvent::Char && _hasInputFocus)
		{
			if (data->Char.ch == VK_BACK)
			{
				if (_value.length() > 0)
					_value.pop_back();
			}
			else if (data->Char.ch == VK_RETURN)
			{
				_hasInputFocus = false;

				if (_onInputFn && _doesCallbackWaitForReturn)
				{
					_onInputFn(this, _value);
				}
			}
			else if (data->Char.ch == VK_TAB)
			{
				// TODO add auto pick next input
			}
			else
				_value.push_back(data->Char.ch);

			if (_onInputFn && _doesCallbackWaitForReturn == false)
			{
				_onInputFn(this, _value);
			}

			return true;
		}
		else if (event == InputEvent::DragAndDrop)
		{
			if (_onDragAndDropFn && IsMouseOver(true))
				_onDragAndDropFn(this, data->DragAndDrop.path);
		}

		return false;
	}

	void LineEdit::SetOnInputFn(OnSetInputFn fn)
	{
		_onInputFn = fn;
	}

	void LineEdit::SetOnDragAndDropFn(OnDragAndDropFn fn)
	{
		_onDragAndDropFn = fn;
	}

	int32_t LineEdit::GetLabelWidth() const
	{
		int32_t width = 0, height = 0;

		if (_label.length() > 0)
			g_pEnv->GetUIManager().GetRenderer()->_style.font->MeasureText((int32_t)Style::FontSize::Tiny, _label, width, height);

		return width;
	}

	std::wstring LineEdit::GetLabelText() const
	{
		return _label;
	}

	void LineEdit::SetIcon(const std::shared_ptr<ITexture2D>& icon, const math::Color& colour)
	{
		_icon = icon;
		_iconColour = colour;
	}
}
