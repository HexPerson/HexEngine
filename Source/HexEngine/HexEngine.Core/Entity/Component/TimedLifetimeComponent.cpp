

#include "TimedLifetimeComponent.hpp"
#include "../../HexEngine.hpp"

namespace HexEngine
{
	TimedLifetimeComponent::TimedLifetimeComponent(Entity* entity, float timeUntilExpiration) :
		UpdateComponent(entity)
	{
		_expirationTime = g_pEnv->_timeManager->GetTime() + timeUntilExpiration;
	}

	void TimedLifetimeComponent::FixedUpdate(float frameTime)
	{
		if (g_pEnv->_timeManager->GetTime() >= _expirationTime)
		{
			GetEntity()->DeleteMe();
		}
	}
}