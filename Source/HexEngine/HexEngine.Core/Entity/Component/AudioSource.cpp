#include "AudioSource.hpp"

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
}