#include "ColourPicker.hpp"
#include "Dialog.hpp"
#include "Button.hpp"
#include "DragFloat.hpp"
#include "LineEdit.hpp"
#include "../GuiRenderer.hpp"
#include "../UIManager.hpp"
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <format>

namespace
{
	float Clamp01(float value)
	{
		return std::clamp(value, 0.0f, 1.0f);
	}

	int32_t MaxI(int32_t a, int32_t b)
	{
		return a > b ? a : b;
	}

	float MaxF(float a, float b)
	{
		return a > b ? a : b;
	}

	float MinF(float a, float b)
	{
		return a < b ? a : b;
	}

	int32_t ToByte(float value)
	{
		return (int32_t)std::round(Clamp01(value) * 255.0f);
	}

	DirectX::SimpleMath::Color HsvToRgb(float hue, float saturation, float value, float alpha)
	{
		const float h = std::fmod(MaxF(hue, 0.0f), 360.0f);
		const float s = Clamp01(saturation);
		const float v = Clamp01(value);
		const float c = v * s;
		const float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
		const float m = v - c;

		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;

		if (h < 60.0f) { r = c; g = x; b = 0.0f; }
		else if (h < 120.0f) { r = x; g = c; b = 0.0f; }
		else if (h < 180.0f) { r = 0.0f; g = c; b = x; }
		else if (h < 240.0f) { r = 0.0f; g = x; b = c; }
		else if (h < 300.0f) { r = x; g = 0.0f; b = c; }
		else { r = c; g = 0.0f; b = x; }

		return DirectX::SimpleMath::Color(r + m, g + m, b + m, Clamp01(alpha));
	}

	void RgbToHsv(const DirectX::SimpleMath::Color& colour, float& hue, float& saturation, float& value)
	{
		const float r = Clamp01(colour.x);
		const float g = Clamp01(colour.y);
		const float b = Clamp01(colour.z);

		const float maxVal = MaxF(MaxF(r, g), b);
		const float minVal = MinF(MinF(r, g), b);
		const float delta = maxVal - minVal;

		value = maxVal;
		saturation = maxVal <= 0.0f ? 0.0f : (delta / maxVal);

		if (delta <= 0.00001f)
		{
			hue = 0.0f;
			return;
		}

		if (maxVal == r)
		{
			hue = 60.0f * std::fmod(((g - b) / delta), 6.0f);
		}
		else if (maxVal == g)
		{
			hue = 60.0f * (((b - r) / delta) + 2.0f);
		}
		else
		{
			hue = 60.0f * (((r - g) / delta) + 4.0f);
		}

		if (hue < 0.0f)
			hue += 360.0f;
	}

	std::wstring ColorToHex(const DirectX::SimpleMath::Color& colour, bool includeAlpha)
	{
		const int32_t r = ToByte(colour.x);
		const int32_t g = ToByte(colour.y);
		const int32_t b = ToByte(colour.z);
		const int32_t a = ToByte(colour.w);

		if (includeAlpha)
			return std::format(L"#{:02X}{:02X}{:02X}{:02X}", r, g, b, a);

		return std::format(L"#{:02X}{:02X}{:02X}", r, g, b);
	}

	bool ParseHexColor(const std::wstring& text, DirectX::SimpleMath::Color& outColour)
	{
		std::wstring clean;
		clean.reserve(text.size());

		for (wchar_t ch : text)
		{
			if (std::iswxdigit(ch))
			{
				clean.push_back((wchar_t)std::towupper(ch));
			}
		}

		if (clean.size() != 6 && clean.size() != 8)
			return false;

		if (clean.size() == 6)
			clean += L"FF";

		auto parseByte = [](const std::wstring& value, int32_t offset, int32_t& outValue) -> bool
		{
			try
			{
				outValue = std::stoi(value.substr(offset, 2), nullptr, 16);
				return true;
			}
			catch (...)
			{
				return false;
			}
		};

		int32_t r = 0, g = 0, b = 0, a = 255;
		if (!parseByte(clean, 0, r) || !parseByte(clean, 2, g) || !parseByte(clean, 4, b) || !parseByte(clean, 6, a))
			return false;

		outColour = DirectX::SimpleMath::Color((float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, (float)a / 255.0f);
		return true;
	}
}

namespace HexEngine
{
	class ColourPickerPopupDialog : public Dialog
	{
	public:
		enum class EditMode
		{
			RGB,
			HSV
		};

