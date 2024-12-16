
#include "MessageBox.hpp"
#include "../UIManager.hpp"

namespace HexEngine
{
	MessageBox::MessageBox(const std::wstring& title, const std::wstring& message, CallbackFn callback) :
		Dialog(g_pEnv->_uiManager->GetRootElement(), Point(), Point(), _title, callback),
		_message(message)
	{		
		int32_t width, height;
		g_pEnv->_uiManager->GetRenderer()->_style.font->MeasureText((uint8_t)Style::FontSize::Tiny, message, width, height);

		_size.x = width + 50;
		_size.y = height + 100;

		_position = Point::GetScreenCenterWithOffset(_size.y, _size.y);

		_textWidth = width;

		_button = new Button(this, Point(GetSize().x - 100, GetSize().y - 50), Point(80, 20), L"OK", std::bind(&MessageBox::OnClickOk, this));
	}

	MessageBox* MessageBox::Info(const std::wstring& title, const std::wstring& message, CallbackFn callback)
	{
		MessageBox* dlg = new MessageBox(
			title,
			message,
			callback);

		dlg->BringToFront();

		return dlg;
	}

	MessageBox::~MessageBox()
	{

	}

	bool MessageBox::OnClickOk()
	{
		if (_callback)
			_callback();

		DeleteMe();
		return true;
	}

	void MessageBox::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		Dialog::Render(renderer, w, h);

		auto textPos = GetAbsolutePosition();

		g_pEnv->_uiManager->GetRenderer()->PrintText(
			g_pEnv->_uiManager->GetRenderer()->_style.font,
			(uint8_t)Style::FontSize::Tiny,
			textPos.x + 30, textPos.y + 10,
			g_pEnv->_uiManager->GetRenderer()->_style.text_regular,
			FontAlign::None,
			_message);
	}
}