#include "CityRoutineSystemComponent.hpp"
#include "CityEmergencyDispatcherSystemComponent.hpp"
#include "DayNightCycleComponent.hpp"
#include "NavigationComponent.hpp"
#include "PlaceOfWorkComponent.hpp"
#include "ResidenceComponent.hpp"
#include "RoutineAgentComponent.hpp"
#include "TrafficLaneComponent.hpp"
#include "TrafficVehicleComponent.hpp"
#include "ServiceStationComponent.hpp"
#include "Transform.hpp"
#include "../Entity.hpp"
#include "../../GUI/Elements/Checkbox.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/DragFloat.hpp"
#include "../../GUI/Elements/DragInt.hpp"
#include "../../Math/FloatMath.hpp"
#include "../../Scene/Scene.hpp"
#include "../../Scene/SceneManager.hpp"
#include "../../Environment/LogFile.hpp"
#include "../../GUI/UIManager.hpp"
#include "../../Graphics/DebugRenderer.hpp"
#include "../../Input/InputSystem.hpp"
#include <cstring>
#include <format>

namespace HexEngine
{
	namespace
	{
		constexpr float kWorkplacePriorityWeight = 100.0f;

		const char* ToString(RoutineExecutionState state)
		{
			switch (state)
			{
			case RoutineExecutionState::Idle: return "Idle";
			case RoutineExecutionState::WalkToVehicleAnchor: return "WalkToVehicle";
			case RoutineExecutionState::AcquireVehicle: return "AcquireVehicle";
			case RoutineExecutionState::DriveToDestinationParking: return "DriveToDestination";
			case RoutineExecutionState::WalkToTaskEntry: return "WalkToEntry";
			case RoutineExecutionState::ExecuteTask: return "ExecuteTask";
			case RoutineExecutionState::ReturnHome: return "ReturnHome";
			case RoutineExecutionState::Failed: return "Failed";
			default: return "Unknown";
			}
		}

		const char* ToString(RoutineTaskType type)
		{
			switch (type)
			{
			case RoutineTaskType::None: return "None";
			case RoutineTaskType::GoHome: return "GoHome";
			case RoutineTaskType::GoToWork: return "GoToWork";
			case RoutineTaskType::RespondEmergency: return "RespondEmergency";
			case RoutineTaskType::CustomTravel: return "CustomTravel";
			default: return "Unknown";
			}
		}
	}

	CityRoutineSystemComponent::CityRoutineSystemComponent(Entity* entity) :
		UpdateComponent(entity)
	{
		RegisterEntityListener();
	}

	CityRoutineSystemComponent::CityRoutineSystemComponent(Entity* entity, CityRoutineSystemComponent* copy) :
		UpdateComponent(entity, copy)
	{
		RegisterEntityListener();
		if (copy != nullptr)
		{
			_enabled = copy->_enabled;
			_logMissingClockWarning = copy->_logMissingClockWarning;
			_navMeshId = copy->_navMeshId;
			_walkStateTimeoutSeconds = copy->_walkStateTimeoutSeconds;
			_driveStateTimeoutSeconds = copy->_driveStateTimeoutSeconds;
			_drawSelectedAgentHud = copy->_drawSelectedAgentHud;
			_drawRoutineWalkPaths = copy->_drawRoutineWalkPaths;
			_enableWalkStuckRecovery = copy->_enableWalkStuckRecovery;
			_walkStuckSpeedThreshold = copy->_walkStuckSpeedThreshold;
			_walkStuckDurationSeconds = copy->_walkStuckDurationSeconds;
			_walkStuckRepathCooldownSeconds = copy->_walkStuckRepathCooldownSeconds;
		}
	}

	CityRoutineSystemComponent::~CityRoutineSystemComponent()
	{
		UnregisterEntityListener();
	}

	void CityRoutineSystemComponent::Destroy()
	{
		UnregisterEntityListener();
	}

	void CityRoutineSystemComponent::Serialize(json& data, JsonFile* file)
	{
		file->Serialize(data, "_enabled", _enabled);
		file->Serialize(data, "_logMissingClockWarning", _logMissingClockWarning);
		file->Serialize(data, "_navMeshId", _navMeshId);
		file->Serialize(data, "_walkStateTimeoutSeconds", _walkStateTimeoutSeconds);
		file->Serialize(data, "_driveStateTimeoutSeconds", _driveStateTimeoutSeconds);
		file->Serialize(data, "_drawSelectedAgentHud", _drawSelectedAgentHud);
		file->Serialize(data, "_drawRoutineWalkPaths", _drawRoutineWalkPaths);
		file->Serialize(data, "_enableWalkStuckRecovery", _enableWalkStuckRecovery);
		file->Serialize(data, "_walkStuckSpeedThreshold", _walkStuckSpeedThreshold);
		file->Serialize(data, "_walkStuckDurationSeconds", _walkStuckDurationSeconds);
		file->Serialize(data, "_walkStuckRepathCooldownSeconds", _walkStuckRepathCooldownSeconds);
	}

