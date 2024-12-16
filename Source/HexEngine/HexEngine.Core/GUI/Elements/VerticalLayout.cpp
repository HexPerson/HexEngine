
#include "VerticalLayout.hpp"

namespace HexEngine
{
	void VerticalLayout::OnAddChild(Element* child)
	{
		_nextPos.y += child->GetSize().y;
	}
	
	void VerticalLayout::OnRemoveChild(Element* child)
	{
		_nextPos.y -= child->GetSize().y;
	}
}