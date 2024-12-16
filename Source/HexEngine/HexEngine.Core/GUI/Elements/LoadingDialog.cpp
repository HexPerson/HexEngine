
#include "LoadingDialog.hpp"
#include "../GuiRenderer.hpp"

namespace HexEngine
{
	LoadingDialog::LoadingDialog(Element* parent, const Point& position, const Point& size, const std::wstring& title) :
		Dialog(parent, position, size, title)
	{}

	void LoadingDialog::SetPercentage(float percentage)
	{
		std::unique_lock lock(_lock);
		
		_percentage = percentage;

		if (_percentage >= 1.0f)
			DeleteMe();
	}

	void LoadingDialog::SetText(const std::wstring& text)
	{
		std::unique_lock lock(_lock);

		_text = text;
	}

	void LoadingDialog::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		Dialog::Render(renderer, w, h);

		std::unique_lock lock(_lock);

		if (_text.length() > 0)
		{
			renderer->PushPrintText(renderer->_style.font, (uint8_t)Style::FontSize::Tiny, _position.x + 10, _position.y + 40, renderer->_style.text_regular, FontAlign::None, _text);
		}

		renderer->PushFillQuad(_position.x + 10, _position.y + 60, _size.x - 20, 20, renderer->_style.lineedit_back);

		if (_percentage > 0.0f)
		{
			renderer->PushFillQuad(_position.x + 12, _position.y + 62, (int32_t)((float)(_size.x - 24) * _percentage), 18, renderer->_style.win_highlight);
		}
		
		renderer->PushFrame(_position.x + 10, _position.y + 60, _size.x - 20, 20, 1, renderer->_style.win_border);
	}
}