#pragma once

#include <string>

namespace HexEngine
{
	// A district the game keeps closed until it is unlocked (implemented by the
	// CitySimulation plugin's DistrictGateComponent), so game code can open it and
	// keep content out of it without linking the plugin. Look it up with
	// Entity::GetComponentDerived<IDistrictGate>() (a dynamic_cast cross-cast).
	class IDistrictGate
	{
	public:
		virtual ~IDistrictGate() = default;

		virtual bool IsLocked() const = 0;
		// Runtime only: applied while the game runs (closes/opens traffic lanes,
		// lifts/drops the barrier); the authored scene stays locked.
		virtual void SetLocked(bool locked) = 0;
		virtual const std::string& GetDistrictName() const = 0;
		// True when the world-space point lies inside the district's bounds
		// (whatever the lock state).
		virtual bool ContainsPoint(float x, float y, float z) const = 0;
	};
}
