
#include "ContextMenu.hpp"
#include "../GuiRenderer.hpp"
#include "../UIManager.hpp"

namespace HexEngine
{
	ContextMenu::ContextMenu(Element* parent, const Point& position, const Point& size) :
		Element(parent, position, size)
	{
		_triangle = ITexture2D::Create("EngineData.Textures/UI/triangle.png");

		_root = new ContextRoot;
		_root->open = true;
	}

	ContextMenu::~ContextMenu()
	{
		DeleteItems(_root);

		SAFE_DELETE(_root);
	}

	void ContextMenu::DeleteItems(ContextRoot* root)
	{
		for (auto& item : root->items)
		{
			if (item->submenu != nullptr)
			{
				DeleteItems(item->submenu);
				delete item->submenu;
			}

			delete item;
		}
	}

	void ContextMenu::AddItem(ContextItem* item, ContextRoot* parent)
	{
		ContextRoot* root = parent ? parent : _root;

		if (root)
		{
			root->items.push_back(item);

			int32_t width, height;
			g_pEnv->_uiManager->GetRenderer()->_style.font->MeasureText((int32_t)Style::FontSize::Tiny, item->name, width, height);

			width += 8; // padding

			if (width > root->largestWidth)
				root->largestWidth = width;

			item->root = root;
		}
	}

	void ContextMenu::AddItems(const std::vector<ContextItem*>& items, ContextRoot* parent)
	{
		ContextRoot * root = parent ? parent : _root;

		for (auto& item : items)
			AddItem(item, root);
	}

	ContextRoot* ContextMenu::CreateSubMenu(ContextItem* root)
	{
		if (root->submenu == nullptr)
		{
			root->submenu = new ContextRoot;
			root->root->largestWidth += (uint8_t)Style::FontSize::Tiny; // add on size for the arrow icon
		}

		return root->submenu;
	}

	void ContextMenu::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		auto pos = GetAbsolutePosition();

		/*if (_size.x == -1)
			_size.x = _root->largestWidth;

		if (_size.y == -1)
			_size.y = (int32_t)_items.size() * ((int32_t)Style::FontSize::Tiny + 4);*/

		

		_hovering = nullptr;

		RenderRoot(_root, renderer, pos.x, pos.y);
	}

	void ContextMenu::RenderRoot(ContextRoot* root, GuiRenderer* renderer, int32_t x, int32_t y)
	{
		int32_t height = (int32_t)root->items.size() * ((int32_t)Style::FontSize::Tiny + 4);

		// frame and background
		renderer->FillQuad(x, y, root->largestWidth, height, renderer->_style.context_back);
		renderer->Frame(x, y, root->largestWidth, height, 1, math::Color(0, 0, 0, 1));

		for (auto& item : root->items)
		{
			if (IsMouseOver(x, y, root->largestWidth, ((int32_t)Style::FontSize::Tiny + 4)))
			{
				_didInitialHover = true;
				_hovering = item;
				renderer->FillQuad(x, y, root->largestWidth - 1, (int32_t)Style::FontSize::Tiny + 4, renderer->_style.context_highlight);

				renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, x + 2, y + ((int32_t)Style::FontSize::Tiny + 4) / 2, math::Color(0, 0, 0, 1), FontAlign::CentreUD, item->name);
			}
			else
				renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, x + 2, y + ((int32_t)Style::FontSize::Tiny + 4) / 2, math::Color(1, 1, 1, 1), FontAlign::CentreUD, item->name);

			if (item->submenu != nullptr)
			{
				renderer->FillTexturedQuad(_triangle.get(), x + root->largestWidth - (uint8_t)Style::FontSize::Tiny, y, (uint8_t)Style::FontSize::Tiny, (uint8_t)Style::FontSize::Tiny, math::Color(1, 1, 1, 1)/*, -90.0f*/);

				if (item->submenu->open)
				{
					RenderRoot(item->submenu, renderer, x + root->largestWidth, y);
				}
			}

			y += ((int32_t)Style::FontSize::Tiny + 4);
		}
	}

	bool ContextMenu::OnInputEvent(InputEvent event, InputData* data)
	{
		if (event == InputEvent::MouseDown && IsInputEnabled() && _hovering != nullptr)
		{
			if (_hovering->submenu != nullptr)
			{
				_hovering->submenu->open = !_hovering->submenu->open;
				return true;
			}
			else
			{
				if (_hovering->clickFn)
					_hovering->clickFn(_hovering->name);
			}

			if (_onClicked)
				_onClicked(_hovering);

			_didInitialHover = false;

			Disable();

			return true;
		}
		else if (_hovering == nullptr)
		{
			if (_didInitialHover == true)
			{
				Disable();
				_didInitialHover = false;
				return false;
			}
		}

		return false;
	}
}