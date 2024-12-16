
#pragma once

#include "Dialog.hpp"

namespace HexEngine
{
	class QuestionDialog : public Dialog
	{
	public:
		enum class ButtonOptions
		{
			Yes = HEX_BITSET(0),
			No = HEX_BITSET(1),
			Cancel = HEX_BITSET(2)
		};
		using OnCompleteQuestionDialog = std::function<void(QuestionDialog*, const ButtonOptions options)>;

		QuestionDialog(Element* parent, const Point& position, const Point& size, const std::wstring& title, const std::wstring& message , OnCompleteQuestionDialog callback);

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;

	private:
		OnCompleteQuestionDialog _onComplete;
		std::wstring _message;
	};
}
