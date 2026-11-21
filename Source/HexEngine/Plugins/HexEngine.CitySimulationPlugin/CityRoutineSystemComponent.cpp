#include "CityRoutineSystemComponent.hpp"
#include "CityEmergencyDispatcherSystemComponent.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_set>
#include <HexEngine.Core/Entity/Component/DayNightCycleComponent.hpp>
#include <HexEngine.Core/Entity/Component/NavigationComponent.hpp>
#include <HexEngine.Core/Entity/Component/SkeletalAnimationComponent.hpp>
#include <HexEngine.Core/GUI/Elements/LineEdit.hpp>
#include <HexEngine.Core/GUI/Elements/ArrayElement.hpp>
#include <format>
#include "CityAnchorUtils.hpp"
#include "PlaceOfWorkComponent.hpp"
#include "CityAssetPins.hpp"
#include "ResidenceComponent.hpp"
#include "RoutineAgentComponent.hpp"
#include "TrafficLaneComponent.hpp"
#include "TrafficVehicleComponent.hpp"
#include "ServiceStationComponent.hpp"
#include <HexEngine.Core/Entity/Component/Transform.hpp>
#include <HexEngine.Core/Entity/Entity.hpp>
#include <HexEngine.Core/FileSystem/PrefabLoader.hpp>
#include <HexEngine.Core/GUI/Elements/Checkbox.hpp>
#include <HexEngine.Core/GUI/Elements/ComponentWidget.hpp>
#include <HexEngine.Core/GUI/Elements/DragFloat.hpp>
#include <HexEngine.Core/GUI/Elements/DragInt.hpp>
#include <HexEngine.Core/Math/FloatMath.hpp>
#include <HexEngine.Core/Scene/Scene.hpp>
#include <HexEngine.Core/Scene/SceneManager.hpp>
#include <HexEngine.Core/Environment/LogFile.hpp>
#include <HexEngine.Core/GUI/UIManager.hpp>
#include <HexEngine.Core/Graphics/DebugRenderer.hpp>
#include <HexEngine.Core/Input/InputSystem.hpp>

namespace
{
	// Descendants hidden BY SetAgentHidden (so showing the agent doesn't unhide a
	// child that was hidden for its own reasons). Generation-checked ids.
	std::unordered_set<HexEngine::EntityId, HexEngine::EntityIdHasher> g_childrenHiddenWithAgent;

	void SetHiddenAndFlush(HexEngine::Entity* entity, bool hidden)
	{
		if (entity->HasFlag(HexEngine::EntityFlags::DoNotRender) == hidden)
			return;
		if (hidden)
			entity->SetFlag(HexEngine::EntityFlags::DoNotRender);
		else
			entity->ClearFlags(HexEngine::EntityFlags::DoNotRender);
		// DoNotRender is only read when a PVS rebuilds. Without this flush a shown
		// agent stayed invisible (and a hidden one visible) until something else
		// forced a rebuild - the "body missing, hair still there" residents.
		if (auto* scene = entity->GetScene())
			scene->FlushPVS(entity);
	}

	// Hide / show a routine agent INCLUDING its child entities: DoNotRender is
	// per-entity, so hiding only the root (getting into a car, going indoors) left
	// bone-bound hair, hats and accessories floating where the body had been.
	void SetAgentHidden(HexEngine::Entity* root, bool hidden)
	{
		if (root == nullptr)
			return;
		SetHiddenAndFlush(root, hidden);

		std::function<void(HexEngine::Entity*)> visit = [&](HexEngine::Entity* parent)
		{
			for (HexEngine::Entity* child : parent->GetChildren())
			{
				if (child == nullptr || child->IsPendingDeletion())
					continue;
				if (hidden)
				{
					if (!child->HasFlag(HexEngine::EntityFlags::DoNotRender))
					{
						g_childrenHiddenWithAgent.insert(child->GetId());
						SetHiddenAndFlush(child, true);
					}
				}
				else if (g_childrenHiddenWithAgent.erase(child->GetId()) != 0)
				{
					SetHiddenAndFlush(child, false);
				}
				visit(child);
			}
		};
		visit(root);
	}

	constexpr float kWorkplacePriorityWeight = 100.0f;
	constexpr float kArrivalRadius = 8.0f;

	// Deterministic [0,1) from a string + salt (stable across runs and saves).
	float Hash01(const std::string& text, uint64_t salt)
	{
		const uint32_t h = HexEngine::CRC32::HashString((text + "#" + std::to_string(salt)).c_str());
		return (float)(h & 0xFFFFFF) / (float)0x1000000;
	}

	bool HourInWindow(float hour, float start, float end)
	{
		if (start <= end)
			return hour >= start && hour < end;
		return hour >= start || hour < end;	// wraps midnight
	}

	bool ContainsNoCase(const std::string& haystack, const std::string& needle)
	{
		auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
			[](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
		return it != haystack.end();
	}

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

CityRoutineSystemComponent::CityRoutineSystemComponent(HexEngine::Entity* entity) :
	UpdateComponent(entity)
{
	RegisterEntityListener();
}

CityRoutineSystemComponent::CityRoutineSystemComponent(HexEngine::Entity* entity, CityRoutineSystemComponent* copy) :
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
		_walkMaxDistance = copy->_walkMaxDistance;
		_defaultVehiclePrefabPath = copy->_defaultVehiclePrefabPath;
		_roleVehicleRules = copy->_roleVehicleRules;
		_spawnVehicleMinDistance = copy->_spawnVehicleMinDistance;
		_hideAgentsAtDestination = copy->_hideAgentsAtDestination;
		_failedTaskCooldownSeconds = copy->_failedTaskCooldownSeconds;
		_vehicleStuckSeconds = copy->_vehicleStuckSeconds;
		_animateLocomotion = copy->_animateLocomotion;
		_idleClipName = copy->_idleClipName;
		_walkClipName = copy->_walkClipName;
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

void CityRoutineSystemComponent::Serialize(json& data, HexEngine::JsonFile* file)
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
	file->Serialize(data, "_walkMaxDistance", _walkMaxDistance);
	file->Serialize(data, "_defaultVehiclePrefabPath", _defaultVehiclePrefabPath);
	file->Serialize(data, "_roleVehicleRules", _roleVehicleRules);
	file->Serialize(data, "_spawnVehicleMinDistance", _spawnVehicleMinDistance);
	file->Serialize(data, "_hideAgentsAtDestination", _hideAgentsAtDestination);
	file->Serialize(data, "_failedTaskCooldownSeconds", _failedTaskCooldownSeconds);
	file->Serialize(data, "_vehicleStuckSeconds", _vehicleStuckSeconds);
	file->Serialize(data, "_animateLocomotion", _animateLocomotion);
	file->Serialize(data, "_idleClipName", _idleClipName);
	file->Serialize(data, "_walkClipName", _walkClipName);
	file->Serialize(data, "_onSiteWander", _onSiteWander);
	file->Serialize(data, "_onSiteWanderRadius", _onSiteWanderRadius);
}

void CityRoutineSystemComponent::Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask)
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
	file->Deserialize(data, "_walkMaxDistance", _walkMaxDistance);
	file->Deserialize(data, "_defaultVehiclePrefabPath", _defaultVehiclePrefabPath);
	file->Deserialize(data, "_roleVehicleRules", _roleVehicleRules);
	file->Deserialize(data, "_spawnVehicleMinDistance", _spawnVehicleMinDistance);
	_vehicleRulesParsed = false;
	file->Deserialize(data, "_hideAgentsAtDestination", _hideAgentsAtDestination);
	file->Deserialize(data, "_failedTaskCooldownSeconds", _failedTaskCooldownSeconds);
	file->Deserialize(data, "_vehicleStuckSeconds", _vehicleStuckSeconds);
	file->Deserialize(data, "_animateLocomotion", _animateLocomotion);
	file->Deserialize(data, "_idleClipName", _idleClipName);
	file->Deserialize(data, "_walkClipName", _walkClipName);
	file->Deserialize(data, "_onSiteWander", _onSiteWander);
	file->Deserialize(data, "_onSiteWanderRadius", _onSiteWanderRadius);
	_agentMotionStates.clear();
	_onSiteStates.clear();
	_agentAnimStates.clear();
	_locomotionClipCache.clear();
	_lastSimulationHour = -1.0f;
	_dayIndex = 0;
}

