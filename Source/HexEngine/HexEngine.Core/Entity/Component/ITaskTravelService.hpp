#pragma once

#include "RoutineAgentComponent.hpp"

namespace HexEngine
{
	class Entity;

	class HEX_API ITaskTravelService
	{
	public:
		virtual ~ITaskTravelService() = default;

		virtual bool EnqueueTask(Entity* agentEntity, const RoutineTaskSpec& spec) = 0;
		virtual bool RequestTravel(Entity* agentEntity, const std::string& destinationAnchorWaypointEntityName, RoutineTravelMode travelMode = RoutineTravelMode::DriveFirst) = 0;
		virtual bool CancelTask(Entity* agentEntity, const std::string& reason) = 0;
	};
}
