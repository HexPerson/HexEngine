#include "TransitPlayerState.hpp"

namespace HexEngine
{
	TransitPlayerState& GetTransitPlayerState()
	{
		static TransitPlayerState state;
		return state;
	}

	TrafficPlayerState& GetTrafficPlayerState()
	{
		static TrafficPlayerState state;
		return state;
	}
}
