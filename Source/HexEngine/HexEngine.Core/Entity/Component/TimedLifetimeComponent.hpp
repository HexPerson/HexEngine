

#pragma once

#include "UpdateComponent.hpp"

namespace HexEngine
{
	class TimedLifetimeComponent : public UpdateComponent
	{
	public:
		CREATE_COMPONENT_ID(TimedLifetimeComponent);

		TimedLifetimeComponent(Entity* entity, float timeUntilExpiration = 1.0f);

		TimedLifetimeComponent(Entity* entity, TimedLifetimeComponent* copy) : UpdateComponent(entity) {}

		virtual void FixedUpdate(float frameTime) override;

	private:
		float _expirationTime;
	};
}
