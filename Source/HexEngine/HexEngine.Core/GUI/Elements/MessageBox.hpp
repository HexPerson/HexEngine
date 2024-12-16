
#pragma once

#include "Dialog.hpp"
#include "Button.hpp"

namespace HexEngine
{
#undef MessageBox
	class MessageBox : public Dialog
	{
	public:
		static MessageBox* Info(const std::wstring& title, const std::wstring& message, CallbackFn callback = nullptr);

		MessageBox(const std::wstring& title, const std::wstring& message, CallbackFn callback = nullptr);

		virtual ~MessageBox();

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		bool OnClickOk();

	private:
		std::wstring _title;
		std::wstring _message;
		int32_t _textWidth;
		Button* _button;
	};
}
