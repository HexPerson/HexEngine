
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

	void TabView::SetActiveTab(TabItem* item)
	{
		for (auto& tab : _items)
		{
			if (tab == item)
			{
				_selectedTab = tab;
				tab->SetSelected(true);
			}
			else
			{
				tab->SetSelected(false);
			}
		}
	}
}