#include "CityEmergencyDispatcherSystemComponent.hpp"
#include "CityRoutineSystemComponent.hpp"
#include "RoutineAgentComponent.hpp"
#include "ServiceStationComponent.hpp"
#include "TrafficLaneComponent.hpp"
#include <HexEngine.Core/Entity/Entity.hpp>
#include <HexEngine.Core/GUI/Elements/Button.hpp>
#include <HexEngine.Core/GUI/Elements/Checkbox.hpp>
#include <HexEngine.Core/GUI/Elements/ComponentWidget.hpp>
#include <HexEngine.Core/GUI/Elements/DragFloat.hpp>
#include <HexEngine.Core/Scene/Scene.hpp>
#include <HexEngine.Core/Math/FloatMath.hpp>
#include <HexEngine.Core/Environment/TimeManager.hpp>
#include <HexEngine.Core/Environment/LogFile.hpp>
#include <HexEngine.Core/Utility/CRC32.hpp>

CityEmergencyDispatcherSystemComponent::CityEmergencyDispatcherSystemComponent(HexEngine::Entity* entity) :
	UpdateComponent(entity)
{
	RegisterEntityListener();
}

CityEmergencyDispatcherSystemComponent::CityEmergencyDispatcherSystemComponent(HexEngine::Entity* entity, CityEmergencyDispatcherSystemComponent* copy) :
	UpdateComponent(entity, copy)
{
	RegisterEntityListener();
	if (copy != nullptr)
	{
		_enabled = copy->_enabled;
		_dispatchTickSeconds = copy->_dispatchTickSeconds;
	}
}

CityEmergencyDispatcherSystemComponent::~CityEmergencyDispatcherSystemComponent()
{
	UnregisterEntityListener();
}

void CityEmergencyDispatcherSystemComponent::Destroy()
{
	UnregisterEntityListener();
	_incidents.clear();
}

void CityEmergencyDispatcherSystemComponent::Serialize(json& data, HexEngine::JsonFile* file)
{
	file->Serialize(data, "_enabled", _enabled);
	file->Serialize(data, "_dispatchTickSeconds", _dispatchTickSeconds);
}

void CityEmergencyDispatcherSystemComponent::Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask)
{
	(void)mask;
	file->Deserialize(data, "_enabled", _enabled);
	file->Deserialize(data, "_dispatchTickSeconds", _dispatchTickSeconds);
	_dispatcherAccumulator = 0.0f;
	_incidents.clear();
}

bool CityEmergencyDispatcherSystemComponent::CreateWidget(HexEngine::ComponentWidget* widget)
{
	auto* enabled = new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Enabled", &_enabled);
	auto* dispatchTick = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Dispatch Tick", &_dispatchTickSeconds, 0.05f, 60.0f, 0.05f, 2);
	enabled->SetPrefabOverrideBinding(GetComponentName(), "/_enabled");
	dispatchTick->SetPrefabOverrideBinding(GetComponentName(), "/_dispatchTickSeconds");

	new HexEngine::Button(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Report Medical Incident", [this](HexEngine::Button* button) -> bool
		{
			const math::Vector3 aroundSystem = GetEntity()->GetWorldTM().Translation();
			const math::Vector3 offset(HexEngine::GetRandomFloat(-1000.0f, 1000.0f), 0.0f, HexEngine::GetRandomFloat(-1000.0f, 1000.0f));
			ReportEmergency(aroundSystem + offset, "Medical");
			return true;
		});

	return true;
}

void CityEmergencyDispatcherSystemComponent::OnRemoveEntity(HexEngine::Entity* entity)
{
	if (entity == nullptr)
		return;

	for (auto& incident : _incidents)
	{
		if (incident.claimedAgentEntityName == entity->GetName())
			incident.claimedAgentEntityName.clear();
	}
}

uint32_t CityEmergencyDispatcherSystemComponent::ReportEmergency(const math::Vector3& worldPosition, const std::string& requiredServiceTag)
{
	EmergencyIncident incident;
	incident.id = _nextIncidentId++;
	incident.worldPosition = worldPosition;
	incident.requiredServiceTag = requiredServiceTag.empty() ? "Medical" : requiredServiceTag;
	incident.destinationParkingWaypointName = FindNearestWaypointName(worldPosition);
	incident.destinationEntryWaypointName = incident.destinationParkingWaypointName;
	incident.createdAtTime = HexEngine::g_pEnv->_timeManager != nullptr ? HexEngine::g_pEnv->_timeManager->_currentTime : 0.0f;
	_incidents.push_back(incident);

	LOG_INFO("CityEmergencyDispatcher: Reported incident %u at %.2f %.2f %.2f", incident.id, worldPosition.x, worldPosition.y, worldPosition.z);
	return incident.id;
}

