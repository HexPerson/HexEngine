
#pragma once

#include "Element.hpp"
#include "TabItem.hpp"

namespace HexEngine
{
	class HEX_API TabView : public Element
	{
	public:
		TabView(Element* parent, const Point& position, const Point& size);

		TabItem* AddTab(const std::wstring& label);

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		int32_t GetCurrentTabIndex() const;

		void SetActiveTab(int32_t idx);
		void SetActiveTab(TabItem* item);

		// Removes and deletes `item`, sliding later tabs (and their content) left. If
		// it was active, the tab before it becomes active.
		void RemoveTab(TabItem* item);

		// Re-packs the tab headers left to right (after a header changes width).
		void Relayout();
		const std::vector<TabItem*>& GetTabs() const { return _items; }

	private:
		std::vector<TabItem*> _items;
		int32_t _currentIndex = -1;
		int32_t _currentOffset = 0;
		TabItem* _selectedTab = nullptr;
	};
}
