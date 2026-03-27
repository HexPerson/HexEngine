
#include "TreeList.hpp"
#include "../GuiRenderer.hpp"
#include "../../Environment/IEnvironment.hpp"
#include "../../Graphics/IGraphicsDevice.hpp"
#include "../../Environment/LogFile.hpp"
#include "../../Input/CommandManager.hpp"
#include "../../Input/Console.hpp"

namespace HexEngine
{
	

	TreeList::TreeList(Element* parent, const Point& position, const Point& size) :
		ScrollView(parent, position, size)
	{
	}

	TreeList::~TreeList()
	{
		for (auto& item : _items)
		{
			SAFE_DELETE(item);
		}
	}

	/*void TreeList::CreateRenderTarget()
	{
		SAFE_DELETE(_renderTarget);

		_renderTarget = g_pEnv->_graphicsDevice->CreateTexture2D(
			_size.x, _size.y,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0,
			1,
			0,
			D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			D3D11_SRV_DIMENSION_TEXTURE2D);

		if (!_renderTarget)
		{
			LOG_CRIT("Failed to create render target for TreeList");
			return;
		}

		_renderTarget->SetDebugName("TreeList");
	}*/

	void TreeList::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		ScrollView::Render(renderer, w, h);

		auto pos = GetAbsolutePosition();

		SetManualContentHeight(std::max(_size.y, CountVisibleRows(_items) * TreeListLineHeight));

		_hoveredItem = nullptr;
		_dragTarget = nullptr;
		int32_t y = pos.y - (int32_t)std::round(GetScrollOffset());
		UpdateHovering(renderer, _items, y);

		Point drawPos(pos.x, pos.y - (int32_t)std::round(GetScrollOffset()));
		int32_t c = -1;

		{
			std::unique_lock lock(_lock);
			_renderCount = 0;
			RenderItems(renderer, drawPos, pos, _items, c);
		}

		if (_draggedItem != nullptr)
		{
			int32_t mx, my;
			g_pEnv->_inputSystem->GetMousePosition(mx, my);
			Point hoverPos(mx, my);
			RenderItem(renderer, hoverPos, Point(mx, my), _draggedItem, nullptr, c);
		}

