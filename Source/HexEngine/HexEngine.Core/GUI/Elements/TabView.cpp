
#include "TabView.hpp"
#include "../GuiRenderer.hpp"

namespace HexEngine
{
	TabView::TabView(Element* parent, const Point& position, const Point& size) :
		Element(parent, position, size)
	{}

	TabItem* TabView::AddTab(const std::wstring& label)
	{
		TabItem* item = new TabItem(this, Point(_currentOffset, 0), this->GetSize(), label);

		_items.push_back(item);

		// if we had no entries, set the active selection to slot 0
		if (_currentIndex == -1)
		{
			item->SetSelected(true);
			_currentIndex = 0;
			_selectedTab = item;
		}

		_currentOffset += item->GetTabWidth();

		return item;
	}

	void TabView::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		const auto& pos = GetAbsolutePosition();		

		renderer->FillQuad(pos.x, pos.y + renderer->_style.tab_height, _size.x, _size.y - renderer->_style.tab_height, renderer->_style.tabview_back);
		renderer->Frame(pos.x, pos.y + renderer->_style.tab_height, _size.x, _size.y - renderer->_style.tab_height, 1, renderer->_style.tabview_border);

		int32_t x = pos.x;
		uint32_t idx = 0;

		/*for (auto& item : _items)
		{
			int32_t width, height;
			style.font->MeasureText((int32_t)Style::FontSize::Small, item->_label, width, height);

			if (_currentIndex == idx)
			{
				renderer->FillQuad(x, pos.y, width + 10, HeaderSize, style.tabview_tab_highlight);
				renderer->Frame(x, pos.y, width + 10, HeaderSize, 1, style.tabview_border);

				renderer->PrintText(style.font, (uint8_t)Style::FontSize::Small, x + (width / 2) + 5, pos.y + HeaderSize / 2, style.tabview_text_highlight, FontAlign::CentreUD | FontAlign::CentreLR, item->_label);
			}
			else
			{
				renderer->FillQuad(x, pos.y, width + 10, HeaderSize, style.tabview_tab_back);
				renderer->Frame(x, pos.y, width + 10, HeaderSize, 1, style.tabview_border);

				renderer->PrintText(style.font, (uint8_t)Style::FontSize::Small, x + (width / 2) + 5, pos.y + HeaderSize / 2, style.text_regular, FontAlign::CentreUD | FontAlign::CentreLR, item->_label);
			}

			++idx;
			x += width + 10;
		}*/
	}

	int32_t TabView::GetCurrentTabIndex() const
	{
		return _currentIndex;
	}

	void TabView::SetActiveTab(int32_t idx)
	{
		SetActiveTab(_items.at(idx));
	}

	void TabView::RemoveTab(TabItem* item)
	{
		auto it = std::find(_items.begin(), _items.end(), item);
		if (it == _items.end())
			return;

		const int32_t removedIndex = (int32_t)(it - _items.begin());
		const bool wasActive = (item == _selectedTab) || removedIndex == _currentIndex;

		// Hide its content now (deletion is deferred to the end of the frame).
		item->SetSelected(false);
		_items.erase(it);
		if (_selectedTab == item)
			_selectedTab = nullptr;
		item->DeleteMe();

		Relayout();

		if (_items.empty())
		{
			_currentIndex = -1;
			_selectedTab = nullptr;
			return;
		}

		if (wasActive)
		{
			SetActiveTab(_items[std::max(0, removedIndex - 1)]);
		}
		else
		{
			const auto selected = std::find(_items.begin(), _items.end(), _selectedTab);
			_currentIndex = selected != _items.end() ? (int32_t)(selected - _items.begin()) : 0;
		}
	}

	void TabView::Relayout()
	{
		// Tab content sits at -tabX inside its tab (so it lines up with the view's left
		// edge), so moving a tab moves its content back the other way.
		int32_t x = 0;
		for (auto* tab : _items)
		{
			const int32_t oldX = tab->GetPosition().x;
			if (oldX != x)
			{
				tab->SetPosition(Point(x, tab->GetPosition().y));
				for (auto* child : tab->GetChildren())
					child->SetPosition(child->GetPosition() + Point(oldX - x, 0));
			}
			x += tab->GetTabWidth();
		}
		_currentOffset = x;
	}

	void TabView::SetActiveTab(TabItem* item)
	{
		int32_t idx = 0;
		for (auto& tab : _items)
		{
			if (tab == item)
			{
				_selectedTab = tab;
				tab->SetSelected(true);
				_currentIndex = idx;
			}
			else
			{
				tab->SetSelected(false);
			}

			++idx;
		}
	}
}
