
#pragma once

#include "BaseComponent.hpp"

namespace HexEngine
{
	class HEX_API AudioListener : public BaseComponent
	{
	public:
		CREATE_COMPONENT_ID(AudioListener);
		DEFINE_COMPONENT_CTOR(AudioListener);
	};
}