bool CityRoutineSystemComponent::CreateWidget(HexEngine::ComponentWidget* widget)
{
	auto* enabled = new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Enabled", &_enabled);
	auto* warn = new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Warn Missing Clock", &_logMissingClockWarning);
	auto* navMesh = new HexEngine::DragInt(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"NavMesh Id", (int32_t*)&_navMeshId, 0, 1024, 1);
	auto* walkTimeout = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Walk Timeout", &_walkStateTimeoutSeconds, 1.0f, 3600.0f, 1.0f, 1);
	auto* driveTimeout = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Drive Timeout", &_driveStateTimeoutSeconds, 1.0f, 3600.0f, 1.0f, 1);
	auto* drawHud = new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Draw Selected Agent HUD", &_drawSelectedAgentHud);
	auto* drawPaths = new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Draw Routine Walk Paths", &_drawRoutineWalkPaths);
	auto* enableStuckRecovery = new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Enable Walk Stuck Recovery", &_enableWalkStuckRecovery);
	auto* stuckSpeed = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Stuck Speed Threshold", &_walkStuckSpeedThreshold, 0.0f, 10.0f, 0.01f, 2);
	auto* stuckDuration = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Stuck Duration", &_walkStuckDurationSeconds, 0.1f, 60.0f, 0.1f, 1);
	auto* stuckCooldown = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Stuck Repath Cooldown", &_walkStuckRepathCooldownSeconds, 0.1f, 60.0f, 0.1f, 1);

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

	auto* walkMax = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Walk Max Distance", &_walkMaxDistance, 0.0f, 10000.0f, 1.0f, 0);
	auto* hideAtDest = new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Hide Agents At Work", &_hideAgentsAtDestination);
	auto* failCooldown = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Failed Task Cooldown", &_failedTaskCooldownSeconds, 0.0f, 600.0f, 0.5f, 1);
	auto* vehicleStuck = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Vehicle Stuck Seconds", &_vehicleStuckSeconds, 0.0f, 600.0f, 1.0f, 0);
	vehicleStuck->SetPrefabOverrideBinding(GetComponentName(), "/_vehicleStuckSeconds");
	auto* animate = new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Animate Locomotion", &_animateLocomotion);
	auto* vehicle = new HexEngine::LineEdit(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Default Vehicle Prefab");
	vehicle->SetValue(std::wstring(_defaultVehiclePrefabPath.begin(), _defaultVehiclePrefabPath.end()));
	vehicle->SetDoesCallbackWaitForReturn(false);
	vehicle->SetOnInputFn([this](HexEngine::LineEdit*, const std::wstring& value) { _defaultVehiclePrefabPath = ws2s(value); });
	new HexEngine::ArrayElement<std::string>(
		widget,
		widget->GetNextPos(),
		HexEngine::Point(widget->GetSize().x - 20, 160),
		L"Role Vehicle Rules (Roles[@Workplaces] = prefabs)",
		_roleVehicleRules,
		[this](HexEngine::Element* parent, std::string& item, int32_t)
		{
			auto* edit = new HexEngine::LineEdit(parent, HexEngine::Point(0, 0), HexEngine::Point(parent->GetSize().x, 18), L"");
			edit->SetValue(std::wstring(item.begin(), item.end()));
			edit->SetDoesCallbackWaitForReturn(true);
			edit->SetOnInputFn([this, &item](HexEngine::LineEdit*, const std::wstring& value) { item = ws2s(value); _vehicleRulesParsed = false; });
		},
		[]() -> std::string { return std::string(); },
		[](const std::string&, int32_t) -> int32_t { return 22; },
		[](const std::string&, int32_t index) -> std::wstring { return std::format(L"Rule {}", index + 1); });

	auto* idleClip = new HexEngine::LineEdit(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Idle Clip");
	idleClip->SetValue(std::wstring(_idleClipName.begin(), _idleClipName.end()));
	idleClip->SetDoesCallbackWaitForReturn(false);
	idleClip->SetOnInputFn([this](HexEngine::LineEdit*, const std::wstring& value) { _idleClipName = ws2s(value); _locomotionClipCache.clear(); });
	auto* walkClip = new HexEngine::LineEdit(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Walk Clip");
	walkClip->SetValue(std::wstring(_walkClipName.begin(), _walkClipName.end()));
	walkClip->SetDoesCallbackWaitForReturn(false);
	walkClip->SetOnInputFn([this](HexEngine::LineEdit*, const std::wstring& value) { _walkClipName = ws2s(value); _locomotionClipCache.clear(); });

	walkMax->SetPrefabOverrideBinding(GetComponentName(), "/_walkMaxDistance");
	hideAtDest->SetPrefabOverrideBinding(GetComponentName(), "/_hideAgentsAtDestination");
	failCooldown->SetPrefabOverrideBinding(GetComponentName(), "/_failedTaskCooldownSeconds");
	animate->SetPrefabOverrideBinding(GetComponentName(), "/_animateLocomotion");
	vehicle->SetPrefabOverrideBinding(GetComponentName(), "/_defaultVehiclePrefabPath");
	idleClip->SetPrefabOverrideBinding(GetComponentName(), "/_idleClipName");
	walkClip->SetPrefabOverrideBinding(GetComponentName(), "/_walkClipName");

	return true;
}

void CityRoutineSystemComponent::OnRemoveEntity(HexEngine::Entity* entity)
{
	if (entity == nullptr)
		return;

	_agentMotionStates.erase(entity->GetName());
	_agentAnimStates.erase(entity->GetName());
	_onSiteStates.erase(entity->GetName());
}

bool CityRoutineSystemComponent::EnqueueTask(HexEngine::Entity* agentEntity, const RoutineTaskSpec& spec)
{
	if (agentEntity == nullptr)
		return false;

	auto* agent = agentEntity->GetComponent<RoutineAgentComponent>();
	if (agent == nullptr)
		return false;

	agent->EnqueueTask(spec);
	return true;
}

bool CityRoutineSystemComponent::RequestTravel(HexEngine::Entity* agentEntity, const std::string& destinationAnchorWaypointEntityName, RoutineTravelMode travelMode)
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

bool CityRoutineSystemComponent::CancelTask(HexEngine::Entity* agentEntity, const std::string& reason)
{
	if (agentEntity == nullptr)
		return false;

	auto* agent = agentEntity->GetComponent<RoutineAgentComponent>();
	if (agent == nullptr)
		return false;

	CleanupAgentVehicle(agent);
	SetAgentHidden(agent->GetEntity(), false);
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

	// Edit mode: don't move agents. They walked off in the editor, the moved
	// positions got saved, and every step bumped the shadow/GI revisions.
	const bool running = HexEngine::g_pEnv != nullptr && HexEngine::g_pEnv->IsGameRunning();
	if (!running)
	{
		// Play just stopped: the restore skips unchanged components, so an
		// agent's NavigationComponent kept its runtime path and went on walking
		// in the editor. Drop the paths and per-trip state.
		if (_wasGameRunning)
		{
			CityAssetPins::Clear();
			_vehicleRulesParsed = false;	// re-expand wildcards next play (new variants)
			ResetOwnedCars();
		}
		if (_wasGameRunning && GetEntity() != nullptr && GetEntity()->GetScene() != nullptr)
		{
			std::vector<RoutineAgentComponent*> agents;
			GetEntity()->GetScene()->GetComponents<RoutineAgentComponent>(agents);
			for (auto* agent : agents)
			{
				if (agent == nullptr || agent->GetEntity() == nullptr)
					continue;
				CleanupAgentVehicle(agent);	// the trip's car is play-time only
				if (auto* nav = agent->GetEntity()->GetComponent<HexEngine::NavigationComponent>(); nav != nullptr)
					nav->ClearPath();
			}
			_agentMotionStates.clear();
			_agentAnimStates.clear();
			_lastSimulationHour = -1.0f;
		}
		_wasGameRunning = false;
		return;
	}
	_wasGameRunning = true;

	if (!_enabled)
		return;

	const float simulationHour = GetSimulationHour();
	if (simulationHour < 0.0f)
		return;

	// Day counter (the clock only reports the hour): advances when the hour
	// wraps, so per-day venue choices vary from day to day.
	if (_lastSimulationHour >= 0.0f && simulationHour + 12.0f < _lastSimulationHour)
		++_dayIndex;
	_lastSimulationHour = simulationHour;

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

	std::vector<HexEngine::DayNightCycleComponent*> dayNight;
	scene->GetComponents<HexEngine::DayNightCycleComponent>(dayNight);
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
		UpdateLocomotionAnimation(agent, frameTime);
	}
}

void CityRoutineSystemComponent::UpdateAgentState(RoutineAgentComponent* agent, float frameTime, float simulationHour, const std::unordered_map<std::string, int32_t>& workplaceOccupancy)
{
	agent->SeedCooldownRef() = std::max(0.0f, agent->SeedCooldownRef() - frameTime);
	EnsureWorkplaceAssignment(agent, simulationHour, workplaceOccupancy);

	// Held by the game (e.g. a job pickup is waiting on this NPC): stay on site
	// - no new trip, schedule changes wait until the hold is released.
	if (agent->IsHeldAtVenue() && !agent->GetActiveTask().has_value() && !agent->GetSettledVenueName().empty())
	{
		UpdateOnSite(agent, frameTime);
		return;
	}

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
				SetAgentHidden(agent->GetEntity(), false);
				agent->SuspendCurrentTask();
			}
		}
	}

	if (!agent->GetActiveTask().has_value())
	{
		RoutineTaskSpec nextTask;
		if (agent->TryPopTask(nextTask))
		{
			// Leaving the venue: drop the on-site stroll and any stale arrival flag
			// from it so the new task's first walk doesn't "arrive" instantly.
			if (_onSiteStates.erase(agent->GetEntity()->GetName()) > 0)
				agent->ConsumeNavigationReached();
			agent->SetSettledVenueName(std::string());
			agent->SetActiveTask(nextTask);
			SetAgentHidden(agent->GetEntity(), false);
			agent->SetExecutionState(RoutineExecutionState::Idle);
			agent->SetStateTimer(0.0f);
			agent->RetryTimerRef() = 0.0f;
			agent->SetBusy(true);
		}
	}

	if (!agent->GetActiveTask().has_value())
	{
		UpdateOnSite(agent, frameTime);
		return;
	}

	auto task = *agent->GetActiveTask();
	// Waiting at a red pedestrian crossing is part of the walk, not lost time:
	// don't let it run down the walk timeout.
	const auto* gateNav = agent->GetEntity()->GetComponent<HexEngine::NavigationComponent>();
	const bool heldAtCrossing = gateNav != nullptr && gateNav->IsHeldByGate();
	if (!heldAtCrossing)
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
		std::string sourceParkingWaypointName = task.sourceParkingWaypointName.empty() ? task.targetParkingWaypointName : task.sourceParkingWaypointName;
		// Own car nearby: walk to it (driveway / where it was left) rather than
		// to a lane waypoint where a car would be conjured.
		if (agent->StateTimerRef() <= frameTime)
			RehomeOwnedCarIfOwnerHome(agent);
		if (IsOwnedCarAvailable(agent, agent->GetEntity()->GetWorldTM().Translation()))
			sourceParkingWaypointName = agent->GetOwnedVehicleEntityName();

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

		if (agent->StateTimerRef() > GetWalkTimeout(agent))
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
		HexEngine::Entity* vehicleEntity = AcquireVehicleForTask(agent, task);
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

		if (auto* nav = agent->GetEntity()->GetComponent<HexEngine::NavigationComponent>(); nav != nullptr)
		{
			nav->ClearPath();
		}

		SetAgentHidden(agent->GetEntity(), true);
		agent->SetExecutionState(RoutineExecutionState::DriveToDestinationParking);
		agent->SetStateTimer(0.0f);
		break;
	}

	case RoutineExecutionState::DriveToDestinationParking:
	{
		HexEngine::Entity* vehicleEntity = nullptr;
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

		// Traffic jams (lane spawners can wedge cars at a junction) would
		// otherwise hold the agent until the drive timeout. Abandon a car that
		// has not moved for a while; the agent gets out there and walks on.
		{
			auto& motion = _agentMotionStates[agent->GetEntity()->GetName()];
			const math::Vector3 vehiclePos = vehicleEntity->GetWorldTM().Translation();
			if (!motion.hasLastVehiclePosition || (vehiclePos - motion.lastVehiclePosition).LengthSquared() > 0.25f)
			{
				motion.lastVehiclePosition = vehiclePos;
				motion.hasLastVehiclePosition = true;
				motion.vehicleStillSeconds = 0.0f;
			}
			else
			{
				motion.vehicleStillSeconds += frameTime;
			}

			// The hidden driver rides along: without this the agent entity stays
			// at the parking spot it left, so anything locating the person (the
			// game's map markers, interaction, proximity) points at an empty car
			// park. Only re-placed after a real move to keep transform churn low.
			const math::Vector3 agentPos = agent->GetEntity()->GetWorldTM().Translation();
			if ((vehiclePos - agentPos).LengthSquared() > 1.0f)
				agent->GetEntity()->ForcePosition(vehiclePos);
			if (_vehicleStuckSeconds > 0.0f && motion.vehicleStillSeconds > _vehicleStuckSeconds)
			{
				motion.hasLastVehiclePosition = false;
				motion.vehicleStillSeconds = 0.0f;
				HandleTaskRetryOrFailure(agent, "Vehicle stuck during drive");
				break;
			}
		}

		if (vehicle->ConsumeRouteEndReachedEvent())
		{
			math::Vector3 exitPosition = vehicleEntity->GetWorldTM().Translation();
			if (vehicleEntity->GetName() == agent->GetOwnedVehicleEntityName())
			{
				// Own car: park it now (driveway at home, car park elsewhere) so it
				// doesn't sit in the lane while the owner walks in.
				CleanupAgentVehicle(agent);
				exitPosition = vehicleEntity->GetPosition();	// root entity: local == world; the world TM lags a ForcePosition
			}
			SetAgentHidden(agent->GetEntity(), false);
			agent->GetEntity()->ForcePosition(exitPosition);
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

		if (agent->StateTimerRef() > GetWalkTimeout(agent))
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
		const uint32_t tie = HexEngine::CRC32::HashString(workplace->GetEntity()->GetName().c_str());

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

	if (agent->GetActiveTask().has_value() || agent->HasQueuedTasks() || agent->SeedCooldownRef() > 0.0f)
		return;

	auto* scene = agent->GetEntity()->GetScene();
	if (scene == nullptr)
		return;

	RoutineTaskSpec task;
	if (!BuildScheduledTask(agent, simulationHour, task))
		return;

	const math::Vector3 agentPos = agent->GetEntity()->GetWorldTM().Translation();
	math::Vector3 destination;
	if (!ResolveAnchorPosition(task.targetEntryWaypointName, destination) &&
		!ResolveAnchorPosition(task.targetEntityName, destination))
		return;

	if ((destination - agentPos).LengthSquared() <= kArrivalRadius * kArrivalRadius)
		return;	// already there

	task.travelMode = ChooseTravelMode(agent, agentPos, destination);
	if (task.travelMode == RoutineTravelMode::DriveFirst)
	{
		// Buildings rarely name lane waypoints; use the nearest ones.
		if (task.targetParkingWaypointName.empty())
			task.targetParkingWaypointName = FindNearestWaypointName(destination);
		if (task.sourceParkingWaypointName.empty())
			task.sourceParkingWaypointName = FindNearestWaypointName(agentPos);
		if (task.targetParkingWaypointName.empty() || task.sourceParkingWaypointName.empty() ||
			task.targetParkingWaypointName == task.sourceParkingWaypointName)
			task.travelMode = RoutineTravelMode::WalkOnly;
	}

	agent->EnqueueTask(task);
}

bool CityRoutineSystemComponent::BuildScheduledTask(RoutineAgentComponent* agent, float simulationHour, RoutineTaskSpec& task)
{
	auto* scene = agent->GetEntity()->GetScene();

	PlaceOfWorkComponent* workplace = nullptr;
	ResidenceComponent* residence = nullptr;
	if (!agent->GetAssignedWorkplaceEntityName().empty())
	{
		if (auto* workEntity = scene->GetEntityByName(agent->GetAssignedWorkplaceEntityName()); workEntity != nullptr)
			workplace = workEntity->GetComponent<PlaceOfWorkComponent>();
	}
	if (!agent->GetHomeEntityName().empty())
	{
		if (auto* homeEntity = scene->GetEntityByName(agent->GetHomeEntityName()); homeEntity != nullptr)
			residence = homeEntity->GetComponent<ResidenceComponent>();
	}

	// What the agent should be doing now.
	std::string activity;
	uint64_t blockKey = 0;
	const auto& schedule = agent->GetSchedule();
	if (schedule.empty())
	{
		activity = (workplace != nullptr && workplace->IsHourInAnyShift(simulationHour)) ? "Work" : "Home";
	}
	else
	{
		// One stable per-agent offset shifts the whole day, so a crowd with
		// the same schedule doesn't move in lockstep.
		const float offset = (Hash01(agent->GetEntity()->GetName(), 0) - 0.5f) * agent->GetScheduleJitterHours();
		float hour = std::fmod(simulationHour - offset + 24.0f, 24.0f);
		activity = "Home";
		for (size_t i = 0; i < schedule.size(); ++i)
		{
			if (HourInWindow(hour, schedule[i].start, schedule[i].end))
			{
				activity = schedule[i].activity;
				blockKey = ((uint64_t)_dayIndex << 8) | (uint64_t)(i + 1);
				break;
			}
		}
	}

	if (_stricmp(activity.c_str(), "Work") == 0 && workplace == nullptr)
		activity = "Home";
	agent->SetOnShift(_stricmp(activity.c_str(), "Work") == 0);

	task.priority = 0;
	if (_stricmp(activity.c_str(), "Work") == 0)
	{
		task.type = RoutineTaskType::GoToWork;
		task.targetEntityName = workplace->GetEntity()->GetName();
		task.targetEntryWaypointName = workplace->ResolveEntryWaypointName();
		task.targetParkingWaypointName = workplace->ResolveParkingWaypointName();
		task.sourceParkingWaypointName = residence != nullptr ? residence->ResolveParkingWaypointName() : std::string();
	}
	else if (_stricmp(activity.c_str(), "Home") == 0)
	{
		if (residence == nullptr)
			return false;
		task.type = RoutineTaskType::GoHome;
		task.targetEntityName = residence->GetEntity()->GetName();
		task.targetEntryWaypointName = residence->ResolveEntryWaypointName();
		task.targetParkingWaypointName = residence->ResolveParkingWaypointName();
	}
	else
	{
		// A visit: pick a venue offering this service once per block.
		if (agent->GetPlannedBlockKey() != blockKey)
			agent->SetPlannedBlock(blockKey, PickVenue(agent, activity, simulationHour, blockKey));

		auto* venueEntity = agent->GetPlannedTargetName().empty() ? nullptr : scene->GetEntityByName(agent->GetPlannedTargetName());
		auto* venue = venueEntity != nullptr ? venueEntity->GetComponent<PlaceOfWorkComponent>() : nullptr;
		if (venue == nullptr)
		{
			// Nowhere open for it: go home instead.
			if (residence == nullptr)
				return false;
			task.type = RoutineTaskType::GoHome;
			task.targetEntityName = residence->GetEntity()->GetName();
			task.targetEntryWaypointName = residence->ResolveEntryWaypointName();
			task.targetParkingWaypointName = residence->ResolveParkingWaypointName();
		}
		else
		{
			task.type = RoutineTaskType::CustomTravel;
			task.reason = activity;
			task.targetEntityName = venueEntity->GetName();
			task.targetEntryWaypointName = venue->ResolveEntryWaypointName();
			task.targetParkingWaypointName = venue->ResolveParkingWaypointName();
		}
	}

	// No entry waypoint: walk to the building itself (nearest navmesh point).
	if (task.targetEntryWaypointName.empty())
		task.targetEntryWaypointName = task.targetEntityName;
	return true;
}

std::string CityRoutineSystemComponent::PickVenue(RoutineAgentComponent* agent, const std::string& serviceTag, float simulationHour, uint64_t blockKey) const
{
	auto* scene = agent->GetEntity()->GetScene();
	std::vector<PlaceOfWorkComponent*> places;
	scene->GetComponents<PlaceOfWorkComponent>(places);

	std::vector<RoutineAgentComponent*> agents;
	scene->GetComponents<RoutineAgentComponent>(agents);

	const math::Vector3 agentPos = agent->GetEntity()->GetWorldTM().Translation();
	std::vector<std::pair<std::string, float>> candidates;
	float total = 0.0f;
	for (auto* place : places)
	{
		if (place == nullptr || place->GetEntity() == nullptr || place->GetEntity()->IsPendingDeletion())
			continue;
		if (!place->OffersService(serviceTag) || !place->IsHourInAnyShift(simulationHour))
			continue;

		const std::string& name = place->GetEntity()->GetName();
		int32_t visitors = 0;
		for (auto* other : agents)
		{
			if (other != nullptr && other != agent && other->GetPlannedTargetName() == name)
				++visitors;
		}
		if (place->GetVisitorCapacity() > 0 && visitors >= place->GetVisitorCapacity())
			continue;

		// Prefer nearby places, but not exclusively.
		const float distance = (place->GetEntity()->GetWorldTM().Translation() - agentPos).Length();
		const float weight = 1.0f / ((1.0f + distance / 60.0f) * (1.0f + distance / 60.0f));
		candidates.emplace_back(name, weight);
		total += weight;
	}

	if (candidates.empty() || total <= 0.0f)
		return {};

	float roll = Hash01(agent->GetEntity()->GetName(), blockKey + 1) * total;
	for (const auto& [name, weight] : candidates)
	{
		if (roll < weight)
			return name;
		roll -= weight;
	}
	return candidates.back().first;
}

RoutineTravelMode CityRoutineSystemComponent::ChooseTravelMode(RoutineAgentComponent* agent, const math::Vector3& from, const math::Vector3& to) const
{
	switch (agent->GetTravelPreference())
	{
	case RoutineTravelPreference::Walk:
		return RoutineTravelMode::WalkOnly;
	case RoutineTravelPreference::Drive:
		return RoutineTravelMode::DriveFirst;
	default:
		break;
	}

	const float distance = (to - from).Length();
	if (distance <= _walkMaxDistance)
		return RoutineTravelMode::WalkOnly;
	// Own car to hand: drive. Otherwise a car would have to appear out of thin
	// air - only do that when the trip is genuinely too long to walk.
	if (IsOwnedCarAvailable(agent, from))
		return RoutineTravelMode::DriveFirst;
	if (agent->GetPreferredVehiclePrefabPath().empty() && _defaultVehiclePrefabPath.empty() && _roleVehicleRules.empty())
		return RoutineTravelMode::WalkOnly;
	return distance > _spawnVehicleMinDistance ? RoutineTravelMode::DriveFirst : RoutineTravelMode::WalkOnly;
}

namespace
{
	std::string TrimCopy(const std::string& v)
	{
		const auto b = v.find_first_not_of(" \t");
		if (b == std::string::npos)
			return std::string();
		const auto e = v.find_last_not_of(" \t");
		return v.substr(b, e - b + 1);
	}

	std::vector<std::string> SplitCsv(const std::string& v)
	{
		std::vector<std::string> out;
		size_t start = 0;
		while (start <= v.size())
		{
			const auto comma = v.find(',', start);
			const auto token = TrimCopy(v.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
			if (!token.empty())
				out.push_back(token);
			if (comma == std::string::npos)
				break;
			start = comma + 1;
		}
		return out;
	}

	bool EqualsNoCase(const std::string& a, const std::string& b)
	{
		return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
			[](char x, char y) { return std::tolower((unsigned char)x) == std::tolower((unsigned char)y); });
	}

	// "GameData.Prefabs/Vehicles/V_Sedan_*.hprefab" -> every matching file, as refs in
	// the same "<FileSystem>.<dir>/<file>" form. Loose-file data only (a packaged
	// build lists nothing - spell such prefabs out in full).
	void ExpandPrefabPattern(const std::string& pattern, std::vector<std::string>& out)
	{
		const auto star = pattern.find('*');
		if (star == std::string::npos)
		{
			out.push_back(pattern);
			return;
		}

		const auto dot = pattern.find('.');
		const auto slash = pattern.find_last_of('/');
		if (dot == std::string::npos || slash == std::string::npos || slash < dot || star < slash)
		{
			LOG_WARN("CityRoutineSystem: vehicle prefab pattern '%s' must look like GameData.Dir/Name*.hprefab", pattern.c_str());
			return;
		}

		const std::string fsName = pattern.substr(0, dot);
		const std::string relDir = pattern.substr(dot + 1, slash - dot - 1);
		const std::string filePattern = pattern.substr(slash + 1);
		const std::string prefix = filePattern.substr(0, filePattern.find('*'));
		const std::string suffix = filePattern.substr(filePattern.find('*') + 1);

		auto* fileSystem = HexEngine::g_pEnv->GetResourceSystem().FindFileSystemByName(std::wstring(fsName.begin(), fsName.end()));
		if (fileSystem == nullptr)
		{
			LOG_WARN("CityRoutineSystem: no file system '%s' for vehicle prefab pattern '%s'", fsName.c_str(), pattern.c_str());
			return;
		}

		std::error_code ec;
		const fs::path dir = fileSystem->GetDataDirectory() / relDir;
		std::vector<std::string> found;
		for (const auto& entry : fs::directory_iterator(dir, ec))
		{
			if (!entry.is_regular_file(ec))
				continue;
			const std::string name = entry.path().filename().string();
			if (name.size() >= prefix.size() + suffix.size() &&
				name.compare(0, prefix.size(), prefix) == 0 &&
				name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
			{
				found.push_back(fsName + "." + relDir + "/" + name);
			}
		}

		if (found.empty())
			LOG_WARN("CityRoutineSystem: vehicle prefab pattern '%s' matched nothing", pattern.c_str());
		std::sort(found.begin(), found.end());	// stable picks across runs
		out.insert(out.end(), found.begin(), found.end());
	}
}

void CityRoutineSystemComponent::ParseVehicleRules()
{
	_parsedVehicleRules.clear();
	_vehicleRulesParsed = true;

	for (const auto& line : _roleVehicleRules)
	{
		const auto eq = line.find('=');
		if (eq == std::string::npos)
		{
			if (!TrimCopy(line).empty())
				LOG_WARN("CityRoutineSystem: vehicle rule '%s' has no '='", line.c_str());
			continue;
		}

		ParsedVehicleRule rule;
		std::string lhs = line.substr(0, eq);
		const auto at = lhs.find('@');
		if (at != std::string::npos)
		{
			rule.workplaces = SplitCsv(lhs.substr(at + 1));
			lhs = lhs.substr(0, at);
		}
		rule.roles = SplitCsv(lhs);
		for (const auto& pattern : SplitCsv(line.substr(eq + 1)))
			ExpandPrefabPattern(pattern, rule.prefabs);

		if (!rule.roles.empty() && !rule.prefabs.empty())
			_parsedVehicleRules.push_back(std::move(rule));
	}
}

std::string CityRoutineSystemComponent::PickRoleVehiclePrefab(const RoutineAgentComponent* agent)
{
	if (agent == nullptr || _roleVehicleRules.empty())
		return std::string();
	if (!_vehicleRulesParsed)
		ParseVehicleRules();

	const auto& tags = agent->GetRoleTags();
	const auto& workplace = agent->GetAssignedWorkplaceEntityName();

	for (const auto& rule : _parsedVehicleRules)
	{
		bool roleMatch = false;
		for (const auto& role : rule.roles)
		{
			if (role == "*")
			{
				roleMatch = true;
				break;
			}
			for (const auto& tag : tags)
			{
				if (EqualsNoCase(role, tag))
				{
					roleMatch = true;
					break;
				}
			}
			if (roleMatch)
				break;
		}
		if (!roleMatch)
			continue;

		if (!rule.workplaces.empty())
		{
			bool workplaceMatch = false;
			for (const auto& name : rule.workplaces)
			{
				if (EqualsNoCase(name, workplace))
				{
					workplaceMatch = true;
					break;
				}
			}
			if (!workplaceMatch)
				continue;
		}

		// Same agent -> same car every trip (FNV-1a of the entity name: stable
		// across runs, unlike std::hash).
		uint32_t h = 2166136261u;
		const std::string& key = agent->GetEntity() != nullptr ? agent->GetEntity()->GetName() : workplace;
		for (const char c : key)
		{
			h ^= (uint8_t)c;
			h *= 16777619u;
		}
		return rule.prefabs[h % rule.prefabs.size()];
	}

	return std::string();
}

bool CityRoutineSystemComponent::ResolveAnchorPosition(const std::string& entityName, math::Vector3& outPosition) const
{
	if (entityName.empty() || GetEntity() == nullptr || GetEntity()->GetScene() == nullptr)
		return false;
	auto* e = GetEntity()->GetScene()->GetEntityByName(entityName);
	if (e == nullptr || e->IsPendingDeletion())
		return false;
	outPosition = e->GetWorldTM().Translation();
	return true;
}

float CityRoutineSystemComponent::GetWalkTimeout(RoutineAgentComponent* agent)
{
	auto it = _agentMotionStates.find(agent->GetEntity()->GetName());
	const float perWalk = it != _agentMotionStates.end() ? it->second.walkTimeoutSeconds : 0.0f;
	return std::max(_walkStateTimeoutSeconds, perWalk);
}

void CityRoutineSystemComponent::UpdateLocomotionAnimation(RoutineAgentComponent* agent, float frameTime)
{
	if (!_animateLocomotion || frameTime <= 0.0f || agent == nullptr || agent->GetEntity() == nullptr)
		return;

	auto* entity = agent->GetEntity();
	auto* skel = entity->GetComponent<HexEngine::SkeletalAnimationComponent>();
	if (skel == nullptr)
		return;
	auto data = skel->GetAnimationData();
	if (data == nullptr || data->_animations.empty())
		return;

	auto cached = _locomotionClipCache.find(data.get());
	if (cached == _locomotionClipCache.end())
	{
		// Exact clip names first, then a loose match ("walk" but not a strafe).
		LocomotionClips clips;
		const auto& anims = data->_animations;
		for (int32_t i = 0; i < (int32_t)anims.size(); ++i)
		{
			if (clips.idle < 0 && _stricmp(anims[i].name.c_str(), _idleClipName.c_str()) == 0)
				clips.idle = i;
			if (clips.walk < 0 && _stricmp(anims[i].name.c_str(), _walkClipName.c_str()) == 0)
				clips.walk = i;
		}
		for (int32_t i = 0; i < (int32_t)anims.size(); ++i)
		{
			if (clips.idle < 0 && ContainsNoCase(anims[i].name, "idle"))
				clips.idle = i;
			if (clips.walk < 0 && ContainsNoCase(anims[i].name, "walk") && !ContainsNoCase(anims[i].name, "strafe"))
				clips.walk = i;
		}
		if (clips.idle < 0 || clips.walk < 0)
			LOG_WARN("CityRoutineSystem: '%s' has no idle/walk clips (looked for '%s'/'%s'); locomotion animation disabled for it.", entity->GetName().c_str(), _idleClipName.c_str(), _walkClipName.c_str());
		cached = _locomotionClipCache.emplace(data.get(), clips).first;
	}

	const LocomotionClips clips = cached->second;
	if (clips.idle < 0 || clips.walk < 0)
		return;

	// Agents are moved by navigation, but locomotion clips (the retargeted
	// Mixamo walk) carry forward travel on the root bone: ~1.3 m per loop.
	// Left in, the body walks ahead of the entity and snaps back every cycle.
	// Strip it (root motion) and discard the delta - nav owns the position.
	if (!skel->IsRootMotionEnabled())
		skel->SetRootMotionEnabled(true);
	skel->ConsumeRootMotionDelta();

	auto& state = _agentAnimStates[entity->GetName()];
	const math::Vector3 pos = entity->GetWorldTM().Translation();
	if (!state.hasLastPosition)
	{
		state.lastPosition = pos;
		state.hasLastPosition = true;
	}
	math::Vector3 delta = pos - state.lastPosition;
	delta.y = 0.0f;
	state.lastPosition = pos;
	const float speed = delta.Length() / frameTime;
	if (speed < 20.0f)	// ignore teleports (vehicle exit, restore)
		state.smoothedSpeed += (speed - state.smoothedSpeed) * std::min(1.0f, frameTime * 6.0f);

	auto* nav = entity->GetComponent<HexEngine::NavigationComponent>();
	const bool moving = state.smoothedSpeed > 0.3f && (nav == nullptr || nav->HasPath() || state.smoothedSpeed > 0.8f);
	const int32_t wanted = moving ? clips.walk : clips.idle;
	if (state.requestedClip != wanted)
	{
		if ((int32_t)skel->GetAnimationIndex() != wanted)
			skel->BlendToAnimationIndex((uint32_t)wanted);
		state.requestedClip = wanted;
	}
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
		HexEngine::NavigationTargetReachedMessage message;
		message.targetPosition = to;
		message.finalPosition = from;
		agent->GetEntity()->OnMessage(&message, this);
		return true;
	}

	auto* nav = agent->GetEntity()->GetComponent<HexEngine::NavigationComponent>();
	if (nav == nullptr)
		nav = agent->GetEntity()->AddComponent<HexEngine::NavigationComponent>();

	if (nav == nullptr)
		return false;

	nav->ClearPath();
	if (agent->GetWalkSpeed() > 0.0f)
		nav->SetMovementSpeed(agent->GetWalkSpeed());
	nav->FindPath(scene->GetNavMeshId(), from, to, 5.0f);   // scene's navmesh (loaded from sidecar)
	if (!nav->HasPath())
	{
		// Some nav providers may return an empty path when already very close.
		if ((to - from).LengthSquared() <= (kImmediateArrivalRadius * kImmediateArrivalRadius))
		{
			HexEngine::NavigationTargetReachedMessage message;
			message.targetPosition = to;
			message.finalPosition = from;
			agent->GetEntity()->OnMessage(&message, this);
			return true;
		}
		return false;
	}

	// Starting off the walkable mesh (just out of a car on the road: roads have
	// no colliders and are carved out of the navmesh, so ground snapping drops
	// the agent onto the terrain under them and it never gets anywhere). Step
	// onto the path's first point - the nearest pavement.
	{
		const auto& points = nav->GetPathPoints();
		if (!points.empty())
		{
			const float gap = (points.front() - from).Length();
			if (gap > 0.75f && gap < 20.0f)
				agent->GetEntity()->ForcePosition(points.front());
		}
	}

	auto& motion = _agentMotionStates[agent->GetEntity()->GetName()];
	motion.lastPosition = from;
	motion.hasLastPosition = true;
	motion.lowMotionAccumSeconds = 0.0f;
	motion.repathCooldownSeconds = 0.0f;
	{
		float pathLength = 0.0f;
		const auto& points = nav->GetPathPoints();
		for (size_t i = 0; i + 1 < points.size(); ++i)
			pathLength += (points[i + 1] - points[i]).Length();
		const float speed = std::max(0.2f, agent->GetWalkSpeed());
		motion.walkTimeoutSeconds = pathLength / speed * 1.5f + 15.0f;
	}

	return true;
}

HexEngine::Entity* CityRoutineSystemComponent::AcquireVehicleForTask(RoutineAgentComponent* agent, RoutineTaskSpec& task)
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

	// The agent's own car: unpark it and pull out onto the nearest lane. Emergency
	// call-outs still take the station's vehicle.
	if (task.type != RoutineTaskType::RespondEmergency && IsOwnedCarAvailable(agent, agent->GetEntity()->GetWorldTM().Translation()))
	{
		auto* car = GetOwnedCar(agent);
		auto& state = OwnedCarStateFor(car);
		if (state.stored)
		{
			SetAgentHidden(car, false);
			state.stored = false;
		}
		if (auto* traffic = car->GetComponent<TrafficVehicleComponent>(); traffic != nullptr)
			traffic->SetParked(false);
		task.sourceParkingWaypointName = FindNearestWaypointName(car->GetWorldTM().Translation());
		agent->SetCurrentVehicleEntityName(car->GetName());
		return car;
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
		prefabPath = PickRoleVehiclePrefab(agent);
	if (prefabPath.empty())
		prefabPath = _defaultVehiclePrefabPath;
	if (prefabPath.empty())
		return nullptr;

	// Commuter cars spawn all day: keep the prefab resident (see CityAssetPins).
	CityAssetPins::Prefab(prefabPath);
	auto spawned = HexEngine::g_pEnv->_prefabLoader->LoadPrefab(HexEngine::g_pEnv->_sceneManager->GetCurrentScene(), prefabPath);
	// Transient like spawner traffic: never baked into a saved scene.
	for (auto* ent : spawned)
	{
		if (ent != nullptr)
			ent->SetFlag(HexEngine::EntityFlags::DoNotSave);
	}
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

bool CityRoutineSystemComponent::ConfigureVehicleRoute(HexEngine::Entity* vehicleEntity, RoutineAgentComponent* agent, const RoutineTaskSpec& task)
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
		_hideAgentsAtDestination &&
		success &&
		completedTask.has_value() &&
		completedTask->type == RoutineTaskType::GoToWork;

	CleanupAgentVehicle(agent);
	if (success && completedTask.has_value() && completedTask->type == RoutineTaskType::GoHome)
		RehomeOwnedCarIfOwnerHome(agent);
	if (agent->GetEntity() != nullptr)
	{
		if (auto* nav = agent->GetEntity()->GetComponent<HexEngine::NavigationComponent>(); nav != nullptr)
		{
			nav->ClearPath();
		}
	}
	if (hideAfterCompletion)
	{
		SetAgentHidden(agent->GetEntity(), true);
	}
	else
	{
		SetAgentHidden(agent->GetEntity(), false);
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
		// Nothing to resume: if we just arrived at a venue, start strolling there.
		if (completedTask.has_value())
			agent->SetSettledVenueName(completedTask->targetEntityName);
		if (!hideAfterCompletion && completedTask.has_value())
			BeginOnSite(agent, *completedTask);
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

	// Driving is the fragile half of a trip (lane coverage, parking, vehicle
	// prefab). If any vehicle leg fails, finish the trip on foot instead.
	if (task.travelMode == RoutineTravelMode::DriveFirst && !ContainsNoCase(failureReason, "entry"))
	{
		// Mid-drive the hidden agent is still standing where it got in: put it
		// where the car is before the car goes.
		if (!agent->GetCurrentVehicleEntityName().empty() && agent->GetEntity()->GetScene() != nullptr)
		{
			auto* vehicleEntity = agent->GetEntity()->GetScene()->GetEntityByName(agent->GetCurrentVehicleEntityName());
			if (vehicleEntity != nullptr && !vehicleEntity->IsPendingDeletion() &&
				agent->GetExecutionState() == RoutineExecutionState::DriveToDestinationParking)
			{
				agent->GetEntity()->ForcePosition(vehicleEntity->GetWorldTM().Translation());
			}
		}
		CleanupAgentVehicle(agent);
		SetAgentHidden(agent->GetEntity(), false);
		task.travelMode = RoutineTravelMode::WalkOnly;
		agent->SetActiveTask(task);
		agent->SetExecutionState(RoutineExecutionState::Idle);
		agent->SetStateTimer(0.0f);
		agent->RetryTimerRef() = 0.0f;
		LOG_WARN("CityRoutineSystem: Agent '%s' walking instead after: %s", agent->GetEntity()->GetName().c_str(), failureReason.c_str());
		return;
	}

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
	agent->SeedCooldownRef() = _failedTaskCooldownSeconds;
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

HexEngine::Entity* CityRoutineSystemComponent::GetOwnedCar(const RoutineAgentComponent* agent) const
{
	if (agent == nullptr || agent->GetOwnedVehicleEntityName().empty() || agent->GetEntity() == nullptr)
		return nullptr;
	auto* scene = agent->GetEntity()->GetScene();
	auto* car = scene != nullptr ? scene->GetEntityByName(agent->GetOwnedVehicleEntityName()) : nullptr;
	if (car == nullptr || car->IsPendingDeletion() || car->GetComponent<TrafficVehicleComponent>() == nullptr)
		return nullptr;
	return car;
}

CityRoutineSystemComponent::OwnedCarState& CityRoutineSystemComponent::OwnedCarStateFor(HexEngine::Entity* car)
{
	auto it = _ownedCars.find(car->GetName());
	if (it == _ownedCars.end())
	{
		// First sight this play session: wherever it stands now is home.
		OwnedCarState state;
		state.homePosition = car->GetWorldTM().Translation();
		state.homeRotation = car->GetRotation();
		it = _ownedCars.emplace(car->GetName(), state).first;
	}
	return it->second;
}

bool CityRoutineSystemComponent::IsOwnedCarAvailable(const RoutineAgentComponent* agent, const math::Vector3& agentPos) const
{
	auto* car = GetOwnedCar(agent);
	if (car == nullptr)
		return false;
	auto* traffic = car->GetComponent<TrafficVehicleComponent>();
	// Already being driven (by this agent mid-trip, or someone else).
	if (traffic == nullptr || (!traffic->IsParked() && agent->GetCurrentVehicleEntityName() != car->GetName()))
		return false;
	// Close enough to walk to: on the driveway at home, or in the car park at the
	// place the owner drove to (or where a stuck drive left it).
	if ((car->GetWorldTM().Translation() - agentPos).LengthSquared() <= 300.0f * 300.0f)
		return true;
	// Put away elsewhere while the owner is at home: re-homed before use.
	auto it = _ownedCars.find(car->GetName());
	return it != _ownedCars.end() && it->second.stored && (it->second.homePosition - agentPos).LengthSquared() <= 80.0f * 80.0f;
}

void CityRoutineSystemComponent::ParkOwnedCar(HexEngine::Entity* car, bool atHome)
{
	auto& state = OwnedCarStateFor(car);
	if (auto* traffic = car->GetComponent<TrafficVehicleComponent>(); traffic != nullptr)
	{
		traffic->SetParked(true);
		traffic->SetUseWaypointRoute(false);
	}
	if (atHome)
	{
		car->ForcePosition(state.homePosition);
		car->ForceRotation(state.homeRotation);
		if (state.stored)
			SetAgentHidden(car, false);
		state.stored = false;
	}
	else
	{
		SetAgentHidden(car, true);
		state.stored = true;
	}
}

void CityRoutineSystemComponent::RehomeOwnedCarIfOwnerHome(RoutineAgentComponent* agent)
{
	auto* car = GetOwnedCar(agent);
	if (car == nullptr || agent->GetCurrentVehicleEntityName() == car->GetName())
		return;
	auto it = _ownedCars.find(car->GetName());
	if (it == _ownedCars.end() || !it->second.stored)
		return;
	if ((it->second.homePosition - agent->GetEntity()->GetWorldTM().Translation()).LengthSquared() > 80.0f * 80.0f)
		return;
	ParkOwnedCar(car, true);
}

void CityRoutineSystemComponent::ResetOwnedCars()
{
	auto* scene = GetEntity() != nullptr ? GetEntity()->GetScene() : nullptr;
	if (scene != nullptr)
	{
		for (auto& [name, state] : _ownedCars)
		{
			auto* car = scene->GetEntityByName(name);
			if (car == nullptr || car->IsPendingDeletion())
				continue;
			car->ForcePosition(state.homePosition);
			car->ForceRotation(state.homeRotation);
			if (state.stored)
				SetAgentHidden(car, false);
			if (auto* traffic = car->GetComponent<TrafficVehicleComponent>(); traffic != nullptr)
				traffic->SetParked(true);
		}
	}
	_ownedCars.clear();
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
		if (vehicle->GetName() == agent->GetOwnedVehicleEntityName())
		{
			// Home trip ends on the driveway; anywhere else the car goes into the
			// (implicit) car park until the owner comes back for it.
			const auto& active = agent->GetActiveTask();
			const bool goingHome = active.has_value() && active->type == RoutineTaskType::GoHome;
			const auto& state = OwnedCarStateFor(vehicle);
			const bool nearHome = (vehicle->GetWorldTM().Translation() - state.homePosition).LengthSquared() < 60.0f * 60.0f;
			ParkOwnedCar(vehicle, goingHome && nearHome);
		}
		else
		{
			vehicle->DeleteMe();
		}
	}

	agent->SetCurrentVehicleEntityName(std::string());
	if (auto* nav = agent->GetEntity()->GetComponent<HexEngine::NavigationComponent>(); nav != nullptr)
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

	// Held at a red pedestrian crossing: standing still on purpose, not stuck.
	if (auto* nav = agent->GetEntity()->GetComponent<HexEngine::NavigationComponent>(); nav != nullptr && nav->IsHeldByGate())
	{
		motion.lowMotionAccumSeconds = 0.0f;
		return false;
	}

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

namespace
{
	float OnSiteRand01(uint32_t& s)
	{
		s ^= s << 13; s ^= s >> 17; s ^= s << 5;
		return (s & 0xFFFFFF) / (float)0x1000000;
	}
}

void CityRoutineSystemComponent::BeginOnSite(RoutineAgentComponent* agent, const RoutineTaskSpec& completedTask)
{
	if (!_onSiteWander || agent == nullptr || agent->GetEntity() == nullptr)
		return;
	auto* scene = agent->GetEntity()->GetScene();
	if (scene == nullptr)
		return;

	// Only venues (shifts and visits) - not home: residents at home stay put.
	auto* venue = completedTask.targetEntityName.empty() ? nullptr : scene->GetEntityByName(completedTask.targetEntityName);
	if (venue == nullptr || venue->GetComponent<PlaceOfWorkComponent>() == nullptr)
		return;

	OnSiteState state;
	if (!ResolveAnchorPosition(completedTask.targetEntryWaypointName, state.anchor))
		state.anchor = agent->GetEntity()->GetWorldTM().Translation();
	// Stagger the first stroll so a shift's workers don't all set off together.
	state.pauseSeconds = 2.0f + OnSiteRand01(_onSiteRng) * 8.0f;
	_onSiteStates[agent->GetEntity()->GetName()] = state;
}

bool CityRoutineSystemComponent::PickOnSiteSpot(RoutineAgentComponent* agent, const math::Vector3& anchor, math::Vector3& outSpot)
{
	auto* entity = agent->GetEntity();
	auto* scene = entity->GetScene();
	auto* nav = entity->GetComponent<HexEngine::NavigationComponent>();
	if (scene == nullptr || nav == nullptr)
		return false;

	const math::Vector3 from = entity->GetWorldTM().Translation();
	const float radius = std::clamp(_onSiteWanderRadius, 1.5f, kArrivalRadius - 1.0f);
	for (int32_t attempt = 0; attempt < 6; ++attempt)
	{
		const float angle = OnSiteRand01(_onSiteRng) * 6.2831853f;
		const float r = 1.25f + OnSiteRand01(_onSiteRng) * (radius - 1.25f);
		const math::Vector3 candidate(anchor.x + std::cos(angle) * r, anchor.y, anchor.z + std::sin(angle) * r);
		if ((candidate - from).LengthSquared() < 1.0f)
			continue;

		nav->ClearPath();
		nav->FindPath(scene->GetNavMeshId(), from, candidate, 5.0f);
		if (!nav->HasPath())
			continue;

		// Accept only short in-room strolls: the path must end at the candidate on
		// the same floor and not detour (e.g. out of the door and round the block).
		const auto& pts = nav->GetPathPoints();
		const math::Vector3 end = pts.back();
		math::Vector3 endFlat = end - candidate; endFlat.y = 0.0f;
		float length = 0.0f;
		math::Vector3 prev = from;
		for (const auto& p : pts) { length += (p - prev).Length(); prev = p; }
		math::Vector3 direct = end - from; direct.y = 0.0f;
		if (endFlat.Length() > 0.75f || std::fabs(end.y - anchor.y) > 0.75f ||
			length > direct.Length() * 1.6f + 1.5f)
		{
			nav->ClearPath();
			continue;
		}
		outSpot = end;
		return true;
	}
	nav->ClearPath();
	return false;
}

void CityRoutineSystemComponent::UpdateOnSite(RoutineAgentComponent* agent, float frameTime)
{
	auto it = _onSiteStates.find(agent->GetEntity()->GetName());
	if (it == _onSiteStates.end())
		return;
	if (!_onSiteWander || agent->GetEntity()->HasFlag(HexEngine::EntityFlags::DoNotRender))
	{
		_onSiteStates.erase(it);
		return;
	}
	OnSiteState& state = it->second;
	auto* nav = agent->GetEntity()->GetComponent<HexEngine::NavigationComponent>();
	if (nav == nullptr)
		return;

	if (state.walking)
	{
		state.walkSeconds += frameTime;
		const bool arrived = agent->ConsumeNavigationReached();
		if (arrived || !nav->HasPath() || state.walkSeconds > 25.0f)
		{
			nav->ClearPath();
			state.walking = false;
			state.pauseSeconds = 8.0f + OnSiteRand01(_onSiteRng) * 17.0f;   // linger 8-25 s
		}
		return;
	}

	state.pauseSeconds -= frameTime;
	if (state.pauseSeconds > 0.0f)
		return;

	math::Vector3 spot;
	if (PickOnSiteSpot(agent, state.anchor, spot))
	{
		// Stroll, not commute: a little slower than the agent's walk.
		if (agent->GetWalkSpeed() > 0.0f)
			nav->SetMovementSpeed(agent->GetWalkSpeed() * 0.7f);
		agent->ConsumeNavigationReached();   // clear any stale flag before this stroll
		state.walking = true;
		state.walkSeconds = 0.0f;
	}
	else
	{
		state.pauseSeconds = 4.0f;   // cramped spot / no navmesh nearby: try again shortly
	}
}

void CityRoutineSystemComponent::OnDebugRender()
{
	if (!HexEngine::g_pEnv->IsEditorMode())
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

			auto* nav = agent->GetEntity()->GetComponent<HexEngine::NavigationComponent>();
			if (nav == nullptr || !nav->HasPath())
				continue;

			const auto& path = nav->GetPathPoints();
			if (path.size() < 2)
				continue;

			for (size_t i = 0; i + 1 < path.size(); ++i)
			{
				const math::Color color = (i == nav->GetPathIndex()) ? currentColor : pathColor;
				HexEngine::g_pEnv->_debugRenderer->DrawLine(path[i], path[i + 1], color);
			}
		}
	}

	if (!_drawSelectedAgentHud)
		return;

	auto* renderer = HexEngine::g_pEnv->GetUIManager().GetRenderer();
	if (renderer == nullptr || HexEngine::g_pEnv->_inputSystem == nullptr)
		return;

	for (auto* agent : agents)
	{
		if (agent == nullptr || agent->GetEntity() == nullptr || agent->GetEntity()->IsPendingDeletion())
			continue;
		if (!agent->GetEntity()->HasFlag(HexEngine::EntityFlags::SelectedInEditor))
			continue;

		int32_t x = 0;
		int32_t y = 0;
		if (!HexEngine::g_pEnv->_inputSystem->GetWorldToScreenPosition(camera, agent->GetEntity()->GetWorldTM().Translation(), x, y))
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
			(uint8_t)HexEngine::Style::FontSize::Regular,
			x,
			y - 32,
			math::Color(0xFFFFFFFF),
			HexEngine::FontAlign::CentreLR,
			line1);

		renderer->PrintText(
			renderer->_style.font.get(),
			(uint8_t)HexEngine::Style::FontSize::Regular,
			x,
			y - 14,
			math::Color(0xFFE2E2E2),
			HexEngine::FontAlign::CentreLR,
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
