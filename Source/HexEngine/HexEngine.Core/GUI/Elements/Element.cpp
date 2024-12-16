
#include "Element.hpp"
#include "../UIManager.hpp"

namespace HexEngine
{
	//Style Element::style;

	Element::Element(Element* parent, const Point& position) :
		_parent(parent),
		_position(position),
		_nextPos(position)
	{
		if(_parent)
			_parent->OnAddChild(this);
	}

	Element::Element(Element* parent, const Point& position, const Point& size) :
		_parent(parent),
		_position(position),
		_size(size)
	{
		if (_parent)
			_parent->OnAddChild(this);
	}

	Element::~Element()
	{
		if (_parent)
			_parent->OnRemoveChild(this);
	}

	Element* Element::GetParent() const
	{
		return _parent;
	}

	const Point& Element::GetPosition() const
	{
		return _position;
	}

	Point Element::GetAbsolutePosition() const
	{
		Point p = GetPosition();		

		if (Element* parent = GetParent(); parent != nullptr)
		{
			p += parent->GetAbsolutePosition();
		}

		return p;
	}

	const Point& Element::GetSize() const
	{
		return _size;
	}

	const Point& Element::GetNextPos() const
	{
		return _nextPos;
	}

	const std::vector<Element*>& Element::GetChildren() const
	{
		return _children;
	}

	void Element::SetPosition(const Point& position)
	{
		_position = position;
	}

	void Element::SetSize(const Point& size)
	{
		_size = size;
	}

	void Element::OnAddChild(Element* element)
	{
		_children.push_back(element);
	}

	void Element::OnRemoveChild(Element* element)
	{
		if(auto erase = std::remove(_children.begin(), _children.end(), element); erase != _children.end())
			_children.erase(erase);
	}

	bool Element::IsMouseOver(bool absolute)
	{
		return IsMouseOver(absolute ? GetAbsolutePosition() : _position, _size);
	}

	bool Element::IsMouseOver(const Point& position, const Point& size)
	{
		return IsMouseOver(position.x, position.y, size.x, size.y);
	}

	bool Element::IsMouseOver(int32_t x, int32_t y, int32_t w, int32_t h)
	{
		int32_t mx, my;
		g_pEnv->_inputSystem->GetMousePosition(mx, my);

		if (mx >= x && mx < x + w &&
			my >= y && my < y + h)
			return true;

		return false;
	}

	bool Element::OnInputEvent(InputEvent event, InputData* data)
	{
		if (event == InputEvent::MouseDown && IsMouseOver(true))
		{
			if (_onClick)
			{
				_onClick(this, data->MouseDown.button, data->MouseDown.xpos, data->MouseDown.ypos);
				return true;
			}
		}

		return false;
	}

	void Element::BringToFront()
	{
		if (auto parent = GetParent(); parent != nullptr)
		{
			parent->BringToFront();

			Element* current = this;

			auto source = std::find(parent->_children.begin(), parent->_children.end(), this);

			std::rotate(source, source + 1, parent->_children.end());



			//std::swap(source, parent->_children.back());

			//parent->_children.splice(parent->_children.end(), parent->_children, source);
		}
	}

	void Element::SetHasInputFocus(bool focus)
	{
		if(IsInputEnabled())
			_hasInputFocus = focus;
	}

	void Element::DeleteMe()
	{
		g_pEnv->_uiManager->Lock();

		_wantsDeletion = true;		

		for (auto& child : _children)
		{
			child->DeleteMe();
		}

		g_pEnv->_uiManager->MarkForDeletion(this);

		g_pEnv->_uiManager->Unlock();
	}

	bool Element::WantsDeletion() const
	{
		return _wantsDeletion;
	}

	void Element::EnableInput(bool enable)
	{
		_inputEnabled = enable;
	}

	bool Element::IsInputEnabled() const
	{
		return _inputEnabled;
	}

	void Element::SetLabelMinSize(int32_t minSize)
	{
		_labelMinSize = minSize;
	}

	int32_t Element::GetLabelMinSize() const
	{
		return _labelMinSize;
	}

	void Element::Enable()
	{
		_enabled = true;
	}

	void Element::Disable()
	{
		_enabled = false;
	}

	void Element::EnableRecursive()
	{
		Enable();

		for (auto& c : _children)
		{
			c->EnableRecursive();
		}
	}

	void Element::DisableRecursive()
	{
		Disable();

		for (auto& c : _children)
		{
			c->DisableRecursive();
		}
	}

	bool Element::IsEnabled() const
	{
		return _enabled;
	}

	void Element::Toggle()
	{
		_enabled = !_enabled;
	}
}