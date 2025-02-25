
#pragma once

#include "Element.hpp"

namespace HexEngine
{
	class TabView;
	class HEX_API TabItem : public Element
	{
	public:
		TabItem(TabView* parent, const Point& position, const Point& size, const std::wstring& label);

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;
		virtual bool OnInputEvent(InputEvent event, InputData* data) override;

		void SetSelected(bool selected = true);

		int32_t GetTabWidth();

	private:
		std::wstring _label;
		bool _selected = false;
		int32_t _tabWidth = 0;
	};
}