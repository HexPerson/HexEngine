
#pragma once

#include "Dialog.hpp"
#include "LineEdit.hpp"

namespace HexEngine
{
	class LineEditDialog : public Dialog
	{
	public:
		using OnCompleteLineEditDialog = std::function<void(LineEditDialog*, const std::wstring&)>;

		LineEditDialog(Element* parent, const Point& position, const Point& size, const std::wstring& title, OnCompleteLineEditDialog callback);

		void OnInput(const std::wstring& input);

	private:
		OnCompleteLineEditDialog _onComplete;
		LineEdit* _edit;
	};
}
