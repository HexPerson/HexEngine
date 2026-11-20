
#include "TabItem.hpp"
#include "../GuiRenderer.hpp"
#include "TabView.hpp"
#include "../UIManager.hpp"

namespace HexEngine
{
	namespace
	{
		constexpr int32_t kCloseButtonWidth = 16;
	}

	TabItem::TabItem(TabView* parent, const Point& position, const Point& size, const std::wstring& label) :
		Element(parent, position, size),
		_label(label)
	{}

	void TabItem::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		const auto& pos = GetAbsolutePosition();
		int32_t x = 0, y = 0;

		// The tab visual only covers the label area, not the whole tab view.
		const int32_t tabWidth = GetTabWidth();
		const int32_t tabHeight = renderer->_style.tab_height;

		if (_closable)
		{
			const bool hovered = IsMouseOverCloseButton();
			if (hovered != _closeHovered)
			{
				_closeHovered = hovered;
				_canvas.Redraw();
			}
		}

		if (_canvas.BeginDraw(renderer, (uint32_t)tabWidth, (uint32_t)tabHeight))
		{
			int32_t width, height;
			renderer->_style.font->MeasureText((int32_t)Style::FontSize::Small, _label, width, height);

			if (_selected)
			{
				renderer->FillQuad(x, y, tabWidth, renderer->_style.tab_height, renderer->_style.tabview_tab_highlight);
				renderer->Frame(x, y, tabWidth, renderer->_style.tab_height, 1, renderer->_style.tabview_border);

				renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Small, x + (width / 2) + 5, y + renderer->_style.tab_height / 2, renderer->_style.tabview_text_highlight, FontAlign::CentreUD | FontAlign::CentreLR, _label);
			}
			else
			{
				renderer->FillQuad(x, y, tabWidth, renderer->_style.tab_height, renderer->_style.tabview_tab_back);
				renderer->Frame(x, y, tabWidth, renderer->_style.tab_height, 1, renderer->_style.tabview_border);

				renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Small, x + (width / 2) + 5, y + renderer->_style.tab_height / 2, renderer->_style.text_regular, FontAlign::CentreUD | FontAlign::CentreLR, _label);
			}

			if (_closable)
			{
				const int32_t closeX = tabWidth - kCloseButtonWidth;
				if (_closeHovered)
					renderer->FillQuad(closeX + 1, y + 3, kCloseButtonWidth - 4, renderer->_style.tab_height - 6, renderer->_style.button_hover);
				renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Small, closeX + (kCloseButtonWidth - 2) / 2, y + renderer->_style.tab_height / 2,
					_closeHovered ? renderer->_style.text_highlight : renderer->_style.text_regular, FontAlign::CentreUD | FontAlign::CentreLR, L"x");
			}

			_canvas.EndDraw(renderer);
		}

		_canvas.Present(renderer, pos.x, pos.y, tabWidth, tabHeight);

	}

	bool TabItem::OnInputEvent(InputEvent event, InputData* data)
	{	
		if (_closable && event == InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON && IsMouseOverCloseButton())
		{
			if (!_onClose || _onClose())
				((TabView*)GetParent())->RemoveTab(this);
			return true;
		}

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
		const bool changed = _selected != selected;
		_selected = selected;

		for (auto& child : _children)
		{
			if (selected)
				child->EnableRecursive();
			else
				child->DisableRecursive();
		}

		_canvas.Redraw();

		if (changed && _onSelectedChanged)
			_onSelectedChanged(selected);
	}

	void TabItem::SetClosable(bool closable, OnCloseFn onClose)
	{
		_closable = closable;
		_onClose = std::move(onClose);
		_tabWidth = 0;	// the header grows by the close button
		_canvas.Redraw();
		if (auto* view = dynamic_cast<TabView*>(GetParent()); view != nullptr)
			view->Relayout();
	}

	bool TabItem::IsMouseOverCloseButton() const
	{
		const auto pos = GetAbsolutePosition();
		const int32_t width = const_cast<TabItem*>(this)->GetTabWidth();
		return Element::IsMouseOver(pos.x + width - kCloseButtonWidth, pos.y, kCloseButtonWidth, g_pEnv->GetUIManager().GetRenderer()->_style.tab_height);
	}

	int32_t TabItem::GetTabWidth()
	{
		if (_tabWidth == 0)
		{
			int32_t width, height;
			g_pEnv->GetUIManager().GetRenderer()->_style.font->MeasureText((int32_t)Style::FontSize::Small, _label, width, height);

			_tabWidth = width + 10 + (_closable ? kCloseButtonWidth : 0);
		}

		return _tabWidth;
	}
}