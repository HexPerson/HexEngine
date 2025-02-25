
#pragma once

#include "Element.hpp"

namespace HexEngine
{
	class HEX_API GroupBox : public Element
	{
	public:
		GroupBox(Element* parent, const Point& position, const Point& size, const std::wstring& label);

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		virtual Point GetAbsolutePosition() const override;

	private:
		std::wstring _label;
	};
}