		if (IsMouseOver(true) == false)
			_hoveredItem = nullptr;
	}

	void TreeList::UpdateHovering(GuiRenderer* renderer, const std::vector<ListNode*>& items, int32_t& y)
	{
		auto pos = GetAbsolutePosition();
		
		//pos += GetPosition();

		std::unique_lock lock(_lock);

		for (auto& item : items)
		{
			if (item->_parent != nullptr && item->_parent->_isOpen == false)
				continue;

			Point highlightPos(pos.x, y);
			Point highlightSize(_size.x, TreeListLineHeight);

			if (highlightPos.y + highlightSize.y >= pos.y && highlightPos.y <= pos.y + _size.y)
			{
				if (IsMouseOver(highlightPos, highlightSize))
				{
					renderer->FillQuad(highlightPos.x, highlightPos.y, highlightSize.x, highlightSize.y, renderer->_style.listbox_highlight);
					_hoveredItem = item;
					if (_draggedItem != nullptr && item != _draggedItem)
					{
						_dragTarget = item;
					}
				}
			}			

			if (_lastHoveredItem != _hoveredItem)
			{
				//_redrawRenderTarget = true;
				//_canvas.Redraw();
				_lastHoveredItem = _hoveredItem;

				//CON_ECHO("Hovered item changed");
			}

			y += item->GetHeight();

			if (item->_child && item->_isOpen)
			{
				UpdateHovering(renderer, item->_items, y);
			}
		}
	}

	/*void TreeList::SetOnSelectItem(OnSelectItem cb)
	{
		_onSelect = cb;
	}*/

	bool TreeList::OnInputEvent(InputEvent event, InputData* data)
	{
		if (ScrollView::OnInputEvent(event, data))
		{
			return true;
		}

		if (event == InputEvent::MouseDown)
		{
			if (IsMouseOver(true))
			{
				if (data->MouseDown.button == VK_LBUTTON)
				{
					_isDragging = true;
					_dragStart = Point(data->MouseDown.xpos, data->MouseDown.ypos);
				}
				else if(data->MouseDown.button == VK_RBUTTON)
				{
					if (_hoveredItem != nullptr)
					{
						_hoveredItem->OnClick(VK_RBUTTON, data->MouseDown.xpos, data->MouseDown.ypos);
					}
				}
			}
		}
		else if (event == InputEvent::MouseUp)
		{
			if (data->MouseUp.button == VK_LBUTTON)
			{
				const bool isMouseOver = IsMouseOver(true);
				const bool hadDraggedItem = _draggedItem != nullptr;
				bool handledDrop = false;

				if (_draggedItem && _dragTarget && _isDragging)
				{
					_draggedItem->OnDragAndDrop(_dragTarget);
					handledDrop = true;
				}

				if (isMouseOver && !hadDraggedItem && _hoveredItem != nullptr)
				{
					_hoveredItem->_isOpen = !_hoveredItem->_isOpen;
					_hoveredItem->OnClick(VK_LBUTTON, data->MouseUp.xpos, data->MouseUp.ypos);

					SetManualContentHeight(std::max(_size.y, CountVisibleRows(_items) * TreeListLineHeight));
					_canvas.Redraw();
				}

				ResetDragState();

				if (handledDrop)
					return true;
			}
		}
		else if (event == InputEvent::MouseMove)
		{
			if (_isDragging && _hoveredItem != nullptr)
			{
				auto dx = data->MouseMove.x - _dragStart.x;
				auto dy = data->MouseMove.y - _dragStart.y;

				if (abs(dx) >= 5 || abs(dy) >= 5)
				{
					_draggedItem = _hoveredItem;
				}
			}

			if (IsMouseOver(true))
			{
				_canvas.Redraw();
			}
		}

		if (event == InputEvent::MouseDown || event == InputEvent::MouseUp)
		{
			_canvas.Redraw();
		}

		return false;
	}

#if 0
	ListNode* TreeList::AddItem(const std::wstring& label, ITexture2D* icons[2], void* objectPtr)
	{
		std::unique_lock lock(_lock);

		if (auto exist = FindItemByLabelParented(label); exist != nullptr)
			return exist;

		bool openByDefault = _items.size() == 0;

		_items.push_back(new ListNode{
			label,
			icons ? icons[0] : nullptr,
			icons ? icons[1] : nullptr,
			ITexture2D::Create("EngineData.Textures/UI/triangle.png"),
			nullptr,
			nullptr,
			openByDefault,
			{}, 
			objectPtr });

		_numItems++;

		return _items.at(_items.size() - 1);
	}

	ListNode* TreeList::AddItem(ListNode* parent, const std::wstring& label, ITexture2D* icons[2], void* objectPtr)
	{
		std::unique_lock lock(_lock);

		if (auto exist = FindItemByLabelParented(label, parent); exist != nullptr)
			return exist;

		if (parent)
		{
			auto item = new ListNode{
				label,
				icons ? icons[0] : nullptr,
				icons ? icons[1] : nullptr,
				ITexture2D::Create("EngineData.Textures/UI/triangle.png"),
				nullptr,
				parent,
				false,
				{},
				objectPtr };

			parent->_items.push_back(item);
			parent->_child = item;

			_numItems++;

			return item;
		}
		else
			return AddItem(label, icons);
	}
