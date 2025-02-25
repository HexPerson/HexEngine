
#pragma once

#include "Element.hpp"

namespace HexEngine
{
	class HEX_API VerticalLayout : public Element
	{
	public:
		virtual void OnAddChild(Element* child) override;
		virtual void OnRemoveChild(Element* child) override;
	};
}
