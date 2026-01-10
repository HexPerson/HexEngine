
#include "DragInt.hpp"
#include "../GuiRenderer.hpp"

namespace HexEngine
{
	DragInt::DragInt(Element* parent, const Point& position, const Point& size, const std::wstring& label, int32_t* value, int32_t min, int32_t max, int32_t scale) :
		LineEdit(parent, position, size, label),
		_value(value),
		_min(min),
		_max(max),
		_scale(scale),
		_lastValue(*_value)
	{		
		SetValue(std::format(L"{:d}", *_value)); 
	}

	void DragInt::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		LineEdit::Render(renderer, w, h);

		if (_dragging)
		{
			int32_t mx, my;
			g_pEnv->_inputSystem->GetMousePosition(mx, my);

			int32_t delta = mx - _startDrag;

			if (delta != 0)
			{

				*_value += delta * _scale;

				*_value = std::clamp(*_value, _min, _max);

				if (_lastValue != *_value)
				{
					SetValue(std::format(L"{:d}", *_value));

					if (_onDrag)
						_onDrag(_value, _min, _max);
				}

				SetCursor(LoadCursor(nullptr, MAKEINTRESOURCE(IDC_SIZEWE)));

				_startDrag = mx;
			}
		}
		else
		{
			SetCursor(LoadCursor(nullptr, MAKEINTRESOURCE(IDC_ARROW)));
		}
	}

	void DragInt::SetOnDrag(OnDrag fn)
	{
		_onDrag = fn;
	}

	bool DragInt::OnInputEvent(InputEvent event, InputData* data)
	{
		if (event == InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON && _hovering)
		{
			if (_startDrag == -1)
			{
				_startDrag = data->MouseDown.xpos;
			}
		}
		else if (event == InputEvent::MouseMove && _startDrag != -1)
		{
			if (abs(_startDrag - data->MouseDown.xpos) > 0 && _dragging == false)
			{
				_dragging = true;
				SetHasInputFocus(false);
				return true;
			}
		}
		else if (event == InputEvent::MouseUp && data->MouseUp.button == VK_LBUTTON)
		{
			_dragging = false;
			_startDrag = -1;

			return true;
		}

		return LineEdit::OnInputEvent(event, data);
	}
}