bool CityEmergencyDispatcherSystemComponent::ResolveEmergency(uint32_t incidentId)
{
	for (auto& incident : _incidents)
	{
		if (incident.id != incidentId)
			continue;

		incident.isResolved = true;
		return true;
	}
	return false;
}

void CityEmergencyDispatcherSystemComponent::Update(float frameTime)
{
	UpdateComponent::Update(frameTime);

	if (!_enabled)
		return;

	UpdateDispatcher(frameTime);
}

void CityEmergencyDispatcherSystemComponent::RegisterEntityListener()
{
	if (_isListeningForEntityEvents)
		return;

	auto* owner = GetEntity();
	if (owner == nullptr || owner->GetScene() == nullptr)
		return;

	owner->GetScene()->AddEntityListener(this);
	_isListeningForEntityEvents = true;
}

void CityEmergencyDispatcherSystemComponent::UnregisterEntityListener()
{
	if (!_isListeningForEntityEvents)
		return;

	auto* owner = GetEntity();
	if (owner != nullptr && owner->GetScene() != nullptr)
	{
		owner->GetScene()->RemoveEntityListener(this);
	}

	_isListeningForEntityEvents = false;
}

void CityEmergencyDispatcherSystemComponent::UpdateDispatcher(float frameTime)
{
	_dispatcherAccumulator += frameTime;
	if (_dispatcherAccumulator < std::max(_dispatchTickSeconds, 0.05f))
		return;
	_dispatcherAccumulator = 0.0f;

	auto* routineSystem = GetRoutineSystem();
	if (routineSystem == nullptr)
		return;

	for (auto& incident : _incidents)
	{
		if (incident.isResolved || !incident.claimedAgentEntityName.empty())
			continue;

		auto* station = FindNearestServiceStationForTag(incident.worldPosition, incident.requiredServiceTag);
		if (station == nullptr)
			continue;

		auto* responder = FindBestEmergencyResponder(incident, station);
		if (responder == nullptr)
			continue;

		if (AssignEmergencyTask(routineSystem, responder, station, incident))
		{
			incident.claimedAgentEntityName = responder->GetEntity()->GetName();
		}
	}

	_incidents.erase(std::remove_if(_incidents.begin(), _incidents.end(),
		[](const EmergencyIncident& incident)
		{
			return incident.isResolved;
		}), _incidents.end());
}

CityRoutineSystemComponent* CityEmergencyDispatcherSystemComponent::GetRoutineSystem() const
{
	auto* scene = GetEntity() != nullptr ? GetEntity()->GetScene() : nullptr;
	if (scene == nullptr)
		return nullptr;

	std::vector<CityRoutineSystemComponent*> systems;
	scene->GetComponents<CityRoutineSystemComponent>(systems);
	if (systems.empty())
		return nullptr;

	return systems[0];
}

std::string CityEmergencyDispatcherSystemComponent::FindNearestWaypointName(const math::Vector3& worldPosition) const
{
	auto* scene = GetEntity() != nullptr ? GetEntity()->GetScene() : nullptr;
	if (scene == nullptr)
		return {};

	std::vector<TrafficLaneComponent*> lanes;
	scene->GetComponents<TrafficLaneComponent>(lanes);

	float bestDistSq = std::numeric_limits<float>::max();
	std::string best;

	for (auto* lane : lanes)
	{
		if (lane == nullptr)
			continue;

		std::vector<HexEngine::Entity*> waypoints;
		lane->GatherLaneWaypointEntities(waypoints);
		for (auto* wp : waypoints)
		{
			if (wp == nullptr || wp->IsPendingDeletion())
				continue;

			const float distSq = (wp->GetWorldTM().Translation() - worldPosition).LengthSquared();
			if (distSq < bestDistSq)
			{
				bestDistSq = distSq;
				best = wp->GetName();
			}
		}
	}

	return best;
}

