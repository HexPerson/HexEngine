
#pragma once

#include "Element.hpp"

namespace HexEngine
{
	class HEX_API Dock : public Element
	{
	public:
		enum class Anchor
		{
			Left,
			Right,
			Top,
			Bottom,
			Middle
		};

		

		Dock(Element* parent, const Point& position, const Point& size, Anchor anchor);

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		virtual bool OnInputEvent(InputEvent event, InputData* data) override;

		

	private:
		Anchor _anchor;
		
	};
}