	void CityRoutineSystemComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		(void)mask;
		file->Deserialize(data, "_enabled", _enabled);
		file->Deserialize(data, "_logMissingClockWarning", _logMissingClockWarning);
		file->Deserialize(data, "_navMeshId", _navMeshId);
		file->Deserialize(data, "_walkStateTimeoutSeconds", _walkStateTimeoutSeconds);
		file->Deserialize(data, "_driveStateTimeoutSeconds", _driveStateTimeoutSeconds);
		file->Deserialize(data, "_drawSelectedAgentHud", _drawSelectedAgentHud);
		file->Deserialize(data, "_drawRoutineWalkPaths", _drawRoutineWalkPaths);
		file->Deserialize(data, "_enableWalkStuckRecovery", _enableWalkStuckRecovery);
		file->Deserialize(data, "_walkStuckSpeedThreshold", _walkStuckSpeedThreshold);
		file->Deserialize(data, "_walkStuckDurationSeconds", _walkStuckDurationSeconds);
		file->Deserialize(data, "_walkStuckRepathCooldownSeconds", _walkStuckRepathCooldownSeconds);
		_agentMotionStates.clear();
	}

	bool CityRoutineSystemComponent::CreateWidget(ComponentWidget* widget)
	{
		auto* enabled = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Enabled", &_enabled);
		auto* warn = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Warn Missing Clock", &_logMissingClockWarning);
		auto* navMesh = new DragInt(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"NavMesh Id", (int32_t*)&_navMeshId, 0, 1024, 1);
		auto* walkTimeout = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Walk Timeout", &_walkStateTimeoutSeconds, 1.0f, 3600.0f, 1.0f, 1);
		auto* driveTimeout = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Drive Timeout", &_driveStateTimeoutSeconds, 1.0f, 3600.0f, 1.0f, 1);
		auto* drawHud = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Draw Selected Agent HUD", &_drawSelectedAgentHud);
		auto* drawPaths = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Draw Routine Walk Paths", &_drawRoutineWalkPaths);
		auto* enableStuckRecovery = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Enable Walk Stuck Recovery", &_enableWalkStuckRecovery);
		auto* stuckSpeed = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Stuck Speed Threshold", &_walkStuckSpeedThreshold, 0.0f, 10.0f, 0.01f, 2);
		auto* stuckDuration = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Stuck Duration", &_walkStuckDurationSeconds, 0.1f, 60.0f, 0.1f, 1);
		auto* stuckCooldown = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Stuck Repath Cooldown", &_walkStuckRepathCooldownSeconds, 0.1f, 60.0f, 0.1f, 1);

		enabled->SetPrefabOverrideBinding(GetComponentName(), "/_enabled");
		warn->SetPrefabOverrideBinding(GetComponentName(), "/_logMissingClockWarning");
		navMesh->SetPrefabOverrideBinding(GetComponentName(), "/_navMeshId");
		walkTimeout->SetPrefabOverrideBinding(GetComponentName(), "/_walkStateTimeoutSeconds");
		driveTimeout->SetPrefabOverrideBinding(GetComponentName(), "/_driveStateTimeoutSeconds");
		drawHud->SetPrefabOverrideBinding(GetComponentName(), "/_drawSelectedAgentHud");
		drawPaths->SetPrefabOverrideBinding(GetComponentName(), "/_drawRoutineWalkPaths");
		enableStuckRecovery->SetPrefabOverrideBinding(GetComponentName(), "/_enableWalkStuckRecovery");
		stuckSpeed->SetPrefabOverrideBinding(GetComponentName(), "/_walkStuckSpeedThreshold");
		stuckDuration->SetPrefabOverrideBinding(GetComponentName(), "/_walkStuckDurationSeconds");
		stuckCooldown->SetPrefabOverrideBinding(GetComponentName(), "/_walkStuckRepathCooldownSeconds");

		return true;
	}

	void CityRoutineSystemComponent::OnRemoveEntity(Entity* entity)
	{
		if (entity == nullptr)
			return;

		_agentMotionStates.erase(entity->GetName());
	}

	bool CityRoutineSystemComponent::EnqueueTask(Entity* agentEntity, const RoutineTaskSpec& spec)
	{
		if (agentEntity == nullptr)
			return false;

		auto* agent = agentEntity->GetComponent<RoutineAgentComponent>();
		if (agent == nullptr)
			return false;

		agent->EnqueueTask(spec);
		return true;
	}

	bool CityRoutineSystemComponent::RequestTravel(Entity* agentEntity, const std::string& destinationAnchorWaypointEntityName, RoutineTravelMode travelMode)
	{
		if (agentEntity == nullptr || destinationAnchorWaypointEntityName.empty())
			return false;

		RoutineTaskSpec task;
		task.type = RoutineTaskType::CustomTravel;
		task.travelMode = travelMode;
		task.targetEntryWaypointName = destinationAnchorWaypointEntityName;
		task.targetParkingWaypointName = destinationAnchorWaypointEntityName;
		task.priority = 1;
		return EnqueueTask(agentEntity, task);
	}

	bool CityRoutineSystemComponent::CancelTask(Entity* agentEntity, const std::string& reason)
	{
		if (agentEntity == nullptr)
			return false;

		auto* agent = agentEntity->GetComponent<RoutineAgentComponent>();
		if (agent == nullptr)
			return false;

		CleanupAgentVehicle(agent);
		agent->GetEntity()->ClearFlags(EntityFlags::DoNotRender);
		agent->SetActiveTask(std::nullopt);
		agent->ClearSuspendedTask();
		agent->SetExecutionState(RoutineExecutionState::Idle);
		agent->SetBusy(false);
		agent->ClearQueuedTasks();
		_agentMotionStates.erase(agentEntity->GetName());
		LOG_INFO("CityRoutineSystem: Cancelled tasks for '%s'. Reason: %s", agentEntity->GetName().c_str(), reason.c_str());
		return true;
	}

	uint32_t CityRoutineSystemComponent::ReportEmergency(const math::Vector3& worldPosition, const std::string& requiredServiceTag)
	{
		auto* dispatcher = GetDispatcherSystem();
		if (dispatcher == nullptr)
		{
			LOG_WARN("CityRoutineSystem: ReportEmergency ignored because no CityEmergencyDispatcherSystemComponent is present.");
			return 0;
		}

		return dispatcher->ReportEmergency(worldPosition, requiredServiceTag);
	}

	bool CityRoutineSystemComponent::ResolveEmergency(uint32_t incidentId)
	{
		auto* dispatcher = GetDispatcherSystem();
		if (dispatcher == nullptr)
		{
			LOG_WARN("CityRoutineSystem: ResolveEmergency ignored because no CityEmergencyDispatcherSystemComponent is present.");
			return false;
		}

		return dispatcher->ResolveEmergency(incidentId);
	}

	void CityRoutineSystemComponent::Update(float frameTime)
	{
		UpdateComponent::Update(frameTime);

		if (!_enabled)
			return;

		const float simulationHour = GetSimulationHour();
		if (simulationHour < 0.0f)
			return;

		UpdateAgents(frameTime, simulationHour);
	}

	void CityRoutineSystemComponent::RegisterEntityListener()
	{
		if (_isListeningForEntityEvents)
			return;

		auto* owner = GetEntity();
		if (owner == nullptr || owner->GetScene() == nullptr)
			return;

		owner->GetScene()->AddEntityListener(this);
		_isListeningForEntityEvents = true;
	}

	void CityRoutineSystemComponent::UnregisterEntityListener()
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

	float CityRoutineSystemComponent::GetSimulationHour() const
	{
		auto* scene = GetEntity() != nullptr ? GetEntity()->GetScene() : nullptr;
		if (scene == nullptr)
			return -1.0f;

		std::vector<DayNightCycleComponent*> dayNight;
		scene->GetComponents<DayNightCycleComponent>(dayNight);
		if (dayNight.empty() || dayNight[0] == nullptr)
		{
			if (_logMissingClockWarning)
			{
				LOG_WARN("CityRoutineSystem: No DayNightCycleComponent found, routine scheduling paused.");
			}
			return -1.0f;
		}

		return dayNight[0]->GetCurrentTimeHours();
	}

	CityEmergencyDispatcherSystemComponent* CityRoutineSystemComponent::GetDispatcherSystem() const
	{
		auto* scene = GetEntity() != nullptr ? GetEntity()->GetScene() : nullptr;
		if (scene == nullptr)
			return nullptr;

		std::vector<CityEmergencyDispatcherSystemComponent*> systems;
		scene->GetComponents<CityEmergencyDispatcherSystemComponent>(systems);
		if (systems.empty())
			return nullptr;

		return systems[0];
	}

	void CityRoutineSystemComponent::UpdateAgents(float frameTime, float simulationHour)
	{
		auto* scene = GetEntity() != nullptr ? GetEntity()->GetScene() : nullptr;
		if (scene == nullptr)
			return;

		std::vector<RoutineAgentComponent*> agents;
		scene->GetComponents<RoutineAgentComponent>(agents);

		std::unordered_map<std::string, int32_t> workplaceOccupancy;
		for (auto* agent : agents)
		{
			if (agent == nullptr || agent->GetEntity() == nullptr || agent->GetEntity()->IsPendingDeletion())
				continue;

			const auto& workplace = agent->GetAssignedWorkplaceEntityName();
			if (!workplace.empty())
				++workplaceOccupancy[workplace];
		}

		for (auto* agent : agents)
		{
			if (agent == nullptr || agent->GetEntity() == nullptr || agent->GetEntity()->IsPendingDeletion())
				continue;

			UpdateAgentState(agent, frameTime, simulationHour, workplaceOccupancy);
		}
	}

	void CityRoutineSystemComponent::UpdateAgentState(RoutineAgentComponent* agent, float frameTime, float simulationHour, const std::unordered_map<std::string, int32_t>& workplaceOccupancy)
	{
		EnsureWorkplaceAssignment(agent, simulationHour, workplaceOccupancy);
		SeedBaselineTaskIfIdle(agent, simulationHour);

		if (agent->GetActiveTask().has_value())
		{
			RoutineTaskSpec headTask;
			if (agent->PeekTask(headTask))
			{
				const auto& activeTask = *agent->GetActiveTask();
				if (headTask.preemptive && headTask.priority > activeTask.priority)
				{
					CleanupAgentVehicle(agent);
					agent->GetEntity()->ClearFlags(EntityFlags::DoNotRender);
					agent->SuspendCurrentTask();
				}
			}
		}

		if (!agent->GetActiveTask().has_value())
		{
			RoutineTaskSpec nextTask;
			if (agent->TryPopTask(nextTask))
			{
				agent->SetActiveTask(nextTask);
				agent->GetEntity()->ClearFlags(EntityFlags::DoNotRender);
				agent->SetExecutionState(RoutineExecutionState::Idle);
				agent->SetStateTimer(0.0f);
				agent->RetryTimerRef() = 0.0f;
				agent->SetBusy(true);
			}
		}

		if (!agent->GetActiveTask().has_value())
			return;

		auto task = *agent->GetActiveTask();
		agent->StateTimerRef() += frameTime;

		switch (agent->GetExecutionState())
		{
		case RoutineExecutionState::Idle:
		{
			agent->SetExecutionState(task.travelMode == RoutineTravelMode::WalkOnly ? RoutineExecutionState::WalkToTaskEntry : RoutineExecutionState::WalkToVehicleAnchor);
			agent->SetStateTimer(0.0f);
			break;
		}

		case RoutineExecutionState::WalkToVehicleAnchor:
		{
			const std::string sourceParkingWaypointName = task.sourceParkingWaypointName.empty() ? task.targetParkingWaypointName : task.sourceParkingWaypointName;

			if (agent->StateTimerRef() <= frameTime)
			{
				const bool started = BeginWalkTo(agent, sourceParkingWaypointName);
				if (!started)
				{
					HandleTaskRetryOrFailure(agent, "No source parking waypoint");
					break;
				}
			}

			if (agent->ConsumeNavigationReached())
			{
				agent->SetExecutionState(RoutineExecutionState::AcquireVehicle);
				agent->SetStateTimer(0.0f);
				break;
			}

			if (agent->StateTimerRef() > _walkStateTimeoutSeconds)
			{
				HandleTaskRetryOrFailure(agent, "Timed out walking to vehicle");
				break;
			}

			if (UpdateWalkStuckRecovery(agent, frameTime, sourceParkingWaypointName, "Stuck while walking to vehicle"))
			{
				break;
			}
			break;
		}

		case RoutineExecutionState::AcquireVehicle:
		{
			Entity* vehicleEntity = AcquireVehicleForTask(agent, task);
			if (vehicleEntity == nullptr)
			{
				HandleTaskRetryOrFailure(agent, "Failed to acquire vehicle");
				break;
			}

			if (!ConfigureVehicleRoute(vehicleEntity, agent, task))
			{
				HandleTaskRetryOrFailure(agent, "Failed to configure vehicle route");
				break;
			}

			if (auto* nav = agent->GetEntity()->GetComponent<NavigationComponent>(); nav != nullptr)
			{
				nav->ClearPath();
			}

			agent->GetEntity()->SetFlag(EntityFlags::DoNotRender);
			agent->SetExecutionState(RoutineExecutionState::DriveToDestinationParking);
			agent->SetStateTimer(0.0f);
			break;
		}

		case RoutineExecutionState::DriveToDestinationParking:
		{
			Entity* vehicleEntity = nullptr;
			if (!agent->GetCurrentVehicleEntityName().empty())
			{
				auto* scene = agent->GetEntity()->GetScene();
				if (scene != nullptr)
					vehicleEntity = scene->GetEntityByName(agent->GetCurrentVehicleEntityName());
			}

			auto* vehicle = vehicleEntity != nullptr ? vehicleEntity->GetComponent<TrafficVehicleComponent>() : nullptr;
			if (vehicle == nullptr)
			{
				HandleTaskRetryOrFailure(agent, "Vehicle disappeared during drive");
				break;
			}

			if (vehicle->ConsumeRouteEndReachedEvent())
			{
				agent->GetEntity()->ClearFlags(EntityFlags::DoNotRender);
				agent->GetEntity()->ForcePosition(vehicleEntity->GetWorldTM().Translation());
				agent->SetExecutionState(RoutineExecutionState::WalkToTaskEntry);
				agent->SetStateTimer(0.0f);
				break;
			}

			if (agent->StateTimerRef() > _driveStateTimeoutSeconds)
			{
				HandleTaskRetryOrFailure(agent, "Timed out driving to destination");
			}
			break;
		}

		case RoutineExecutionState::WalkToTaskEntry:
		{
			const std::string targetEntryWaypointName = task.targetEntryWaypointName.empty() ? task.targetParkingWaypointName : task.targetEntryWaypointName;

			if (agent->StateTimerRef() <= frameTime)
			{
				if (!BeginWalkTo(agent, targetEntryWaypointName))
				{
					HandleTaskRetryOrFailure(agent, "No destination entry waypoint");
					break;
				}
			}

			if (agent->ConsumeNavigationReached())
			{
				agent->SetExecutionState(RoutineExecutionState::ExecuteTask);
				agent->SetStateTimer(0.0f);
				break;
			}

			if (agent->StateTimerRef() > _walkStateTimeoutSeconds)
			{
				HandleTaskRetryOrFailure(agent, "Timed out walking to entry");
				break;
			}

			if (UpdateWalkStuckRecovery(agent, frameTime, targetEntryWaypointName, "Stuck while walking to entry"))
			{
				break;
			}
			break;
		}

		case RoutineExecutionState::ExecuteTask:
		{
			if (task.type == RoutineTaskType::RespondEmergency && task.incidentId != 0 && task.autoResolveOnArrival)
			{
				ResolveEmergency(task.incidentId);
			}

			FinishTask(agent, true);
			break;
		}

		case RoutineExecutionState::Failed:
		{
			agent->RetryTimerRef() -= frameTime;
			if (agent->RetryTimerRef() <= 0.0f)
			{
				agent->SetExecutionState(RoutineExecutionState::Idle);
				agent->SetStateTimer(0.0f);
			}
			break;
		}

		default:
			break;
		}
	}

	void CityRoutineSystemComponent::EnsureWorkplaceAssignment(RoutineAgentComponent* agent, float simulationHour, const std::unordered_map<std::string, int32_t>& workplaceOccupancy)
	{
		if (agent == nullptr || agent->GetEntity() == nullptr)
			return;

		auto* scene = agent->GetEntity()->GetScene();
		if (scene == nullptr)
			return;

		if (!agent->GetAssignedWorkplaceEntityName().empty())
		{
			if (scene->GetEntityByName(agent->GetAssignedWorkplaceEntityName()) != nullptr)
				return;
			agent->SetAssignedWorkplaceEntityName(std::string());
		}

		std::vector<PlaceOfWorkComponent*> workplaces;
		scene->GetComponents<PlaceOfWorkComponent>(workplaces);
		if (workplaces.empty())
			return;

		const math::Vector3 homePos = [&]()
		{
			if (agent->GetHomeEntityName().empty())
				return agent->GetEntity()->GetWorldTM().Translation();
			auto* home = scene->GetEntityByName(agent->GetHomeEntityName());
			if (home == nullptr)
				return agent->GetEntity()->GetWorldTM().Translation();
			return home->GetWorldTM().Translation();
		}();

		float bestScore = std::numeric_limits<float>::max();
		uint32_t bestTie = std::numeric_limits<uint32_t>::max();
		std::string bestName;

		for (auto* workplace : workplaces)
		{
			if (workplace == nullptr || workplace->GetEntity() == nullptr || workplace->GetEntity()->IsPendingDeletion())
				continue;

			if (!IsRoleMatch(agent->GetRoleTags(), workplace->GetRoleTags()))
				continue;

			if (!workplace->IsHourInAnyShift(simulationHour))
			{
				// still allow assigning for future shift if unmatched
			}

			const auto itOcc = workplaceOccupancy.find(workplace->GetEntity()->GetName());
			const int32_t occ = itOcc != workplaceOccupancy.end() ? itOcc->second : 0;
			if (occ >= workplace->GetWorkerCapacity())
				continue;

			const math::Vector3 pos = workplace->GetEntity()->GetWorldTM().Translation();
			const float distance = (pos - homePos).Length();
			const float score = distance - (float)workplace->GetServicePriority() * kWorkplacePriorityWeight;
			const uint32_t tie = CRC32::HashString(workplace->GetEntity()->GetName().c_str());

			if (score < bestScore || (fabsf(score - bestScore) <= 0.001f && tie < bestTie))
			{
				bestScore = score;
				bestTie = tie;
				bestName = workplace->GetEntity()->GetName();
			}
		}

		if (!bestName.empty())
		{
			agent->SetAssignedWorkplaceEntityName(bestName);
		}
	}

	void CityRoutineSystemComponent::SeedBaselineTaskIfIdle(RoutineAgentComponent* agent, float simulationHour)
	{
		if (agent == nullptr || agent->GetEntity() == nullptr)
			return;

		if (agent->GetActiveTask().has_value() || agent->HasQueuedTasks())
			return;

		auto* scene = agent->GetEntity()->GetScene();
		if (scene == nullptr)
			return;

		PlaceOfWorkComponent* workplace = nullptr;
		ResidenceComponent* residence = nullptr;

		if (!agent->GetAssignedWorkplaceEntityName().empty())
		{
			auto* workEntity = scene->GetEntityByName(agent->GetAssignedWorkplaceEntityName());
			if (workEntity != nullptr)
				workplace = workEntity->GetComponent<PlaceOfWorkComponent>();
		}

		if (!agent->GetHomeEntityName().empty())
		{
			auto* homeEntity = scene->GetEntityByName(agent->GetHomeEntityName());
			if (homeEntity != nullptr)
				residence = homeEntity->GetComponent<ResidenceComponent>();
		}

		const bool onShift = workplace != nullptr ? workplace->IsHourInAnyShift(simulationHour) : false;
		agent->SetOnShift(onShift);

		const math::Vector3 agentPos = agent->GetEntity()->GetWorldTM().Translation();
		constexpr float kArrivalRadius = 8.0f;
		auto isNear = [&](const std::string& waypointEntityName, Entity* fallbackEntity) -> bool
		{
			math::Vector3 target = math::Vector3::Zero;
			bool hasTarget = false;

			if (!waypointEntityName.empty())
			{
				auto* waypoint = scene->GetEntityByName(waypointEntityName);
				if (waypoint != nullptr && !waypoint->IsPendingDeletion())
				{
					target = waypoint->GetWorldTM().Translation();
					hasTarget = true;
				}
			}

			if (!hasTarget && fallbackEntity != nullptr && !fallbackEntity->IsPendingDeletion())
			{
				target = fallbackEntity->GetWorldTM().Translation();
				hasTarget = true;
			}

			if (!hasTarget)
				return false;

			return (target - agentPos).LengthSquared() <= (kArrivalRadius * kArrivalRadius);
		};

		RoutineTaskSpec task;
		task.travelMode = RoutineTravelMode::DriveFirst;
		task.priority = 0;

		if (onShift && workplace != nullptr)
		{
			if (isNear(workplace->GetEntryWaypointEntityName(), workplace->GetEntity()) ||
				isNear(workplace->GetParkingWaypointEntityName(), workplace->GetEntity()))
			{
				return;
			}

			task.type = RoutineTaskType::GoToWork;
			task.targetEntityName = workplace->GetEntity()->GetName();
			task.targetEntryWaypointName = workplace->GetEntryWaypointEntityName();
			task.targetParkingWaypointName = workplace->GetParkingWaypointEntityName();
			task.sourceParkingWaypointName = residence != nullptr ? residence->GetParkingWaypointEntityName() : std::string();
		}
		else if (residence != nullptr)
		{
			if (isNear(residence->GetEntryWaypointEntityName(), residence->GetEntity()) ||
				isNear(residence->GetParkingWaypointEntityName(), residence->GetEntity()))
			{
				return;
			}

			task.type = RoutineTaskType::GoHome;
			task.targetEntityName = residence->GetEntity()->GetName();
			task.targetEntryWaypointName = residence->GetEntryWaypointEntityName();
			task.targetParkingWaypointName = residence->GetParkingWaypointEntityName();
			if (workplace != nullptr)
				task.sourceParkingWaypointName = workplace->GetParkingWaypointEntityName();
		}
		else
		{
			return;
		}

		agent->EnqueueTask(task);
	}

	bool CityRoutineSystemComponent::BeginWalkTo(RoutineAgentComponent* agent, const std::string& waypointEntityName)
	{
		if (agent == nullptr || agent->GetEntity() == nullptr || waypointEntityName.empty())
			return false;

		auto* scene = agent->GetEntity()->GetScene();
		if (scene == nullptr)
			return false;

		auto* targetEntity = scene->GetEntityByName(waypointEntityName);
		if (targetEntity == nullptr)
			return false;

		const math::Vector3 from = agent->GetEntity()->GetWorldTM().Translation();
		const math::Vector3 to = targetEntity->GetWorldTM().Translation();
		constexpr float kImmediateArrivalRadius = 1.5f;
		if ((to - from).LengthSquared() <= (kImmediateArrivalRadius * kImmediateArrivalRadius))
		{
			NavigationTargetReachedMessage message;
			message.targetPosition = to;
			message.finalPosition = from;
			agent->GetEntity()->OnMessage(&message, this);
			return true;
		}

		auto* nav = agent->GetEntity()->GetComponent<NavigationComponent>();
		if (nav == nullptr)
			nav = agent->GetEntity()->AddComponent<NavigationComponent>();

		if (nav == nullptr)
			return false;

		nav->ClearPath();
		nav->FindPath(_navMeshId, from, to, 5.0f);
		if (!nav->HasPath())
		{
			// Some nav providers may return an empty path when already very close.
			if ((to - from).LengthSquared() <= (kImmediateArrivalRadius * kImmediateArrivalRadius))
			{
				NavigationTargetReachedMessage message;
				message.targetPosition = to;
				message.finalPosition = from;
				agent->GetEntity()->OnMessage(&message, this);
				return true;
			}
			return false;
		}

		auto& motion = _agentMotionStates[agent->GetEntity()->GetName()];
		motion.lastPosition = from;
		motion.hasLastPosition = true;
		motion.lowMotionAccumSeconds = 0.0f;
		motion.repathCooldownSeconds = 0.0f;

		return true;
	}

	Entity* CityRoutineSystemComponent::AcquireVehicleForTask(RoutineAgentComponent* agent, RoutineTaskSpec& task)
	{
		auto* scene = agent != nullptr && agent->GetEntity() != nullptr ? agent->GetEntity()->GetScene() : nullptr;
		if (scene == nullptr)
			return nullptr;

		if (!agent->GetCurrentVehicleEntityName().empty())
		{
			auto* existing = scene->GetEntityByName(agent->GetCurrentVehicleEntityName());
			if (existing != nullptr && !existing->IsPendingDeletion())
				return existing;
		}

		std::string prefabPath = agent->GetPreferredVehiclePrefabPath();
		if (task.type == RoutineTaskType::RespondEmergency && !task.reason.empty())
		{
			auto* stationEntity = scene->GetEntityByName(task.reason);
			auto* station = stationEntity != nullptr ? stationEntity->GetComponent<ServiceStationComponent>() : nullptr;
			if (station != nullptr && !station->GetVehiclePrefabPath().empty())
			{
				prefabPath = station->GetVehiclePrefabPath();
				if (task.sourceParkingWaypointName.empty())
					task.sourceParkingWaypointName = station->GetParkingWaypointEntityName();
			}
		}

		if (prefabPath.empty())
			return nullptr;

		auto spawned = g_pEnv->_sceneManager->LoadPrefab(g_pEnv->_sceneManager->GetCurrentScene(), prefabPath);
		for (auto* ent : spawned)
		{
			if (ent == nullptr || ent->IsPendingDeletion())
				continue;
			if (ent->GetParent() != nullptr)
				continue;

			agent->SetCurrentVehicleEntityName(ent->GetName());
			return ent;
		}
		return nullptr;
	}

	bool CityRoutineSystemComponent::ConfigureVehicleRoute(Entity* vehicleEntity, RoutineAgentComponent* agent, const RoutineTaskSpec& task)
	{
		if (vehicleEntity == nullptr)
			return false;

		auto* vehicle = vehicleEntity->GetComponent<TrafficVehicleComponent>();
		if (vehicle == nullptr)
			vehicle = vehicleEntity->AddComponent<TrafficVehicleComponent>();
		if (vehicle == nullptr)
			return false;

		if (task.targetParkingWaypointName.empty())
			return false;

		std::string sourceParkingWaypointName = task.sourceParkingWaypointName;
		if (sourceParkingWaypointName.empty())
		{
			sourceParkingWaypointName = FindNearestWaypointName(agent->GetEntity()->GetWorldTM().Translation());
		}

		if (sourceParkingWaypointName.empty())
		{
			auto* destinationParking = vehicleEntity->GetScene()->GetEntityByName(task.targetParkingWaypointName);
			if (destinationParking == nullptr)
				return false;
			vehicleEntity->ForcePosition(agent->GetEntity()->GetWorldTM().Translation());
		}
		else
		{
			auto* sourceParking = vehicleEntity->GetScene()->GetEntityByName(sourceParkingWaypointName);
			if (sourceParking == nullptr)
				return false;
			vehicleEntity->ForcePosition(sourceParking->GetWorldTM().Translation());
		}

		vehicle->SetUseWaypointRoute(true);
		vehicle->SetWaypointRouteEndpoints(sourceParkingWaypointName.empty() ? task.targetParkingWaypointName : sourceParkingWaypointName, task.targetParkingWaypointName);
		vehicle->SetDespawnAtRouteEnd(false);
		vehicle->RestartPath();
		return true;
	}

	void CityRoutineSystemComponent::FinishTask(RoutineAgentComponent* agent, bool success)
	{
		if (agent == nullptr)
			return;

		const auto completedTask = agent->GetActiveTask();
		const bool hideAfterCompletion =
			success &&
			completedTask.has_value() &&
			completedTask->type == RoutineTaskType::GoToWork;

		CleanupAgentVehicle(agent);
		if (agent->GetEntity() != nullptr)
		{
			if (auto* nav = agent->GetEntity()->GetComponent<NavigationComponent>(); nav != nullptr)
			{
				nav->ClearPath();
			}
		}
		if (hideAfterCompletion)
		{
			agent->GetEntity()->SetFlag(EntityFlags::DoNotRender);
		}
		else
		{
			agent->GetEntity()->ClearFlags(EntityFlags::DoNotRender);
		}
		agent->SetBusy(false);
		agent->SetExecutionState(RoutineExecutionState::Idle);
		agent->SetStateTimer(0.0f);
		agent->RetryTimerRef() = 0.0f;
		_agentMotionStates.erase(agent->GetEntity()->GetName());

		if (!success)
		{
			agent->SetActiveTask(std::nullopt);
			return;
		}

		agent->SetActiveTask(std::nullopt);
		if (!agent->RestoreSuspendedTask())
		{
			// nothing to restore
		}
		else
		{
			const auto& resumed = agent->GetActiveTask();
			auto* scene = agent->GetEntity() != nullptr ? agent->GetEntity()->GetScene() : nullptr;
			if (resumed.has_value() && scene != nullptr)
			{
				const auto& task = *resumed;
				const bool isCommuteLike =
					task.type == RoutineTaskType::GoHome ||
					task.type == RoutineTaskType::GoToWork ||
					task.type == RoutineTaskType::CustomTravel;

				if (isCommuteLike)
				{
					constexpr float kArrivalRadius = 8.0f;
					math::Vector3 destination = math::Vector3::Zero;
					bool hasDestination = false;

					const std::string waypointName = task.targetEntryWaypointName.empty() ? task.targetParkingWaypointName : task.targetEntryWaypointName;
					if (!waypointName.empty())
					{
						auto* waypoint = scene->GetEntityByName(waypointName);
						if (waypoint != nullptr && !waypoint->IsPendingDeletion())
						{
							destination = waypoint->GetWorldTM().Translation();
							hasDestination = true;
						}
					}

					if (!hasDestination && !task.targetEntityName.empty())
					{
						auto* targetEntity = scene->GetEntityByName(task.targetEntityName);
						if (targetEntity != nullptr && !targetEntity->IsPendingDeletion())
						{
							destination = targetEntity->GetWorldTM().Translation();
							hasDestination = true;
						}
					}

					if (hasDestination)
					{
						const math::Vector3 agentPos = agent->GetEntity()->GetWorldTM().Translation();
						if ((destination - agentPos).LengthSquared() <= (kArrivalRadius * kArrivalRadius))
						{
							agent->SetActiveTask(std::nullopt);
							agent->SetBusy(false);
							agent->SetExecutionState(RoutineExecutionState::Idle);
							agent->SetStateTimer(0.0f);
						}
					}
				}
			}
		}
	}

	void CityRoutineSystemComponent::HandleTaskRetryOrFailure(RoutineAgentComponent* agent, const std::string& failureReason)
	{
		if (agent == nullptr || !agent->GetActiveTask().has_value())
			return;

		auto task = *agent->GetActiveTask();
		std::string lowerReason = failureReason;
		std::transform(lowerReason.begin(), lowerReason.end(), lowerReason.begin(),
			[](unsigned char c) { return (char)std::tolower(c); });
		const bool isRouteFailure = lowerReason.find("route") != std::string::npos || lowerReason.find("drive") != std::string::npos;
		if (isRouteFailure && task.rerouteAttempts < task.maxReroutes)
		{
			task.rerouteAttempts += 1;

			const math::Vector3 agentPos = agent->GetEntity()->GetWorldTM().Translation();
			task.sourceParkingWaypointName = FindNearestWaypointName(agentPos);

			math::Vector3 destinationPos = agentPos;
			auto* scene = agent->GetEntity()->GetScene();
			if (scene != nullptr && !task.targetEntityName.empty())
			{
				auto* target = scene->GetEntityByName(task.targetEntityName);
				if (target != nullptr && !target->IsPendingDeletion())
					destinationPos = target->GetWorldTM().Translation();
			}
			task.targetParkingWaypointName = FindNearestWaypointName(destinationPos);
			if (task.targetEntryWaypointName.empty())
				task.targetEntryWaypointName = task.targetParkingWaypointName;

			CleanupAgentVehicle(agent);
			agent->SetActiveTask(task);
			agent->SetExecutionState(RoutineExecutionState::Idle);
			agent->SetStateTimer(0.0f);
			agent->RetryTimerRef() = 0.0f;
			LOG_WARN("CityRoutineSystem: Agent '%s' rerouting task attempt %d/%d after: %s", agent->GetEntity()->GetName().c_str(), task.rerouteAttempts, task.maxReroutes, failureReason.c_str());
			return;
		}

		task.retries += 1;
		if (task.retries <= task.maxRetries)
		{
			CleanupAgentVehicle(agent);
			agent->SetActiveTask(task);
			agent->SetExecutionState(RoutineExecutionState::Failed);
			agent->RetryTimerRef() = std::max(task.retryDelaySeconds, 0.1f);
			agent->SetStateTimer(0.0f);
			LOG_WARN("CityRoutineSystem: Agent '%s' task retry %d/%d because: %s", agent->GetEntity()->GetName().c_str(), task.retries, task.maxRetries, failureReason.c_str());
			return;
		}

		LOG_WARN("CityRoutineSystem: Agent '%s' task failed permanently: %s", agent->GetEntity()->GetName().c_str(), failureReason.c_str());
		FinishTask(agent, false);
	}

	std::string CityRoutineSystemComponent::FindNearestWaypointName(const math::Vector3& worldPosition) const
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

			std::vector<Entity*> waypoints;
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

	void CityRoutineSystemComponent::CleanupAgentVehicle(RoutineAgentComponent* agent)
	{
		if (agent == nullptr || agent->GetEntity() == nullptr || agent->GetCurrentVehicleEntityName().empty())
			return;

		auto* scene = agent->GetEntity()->GetScene();
		if (scene == nullptr)
			return;

		auto* vehicle = scene->GetEntityByName(agent->GetCurrentVehicleEntityName());
		if (vehicle != nullptr && !vehicle->IsPendingDeletion())
		{
			vehicle->DeleteMe();
		}

		agent->SetCurrentVehicleEntityName(std::string());
		if (auto* nav = agent->GetEntity()->GetComponent<NavigationComponent>(); nav != nullptr)
		{
			nav->ClearPath();
		}
	}

	bool CityRoutineSystemComponent::UpdateWalkStuckRecovery(RoutineAgentComponent* agent, float frameTime, const std::string& waypointEntityName, const char* failureReason)
	{
		if (!_enableWalkStuckRecovery || agent == nullptr || agent->GetEntity() == nullptr || frameTime <= 0.0f)
			return false;

		if (waypointEntityName.empty())
			return false;

		auto& motion = _agentMotionStates[agent->GetEntity()->GetName()];
		const math::Vector3 currentPos = agent->GetEntity()->GetWorldTM().Translation();

		if (!motion.hasLastPosition)
		{
			motion.lastPosition = currentPos;
			motion.hasLastPosition = true;
			return false;
		}

		const float speed = (currentPos - motion.lastPosition).Length() / frameTime;
		motion.lastPosition = currentPos;
		motion.repathCooldownSeconds = std::max(0.0f, motion.repathCooldownSeconds - frameTime);

		if (speed <= std::max(0.0f, _walkStuckSpeedThreshold))
		{
			motion.lowMotionAccumSeconds += frameTime;
		}
		else
		{
			motion.lowMotionAccumSeconds = 0.0f;
		}

		if (motion.lowMotionAccumSeconds < std::max(0.1f, _walkStuckDurationSeconds) || motion.repathCooldownSeconds > 0.0f)
			return false;

		motion.lowMotionAccumSeconds = 0.0f;
		motion.repathCooldownSeconds = std::max(0.1f, _walkStuckRepathCooldownSeconds);

		if (!BeginWalkTo(agent, waypointEntityName))
		{
			HandleTaskRetryOrFailure(agent, failureReason != nullptr ? failureReason : "Stuck while walking");
			return true;
		}

		agent->SetStateTimer(0.0f);
		LOG_WARN("CityRoutineSystem: Agent '%s' triggered walk stuck recovery toward '%s'", agent->GetEntity()->GetName().c_str(), waypointEntityName.c_str());
		return false;
	}

	void CityRoutineSystemComponent::OnDebugRender()
	{
		if (!g_pEnv->IsEditorMode())
			return;

		if (!_drawSelectedAgentHud && !_drawRoutineWalkPaths)
			return;

		auto* scene = GetEntity() != nullptr ? GetEntity()->GetScene() : nullptr;
		if (scene == nullptr)
			return;

		std::vector<RoutineAgentComponent*> agents;
		scene->GetComponents<RoutineAgentComponent>(agents);
		if (agents.empty())
			return;

		auto* camera = scene->GetMainCamera();
		if (camera == nullptr)
			return;

		if (_drawRoutineWalkPaths)
		{
			const math::Color pathColor = math::Color(HEX_RGBA_TO_FLOAT4(46, 204, 113, 220));
			const math::Color currentColor = math::Color(HEX_RGBA_TO_FLOAT4(52, 152, 219, 230));

			for (auto* agent : agents)
			{
				if (agent == nullptr || agent->GetEntity() == nullptr || agent->GetEntity()->IsPendingDeletion())
					continue;

				auto* nav = agent->GetEntity()->GetComponent<NavigationComponent>();
				if (nav == nullptr || !nav->HasPath())
					continue;

				const auto& path = nav->GetPathPoints();
				if (path.size() < 2)
					continue;

				for (size_t i = 0; i + 1 < path.size(); ++i)
				{
					const math::Color color = (i == nav->GetPathIndex()) ? currentColor : pathColor;
					g_pEnv->_debugRenderer->DrawLine(path[i], path[i + 1], color);
				}
			}
		}

		if (!_drawSelectedAgentHud)
			return;

		auto* renderer = g_pEnv->GetUIManager().GetRenderer();
		if (renderer == nullptr || g_pEnv->_inputSystem == nullptr)
			return;

		for (auto* agent : agents)
		{
			if (agent == nullptr || agent->GetEntity() == nullptr || agent->GetEntity()->IsPendingDeletion())
				continue;
			if (!agent->GetEntity()->HasFlag(EntityFlags::SelectedInEditor))
				continue;

			int32_t x = 0;
			int32_t y = 0;
			if (!g_pEnv->_inputSystem->GetWorldToScreenPosition(camera, agent->GetEntity()->GetWorldTM().Translation(), x, y))
				continue;

			const auto& active = agent->GetActiveTask();
			const std::string taskType = active.has_value() ? ToString(active->type) : "None";
			const std::string target = active.has_value()
				? (active->targetEntryWaypointName.empty() ? active->targetParkingWaypointName : active->targetEntryWaypointName)
				: std::string();
			const int32_t retries = active.has_value() ? active->retries : 0;
			const int32_t maxRetries = active.has_value() ? active->maxRetries : 0;
			const char* stateCStr = ToString(agent->GetExecutionState());
			const std::wstring stateW(stateCStr, stateCStr + strlen(stateCStr));
			const std::wstring taskTypeW(taskType.begin(), taskType.end());
			const std::wstring targetW(target.begin(), target.end());

			const std::wstring line1 = std::format(L"Routine: {} | Task: {}", stateW, taskTypeW);
			const std::wstring line2 = std::format(L"Target: {} | Retries: {}/{}", targetW, retries, maxRetries);

			renderer->PrintText(
				renderer->_style.font.get(),
				(uint8_t)Style::FontSize::Regular,
				x,
				y - 32,
				math::Color(0xFFFFFFFF),
				FontAlign::CentreLR,
				line1);

			renderer->PrintText(
				renderer->_style.font.get(),
				(uint8_t)Style::FontSize::Regular,
				x,
				y - 14,
				math::Color(0xFFE2E2E2),
				FontAlign::CentreLR,
				line2);
		}
	}

	bool CityRoutineSystemComponent::IsRoleMatch(const std::vector<std::string>& a, const std::vector<std::string>& b) const
	{
		if (a.empty() || b.empty())
			return false;

		for (const auto& left : a)
		{
			for (const auto& right : b)
			{
				if (_stricmp(left.c_str(), right.c_str()) == 0)
					return true;
			}
		}
		return false;
	}

}
