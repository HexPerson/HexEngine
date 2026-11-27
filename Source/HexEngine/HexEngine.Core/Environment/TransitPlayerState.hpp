#pragma once

#include "../Required.hpp"
#include <string>

namespace HexEngine
{
	// What the local player is riding, published by the city-simulation
	// transit system for the game's HUD and fares (the game can't see the
	// plugin's classes). Written every frame the player is aboard.
	struct TransitPlayerState
	{
		uint64_t frame = 0;				// engine frame of the last write (aboard = written recently)
		std::string lineName;			// display name, e.g. "1 Downtown - Suburbs"
		std::string nextStopName;		// where it is heading
		std::string currentStopName;	// where it is standing (dwelling), else empty
		int32_t mode = 0;				// 0 bus, 1 train
		uint32_t boardings = 0;			// +1 each time the player steps aboard a vehicle
		int32_t fare = 0;				// fare for the latest boarding
	};

	HEX_API TransitPlayerState& GetTransitPlayerState();

	// Near misses between the player's ridden vehicle and simulated traffic,
	// published by the city-simulation traffic manager (Trello Drop 05). The
	// game polls nearMisses and reacts to each increment.
	struct TrafficPlayerState
	{
		uint32_t nearMisses = 0;		// +1 per clean close pass
		float lastGap = 0.0f;			// closest bike-to-car-body gap of the latest one (m)
		float lastRelativeSpeed = 0.0f;	// closing speed at that moment (m/s)
		uint32_t nearbyVehicles = 0;	// traffic within 12 m of the player this frame
	};

	HEX_API TrafficPlayerState& GetTrafficPlayerState();
}
