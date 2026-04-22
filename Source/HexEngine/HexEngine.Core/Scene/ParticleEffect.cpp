#include "ParticleEffect.hpp"

namespace HexEngine
{
	void ParticleEffect::Destroy()
	{
		emitters.clear();
	}

	ResourceType ParticleEffect::GetResourceType() const
	{
		return ResourceType::ParticleEffect;
	}
}
