
#include "DragFloat.hpp"
#include "../GuiRenderer.hpp"
#include "../UIManager.hpp"
#include <algorithm>
#include <cmath>
#include <cwchar>

namespace HexEngine
{
	namespace
	{
		constexpr int32_t kSliderTrackPadding = 8;
		constexpr int32_t kSliderTrackHeight = 4;
		constexpr int32_t kThumbWidth = 8;
	}

	DragFloat::DragFloat(Element* parent, const Point& position, const Point& size, const std::wstring& label, float* value, float min, float max, float scale, int32_t decimalPlaces) :
		LineEdit(parent, position, size, label),
		_value(value),
		_min(min),
		_max(max),
		_scale(scale),
		_lastValue(value ? *value : 0.0f),
		_decimalPlaces(decimalPlaces)
	{
		if (_value != nullptr)
		{
			*_value = std::clamp(*_value, _min, _max);
			_lastValue = *_value;
		}
		SetValue(FormatValue(_lastValue));
	}

	std::wstring DragFloat::FormatValue(float value) const
	{
		switch (_decimalPlaces)
		{
		case 0:	return std::format(L"{:.0f}", value);
		case 1:	return std::format(L"{:.1f}", value);
		case 2:	return std::format(L"{:.2f}", value);
		case 3:	return std::format(L"{:.3f}", value);
		case 4:	return std::format(L"{:.4f}", value);
		case 5:	return std::format(L"{:.5f}", value);
		case 6:	return std::format(L"{:.6f}", value);
		default:return std::format(L"{:f}", value);
		}
	}

	void DragFloat::ApplyValue(float value, bool invokeCallback)
	{
		if (_value == nullptr)
			return;

		const float clamped = std::clamp(value, _min, _max);
		const bool changed = std::abs(_lastValue - clamped) > 1e-6f;

		*_value = clamped;
		_lastValue = clamped;
		SetValue(FormatValue(clamped));

		if (changed && invokeCallback && _onDrag)
		{
			_onDrag(clamped, _min, _max);
		}
	}

	void DragFloat::CommitTextValue()
	{
		const std::wstring text = GetValue();
		if (text.empty())
		{
			SetValue(FormatValue(_lastValue));
			return;
		}

		wchar_t* end = nullptr;
		const float parsed = std::wcstof(text.c_str(), &end);
		if (end == text.c_str() || (end != nullptr && *end != L'\0') || !std::isfinite(parsed))
		{
			SetValue(FormatValue(_lastValue));
			return;
		}

		ApplyValue(parsed, true);
	}

	void DragFloat::ComputeSliderLayout(GuiRenderer* renderer, int32_t& boxX, int32_t& boxY, int32_t& boxW, int32_t& boxH, int32_t& trackX, int32_t& trackW, int32_t& thumbX, int32_t& thumbY, int32_t& thumbW, int32_t& thumbH) const
	{
		const auto position = GetAbsolutePosition();
		const std::wstring label = GetLabelText();

		int32_t labelWidth = 0;
		if (renderer != nullptr && !label.empty())
		{
			int32_t labelHeight = 0;
			renderer->_style.font->MeasureText((uint8_t)Style::FontSize::Tiny, label, labelWidth, labelHeight);
		}

		int32_t minSize = _labelMinSize > 0 ? _labelMinSize : (labelWidth + 20);
		if (label.empty())
			minSize = 0;

		boxX = position.x + minSize;
		boxY = position.y;
		boxW = std::max(24, _size.x - minSize);
		boxH = _size.y;

		trackX = boxX + kSliderTrackPadding;
		trackW = std::max(8, boxW - (kSliderTrackPadding * 2));

		const float range = std::max(1e-6f, _max - _min);
		const float normalized = std::clamp((_lastValue - _min) / range, 0.0f, 1.0f);
		const int32_t fillW = std::clamp(static_cast<int32_t>(std::round(normalized * static_cast<float>(trackW))), 0, trackW);

		thumbW = kThumbWidth;
		thumbH = std::max(10, boxH - 6);
		thumbY = boxY + (boxH - thumbH) / 2;
		thumbX = std::clamp(trackX + fillW - (kThumbWidth / 2), boxX + 2, boxX + boxW - kThumbWidth - 2);
	}

