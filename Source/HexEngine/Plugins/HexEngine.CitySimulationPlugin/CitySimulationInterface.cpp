#include "CitySimulationInterface.hpp"
#include "RoadComponent.hpp"
#include "VehicleComponent.hpp"
#include "TrafficLaneComponent.hpp"
#include "TrafficManagerComponent.hpp"
#include "TrafficSpawnerComponent.hpp"
#include "TrafficVehicleComponent.hpp"
#include "RoutineAgentComponent.hpp"
#include "PlaceOfWorkComponent.hpp"
#include "ResidenceComponent.hpp"
#include "ServiceStationComponent.hpp"
#include "CityRoutineSystemComponent.hpp"
#include "CityEmergencyDispatcherSystemComponent.hpp"
#include <HexEngine.Core/FileSystem/PrefabLoader.hpp>
#include <algorithm>
#include <cfloat>
#include <iterator>

namespace
{
	using namespace HexEngine;

	constexpr const char* kRegisteredClassNames[] =
	{
		"TrafficLaneComponent",
		"TrafficSpawnerComponent",
		"TrafficManagerComponent",
		"TrafficVehicleComponent",
		"RoutineAgentComponent",
		"PlaceOfWorkComponent",
		"ResidenceComponent",
		"ServiceStationComponent",
		"CityRoutineSystemComponent",
		"CityEmergencyDispatcherSystemComponent",
		"RoadComponent",
		"VehicleComponent",
	};

	math::Vector3 ResolveRoadForwardVector(RoadComponent::ForwardAxis axis)
	{
		switch (axis)
		{
		case RoadComponent::ForwardAxis::PositiveX: return math::Vector3::Right;
		case RoadComponent::ForwardAxis::NegativeX: return -math::Vector3::Right;
		case RoadComponent::ForwardAxis::NegativeZ: return -math::Vector3::Forward;
		case RoadComponent::ForwardAxis::PositiveZ:
		default: return math::Vector3::Forward;
		}
	}

	void CollectEntityRecursive(Entity* entity, std::vector<Entity*>& outEntities)
	{
		if (entity == nullptr || entity->IsPendingDeletion())
			return;

		outEntities.push_back(entity);
		for (auto* child : entity->GetChildren())
		{
			CollectEntityRecursive(child, outEntities);
		}
	}
}

bool CitySimulationInterface::Create()
{
	if (_registered)
		return true;

	using namespace HexEngine;
	REG_CLASS(TrafficLaneComponent);
	REG_CLASS(TrafficSpawnerComponent);
	REG_CLASS(TrafficManagerComponent);
	REG_CLASS(TrafficVehicleComponent);
	REG_CLASS(RoutineAgentComponent);
	REG_CLASS(PlaceOfWorkComponent);
	REG_CLASS(ResidenceComponent);
	REG_CLASS(ServiceStationComponent);
	REG_CLASS(CityRoutineSystemComponent);
	REG_CLASS(CityEmergencyDispatcherSystemComponent);
	REG_CLASS(RoadComponent);
	REG_CLASS(VehicleComponent);

	LogRegistrationSummary();
	_registered = true;
	return true;
}

void CitySimulationInterface::Destroy()
{
	_registered = false;
}

bool CitySimulationInterface::OnEntityDuplicated(HexEngine::Entity* sourceEntity, HexEngine::Entity* duplicatedEntity)
{
	if (sourceEntity == nullptr || duplicatedEntity == nullptr || sourceEntity == duplicatedEntity)
		return false;

	// Ensure duplicated roads reroll generated variants (e.g. random pavement).
	if (auto* duplicateRoad = duplicatedEntity->GetComponent<RoadComponent>(); duplicateRoad != nullptr)
	{
		duplicateRoad->RebuildGeneratedEntities();
	}
	if (auto* duplicateVehicle = duplicatedEntity->GetComponent<VehicleComponent>(); duplicateVehicle != nullptr)
	{
		duplicateVehicle->RebuildGeneratedEntities();
	}

	auto* sourceLane = sourceEntity->GetComponent<TrafficLaneComponent>();
	auto* duplicateLane = duplicatedEntity->GetComponent<TrafficLaneComponent>();
	if (sourceLane == nullptr || duplicateLane == nullptr)
		return false;

	return sourceLane->AddNextLaneEntityName(duplicatedEntity->GetName());
}

