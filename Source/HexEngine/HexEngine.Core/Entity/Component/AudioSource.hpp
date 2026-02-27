

#pragma once

#include "BaseComponent.hpp"
#include "../../Audio/SoundEffect.hpp"

namespace HexEngine
{
	class HEX_API AudioSource : public BaseComponent
	{
	public:
		CREATE_COMPONENT_ID(AudioSource);
		DEFINE_COMPONENT_CTOR(AudioSource);

		void SetSoundSource(std::shared_ptr<SoundEffect> sound);
		void Play();
		void Stop();
		void Loop();

	private:
		std::shared_ptr<SoundEffect> _sound;

	};
}
