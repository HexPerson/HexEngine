
#pragma once

#include "Element.hpp"

namespace HexEngine
{
	class HEX_API MenuBar : public Element
	{
	public:
		struct Item
		{
			Item() :
				parent(nullptr),
				isOpen(false),
				hoverIdx(-1),
				idx(-1)
			{}

			int32_t idx;
			std::wstring name;
			std::vector<Item*> items;
			Item* parent;
			bool isOpen;
			std::function<void(Item*)> action;
			int32_t hoverIdx;
		};

		struct RootItem : Item
		{
			
		};

		MenuBar(Element* parent, const Point& position, const Point& size);

		virtual ~MenuBar();

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		void AddRootItem(RootItem* item);

		void AddSubItem(RootItem* parent, Item* item);

		void RemoveMenuItem(Item* item);

		bool IsOpen() const;

	private:
		void RenderRootItems(GuiRenderer* renderer, int32_t x, int32_t y, uint32_t w, uint32_t h);
		void RenderSubMenu(Item* item, GuiRenderer* renderer, int32_t x, int32_t y);
		virtual bool OnInputEvent(InputEvent event, InputData* data) override;
		bool CheckForSubItemInput(Item* item, InputEvent event, InputData* data);
		void CloseAllMenus();

	private:
		std::vector<RootItem*> _rootItems;
		//Item _root;
		int32_t _rootHoverIdx = -1;
	};
}
