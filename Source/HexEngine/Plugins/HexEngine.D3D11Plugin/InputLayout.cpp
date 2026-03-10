

#include "InputLayout.hpp"

InputLayout::~InputLayout()
{
	Destroy();
}

void InputLayout::Destroy()
{
	SAFE_RELEASE(_inputLayout);
}

void* InputLayout::GetNativePtr()
{
	return reinterpret_cast<void*>(_inputLayout);
}