	void DragFloat::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		(void)w;
		(void)h;

		if (_value != nullptr && !_dragging && !_hasInputFocus && std::abs(_lastValue - *_value) > 1e-6f)
		{
			_lastValue = std::clamp(*_value, _min, _max);
			SetValue(FormatValue(_lastValue));
		}

		const auto position = GetAbsolutePosition();
		const std::wstring label = GetLabelText();
		int32_t boxX = 0, boxY = 0, boxW = 0, boxH = 0;
		int32_t trackX = 0, trackW = 0;
		int32_t thumbX = 0, thumbY = 0, thumbW = 0, thumbH = 0;
		ComputeSliderLayout(renderer, boxX, boxY, boxW, boxH, trackX, trackW, thumbX, thumbY, thumbW, thumbH);

		const bool boxHovered = IsMouseOver(boxX, boxY, boxW, boxH);
		_hovering = boxHovered;

		if (!label.empty())
		{
			const math::Color labelColour = (boxHovered || _hasInputFocus || _dragging)
				? renderer->_style.text_highlight
				: renderer->_style.text_regular;
			renderer->PrintText(
				renderer->_style.font.get(),
				(uint8_t)Style::FontSize::Tiny,
				position.x,
				position.y + _size.y / 2,
				labelColour,
				FontAlign::CentreUD,
				label);
		}

		renderer->FillQuad(boxX, boxY, boxW, boxH, renderer->_style.lineedit_back);
		if (boxHovered || _hasInputFocus || _dragging)
		{
			renderer->FillQuadVerticalGradient(boxX, boxY, boxW, boxH, math::Color(HEX_RGBA_TO_FLOAT4(55, 60, 68, 220)), math::Color(HEX_RGBA_TO_FLOAT4(35, 38, 42, 220)));
		}
		const math::Color borderColour = (_hasInputFocus || _dragging)
			? renderer->_style.text_highlight
			: renderer->_style.win_border;
		renderer->Frame(boxX, boxY, boxW, boxH, 1, borderColour);

		const int32_t trackY = boxY + boxH - (kSliderTrackHeight + 3);
		renderer->FillQuad(trackX, trackY, trackW, kSliderTrackHeight, renderer->_style.listbox_highlight);

		const float range = std::max(1e-6f, _max - _min);
		const float normalized = std::clamp((_lastValue - _min) / range, 0.0f, 1.0f);
		const int32_t fillW = std::clamp(static_cast<int32_t>(std::round(normalized * static_cast<float>(trackW))), 0, trackW);
		renderer->FillQuad(trackX, trackY, fillW, kSliderTrackHeight, renderer->_style.button_hover);

		const math::Color thumbColour = _dragging
			? renderer->_style.button_hover
			: (boxHovered ? renderer->_style.text_highlight : renderer->_style.win_highlight);
		renderer->FillQuad(thumbX, thumbY, thumbW, thumbH, thumbColour);
		renderer->Frame(thumbX, thumbY, thumbW, thumbH, 1, renderer->_style.win_border);

		std::wstring valueText = GetValue();
		int32_t valueWidth = 0;
		int32_t valueHeight = 0;
		while (!valueText.empty())
		{
			renderer->_style.font->MeasureText((uint8_t)Style::FontSize::Tiny, valueText, valueWidth, valueHeight);
			if (valueWidth <= boxW - 14)
				break;
			valueText.erase(valueText.begin());
		}

		const math::Color valueColour = (_dragging || _hasInputFocus)
			? renderer->_style.text_highlight
			: renderer->_style.text_regular;
		renderer->PrintText(
			renderer->_style.font.get(),
			(uint8_t)Style::FontSize::Tiny,
			boxX + 7,
			boxY + (boxH / 2) - 2,
			valueColour,
			FontAlign::CentreUD,
			valueText);

		if (_hasInputFocus)
		{
			renderer->FillQuad(boxX + 7 + valueWidth + 2, boxY + 3, 2, std::max(1, boxH - 9), renderer->_style.text_regular);
		}