ServiceStationComponent* CityEmergencyDispatcherSystemComponent::FindNearestServiceStationForTag(const math::Vector3& worldPosition, const std::string& tag) const
{
	auto* scene = GetEntity() != nullptr ? GetEntity()->GetScene() : nullptr;
	if (scene == nullptr)
		return nullptr;

	std::vector<ServiceStationComponent*> stations;
	scene->GetComponents<ServiceStationComponent>(stations);

	ServiceStationComponent* best = nullptr;
	float bestScore = std::numeric_limits<float>::max();

	for (auto* station : stations)
	{
		if (station == nullptr || station->GetEntity() == nullptr || station->GetEntity()->IsPendingDeletion())
			continue;

		bool tagMatch = false;
		for (const auto& serviceTag : station->GetServiceTags())
		{
			if (_stricmp(serviceTag.c_str(), tag.c_str()) == 0)
			{
				tagMatch = true;
				break;
			}
		}
		if (!tagMatch)
			continue;

		const float distSq = (station->GetEntity()->GetWorldTM().Translation() - worldPosition).LengthSquared();
		const float maxDistSq = station->GetDispatchRadius() * station->GetDispatchRadius();
		if (distSq > maxDistSq)
			continue;

		const float score = distSq - (float)station->GetPriority() * 1000.0f;
		if (score < bestScore)
		{
			best = station;
			bestScore = score;
		}
	}

	return best;
}

RoutineAgentComponent* CityEmergencyDispatcherSystemComponent::FindBestEmergencyResponder(const EmergencyIncident& incident, const ServiceStationComponent* station) const
{
	(void)station;
	auto* scene = GetEntity() != nullptr ? GetEntity()->GetScene() : nullptr;
	if (scene == nullptr)
		return nullptr;

	std::vector<RoutineAgentComponent*> agents;
	scene->GetComponents<RoutineAgentComponent>(agents);

	RoutineAgentComponent* best = nullptr;
	float bestDistSq = std::numeric_limits<float>::max();
	uint32_t bestTie = std::numeric_limits<uint32_t>::max();

	for (auto* agent : agents)
	{
		if (agent == nullptr || agent->GetEntity() == nullptr || agent->GetEntity()->IsPendingDeletion())
			continue;
		if (!agent->IsEmergencyEligible())
			continue;
		if (!agent->IsOnShift())
			continue;
		if (agent->IsBusy())
		{
			const auto& active = agent->GetActiveTask();
			if (!active.has_value() || active->priority >= 100)
				continue;
		}

		if (agent->GetPreferredVehiclePrefabPath().empty() && station->GetVehiclePrefabPath().empty())
			continue;

		bool hasAmbulanceRole = false;
		for (const auto& role : agent->GetRoleTags())
		{
			if (_stricmp(role.c_str(), "AmbulanceDriver") == 0)
			{
				hasAmbulanceRole = true;
				break;
			}
		}
		if (!hasAmbulanceRole)
			continue;

		const float distSq = (agent->GetEntity()->GetWorldTM().Translation() - incident.worldPosition).LengthSquared();
		const uint32_t tie = HexEngine::CRC32::HashString(agent->GetEntity()->GetName().c_str());
		if (distSq < bestDistSq || (fabsf(distSq - bestDistSq) <= 0.01f && tie < bestTie))
		{
			best = agent;
			bestDistSq = distSq;
			bestTie = tie;
		}
	}

	return best;
}

bool CityEmergencyDispatcherSystemComponent::AssignEmergencyTask(CityRoutineSystemComponent* routineSystem, RoutineAgentComponent* agent, const ServiceStationComponent* station, EmergencyIncident& incident)
{
	if (routineSystem == nullptr || agent == nullptr || station == nullptr || station->GetEntity() == nullptr)
		return false;

	RoutineTaskSpec emergency;
	emergency.type = RoutineTaskType::RespondEmergency;
	emergency.travelMode = RoutineTravelMode::DriveFirst;
	emergency.targetEntryWaypointName = incident.destinationEntryWaypointName;
	emergency.targetParkingWaypointName = incident.destinationParkingWaypointName;
	emergency.sourceParkingWaypointName = station->GetParkingWaypointEntityName();
	emergency.reason = station->GetEntity()->GetName();
	emergency.priority = 100;
	emergency.preemptive = true;
	emergency.incidentId = incident.id;
	emergency.maxRetries = 3;
	emergency.retryDelaySeconds = 2.0f;
	emergency.autoResolveOnArrival = true;
	return routineSystem->EnqueueTask(agent->GetEntity(), emergency);
}