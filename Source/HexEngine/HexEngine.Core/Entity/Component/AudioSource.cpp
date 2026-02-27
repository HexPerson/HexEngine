#include "AudioSource.hpp"
#include "../../Environment/IEnvironment.hpp"
#include "../../Audio/AudioManager.hpp"
#include "../Entity.hpp"

namespace HexEngine
{
	AudioSource::AudioSource(Entity* entity) :
		BaseComponent(entity)
	{

	}

	AudioSource::AudioSource(Entity* entity, AudioSource* copy) :
		BaseComponent(entity)
	{

	}

	void AudioSource::SetSoundSource(std::shared_ptr<SoundEffect> sound)
	{
		_sound = sound;
	}

	void AudioSource::Play()
	{
		if (_sound)
			g_pEnv->_audioManager->Play(_sound, GetEntity()->GetWorldTM().Translation());
	}

	void AudioSource::Stop()
	{
		if (_sound)
			g_pEnv->_audioManager->Stop(_sound);
	}

	void AudioSource::Loop()
	{
		if (_sound)
			g_pEnv->_audioManager->Loop(_sound, GetEntity()->GetWorldTM().Translation());
	}
}