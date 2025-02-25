
#pragma once

#include "Dialog.hpp"
#include "EntityList.hpp"

namespace HexEngine
{
	class HEX_API EntitySelector : public Dialog
	{
	public:
		EntitySelector(Element* parent, const Point& position, const Point& size, const std::wstring& label, ComponentSignature componentMask);

		EntityList* GetList() const;

	private:
		EntityList* _list = nullptr;
		uint32_t _componentMask = 0x7FFFFFFF;
	};
}