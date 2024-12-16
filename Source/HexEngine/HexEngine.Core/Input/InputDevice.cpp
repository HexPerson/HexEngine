
#include "InputDevice.hpp"

namespace HexEngine
{
	InputDevice::InputDevice(const std::string& deviceName) :
		_deviceName(deviceName)
	{}

	const std::string& InputDevice::GetDeviceName() const
	{
		return _deviceName;
	}
}