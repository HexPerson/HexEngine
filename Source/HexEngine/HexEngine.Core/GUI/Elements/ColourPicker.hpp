
#pragma once

#include "Dialog.hpp"

namespace HexEngine
{
	class HEX_API ColourPicker : public Dialog
	{
	public:
		ColourPicker(Element* parent, const Point& position, const Point& size, const std::wstring& label, math::Color* col);

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;

	private:
		math::Color* _colour;
	};
}