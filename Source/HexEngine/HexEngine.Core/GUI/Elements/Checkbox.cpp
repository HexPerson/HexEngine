
#include "Checkbox.hpp"
#include "../GuiRenderer.hpp"
#include "../UIManager.hpp"

namespace HexEngine
{
	Checkbox::Checkbox(Element* parent, const Point& position, const Point& size, const std::wstring& label, bool* value) :
		Element(parent, position, size),
		_label(label),
		_value(value)
	{
		_tickImg = ITexture2D::Create("EngineData.Textures/UI/tick.png");
	}

	Checkbox::~Checkbox()
	{
		SAFE_UNLOAD(_tickImg);
	}

	void Checkbox::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		_hovering = false;

		auto pos = GetAbsolutePosition();

		int32_t startX = pos.x;

		int32_t width = 0, height = 0;
		if (_label.length() > 0)
		{
			renderer->PrintText(renderer->_style.font, (uint8_t)Style::FontSize::Tiny, pos.x, pos.y + _size.y / 2, renderer->_style.text_regular, FontAlign::CentreUD, _label);

			
			renderer->_style.font->MeasureText((int32_t)Style::FontSize::Tiny, _label, width, height);

			pos.x += width + 20;
		}

		if ((pos.x - startX) < _labelMinSize)
			pos.x += (_labelMinSize - (pos.x - startX));

		renderer->FillQuad(pos.x, pos.y, _size.y, _size.y, math::Color(1, 1, 1, 1));
		renderer->Frame(pos.x, pos.y, _size.y, _size.y, 1, math::Color(0, 0, 0, 1));

		if (_value && *_value)
		{
			renderer->FillTexturedQuad(_tickImg, pos.x + 1, pos.y , _size.y - 2, _size.y, math::Color(1, 1, 1, 1));
		}

		if (IsMouseOver(pos.x, pos.y, _size.y, _size.y))
		{
			_hovering = true;
		}
	}

	bool Checkbox::OnInputEvent(InputEvent event, InputData* data)
	{
		if (event == InputEvent::MouseDown && _hovering)
		{
			if(_value)
				*_value = !*_value;

			if (_onCheckFn)
				_onCheckFn(this, (_value ? *_value : false));

			return true;
		}
		return false;
	}

	void Checkbox::SetOnCheckFn(OnCheckFn fn)
	{
		_onCheckFn = fn;
	}

	int32_t Checkbox::GetLabelWidth() const
	{
		int32_t width = 0, height = 0;

		if(_label.length() > 0)
			g_pEnv->_uiManager->GetRenderer()->_style.font->MeasureText((int32_t)Style::FontSize::Tiny, _label, width, height);

		return width;
	}
}