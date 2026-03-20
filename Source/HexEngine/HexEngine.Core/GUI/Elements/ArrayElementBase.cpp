#include "ArrayElementBase.hpp"
#include "Button.hpp"
#include "../GuiRenderer.hpp"

namespace HexEngine
{
	namespace
	{
		constexpr int32_t HeaderPadding = 4;
		constexpr int32_t HeaderHeight = 24;
		constexpr int32_t ButtonSize = 18;
		constexpr int32_t RowSpacing = 6;
		constexpr int32_t RowHeaderHeight = 14;
		constexpr int32_t RowInnerPadding = 6;
	}

	ArrayElementBase::ArrayElementBase(Element* parent, const Point& position, const Point& size, const std::wstring& label) :
		Element(parent, position, size),
		_label(label)
	{
		_addButton = new Button(this, Point(), Point(ButtonSize, ButtonSize), L"+", std::bind(&ArrayElementBase::OnAddButtonPressed, this, std::placeholders::_1));
		_removeButton = new Button(this, Point(), Point(ButtonSize, ButtonSize), L"-", std::bind(&ArrayElementBase::OnRemoveButtonPressed, this, std::placeholders::_1));
	}

	ArrayElementBase::~ArrayElementBase()
	{
	}

	void ArrayElementBase::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		(void)w;
		(void)h;

		if (_lastKnownWidth != _size.x || (int32_t)_itemRows.size() != GetItemCountInternal())
		{
			RefreshItemRows();
		}

		const auto absPos = GetAbsolutePosition();

		renderer->FillQuad(absPos.x, absPos.y, _size.x, _size.y, renderer->_style.inspector_widget_back);
		renderer->Frame(absPos.x, absPos.y, _size.x, _size.y, 1, renderer->_style.win_border);

		const int32_t itemCount = GetItemCountInternal();
		const std::wstring countLabel = L"[" + std::to_wstring(itemCount) + L"]";

		const int32_t addButtonX = _size.x - HeaderPadding - ButtonSize;
		const int32_t removeButtonX = addButtonX - ButtonSize - HeaderPadding;
		const int32_t buttonY = HeaderPadding - 1;

		_addButton->SetPosition(Point(addButtonX, buttonY));
		_removeButton->SetPosition(Point(removeButtonX, buttonY));
		_removeButton->EnableInput(itemCount > 0);

		renderer->PrintText(
			renderer->_style.font.get(),
			(uint8_t)Style::FontSize::Tiny,
			absPos.x + HeaderPadding,
			absPos.y + HeaderPadding + 2,
			renderer->_style.text_highlight,
			0,
			_label);

		renderer->PrintText(
			renderer->_style.font.get(),
			(uint8_t)Style::FontSize::Tiny,
			absPos.x + removeButtonX - HeaderPadding,
			absPos.y + HeaderPadding + 2,
			renderer->_style.text_regular,
			FontAlign::Right,
			countLabel);

		for (size_t i = 0; i < _rowVisuals.size(); ++i)
		{
			const auto& row = _rowVisuals[i];
			const int32_t rowX = absPos.x + HeaderPadding;
			const int32_t rowY = absPos.y + row.top;
			const int32_t rowWidth = _size.x - HeaderPadding * 2;

			renderer->FillQuad(rowX, rowY, rowWidth, row.height, renderer->_style.lineedit_back);
			renderer->Frame(rowX, rowY, rowWidth, row.height, 1, renderer->_style.win_border);

			renderer->PrintText(
				renderer->_style.font.get(),
				(uint8_t)Style::FontSize::Tiny,
				rowX + RowInnerPadding,
				rowY + 1,
				renderer->_style.text_highlight,
				0,
				GetItemLabelInternal((int32_t)i));
		}
	}

	void ArrayElementBase::RefreshItemRows()
	{
		ClearRows();

		const int32_t itemCount = GetItemCountInternal();
		_rowVisuals.reserve(itemCount);
		_itemRows.reserve(itemCount);

		int32_t rowTop = HeaderHeight + HeaderPadding;

		for (int32_t i = 0; i < itemCount; ++i)
		{
			const int32_t rowHeight = std::max(RowHeaderHeight + 8, GetItemHeightInternal(i));

			ItemRowVisual visual;
			visual.top = rowTop;
			visual.height = rowHeight;
			_rowVisuals.push_back(visual);

			const int32_t contentTop = rowTop + RowHeaderHeight;
			const int32_t contentHeight = std::max(8, rowHeight - RowHeaderHeight - RowInnerPadding);
			const int32_t contentWidth = std::max(8, _size.x - (HeaderPadding + RowInnerPadding) * 2);

			Element* rowRoot = new Element(
				this,
				Point(HeaderPadding + RowInnerPadding, contentTop),
				Point(contentWidth, contentHeight));

			_itemRows.push_back(rowRoot);
			BuildItemEditorInternal(i, rowRoot);

			rowTop += rowHeight + RowSpacing;
		}

		_lastKnownWidth = _size.x;
	}

	void ArrayElementBase::SetOnArrayChanged(OnArrayChangedFn fn)
	{
		_onArrayChanged = fn;
	}

	int32_t ArrayElementBase::GetItemHeightInternal(int32_t index) const
	{
		(void)index;
		return 30;
	}

	std::wstring ArrayElementBase::GetItemLabelInternal(int32_t index) const
	{
		return L"Item " + std::to_wstring(index);
	}

	bool ArrayElementBase::OnAddButtonPressed(Button* button)
	{
		(void)button;
		if (!AddDefaultItemInternal())
			return false;

		RefreshItemRows();
		NotifyArrayChanged();
		return true;
	}

	bool ArrayElementBase::OnRemoveButtonPressed(Button* button)
	{
		(void)button;

		const int32_t count = GetItemCountInternal();
		if (count <= 0)
			return false;

		if (!RemoveItemInternal(count - 1))
			return false;

		RefreshItemRows();
		NotifyArrayChanged();
		return true;
	}

	void ArrayElementBase::ClearRows()
	{
		for (auto* row : _itemRows)
		{
			if (row && !row->WantsDeletion())
				row->DeleteMe();
		}

		_itemRows.clear();
		_rowVisuals.clear();
	}

	void ArrayElementBase::NotifyArrayChanged()
	{
		if (_onArrayChanged)
		{
			_onArrayChanged(this);
		}
	}
}
