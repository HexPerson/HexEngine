
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
		Element(parent, position, size)
	{
		_canvas.Create(size.x, size.y);
	}

	TreeList::~TreeList()
	{
		for (auto& item : _items)
		{
			SAFE_DELETE(item);
		}

		_canvas.Destroy();
		

		//SAFE_DELETE(_renderTarget);
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
		//D3DPERF_BeginEvent(0xffffffff, L"Rendering TreeList");

		auto pos = GetAbsolutePosition();

		renderer->FillQuad(pos.x, pos.y, _size.x, _size.y, renderer->_style.listbox_back);

		int32_t y = 0, i = 0;
		UpdateHovering(renderer, _items, y, i);

		if(_canvas.BeginDraw(renderer, _size.x, _size.y))
		{
			pos.x = pos.y = 0;

			auto originalPos = pos;

			//g_pEnv->_graphicsDevice->SetScissorRect({ pos.x, pos.y, pos.x + _size.x, pos.y + _size.y });

			

			_renderCount = 0;

			

			int32_t c = -1;

			//for (int32_t y = 0, i = 0; y < _size.y; y += TreeListLineHeight, i++)
			//{
			//	if (y > pos.y + _size.y)
			//		break;

			//	Point highlightPos(pos.x, pos.y + y);
			//	Point highlightSize(_size.x, TreeListLineHeight);

			//	if (IsMouseOver(highlightPos, highlightSize))
			//	{
			//		renderer->PushFillQuad(highlightPos.x, highlightPos.y, highlightSize.x, highlightSize.y, renderer->_style.listbox_highlight);
			//	}
			//	//else if (i % 2 == 0)
			//	//	renderer->PushFillQuad(highlightPos.x, highlightPos.y, highlightSize.x, highlightSize.y, renderer->_style.listbox_alternate_colour);
			//}

			std::unique_lock lock(_lock);

			RenderItems(renderer, pos, pos, _items, c);

			renderer->Frame(originalPos.x, originalPos.y, _size.x, _size.y, 1, renderer->_style.listbox_border);

			if (_draggedItem != nullptr)
			{
				int32_t mx, my;
				g_pEnv->_inputSystem->GetMousePosition(mx, my);

				Point hoverPos(mx, my);
				RenderItem(renderer, hoverPos, Point(mx, my), _draggedItem, nullptr, c);
			}

			_canvas.EndDraw(renderer);
		}

		if(IsMouseOver(true) == false)
			_hoveredItem = nullptr;		

		/*renderer->PushFillTexturedQuad(_renderTarget,
			GetAbsolutePosition().x, GetAbsolutePosition().y,
			_size.x, _size.y,
			math::Color(1, 1, 1, 1));*/

		_canvas.Present(renderer,
			GetAbsolutePosition().x, GetAbsolutePosition().y,
			_size.x, _size.y);

		

		//D3DPERF_EndEvent();
	}

	void TreeList::UpdateHovering(GuiRenderer* renderer, const std::vector<ListNode*>& items, int32_t& y, int32_t& i)
	{
		auto pos = GetAbsolutePosition();
		
		//pos += GetPosition();

		std::unique_lock lock(_lock);

		for (auto& item : items)
		{
			Point highlightPos(pos.x, pos.y + y);
			Point highlightSize(_size.x, TreeListLineHeight);

			if (i < _offset)
			{
				i++;

				if (item->_child && item->_isOpen)
				{
					UpdateHovering(renderer, item->_items, y, i);
				}
				//y += item->GetHeight();
				continue;
			}

			if (IsMouseOver(highlightPos, highlightSize))
			{
				renderer->FillQuad(highlightPos.x, highlightPos.y, highlightSize.x, highlightSize.y, renderer->_style.listbox_highlight);

				_hoveredItem = item;

				
			}

			++i;

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
				UpdateHovering(renderer, item->_items, y, i);
			}
		}
	}

	/*void TreeList::SetOnSelectItem(OnSelectItem cb)
	{
		_onSelect = cb;
	}*/

	bool TreeList::OnInputEvent(InputEvent event, InputData* data)
	{
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
			if (IsMouseOver(true))
			{
				/*if (_onSelect && _hoveredItem)
				{
					if (_onSelect(this, _hoveredItem, data->MouseDown.button))
						return true;
				}*/

				if (data->MouseDown.button == VK_LBUTTON)
				{
					if (_draggedItem == nullptr)
					{
						_isDragging = false;
					}

					if (_draggedItem && _dragTarget)
					{
						if (_isDragging)
							_draggedItem->OnDragAndDrop(_dragTarget);
							//_onDragAndDrop(this, _draggedItem, _dragTarget);

						_isDragging = false;
						_draggedItem = nullptr;
						_dragTarget = nullptr;

						return true;
					}

					if (_hoveredItem != nullptr)
					{
						_hoveredItem->_isOpen = !_hoveredItem->_isOpen;
						//_redrawRenderTarget = true;
						_canvas.Redraw();

						_hoveredItem->OnClick(VK_LBUTTON, data->MouseDown.xpos, data->MouseDown.ypos);
					}

					
				}
			}
		}
		else if (event == InputEvent::MouseWheel && IsMouseOver(true))
		{
			_offset -= (int32_t)data->MouseWheel.delta;

			if (_offset < 0)
				_offset = 0;

			//_redrawRenderTarget = true;
			_canvas.Redraw();
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
			if (item->_objectPtr == objectPtr)
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
		_draggedItem = nullptr;
		_isDragging = false;

		for (auto& item : _items)
		{
			ClearItem(item);
			delete item;
		}
		_items.clear();
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

			if(RenderItem(renderer, position, absolutePos, item, parent, c))
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
		/*if (position.y < GetAbsolutePosition().y - 8)
			return false;

		if (position.y > GetAbsolutePosition().y + _size.y)
			return false;*/

		if (++c < _offset)
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
}