bool CitySimulationInterface::PlaceNextRoadSectionFromEntity(HexEngine::Entity* currentSectionEntity, HexEngine::Entity** outPlacedRoot)
{
	using namespace HexEngine;

	if (outPlacedRoot != nullptr)
		*outPlacedRoot = nullptr;

	if (currentSectionEntity == nullptr || currentSectionEntity->IsPendingDeletion())
		return false;

	auto* currentRoad = currentSectionEntity->GetComponent<RoadComponent>();
	if (currentRoad == nullptr)
	{
		LOG_WARN("CitySimulationPlugin: selected entity '%s' does not have a RoadComponent.", currentSectionEntity->GetName().c_str());
		return false;
	}

	const auto prefabPath = currentRoad->GetSectionPrefabPath();
	if (prefabPath.empty())
	{
		LOG_WARN("CitySimulationPlugin: RoadComponent on '%s' has no section prefab path.", currentSectionEntity->GetName().c_str());
		return false;
	}

	auto activeScene = g_pEnv->_sceneManager->GetCurrentScene();
	if (activeScene == nullptr || currentSectionEntity->GetScene() != activeScene.get())
	{
		LOG_WARN("CitySimulationPlugin: PlaceNextRoadSection requires selected entity in the active scene.");
		return false;
	}

	auto placedEntities = g_pEnv->_prefabLoader->LoadPrefab(activeScene, prefabPath);
	if (placedEntities.empty())
	{
		LOG_WARN("CitySimulationPlugin: failed to load road section prefab '%s'.", prefabPath.string().c_str());
		return false;
	}

	Entity* placedRoot = nullptr;
	for (auto* entity : placedEntities)
	{
		if (entity != nullptr && entity->GetParent() == nullptr)
		{
			placedRoot = entity;
			break;
		}
	}
	if (placedRoot == nullptr)
	{
		placedRoot = placedEntities.front();
	}

	if (placedRoot == nullptr)
		return false;

	const math::Vector3 localForward = ResolveRoadForwardVector(currentRoad->GetForwardAxis());
	const math::Vector3 worldForward = math::Vector3::Transform(localForward, currentSectionEntity->GetRotation());
	math::Vector3 targetPosition = currentSectionEntity->GetPosition() + (worldForward * currentRoad->GetSectionLength());
	targetPosition.y += currentRoad->GetSectionEndHeightDelta();

	placedRoot->ForcePosition(targetPosition);
	placedRoot->ForceRotation(currentSectionEntity->GetRotation());

	// Keep lane continuity by linking source lanes that terminate to the nearest placed lane.
	std::vector<Entity*> sourceEntities;
	std::vector<Entity*> placedHierarchy;
	CollectEntityRecursive(currentSectionEntity, sourceEntities);
	CollectEntityRecursive(placedRoot, placedHierarchy);

	std::vector<TrafficLaneComponent*> sourceLanes;
	std::vector<TrafficLaneComponent*> placedLanes;
	for (auto* entity : sourceEntities)
	{
		if (auto* lane = entity->GetComponent<TrafficLaneComponent>(); lane != nullptr)
		{
			sourceLanes.push_back(lane);
		}
	}
	for (auto* entity : placedHierarchy)
	{
		if (auto* lane = entity->GetComponent<TrafficLaneComponent>(); lane != nullptr)
		{
			placedLanes.push_back(lane);
		}
	}

	if (currentRoad->GetAutoGenerateTrafficLanes() && !sourceLanes.empty() && !placedLanes.empty())
	{
		for (auto* sourceLane : sourceLanes)
		{
			if (sourceLane == nullptr || !sourceLane->GetNextLaneEntityNames().empty())
				continue;

			Entity* nearestEntity = nullptr;
			float nearestDistSq = FLT_MAX;
			const math::Vector3 sourcePos = sourceLane->GetEntity()->GetWorldTM().Translation();

			for (auto* targetLane : placedLanes)
			{
				if (targetLane == nullptr || targetLane->GetEntity() == nullptr)
					continue;

				const math::Vector3 targetPos = targetLane->GetEntity()->GetWorldTM().Translation();
				const float distSq = (targetPos - sourcePos).LengthSquared();
				if (distSq < nearestDistSq)
				{
					nearestDistSq = distSq;
					nearestEntity = targetLane->GetEntity();
				}
			}

			if (nearestEntity != nullptr)
			{
				sourceLane->AddNextLaneEntityName(nearestEntity->GetName());
			}
		}
	}

	if (outPlacedRoot != nullptr)
	{
		*outPlacedRoot = placedRoot;
	}

	return true;
}

void CitySimulationInterface::LogRegistrationSummary() const
{
	if (HexEngine::g_pEnv == nullptr || HexEngine::g_pEnv->_classRegistry == nullptr)
		return;

	size_t foundCount = 0;
	for (const char* className : kRegisteredClassNames)
	{
		if (HexEngine::g_pEnv->_classRegistry->Find(className) != nullptr)
		{
			++foundCount;
		}
		else
		{
			LOG_WARN("CitySimulationPlugin: class '%s' is missing from ClassRegistry.", className);
		}
	}

	LOG_INFO("CitySimulationPlugin: registration check %zu/%zu classes available.", foundCount, std::size(kRegisteredClassNames));
}

