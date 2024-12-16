
#pragma once

#include "../Required.hpp"

namespace HexEngine
{
	struct ParticleEmitterParameters
	{
		math::Vector3 position;
	};

	class IParticleSystem
	{
	private:
		virtual ~IParticleSystem() = default;

	public:
		virtual void CreateParticleEmitter(const ParticleEmitterParameters& parameters) = 0;
	};
}
