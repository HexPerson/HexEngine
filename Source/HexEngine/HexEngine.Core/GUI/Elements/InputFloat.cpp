#include "InputFloat.hpp"
#include "../GuiRenderer.hpp"

namespace HexEngine
{
	InputFloat::InputFloat(Element* parent, const Point& position, const Point& size, const std::wstring& label, float* value, float min, float max, float scale, int32_t decimalPlaces) :
		LineEdit(parent, position, size, label),
		_value(value),
		_min(min),
		_max(max),
		_scale(scale)
	{
		if (_value != nullptr)
		{
			*_value = std::clamp(*_value, _min, _max);
			SetValue(std::format(L"{:.{}f}", *_value, decimalPlaces));
		}
	}

	void InputFloat::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		LineEdit::Render(renderer, w, h);

		if (_dragging || _hovering)
			SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
	}

	void InputFloat::SetOnDrag(OnDrag fn)
	{
		_onDrag = fn;
	}

	bool InputFloat::OnInputEvent(InputEvent event, InputData* data)
	{
		if (event == InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON && _hovering)
		{
			if (_startDrag == -1)
			{
				_startDrag = data->MouseDown.xpos;
				_dragging = false;
			}
		}
		else if (event == InputEvent::MouseMove && _startDrag != -1)
		{
			int32_t mouseX = 0;
			if (data->MouseMove.absolute)
			{
				mouseX = static_cast<int32_t>(std::round(data->MouseMove.x));
			}
			else
			{
				int32_t mouseY = 0;
				g_pEnv->_inputSystem->GetMousePosition(mouseX, mouseY);
			}

			const int32_t delta = mouseX - _startDrag;
			if (delta != 0)
			{
				_dragging = true;
				SetHasInputFocus(false);

				if (_value != nullptr)
				{
					const float lastValue = *_value;
					*_value = std::clamp(*_value + static_cast<float>(delta) * _scale, _min, _max);
					SetValue(std::to_wstring(*_value));

					if (_onDrag && lastValue != *_value)
					{
						_onDrag(*_value, _min, _max);
					}
				}

				_startDrag = mouseX;
			}
		}
		else if (event == InputEvent::MouseUp && data->MouseUp.button == VK_LBUTTON && _startDrag != -1)
		{
			_dragging = false;
			_startDrag = -1;
		}

		LineEdit::OnInputEvent(event, data);

		if ((event == InputEvent::Char && data->Char.ch == VK_RETURN) ||
			(event == InputEvent::KeyDown && data->KeyDown.key == VK_RETURN))
		{
			CommitTextValue();
			return true;
		}

		return false;
	}

	void InputFloat::CommitTextValue()
	{
		if (_value == nullptr)
		{
			return;
		}

		const std::wstring value = GetValue();
		if (value.empty())
		{
			return;
		}

		const float lastValue = *_value;
		const float parsed = std::stof(value);
		*_value = std::clamp(parsed, _min, _max);
		SetValue(std::to_wstring(*_value));

		if (_onDrag && lastValue != *_value)
		{
			_onDrag(*_value, _min, _max);
		}
	}
}
