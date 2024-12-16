
#pragma once

#include "InputDevice.hpp"

namespace HexEngine
{
	class MouseInputDevice : public InputDevice
	{
	public:
		MouseInputDevice();

		virtual void OnRawInput(RAWINPUT* input) override;
	};
}
