
#include "MenuBar.hpp"
#include "../GuiRenderer.hpp"

namespace HexEngine
{
	const int32_t MenuItemHeight = 22;

	MenuBar::MenuBar(Element* parent, const Point& position, const Point& size) :
		Element(parent, position, size)
	{
	}

	MenuBar::~MenuBar()
	{
		// recursively remove all menu items starting at the root
		for (auto& child : _rootItems)
			RemoveMenuItem(child);
	}

	void MenuBar::AddRootItem(RootItem* item)
	{
		item->idx = (int32_t)_rootItems.size();
		_rootItems.push_back(item);
	}

	void MenuBar::AddSubItem(RootItem* parent, Item* item)
	{
		item->parent = parent;
		item->idx = (int32_t)parent->items.size();
		parent->items.push_back(item);
	}

	void MenuBar::RemoveMenuItem(Item* item)
	{
		for (auto& child : item->items)
		{
			RemoveMenuItem(child);
		}

		SAFE_DELETE(item);
	}

	void MenuBar::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		int32_t x = _position.x;
		int32_t y = _position.y;

		// draw the background bar
		renderer->FillQuadVerticalGradient(_position.x, _position.y, _size.x, _size.y, renderer->_style.win_back, renderer->_style.win_back2);
		renderer->Line(_position.x, _position.y + _size.y, _position.x + _size.x, _position.y + _size.y, renderer->_style.text_highlight);
		//renderer->Line(_position.x, _position.y + _size.y - 1, _position.x + _size.x, _position.y + _size.y - 1, style.win_highlight);

		RenderRootItems(renderer, x, y, w, h);
		//RenderItem(GetRoot(), renderer, x, y, w, h);
	}

	void MenuBar::RenderRootItems(GuiRenderer* renderer, int32_t x, int32_t y, uint32_t w, uint32_t h)
	{
		x += 20; // offset from the start

		_rootHoverIdx = -1;

		int32_t idx = 0;

		for (auto& item : _rootItems)
		{
			Point itemSize;

			renderer->_style.font->MeasureText((int32_t)Style::FontSize::Small, item->name, itemSize.x, itemSize.y);

			Point pos(x, y);
			Point centre = pos.GetCenter(itemSize);

			if (IsMouseOver(pos, Point(itemSize.x, _size.y)))
			{
				_rootHoverIdx = idx;
				renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Small, x, (_size.y / 2) - (itemSize.y / 2) + 1, renderer->_style.text_highlight, FontAlign::None, item->name);
			}
			else
				renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Small, x, (_size.y / 2) - (itemSize.y / 2) + 1, renderer->_style.text_regular , FontAlign::None, item->name);

			if (item->isOpen)
			{
				RenderSubMenu(item, renderer, x, y + _size.y);
			}

			x += itemSize.x + 26;
			idx++;
		}
	}

	void MenuBar::RenderSubMenu(Item* item, GuiRenderer* renderer, int32_t x, int32_t y)
	{
		// calculate the size of the items
		int32_t h = ((int32_t)item->items.size() * MenuItemHeight) + 8;
		int32_t w = 0;

		for (auto& item : item->items)
		{
			Point itemSize;

			renderer->_style.font->MeasureText((int32_t)Style::FontSize::Small, item->name, itemSize.x, itemSize.y);

			if (itemSize.x > w)
				w = itemSize.x;
		}

		w += 30;

		renderer->FillQuadVerticalGradient(x, y, w, h, renderer->_style.win_back, renderer->_style.win_back2);
		renderer->Frame(x, y, w, h,1, renderer->_style.win_border);

		y += 8;

		int32_t hoverIdx = 0;

		item->hoverIdx = -1;

		for (auto& item : item->items)
		{
			Point pos(x, y);
			Point size(w, MenuItemHeight);

			if (IsMouseOver(x, y, w, MenuItemHeight))
			{
				if (item->parent)
					item->parent->hoverIdx = hoverIdx;

				renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Small, x + 15, y, renderer->_style.text_highlight, FontAlign::None, item->name);
			}
			else
			{
				renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Small, x + 15, y, renderer->_style.text_regular, FontAlign::None, item->name);
			}

			y += MenuItemHeight;
			hoverIdx++;
		}
	}

	bool MenuBar::OnInputEvent(InputEvent event, InputData* data)
	{
		if (_rootHoverIdx != -1)
		{
			if (event == InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON)
			{
				BringToFront();

				// Open the target
				auto& rootItem = _rootItems[_rootHoverIdx];

				// Close all menus
				for (auto& item : _rootItems)
				{
					if(item != rootItem)
						item->isOpen = false;
				}
				

				rootItem->isOpen = !rootItem->isOpen;

				return true;
			}
		}
		else
		{
			bool ret = true;
			for (auto& item : _rootItems)
			{
				if (CheckForSubItemInput(item, event, data) == false)
					ret = false;
				else
				{
					ret = true;
					break;
				}
			}

			return ret;
		}

		return false;
	}
	
	bool MenuBar::CheckForSubItemInput(Item * item, InputEvent event, InputData* data)
	{
		bool ret = false;

		if (item->parent && item->parent->hoverIdx == item->idx && event == InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON)
		{
			if (item->action)
				item->action(item);

			item->parent->hoverIdx = -1;
			CloseAllMenus();
			ret = true;
		}

		for (auto& child : item->items)
		{
			if (CheckForSubItemInput(child, event, data) == false)
				ret = false;
			else
			{
				ret = true;
				break;
			}
		}

		return ret;
	}

	void MenuBar::CloseAllMenus()
	{
		for (auto& item : _rootItems)
		{
			item->isOpen = false;
		}

		_rootHoverIdx = -1;
	}

	bool MenuBar::IsOpen() const
	{
		for (auto& item : _rootItems)
		{
			if (item->isOpen)
				return true;
		}
		return false;
	}
}