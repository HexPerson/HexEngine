

#pragma once

#include "BaseComponent.hpp"
#include "../../Audio/SoundEffect.hpp"

namespace HexEngine
{
	class AudioSource : public BaseComponent
	{
	public:
		CREATE_COMPONENT_ID(AudioSource);
		DEFINE_COMPONENT_CTOR(AudioSource);

		void SetSoundSource(SoundEffect* sound);

	};
}
