
#include "QuestionDialog.hpp"

namespace HexEngine
{
	QuestionDialog::QuestionDialog(Element* parent, const Point& position, const Point& size, const std::wstring& title, const std::wstring& message, OnCompleteQuestionDialog callback) :
		Dialog(parent, position, size, title),
		_message(message)
	{}

	void QuestionDialog::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		Dialog::Render(renderer, w, h);
	}
}