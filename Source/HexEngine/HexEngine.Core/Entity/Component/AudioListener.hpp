
#pragma once

#include "BaseComponent.hpp"

namespace HexEngine
{
	class AudioListener : public BaseComponent
	{
	public:
		CREATE_COMPONENT_ID(AudioListener);
		DEFINE_COMPONENT_CTOR(AudioListener);
	};
}
