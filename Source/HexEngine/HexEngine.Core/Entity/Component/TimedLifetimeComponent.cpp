

#include "TimedLifetimeComponent.hpp"
#include "../../HexEngine.hpp"

namespace HexEngine
{
	TimedLifetimeComponent::TimedLifetimeComponent(Entity* entity, float timeUntilExpiration) :
		UpdateComponent(entity)
	{
		_expirationTime = g_pEnv->_timeManager->GetTime() + timeUntilExpiration;
	}

	TimedLifetimeComponent::TimedLifetimeComponent(Entity* entity, TimedLifetimeComponent* copy) :
		UpdateComponent(entity)
	{
		_expirationTime = copy ? copy->_expirationTime : g_pEnv->_timeManager->GetTime() + 1.0f;
	}

	void TimedLifetimeComponent::Serialize(json& data, JsonFile* file)
	{
		const float remainingSeconds = std::max(0.0f, _expirationTime - g_pEnv->_timeManager->GetTime());
		file->Serialize(data, "_remainingSeconds", remainingSeconds);
	}

	void TimedLifetimeComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		float remainingSeconds = std::max(0.0f, _expirationTime - g_pEnv->_timeManager->GetTime());
		file->Deserialize(data, "_remainingSeconds", remainingSeconds);
		_expirationTime = g_pEnv->_timeManager->GetTime() + std::max(0.0f, remainingSeconds);
	}

	void TimedLifetimeComponent::FixedUpdate(float frameTime)
	{
		if (g_pEnv->_timeManager->GetTime() >= _expirationTime)
		{
			GetEntity()->DeleteMe();
		}
	}
}