#pragma once

#include "RoutineAgentComponent.hpp"

class ITaskTravelService
{
public:
	virtual ~ITaskTravelService() = default;

	virtual bool EnqueueTask(HexEngine::Entity* agentEntity, const RoutineTaskSpec& spec) = 0;
	virtual bool RequestTravel(HexEngine::Entity* agentEntity, const std::string& destinationAnchorWaypointEntityName, RoutineTravelMode travelMode = RoutineTravelMode::DriveFirst) = 0;
	virtual bool CancelTask(HexEngine::Entity* agentEntity, const std::string& reason) = 0;
};