		if (_dragging || boxHovered)
			SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
		else if (_hasInputFocus)
			SetCursor(LoadCursor(nullptr, IDC_IBEAM));
		else
			SetCursor(LoadCursor(nullptr, IDC_ARROW));
	}

	void DragFloat::SetOnDrag(OnDrag fn)
	{
		_onDrag = fn;
	}
	
	bool DragFloat::OnInputEvent(InputEvent event, InputData* data)
	{
		const bool hadInputFocus = IsInputFocus();
		GuiRenderer* renderer = g_pEnv->GetUIManager().GetRenderer();

		if (event == InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON && _hovering)
		{
			if (_startDrag == -1 && _dragMode == DragMode::None)
			{
				int32_t boxX = 0, boxY = 0, boxW = 0, boxH = 0;
				int32_t trackX = 0, trackW = 0;
				int32_t thumbX = 0, thumbY = 0, thumbW = 0, thumbH = 0;
				ComputeSliderLayout(renderer, boxX, boxY, boxW, boxH, trackX, trackW, thumbX, thumbY, thumbW, thumbH);

				const int32_t mouseX = data->MouseDown.xpos;
				const int32_t mouseY = data->MouseDown.ypos;
				const bool overThumb = mouseX >= thumbX && mouseX <= (thumbX + thumbW) && mouseY >= thumbY && mouseY <= (thumbY + thumbH);
				if (overThumb)
				{
					const float range = std::max(1e-6f, _max - _min);
					const float normalized = std::clamp((static_cast<float>(mouseX - trackX) / static_cast<float>(std::max(1, trackW))), 0.0f, 1.0f);
					_dragMode = DragMode::ThumbAbsolute;
					_dragging = true;
					_startDrag = -1;
					SetHasInputFocus(false);
					ApplyValue(_min + normalized * range, true);
					return true;
				}

				_dragMode = DragMode::Delta;
				_startDrag = mouseX;
				_dragging = false;
			}
		}
		else if (event == InputEvent::MouseMove && _dragMode == DragMode::ThumbAbsolute && _dragging)
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

			int32_t boxX = 0, boxY = 0, boxW = 0, boxH = 0;
			int32_t trackX = 0, trackW = 0;
			int32_t thumbX = 0, thumbY = 0, thumbW = 0, thumbH = 0;
			ComputeSliderLayout(renderer, boxX, boxY, boxW, boxH, trackX, trackW, thumbX, thumbY, thumbW, thumbH);

			const float range = std::max(1e-6f, _max - _min);
			const float normalized = std::clamp((static_cast<float>(mouseX - trackX) / static_cast<float>(std::max(1, trackW))), 0.0f, 1.0f);
			ApplyValue(_min + normalized * range, true);
			return true;
		}
		else if (event == InputEvent::MouseMove && _dragMode == DragMode::Delta && _startDrag != -1)
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
				ApplyValue(_lastValue + static_cast<float>(delta) * _scale, true);
				_startDrag = mouseX;
				return true;
			}
		}
		else if (event == InputEvent::MouseUp && data->MouseUp.button == VK_LBUTTON && _startDrag != -1)
		{
			const bool wasDragging = _dragging;
			_dragging = false;
			_startDrag = -1;
			_dragMode = DragMode::None;

			if (wasDragging)
				return true;
		}
		else if (event == InputEvent::MouseUp && data->MouseUp.button == VK_LBUTTON && _dragMode == DragMode::ThumbAbsolute)
		{
			_dragging = false;
			_startDrag = -1;
			_dragMode = DragMode::None;
			return true;
		}

		const bool handled = LineEdit::OnInputEvent(event, data);

		if ((event == InputEvent::Char && data->Char.ch == VK_RETURN) ||
			(event == InputEvent::KeyDown && data->KeyDown.key == VK_RETURN))
		{
			CommitTextValue();
			return true;
		}

		if (event == InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON)
		{
			if (hadInputFocus && !IsInputFocus())
			{
				CommitTextValue();
			}
		}

		return handled;
	}
}
