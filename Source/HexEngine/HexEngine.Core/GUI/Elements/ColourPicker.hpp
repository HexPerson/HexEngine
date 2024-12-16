
#pragma once

#include "Dialog.hpp"

namespace HexEngine
{
	class ColourPicker : public Dialog
	{
	public:
		ColourPicker(Element* parent, const Point& position, const Point& size, const std::wstring& label);


	};
}