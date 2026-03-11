
#include "DragFloat.hpp"
#include "../GuiRenderer.hpp"

namespace HexEngine
{
	DragFloat::DragFloat(Element* parent, const Point& position, const Point& size, const std::wstring& label, float* value, float min, float max, float scale, int32_t decimalPlaces) :
		LineEdit(parent, position, size, label),
		_value(value),
		_min(min),
		_max(max),
		_scale(scale),
		_lastValue(*_value),
		_decimalPlaces(decimalPlaces)
	{
		switch (decimalPlaces)
		{
		case 0:	SetValue(std::format(L"{:.0f}", *_value)); break;
		case 1:	SetValue(std::format(L"{:.1f}", *_value)); break;
		case 2:	SetValue(std::format(L"{:.2f}", *_value)); break;
		case 3:	SetValue(std::format(L"{:.3f}", *_value)); break;
		case 4:	SetValue(std::format(L"{:.4f}", *_value)); break;
		case 5:	SetValue(std::format(L"{:.5f}", *_value)); break;
		case 6:	SetValue(std::format(L"{:.6f}", *_value)); break;
		default:	SetValue(std::format(L"{:f}", *_value)); break;
		}
	}

	void DragFloat::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		LineEdit::Render(renderer, w, h);

		if (_dragging)
		{
			int32_t mx, my;
			g_pEnv->_inputSystem->GetMousePosition(mx, my);

			int32_t delta = mx - _startDrag;

			if (delta != 0)
			{
				float nextValue = *_value + (float)delta * _scale;				
				nextValue = std::clamp(nextValue, _min, _max);

				if (_lastValue != nextValue)
				{
					switch (_decimalPlaces)
					{
					case 0:	SetValue(std::format(L"{:.0f}", nextValue)); break;
					case 1:	SetValue(std::format(L"{:.1f}", nextValue)); break;
					case 2:	SetValue(std::format(L"{:.2f}", nextValue)); break;
					case 3:	SetValue(std::format(L"{:.3f}", nextValue)); break;
					case 4:	SetValue(std::format(L"{:.4f}", nextValue)); break;
					case 5:	SetValue(std::format(L"{:.5f}", nextValue)); break;
					case 6:	SetValue(std::format(L"{:.6f}", nextValue)); break;
					default:	SetValue(std::format(L"{:f}", nextValue)); break;
					}

					if (_onDrag)
						_onDrag(nextValue, _min, _max);

					*_value = nextValue;
					_lastValue = *_value;
				}

				SetCursor(LoadCursor(nullptr, IDC_SIZEWE));

				_startDrag = mx;
			}
		}
		else
		{
			SetCursor(LoadCursor(nullptr, IDC_ARROW));
		}
	}

	void DragFloat::SetOnDrag(OnDrag fn)
	{
		_onDrag = fn;
	}
	
	bool DragFloat::OnInputEvent(InputEvent event, InputData* data)
	{
		if(event == InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON && _hovering)
		{			
			if (_startDrag == -1)
			{
				_startDrag = data->MouseDown.xpos;
			}			
		}
		else if (event == InputEvent::MouseMove && _startDrag != -1)
		{
			if (abs(_startDrag - data->MouseDown.xpos) > 0)
			{
				_dragging = true;
				SetHasInputFocus(false);
				return true;
			}
		}
		else if(event == InputEvent::MouseUp && data->MouseUp.button == VK_LBUTTON && _startDrag != -1)
		{			
			_dragging = false;
			_startDrag = -1;

			return false;
		}

		return LineEdit::OnInputEvent(event, data);
	}
}