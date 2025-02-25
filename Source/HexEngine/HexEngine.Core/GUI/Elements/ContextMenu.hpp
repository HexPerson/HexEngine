
#pragma once

#include "Element.hpp"

namespace HexEngine
{
	using ContextClickFn = std::function<void(const std::wstring&)>;
	struct ContextRoot;

	struct ContextItem
	{
		ContextItem(const std::wstring& itemName, ContextClickFn itemClickFn) :
			name(itemName),
			clickFn(itemClickFn),
			submenu(nullptr),
			root(nullptr)
		{}

		std::wstring name;
		ContextClickFn clickFn;		
		ContextRoot* submenu;
		ContextRoot* root;
	};

	struct ContextRoot
	{
		ContextRoot() :
			largestWidth(0),
			open(false)
		{}

		std::vector<ContextItem*> items;
		int32_t largestWidth;
		bool open;
	};

	class HEX_API ContextMenu : public Element
	{
	public:
		ContextMenu(Element* parent, const Point& position, const Point& size = Point(-1,-1));

		virtual ~ContextMenu();

		void AddItem(ContextItem* item, ContextRoot* parent= nullptr);
		void AddItems(const std::vector<ContextItem*>& items, ContextRoot* parent = nullptr);
		ContextRoot* CreateSubMenu(ContextItem* root);

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		virtual bool OnInputEvent(InputEvent event, InputData* data) override;

		void DeleteItems(ContextRoot* root);

	private:
		void RenderRoot(ContextRoot* root, GuiRenderer* renderer, int32_t x, int32_t y);

	private:
		ContextRoot* _root = nullptr;
		ContextItem* _hovering = nullptr;
		bool _didInitialHover = false;
		std::shared_ptr<ITexture2D> _triangle;

	public:
		std::function<void(ContextItem*)> _onClicked;
	};
}
