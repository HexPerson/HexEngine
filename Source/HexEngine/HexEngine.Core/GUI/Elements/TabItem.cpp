
#include "TabItem.hpp"
#include "../GuiRenderer.hpp"
#include "TabView.hpp"
#include "../UIManager.hpp"

namespace HexEngine
{
	TabItem::TabItem(TabView* parent, const Point& position, const Point& size, const std::wstring& label) :
		Element(parent, position, size),
		_label(label)
	{}

	void TabItem::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		const auto& pos = GetAbsolutePosition();
		int32_t x = 0, y = 0;

		if (_canvas.BeginDraw(renderer, w, h))
		{
			int32_t width, height;
			renderer->_style.font->MeasureText((int32_t)Style::FontSize::Small, _label, width, height);

			if (_selected)
			{
				renderer->FillQuad(x, y, width + 10, renderer->_style.tab_height, renderer->_style.tabview_tab_highlight);
				renderer->Frame(x, y, width + 10, renderer->_style.tab_height, 1, renderer->_style.tabview_border);

				renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Small, x + (width / 2) + 5, y + renderer->_style.tab_height / 2, renderer->_style.tabview_text_highlight, FontAlign::CentreUD | FontAlign::CentreLR, _label);
			}
			else
			{
				renderer->FillQuad(x, y, width + 10, renderer->_style.tab_height, renderer->_style.tabview_tab_back);
				renderer->Frame(x, y, width + 10, renderer->_style.tab_height, 1, renderer->_style.tabview_border);

				renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Small, x + (width / 2) + 5, y + renderer->_style.tab_height / 2, renderer->_style.text_regular, FontAlign::CentreUD | FontAlign::CentreLR, _label);
			}

			_canvas.EndDraw(renderer);
		}

		_canvas.Present(renderer, pos.x, pos.y, w, h);

	}

	bool TabItem::OnInputEvent(InputEvent event, InputData* data)
	{	
		if (event == InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON &&
			IsMouseOver(GetAbsolutePosition(), Point(GetTabWidth(), g_pEnv->GetUIManager().GetRenderer()->_style.tab_height)))
		{
			((TabView*)GetParent())->SetActiveTab(this);			
			return true;
		}

		return Element::OnInputEvent(event, data);
	}

	void TabItem::SetSelected(bool selected)
	{
		_selected = selected;

		for (auto& child : _children)
		{
			if (selected)
				child->EnableRecursive();
			else
				child->DisableRecursive();
		}

		_canvas.Redraw();
	}

	int32_t TabItem::GetTabWidth()
	{
		if (_tabWidth == 0)
		{
			int32_t width, height;
			g_pEnv->GetUIManager().GetRenderer()->_style.font->MeasureText((int32_t)Style::FontSize::Small, _label, width, height);

			_tabWidth = width + 10;
		}

		return _tabWidth;
	}
}