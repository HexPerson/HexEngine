
#pragma once

#include "../Required.hpp"

namespace HexEngine
{
	class InputDevice
	{
	public:
		InputDevice(const std::string& deviceName);

		virtual void OnRawInput(RAWINPUT* input) {}

		const std::string& GetDeviceName() const;

	private:
		std::string _deviceName;
	};
}