		ColourPickerPopupDialog(Element* parent, const Point& position, math::Color* colour) :
			Dialog(parent, position, Point(470, 320), L"Colour Picker"),
			_colour(colour),
			_initialColour(colour ? *colour : math::Color(1, 1, 1, 1))
		{
			if (_colour == nullptr)
			{
				_fallbackColour = math::Color(1, 1, 1, 1);
				_colour = &_fallbackColour;
				_initialColour = _fallbackColour;
			}

			_rgbModeButton = new Button(this, Point(236, 8), Point(56, 22), L"RGB", [this](Button*) -> bool
			{
				SetEditMode(EditMode::RGB);
				return true;
			});

			_hsvModeButton = new Button(this, Point(296, 8), Point(56, 22), L"HSV", [this](Button*) -> bool
			{
				SetEditMode(EditMode::HSV);
				return true;
			});

			_hexEdit = new LineEdit(this, Point(358, 8), Point(98, 22), L"");
			_hexEdit->SetValue(ColorToHex(*_colour, true));
			_hexEdit->SetOnInputFn([this](LineEdit*, const std::wstring& text)
			{
				math::Color parsed;
				if (ParseHexColor(text, parsed))
				{
					*_colour = parsed;
					SyncEditorsFromColor();
				}
				else
				{
					_hexEdit->SetValue(ColorToHex(*_colour, true));
				}
			});

			constexpr int32_t kEditX = 286;
			constexpr int32_t kEditWidth = 170;
			constexpr int32_t kEditHeight = 22;
			constexpr int32_t kEditY = 44;
			constexpr int32_t kEditYStep = 28;

			for (int32_t i = 0; i < 3; ++i)
			{
				_rgbEdits[i] = new DragFloat(this, Point(kEditX, kEditY + kEditYStep * i), Point(kEditWidth, kEditHeight), L"", &_rgbValues[i], 0.0f, 255.0f, 1.0f, 0);
				_rgbEdits[i]->SetOnDrag([this, i](float value, float, float)
				{
					_rgbValues[i] = std::clamp(value, 0.0f, 255.0f);
					ApplyRgbToColor();
				});
				_rgbEdits[i]->SetOnInputFn([this, i](LineEdit*, const std::wstring& text)
				{
					try
					{
						_rgbValues[i] = std::clamp(std::stof(text), 0.0f, 255.0f);
						ApplyRgbToColor();
					}
					catch (...)
					{
						SyncEditorsFromColor();
					}
				});
			}

			_hsvEdits[0] = new DragFloat(this, Point(kEditX, kEditY + kEditYStep * 0), Point(kEditWidth, kEditHeight), L"", &_hsvValues[0], 0.0f, 360.0f, 1.0f, 1);
			_hsvEdits[1] = new DragFloat(this, Point(kEditX, kEditY + kEditYStep * 1), Point(kEditWidth, kEditHeight), L"", &_hsvValues[1], 0.0f, 1.0f, 0.01f, 3);
			_hsvEdits[2] = new DragFloat(this, Point(kEditX, kEditY + kEditYStep * 2), Point(kEditWidth, kEditHeight), L"", &_hsvValues[2], 0.0f, 1.0f, 0.01f, 3);

			for (int32_t i = 0; i < 3; ++i)
			{
				_hsvEdits[i]->SetOnDrag([this, i](float value, float, float)
				{
					if (i == 0) _hsvValues[i] = std::clamp(value, 0.0f, 360.0f);
					else _hsvValues[i] = std::clamp(value, 0.0f, 1.0f);
					ApplyHsvToColor();
				});
				_hsvEdits[i]->SetOnInputFn([this, i](LineEdit*, const std::wstring& text)
				{
					try
					{
						const float parsed = std::stof(text);
						if (i == 0) _hsvValues[i] = std::clamp(parsed, 0.0f, 360.0f);
						else _hsvValues[i] = std::clamp(parsed, 0.0f, 1.0f);
						ApplyHsvToColor();
					}
					catch (...)
					{
						SyncEditorsFromColor();
					}
				});
			}

			_alphaEdit = new DragFloat(this, Point(kEditX, kEditY + kEditYStep * 3), Point(kEditWidth, kEditHeight), L"", &_alphaValue, 0.0f, 1.0f, 0.01f, 3);
			_alphaEdit->SetOnDrag([this](float value, float, float)
			{
				_alphaValue = Clamp01(value);
				ApplyActiveModeToColor();
			});
			_alphaEdit->SetOnInputFn([this](LineEdit*, const std::wstring& text)
			{
				try
				{
					_alphaValue = Clamp01(std::stof(text));
					ApplyActiveModeToColor();
				}
				catch (...)
				{
					SyncEditorsFromColor();
				}
			});

			_revertButton = new Button(this, Point(236, 250), Point(108, 24), L"Revert", [this](Button*) -> bool
			{
				*_colour = _initialColour;
				SyncEditorsFromColor();
				return true;
			});

			_okButton = new Button(this, Point(348, 250), Point(108, 24), L"Close", [this](Button*) -> bool
			{
				DeleteMe();
				return true;
			});

			SetEditMode(EditMode::HSV);
			SyncEditorsFromColor();
			_lastSyncedColour = *_colour;
		}

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override
		{
			if (_colour != nullptr &&
				(std::fabs(_colour->x - _lastSyncedColour.x) > 0.0001f ||
				 std::fabs(_colour->y - _lastSyncedColour.y) > 0.0001f ||
				 std::fabs(_colour->z - _lastSyncedColour.z) > 0.0001f ||
				 std::fabs(_colour->w - _lastSyncedColour.w) > 0.0001f))
			{
				SyncEditorsFromColor();
			}

			Dialog::Render(renderer, w, h);

			const RECT svRect = GetSatValRectAbs();
			const RECT hueRect = GetHueRectAbs();
			const RECT previewRect = GetPreviewRectAbs();
			constexpr int32_t kStep = 4;

			for (int32_t y = svRect.top; y < svRect.bottom; y += kStep)
			{
				const float value = 1.0f - Clamp01((float)(y - svRect.top) / (float)(svRect.bottom - svRect.top));
				for (int32_t x = svRect.left; x < svRect.right; x += kStep)
				{
					const float saturation = Clamp01((float)(x - svRect.left) / (float)(svRect.right - svRect.left));
					const math::Color cell = HsvToRgb(_hsvValues[0], saturation, value, 1.0f);
					renderer->FillQuad(x, y, kStep, kStep, cell);
				}
			}
			renderer->Frame(svRect.left, svRect.top, svRect.right - svRect.left, svRect.bottom - svRect.top, 1, renderer->_style.win_border);

			for (int32_t y = hueRect.top; y < hueRect.bottom; ++y)
			{
				const float hue = Clamp01((float)(y - hueRect.top) / (float)(hueRect.bottom - hueRect.top)) * 360.0f;
				const math::Color hueCol = HsvToRgb(hue, 1.0f, 1.0f, 1.0f);
				renderer->FillQuad(hueRect.left, y, hueRect.right - hueRect.left, 1, hueCol);
			}
			renderer->Frame(hueRect.left, hueRect.top, hueRect.right - hueRect.left, hueRect.bottom - hueRect.top, 1, renderer->_style.win_border);

			const int32_t markerX = svRect.left + (int32_t)std::round(_hsvValues[1] * (float)(svRect.right - svRect.left - 1));
			const int32_t markerY = svRect.bottom - 1 - (int32_t)std::round(_hsvValues[2] * (float)(svRect.bottom - svRect.top - 1));
			renderer->Frame(markerX - 3, markerY - 3, 7, 7, 1, math::Color(0, 0, 0, 1));
			renderer->Frame(markerX - 2, markerY - 2, 5, 5, 1, math::Color(1, 1, 1, 1));

			const int32_t hueMarkerY = hueRect.top + (int32_t)std::round((_hsvValues[0] / 360.0f) * (float)(hueRect.bottom - hueRect.top - 1));
			renderer->Frame(hueRect.left - 3, hueMarkerY - 2, (hueRect.right - hueRect.left) + 6, 4, 1, math::Color(1, 1, 1, 1));

			const int32_t previewWidth = previewRect.right - previewRect.left;
			renderer->FillQuad(previewRect.left, previewRect.top, previewWidth / 2, previewRect.bottom - previewRect.top, _initialColour);
			renderer->FillQuad(previewRect.left + previewWidth / 2, previewRect.top, previewWidth / 2, previewRect.bottom - previewRect.top, *_colour);
			renderer->Frame(previewRect.left, previewRect.top, previewRect.right - previewRect.left, previewRect.bottom - previewRect.top, 1, renderer->_style.win_border);

			renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, previewRect.left + 4, previewRect.top - 13, renderer->_style.text_regular, 0, L"Before");
			renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, previewRect.left + previewWidth / 2 + 4, previewRect.top - 13, renderer->_style.text_regular, 0, L"After");