#endif

	void TreeList::Repaint()
	{
		_canvas.Redraw();
	}

	void TreeList::AddNode(ListNode* node, ListNode* parent, bool repaintImmediately)
	{
		std::unique_lock lock(_lock);

		// don't add it if it already exists
		//
		if (auto exist = FindItemByIdParented(node->_id, parent); exist != nullptr)
			return;

		if (parent)
		{
			parent->_items.push_back(node);
			parent->_child = node;

			node->_parent = parent;

			_numItems++;
		}
		else
		{
			_items.push_back(node);
			_numItems++;
		}

		if(repaintImmediately)
			_canvas.Redraw();

		SetManualContentHeight(std::max(_size.y, CountVisibleRows(_items) * TreeListLineHeight));

		_currentNodeId++;
	}

	ListNode* TreeList::FindItemByLabel(const std::wstring& label)
	{
		std::unique_lock lock(_lock);

		for (auto& item : _items)
		{
			if (item->_label == label)
				return item;
		}
		return nullptr;
	}

	ListNode* TreeList::FindItemByLabelParented(const std::wstring& label, ListNode* parent)
	{
		std::unique_lock lock(_lock);

		for (auto& item : parent ? parent->_items : _items)
		{
			if (item->_label == label)
				return item;

			if (item->_child)
			{
				if (auto itemFound = FindItemByLabelParented(label, item); itemFound != nullptr)
					return itemFound;
			}
		}
		return nullptr;
	}

	ListNode* TreeList::FindItemById(int32_t id)
	{
		std::unique_lock lock(_lock);

		for (auto& item : _items)
		{
			if (item->_id == id)
				return item;
		}
		return nullptr;
	}

	ListNode* TreeList::FindItemByIdParented(int32_t id, ListNode* parent)
	{
		std::unique_lock lock(_lock);

		for (auto& item : parent ? parent->_items : _items)
		{
			if (item->_id == id)
				return item;

			if (item->_child)
			{
				if (auto itemFound = FindItemByIdParented(id, item); itemFound != nullptr)
					return itemFound;
			}
		}
		return nullptr;
	}

	ListNode* TreeList::FindItemByObjectPtr(void* objectPtr, ListNode* parent)
	{
		std::unique_lock lock(_lock);

		for (auto& item : parent ? parent->_items : _items)
		{
			if (item->GetObjectPtr() == objectPtr)
				return item;

			if (item->_child)
			{
				if (auto itemFound = FindItemByObjectPtr(objectPtr, item); itemFound != nullptr)
					return itemFound;
			}
		}
		return nullptr;
	}

	void TreeList::RemoveItem(const std::wstring& label)
	{
		std::unique_lock lock(_lock);

		for (auto& item : _items)
		{
			if (item->_label == label)
			{
				auto it = std::remove(_items.begin(), _items.end(), item);
				if (it != _items.end())
				{
					delete* it;
					_items.erase(it);
				}
				if (_hoveredItem == item)
					_hoveredItem = nullptr;

				return;
			}

			if (item->_child)
			{
				RemoveItem(item->_child, label);
			}
		}
	}

	void TreeList::RemoveItem(ListNode* item, const std::wstring& label)
	{
		std::unique_lock lock(_lock);

		for (auto& item : item->_items)
		{
			if (item->_label == label)
			{
				auto it = std::remove(_items.begin(), _items.end(), item);
				if (it != _items.end())
				{
					delete* it;
					_items.erase(it);
				}

				if (_hoveredItem == item)
					_hoveredItem = nullptr;

				return;
			}

			if (item->_child)
			{
				RemoveItem(item->_child, label);
			}
		}
	}

	void TreeList::Clear()
	{
		std::unique_lock lock(_lock);

		_hoveredItem = nullptr;
		ResetDragState();

		for (auto& item : _items)
		{
			ClearItem(item);
			delete item;
		}
		_items.clear();
		SetManualContentHeight(_size.y);
		_canvas.Redraw();
	}

	void TreeList::ResetDragState()
	{
		_isDragging = false;
		_draggedItem = nullptr;
		_dragTarget = nullptr;
	}

	void TreeList::ClearItem(ListNode* item)
	{
		std::unique_lock lock(_lock);

		for (auto& item : item->_items)
		{
			ClearItem(item);
			delete item;
		}
		item->_items.clear();
	}

	int32_t TreeList::GetCurrentNodeId() const
	{
		return _currentNodeId;
	}

	void TreeList::RenderItems(GuiRenderer* renderer, Point& position, const Point& absolutePos, const std::vector<ListNode*>& items, int32_t& c)
	{
		Point itemPos = position;
		Point itemSize(_size.x, TreeListLineHeight);

		ListNode* parent = nullptr;

		int32_t i = 0;
		for (auto& item : items)
		{
			

			// check that the parent is open
			if (item->_parent != nullptr)
			{
				if (item->_parent->_isOpen == false)
					continue;
			}

			/*if (IsMouseOver(Point(absolutePos.x, position.y), itemSize))
			{
				if (_draggedItem)
					_dragTarget = item;
				else
					_hoveredItem = item;
			}*/

			if (RenderItem(renderer, position, absolutePos, item, parent, c))
				position.y += TreeListLineHeight;

			if (item->_items.size() > 0 )
			{
				if (item->_isOpen)
				{
					position.x += 20;

					RenderItems(renderer, position, absolutePos, item->_items, c);

					position.x -= 20;
				}

				

				//position.y += TreeListLineHeight;// *item->items.size();
				//highlightPos.y += TreeListLineHeight;// *item->items.size();
			}

			i++;
		}
	}

	bool TreeList::RenderItem(GuiRenderer* renderer, Point& position, const Point& absolutePos, ListNode* item, ListNode* parent, int32_t& c)
	{
		 if (position.y + TreeListLineHeight < absolutePos.y - TreeListLineHeight)
			return true;

		if (position.y > absolutePos.y + _size.y)
			return false;

		if (_hoveredItem == item)
		{
			Point highlightPos(position.x, position.y);
			Point highlightSize(_size.x, item->GetHeight());

			renderer->FillQuad(highlightPos.x, highlightPos.y, highlightSize.x, highlightSize.y, renderer->_style.listbox_highlight);
		}

		item->Render(renderer, position, Point(_size.x, TreeListLineHeight));

		/*if (item->_child)
		{
			renderer->PushFillTexturedQuad(item->_arrowIcon, _position.x + _size.x - TreeListLineHeight, absolutePos.y, TreeListLineHeight, TreeListLineHeight, math::Color(HEX_RGBA_TO_FLOAT4(100, 100, 100, 255)), item->_isOpen ? 0.0f : 90.0f);

			if (item->_isOpen)
			{
				if (item->_openIcon)
					renderer->PushFillTexturedQuad(item->_openIcon, position.x + 4, position.y + 1, 16, 16, math::Color(1, 1, 1, 1));
			}
			else if (item->_isOpen == false)
			{
				if (item->_closeIcon)
					renderer->PushFillTexturedQuad(item->_closeIcon, position.x + 4, position.y + 1, 16, 16, math::Color(1, 1, 1, 1));
			}
		}
		else
		{
			if (item->_openIcon)
				renderer->PushFillTexturedQuad(item->_openIcon, position.x + 4, position.y + 1, 16, 16, math::Color(1, 1, 1, 1));
		}

		renderer->PushPrintText(renderer->_style.font, (uint8_t)Style::FontSize::Tiny, position.x + 24, position.y + TreeListLineHeight / 2, renderer->_style.text_regular, FontAlign::CentreUD, item->_label);*/


		//pos.y += lineHeight;
		//rectPos.y += lineHeight;

		//if (item->child != nullptr && item->isOpen)
		//	pos.x += 20;
		//else// if(i == _items.size() - 1)
		//	pos.x -= 20;

		//i++;

		_renderCount++;

		return true;
	}

	int32_t TreeList::CountVisibleRows(const std::vector<ListNode*>& items)
	{
		std::unique_lock lock(_lock);

		int32_t rows = 0;

		for (auto* item : items)
		{
			if (item == nullptr)
				continue;

			++rows;

			if (item->_child && item->_isOpen)
			{
				rows += CountVisibleRows(item->_items);
			}
		}

		return rows;
	}
}
