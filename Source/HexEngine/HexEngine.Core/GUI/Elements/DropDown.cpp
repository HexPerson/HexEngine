
#include "DropDown.hpp"

namespace HexEngine
{
	DropDown::DropDown(Element* parent, const Point& position, const Point& size, const std::wstring& label) :
		LineEdit(parent, position, size, label)
	{
		_context = new ContextMenu(this, Point(0, size.y), Point(size.x, -1));
		_context->_onClicked = std::bind(&DropDown::OnSelectedItem, this, std::placeholders::_1);

		_context->Disable();

		_arrowIcon = ITexture2D::Create("EngineData.Textures/UI/triangle.png");
	}

	DropDown::~DropDown()
	{
		SAFE_UNLOAD(_arrowIcon);
	}

	ContextMenu* DropDown::GetContextMenu() const
	{
		return _context;
	}

	void DropDown::OnSelectedItem(ContextItem* item)
	{
		SetValue(item->name);
		_context->Disable();
	}

	void DropDown::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		LineEdit::Render(renderer, w, h);

		if (_hasAdjustedContextPosition == false)
		{
			Point currentPos = _context->GetPosition();
			currentPos.x += _labelMinSize;

			Point currentSize = _context->GetSize();
			currentSize.x -= _labelMinSize;

			_context->SetPosition(currentPos);
			_context->SetSize(currentSize);

			_hasAdjustedContextPosition = true;
		}

		const int32_t iconSize = _size.y - 4;

		renderer->PushFillTexturedQuad(
			_arrowIcon,
			GetAbsolutePosition().x + _size.x - iconSize,
			GetAbsolutePosition().y + (_size.y / 2) - iconSize / 2,
			iconSize, iconSize,
			math::Color(1, 1, 1, 1));
	}

	bool DropDown::OnInputEvent(InputEvent event, InputData* data)
	{
		if (event == InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON)
		{
			if (_context->IsEnabled() && !_context->IsMouseOver(true))
				_context->Disable();
			else if (IsMouseOver(true))
			{
				_context->BringToFront();
				_context->Enable();
				return true;
			}			
		}
		return LineEdit::OnInputEvent(event, data);		
	}
}