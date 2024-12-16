
#include "LineEditDialog.hpp"

namespace HexEngine
{
	LineEditDialog::LineEditDialog(Element* parent, const Point& position, const Point& size, const std::wstring& title, OnCompleteLineEditDialog callback) :
		Dialog(parent, position, size, title),
		_onComplete(callback)
	{
		_edit = new LineEdit(this, Point(20, 20), Point(size.x - 40, 20), L"");
		_edit->SetOnInputFn(std::bind(&LineEditDialog::OnInput, this, std::placeholders::_2));
	}

	void LineEditDialog::OnInput(const std::wstring& input)
	{
		if (_onComplete)
			_onComplete(this, input);

		DeleteMe();
	}
}