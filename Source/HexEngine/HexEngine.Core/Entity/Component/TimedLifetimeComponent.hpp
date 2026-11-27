

#pragma once

#include "UpdateComponent.hpp"

namespace HexEngine
{
	class HEX_API TimedLifetimeComponent : public UpdateComponent
	{
	public:
		CREATE_COMPONENT_ID(TimedLifetimeComponent);

		TimedLifetimeComponent(Entity* entity, float timeUntilExpiration = 1.0f);

		// A clone inherits the source's remaining lifetime (previously the expiry
		// was left uninitialised).
		TimedLifetimeComponent(Entity* entity, TimedLifetimeComponent* copy);

		virtual void FixedUpdate(float frameTime) override;

		// Serialised as _remainingSeconds (time left from now), so a saved or
		// cloned entity doesn't expire instantly on load.
		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;

	private:
		float _expirationTime;
	};
}