			const int32_t labelX = GetAbsolutePosition().x + 236;
			const int32_t firstLabelY = GetAbsolutePosition().y + 49;
			const math::Color rgbLabelColor[3] = {
				math::Color(HEX_RGBA_TO_FLOAT4(255,64,64,255)),
				math::Color(HEX_RGBA_TO_FLOAT4(80,226,75,255)),
				math::Color(HEX_RGBA_TO_FLOAT4(4,155,213,255))
			};

			if (_editMode == EditMode::RGB)
			{
				renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, labelX, firstLabelY + 0 * 28, rgbLabelColor[0], 0, L"R");
				renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, labelX, firstLabelY + 1 * 28, rgbLabelColor[1], 0, L"G");
				renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, labelX, firstLabelY + 2 * 28, rgbLabelColor[2], 0, L"B");
			}
			else
			{
				renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, labelX, firstLabelY + 0 * 28, renderer->_style.text_highlight, 0, L"H");
				renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, labelX, firstLabelY + 1 * 28, renderer->_style.text_highlight, 0, L"S");
				renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, labelX, firstLabelY + 2 * 28, renderer->_style.text_highlight, 0, L"V");
			}

			renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, labelX, firstLabelY + 3 * 28, renderer->_style.text_highlight, 0, L"A");
		}

		virtual bool OnInputEvent(InputEvent event, InputData* data) override
		{
			if (event == InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON)
			{
				const POINT mouse = { data->MouseDown.xpos, data->MouseDown.ypos };
				const RECT svRect = GetSatValRectAbs();
				const RECT hueRect = GetHueRectAbs();

				if (PtInRect(&svRect, mouse))
				{
					_draggingSatVal = true;
					UpdateSatValFromMouse(mouse.x, mouse.y);
					return true;
				}

				if (PtInRect(&hueRect, mouse))
				{
					_draggingHue = true;
					UpdateHueFromMouse(mouse.y);
					return true;
				}
			}
			else if (event == InputEvent::MouseMove)
			{
				if (_draggingSatVal)
				{
					UpdateSatValFromMouse((int32_t)data->MouseMove.x, (int32_t)data->MouseMove.y);
					return true;
				}

				if (_draggingHue)
				{
					UpdateHueFromMouse((int32_t)data->MouseMove.y);
					return true;
				}
			}
			else if (event == InputEvent::MouseUp && data->MouseUp.button == VK_LBUTTON)
			{
				if (_draggingSatVal || _draggingHue)
				{
					_draggingSatVal = false;
					_draggingHue = false;
					return true;
				}
			}

			return Dialog::OnInputEvent(event, data);
		}

	private:
		void SetEditMode(EditMode mode)
		{
			_editMode = mode;

			if (_editMode == EditMode::RGB)
			{
				_rgbModeButton->SetHighlightOverride(g_pEnv->GetUIManager().GetRenderer()->_style.button_hover);
				_hsvModeButton->RemoveHighlightOverride();
			}
			else
			{
				_hsvModeButton->SetHighlightOverride(g_pEnv->GetUIManager().GetRenderer()->_style.button_hover);
				_rgbModeButton->RemoveHighlightOverride();
			}

			for (int32_t i = 0; i < 3; ++i)
			{
				_rgbEdits[i]->EnableInput(_editMode == EditMode::RGB);
				_hsvEdits[i]->EnableInput(_editMode == EditMode::HSV);
				if (_editMode == EditMode::RGB)
				{
					_rgbEdits[i]->Enable();
					_hsvEdits[i]->Disable();
				}
				else
				{
					_hsvEdits[i]->Enable();
					_rgbEdits[i]->Disable();
				}
			}
		}

		void SyncEditorsFromColor()
		{
			_rgbValues[0] = Clamp01(_colour->x) * 255.0f;
			_rgbValues[1] = Clamp01(_colour->y) * 255.0f;
			_rgbValues[2] = Clamp01(_colour->z) * 255.0f;
			_alphaValue = Clamp01(_colour->w);
			RgbToHsv(*_colour, _hsvValues[0], _hsvValues[1], _hsvValues[2]);

			_rgbEdits[0]->SetValue(std::format(L"{:.0f}", _rgbValues[0]));
			_rgbEdits[1]->SetValue(std::format(L"{:.0f}", _rgbValues[1]));
			_rgbEdits[2]->SetValue(std::format(L"{:.0f}", _rgbValues[2]));
			_hsvEdits[0]->SetValue(std::format(L"{:.1f}", _hsvValues[0]));
			_hsvEdits[1]->SetValue(std::format(L"{:.3f}", _hsvValues[1]));
			_hsvEdits[2]->SetValue(std::format(L"{:.3f}", _hsvValues[2]));
			_alphaEdit->SetValue(std::format(L"{:.3f}", _alphaValue));

			if (_hexEdit && !_hexEdit->IsInputFocus())
			{
				_hexEdit->SetValue(ColorToHex(*_colour, true));
			}

			_lastSyncedColour = *_colour;
		}

		void ApplyRgbToColor()
		{
			_colour->x = Clamp01(_rgbValues[0] / 255.0f);
			_colour->y = Clamp01(_rgbValues[1] / 255.0f);
			_colour->z = Clamp01(_rgbValues[2] / 255.0f);
			_colour->w = Clamp01(_alphaValue);
			SyncEditorsFromColor();
		}

		void ApplyHsvToColor()
		{
			*_colour = HsvToRgb(_hsvValues[0], _hsvValues[1], _hsvValues[2], _alphaValue);
			SyncEditorsFromColor();
		}

		void ApplyActiveModeToColor()
		{
			if (_editMode == EditMode::RGB)
				ApplyRgbToColor();
			else
				ApplyHsvToColor();
		}

		void UpdateSatValFromMouse(int32_t mouseX, int32_t mouseY)
		{
			const RECT svRect = GetSatValRectAbs();
			_hsvValues[1] = Clamp01((float)(mouseX - svRect.left) / (float)(svRect.right - svRect.left));
			_hsvValues[2] = 1.0f - Clamp01((float)(mouseY - svRect.top) / (float)(svRect.bottom - svRect.top));
			ApplyHsvToColor();
		}

		void UpdateHueFromMouse(int32_t mouseY)
		{
			const RECT hueRect = GetHueRectAbs();
			_hsvValues[0] = Clamp01((float)(mouseY - hueRect.top) / (float)(hueRect.bottom - hueRect.top)) * 360.0f;
			ApplyHsvToColor();
		}

		RECT GetSatValRectAbs() const
		{
			const auto pos = GetAbsolutePosition();
			return RECT{ pos.x + 16, pos.y + 40, pos.x + 196, pos.y + 220 };
		}

		RECT GetHueRectAbs() const
		{
			const auto pos = GetAbsolutePosition();
			return RECT{ pos.x + 202, pos.y + 40, pos.x + 218, pos.y + 220 };
		}

		RECT GetPreviewRectAbs() const
		{
			const auto pos = GetAbsolutePosition();
			return RECT{ pos.x + 236, pos.y + 170, pos.x + 456, pos.y + 214 };
		}

	private:
		math::Color* _colour = nullptr;
		math::Color _fallbackColour = math::Color(1, 1, 1, 1);
		math::Color _initialColour;
		math::Color _lastSyncedColour;
		EditMode _editMode = EditMode::HSV;
		bool _draggingSatVal = false;
		bool _draggingHue = false;
		float _rgbValues[3] = { 0.0f, 0.0f, 0.0f };
		float _hsvValues[3] = { 0.0f, 0.0f, 0.0f };
		float _alphaValue = 1.0f;
		Button* _rgbModeButton = nullptr;
		Button* _hsvModeButton = nullptr;
		Button* _revertButton = nullptr;
		Button* _okButton = nullptr;
		LineEdit* _hexEdit = nullptr;
		DragFloat* _rgbEdits[3] = { nullptr, nullptr, nullptr };
		DragFloat* _hsvEdits[3] = { nullptr, nullptr, nullptr };
		DragFloat* _alphaEdit = nullptr;
	};

	ColourPicker::ColourPicker(Element* parent, const Point& position, const Point& size, const std::wstring& label, math::Color* col) :
		Element(parent, position, size),
		_label(label),
		_colour(col),
		_ownedColour(col ? *col : math::Color(1, 1, 1, 1))
	{
		EnsureValidColour();
	}

	ColourPicker::~ColourPicker()
	{
		if (_popup != nullptr && !_popup->WantsDeletion())
		{
			_popup->DeleteMe();
			_popup = nullptr;
		}
	}

	void ColourPicker::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		(void)w;
		(void)h;

		EnsureValidColour();

		if (_popup != nullptr && _popup->WantsDeletion())
		{
			_popup = nullptr;
		}

		const auto pos = GetAbsolutePosition();
		int32_t labelWidth = 0;
		int32_t labelHeight = 0;
		if (!_label.empty())
			renderer->_style.font->MeasureText((uint8_t)Style::FontSize::Tiny, _label, labelWidth, labelHeight);

		const int32_t minSize = _label.empty() ? 0 : (_labelMinSize > 0 ? _labelMinSize : labelWidth + 20);
		const int32_t boxX = pos.x + minSize;
		const int32_t boxW = MaxI(10, _size.x - minSize);

		renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, pos.x, pos.y + _size.y / 2, renderer->_style.text_regular, FontAlign::CentreUD, _label);
		renderer->FillQuad(boxX, pos.y, boxW, _size.y, renderer->_style.lineedit_back);
		renderer->Frame(boxX, pos.y, boxW, _size.y, 1, renderer->_style.win_border);

		const int32_t swatchSize = MaxI(8, _size.y - 4);
		const int32_t swatchX = boxX + 2;
		const int32_t swatchY = pos.y + 2;
		constexpr int32_t checker = 4;
		for (int32_t y = 0; y < swatchSize; y += checker)
		{
			for (int32_t x = 0; x < swatchSize; x += checker)
			{
				const bool even = ((x / checker) + (y / checker)) % 2 == 0;
				renderer->FillQuad(swatchX + x, swatchY + y, checker, checker, even ? math::Color(0.75f, 0.75f, 0.75f, 1.0f) : math::Color(0.35f, 0.35f, 0.35f, 1.0f));
			}
		}
		renderer->FillQuad(swatchX, swatchY, swatchSize, swatchSize, *_colour);
		renderer->Frame(swatchX, swatchY, swatchSize, swatchSize, 1, renderer->_style.win_border);
		renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, swatchX + swatchSize + 8, pos.y + _size.y / 2, renderer->_style.text_regular, FontAlign::CentreUD, BuildHexLabel());
	}

	bool ColourPicker::OnInputEvent(InputEvent event, InputData* data)
	{
		if (event == InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON && IsMouseOver(true))
		{
			OpenPopup();
			return true;
		}

		return false;
	}

	int32_t ColourPicker::GetLabelWidth() const
	{
		int32_t width = 0;
		int32_t height = 0;

		if (!_label.empty())
			g_pEnv->GetUIManager().GetRenderer()->_style.font->MeasureText((int32_t)Style::FontSize::Tiny, _label, width, height);

		return width;
	}

	std::wstring ColourPicker::GetLabelText() const
	{
		return _label;
	}

	void ColourPicker::OpenPopup()
	{
		if (_popup != nullptr && !_popup->WantsDeletion())
		{
			_popup->BringToFront();
			return;
		}

		const Point abs = GetAbsolutePosition();
		_popup = new ColourPickerPopupDialog(g_pEnv->GetUIManager().GetRootElement(), Point(abs.x + 20, abs.y + _size.y + 20), _colour);
		_popup->BringToFront();
	}

	void ColourPicker::EnsureValidColour()
	{
		if (_colour == nullptr)
		{
			_colour = &_ownedColour;
		}
	}

	std::wstring ColourPicker::BuildHexLabel() const
	{
		return ColorToHex(*_colour, true);
	}
}
