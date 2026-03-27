
#pragma once

#include "ScrollView.hpp"
#include "ListNode.hpp"

namespace HexEngine
{
	

	class HEX_API TreeList : public ScrollView
	{
	public:
		//using OnSelectItem = std::function<bool(TreeList*, ListNode*, int32_t mouseButton)>;
		//using OnDragAndDropItem = std::function<bool(TreeList*, ListNode* dragSource, ListNode* dragTarget)>;

		TreeList(Element* parent, const Point& position, const Point& size);

		virtual ~TreeList();

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		//ListNode* AddItem(const std::wstring& label, ITexture2D* icons[2] = nullptr, void* objectPtr = nullptr);
		//ListNode* AddItem(ListNode* parent, const std::wstring& label, ITexture2D* icons[2] = nullptr, void* objectPtr = nullptr);
		void AddNode(ListNode* node, ListNode* parent = nullptr, bool repaintImmediately = true);
		ListNode* FindItemByObjectPtr(void* objectPtr, ListNode* parent = nullptr);
		void RemoveItem(const std::wstring& label);
		void RemoveItem(ListNode* parent, const std::wstring& label);
		void Clear();
		void ClearItem(ListNode* item);
		void Repaint();

		ListNode* FindItemByLabel(const std::wstring& label);
		ListNode* FindItemByLabelParented(const std::wstring& label, ListNode* parent = nullptr);

		ListNode* FindItemById(int32_t id);
		ListNode* FindItemByIdParented(int32_t id, ListNode* parent = nullptr);

		//void SetOnSelectItem(OnSelectItem cb);

		virtual bool OnInputEvent(InputEvent event, InputData* data) override;

		int32_t GetCurrentNodeId() const;

	private:
		void RenderItems(GuiRenderer* renderer, Point& position, const Point& absolutePos, const std::vector<ListNode*>& items, int32_t& c);
		bool RenderItem(GuiRenderer* renderer, Point& position, const Point& absolutePos, ListNode* item, ListNode* parent, int32_t& c);
		int32_t CountVisibleRows(const std::vector<ListNode*>& items);
		
	private:
		//void CreateRenderTarget();
		void UpdateHovering(GuiRenderer* renderer, const std::vector<ListNode*>& items, int32_t& y);
		void ResetDragState();

	private:
		std::vector<ListNode*> _items;
		//ITexture2D* _oldDepthStenchil = nullptr;
		//D3D11_VIEWPORT _oldViewport;
		//int32_t _currentIndex = -1;
		ListNode* _hoveredItem = nullptr;
		ListNode* _lastHoveredItem = nullptr;
		int32_t _numItems = 0;
		int32_t _renderCount = 0;
		Point _dragStart;
		bool _isDragging = false;
		ListNode* _draggedItem = nullptr;
		ListNode* _dragTarget = nullptr;
		std::recursive_mutex _lock;
		int32_t _currentNodeId = 0;
		//ITexture2D* _renderTarget = nullptr;
		//bool _redrawRenderTarget = true;

	public:
		//OnSelectItem _onSelect;
		//OnDragAndDropItem _onDragAndDrop;
	};
}
