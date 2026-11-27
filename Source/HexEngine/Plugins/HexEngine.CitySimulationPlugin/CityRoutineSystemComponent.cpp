#include "CityRoutineSystemComponent.hpp"
#include "DistrictGateComponent.hpp"
#include "CityEmergencyDispatcherSystemComponent.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <HexEngine.Core/Entity/Component/DayNightCycleComponent.hpp>
#include <HexEngine.Core/Entity/Component/NavigationComponent.hpp>
#include <HexEngine.Core/Entity/Component/SkeletalAnimationComponent.hpp>
#include <HexEngine.Core/Entity/Component/StaticMeshComponent.hpp>
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
#include "TransitLineComponent.hpp"
#include "TransitStopComponent.hpp"
#include "TransitVehicleComponent.hpp"
#include "SeatComponent.hpp"
#include "ParkingLotComponent.hpp"
#include <HexEngine.Core/Entity/Component/RigidBody.hpp>
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

	bool IsBoneAttachment(HexEngine::Entity* e)
	{
		auto* smc = e->GetComponent<HexEngine::StaticMeshComponent>();
		return smc != nullptr && !smc->GetBoundBoneName().empty();
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
				else if (g_childrenHiddenWithAgent.erase(child->GetId()) != 0 || IsBoneAttachment(child))
				{
					// Bone-bound hair/hats/glasses only exist to dress the body, so
					// they follow it even when they weren't hidden by us this run
					// (hidden flags baked into the scene left every resident bald).
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
		case RoutineExecutionState::WalkToTransitStop: return "WalkToStop";
		case RoutineExecutionState::WaitForTransit: return "WaitForTransit";
		case RoutineExecutionState::RideTransit: return "RideTransit";
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
	SetTickRate(5);
}

CityRoutineSystemComponent::CityRoutineSystemComponent(HexEngine::Entity* entity, CityRoutineSystemComponent* copy) :
	UpdateComponent(entity, copy)
{
	RegisterEntityListener();
	SetTickRate(5);
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
	file->Serialize(data, "_transitEnabled", _transitEnabled);
	file->Serialize(data, "_transitMaxWalkToStop", _transitMaxWalkToStop);
	file->Serialize(data, "_transitShareCarOwners", _transitShareCarOwners);
	file->Serialize(data, "_transitMaxWaitSeconds", _transitMaxWaitSeconds);
	file->Serialize(data, "_hideAgentsAtDestination", _hideAgentsAtDestination);
	file->Serialize(data, "_failedTaskCooldownSeconds", _failedTaskCooldownSeconds);
	file->Serialize(data, "_vehicleStuckSeconds", _vehicleStuckSeconds);
	file->Serialize(data, "_animateLocomotion", _animateLocomotion);
	file->Serialize(data, "_idleClipName", _idleClipName);
	file->Serialize(data, "_walkClipName", _walkClipName);
	file->Serialize(data, "_onSiteWander", _onSiteWander);
	file->Serialize(data, "_onSiteWanderRadius", _onSiteWanderRadius);
	file->Serialize(data, "_sitEnabled", _sitEnabled);
	file->Serialize(data, "_sitChanceOnSite", _sitChanceOnSite);
	file->Serialize(data, "_sitSearchRadius", _sitSearchRadius);
	file->Serialize(data, "_sitMinSeconds", _sitMinSeconds);
	file->Serialize(data, "_sitMaxSeconds", _sitMaxSeconds);
	file->Serialize(data, "_sitAtTransitStops", _sitAtTransitStops);
	file->Serialize(data, "_transitSeatRadius", _transitSeatRadius);
	file->Serialize(data, "_sitHeightOffset", _sitHeightOffset);
	file->Serialize(data, "_sitDebugLog", _sitDebugLog);
	file->Serialize(data, "_parkingEnabled", _parkingEnabled);
	file->Serialize(data, "_parkingDebugLog", _parkingDebugLog);
	file->Serialize(data, "_sitDownClipName", _sitDownClipName);
	file->Serialize(data, "_sitIdleClipName", _sitIdleClipName);
	file->Serialize(data, "_standUpClipName", _standUpClipName);
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
	file->Deserialize(data, "_transitEnabled", _transitEnabled);
	file->Deserialize(data, "_transitMaxWalkToStop", _transitMaxWalkToStop);
	file->Deserialize(data, "_transitShareCarOwners", _transitShareCarOwners);
	file->Deserialize(data, "_transitMaxWaitSeconds", _transitMaxWaitSeconds);
	_vehicleRulesParsed = false;
	file->Deserialize(data, "_hideAgentsAtDestination", _hideAgentsAtDestination);
	file->Deserialize(data, "_failedTaskCooldownSeconds", _failedTaskCooldownSeconds);
	file->Deserialize(data, "_vehicleStuckSeconds", _vehicleStuckSeconds);
	file->Deserialize(data, "_animateLocomotion", _animateLocomotion);
	file->Deserialize(data, "_idleClipName", _idleClipName);
	file->Deserialize(data, "_walkClipName", _walkClipName);
	file->Deserialize(data, "_onSiteWander", _onSiteWander);
	file->Deserialize(data, "_onSiteWanderRadius", _onSiteWanderRadius);
	file->Deserialize(data, "_sitEnabled", _sitEnabled);
	file->Deserialize(data, "_sitChanceOnSite", _sitChanceOnSite);
	file->Deserialize(data, "_sitSearchRadius", _sitSearchRadius);
	file->Deserialize(data, "_sitMinSeconds", _sitMinSeconds);
	file->Deserialize(data, "_sitMaxSeconds", _sitMaxSeconds);
	file->Deserialize(data, "_sitAtTransitStops", _sitAtTransitStops);
	file->Deserialize(data, "_transitSeatRadius", _transitSeatRadius);
	file->Deserialize(data, "_sitHeightOffset", _sitHeightOffset);
	file->Deserialize(data, "_sitDebugLog", _sitDebugLog);
	file->Deserialize(data, "_parkingEnabled", _parkingEnabled);
	file->Deserialize(data, "_parkingDebugLog", _parkingDebugLog);
	file->Deserialize(data, "_sitDownClipName", _sitDownClipName);
	file->Deserialize(data, "_sitIdleClipName", _sitIdleClipName);
	file->Deserialize(data, "_standUpClipName", _standUpClipName);
	_agentMotionStates.clear();
	_onSiteStates.clear();
	_sitStates.clear();
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

	// Sitting (SeatComponent benches and chairs).
	{
		const HexEngine::Point row(widget->GetSize().x - 20, 18);
		new HexEngine::Checkbox(widget, widget->GetNextPos(), row, L"Sit On Seats", &_sitEnabled);
		new HexEngine::DragFloat(widget, widget->GetNextPos(), row, L"Sit Chance (on site)", &_sitChanceOnSite, 0.0f, 1.0f, 0.01f, 2);
		new HexEngine::DragFloat(widget, widget->GetNextPos(), row, L"Seat Search Radius", &_sitSearchRadius, 1.0f, 60.0f, 0.5f, 1);
		new HexEngine::DragFloat(widget, widget->GetNextPos(), row, L"Sit Min Seconds", &_sitMinSeconds, 1.0f, 600.0f, 1.0f, 0);
		new HexEngine::DragFloat(widget, widget->GetNextPos(), row, L"Sit Max Seconds", &_sitMaxSeconds, 1.0f, 600.0f, 1.0f, 0);
		new HexEngine::Checkbox(widget, widget->GetNextPos(), row, L"Sit At Transit Stops", &_sitAtTransitStops);
		new HexEngine::DragFloat(widget, widget->GetNextPos(), row, L"Stop Seat Radius", &_transitSeatRadius, 0.0f, 30.0f, 0.5f, 1);
		new HexEngine::DragFloat(widget, widget->GetNextPos(), row, L"Sit Height Offset (m)", &_sitHeightOffset, -0.5f, 0.5f, 0.005f, 3);
		new HexEngine::Checkbox(widget, widget->GetNextPos(), row, L"Log Sitting", &_sitDebugLog);
		new HexEngine::Checkbox(widget, widget->GetNextPos(), row, L"Park in Car Parks / Driveways", &_parkingEnabled);
		new HexEngine::Checkbox(widget, widget->GetNextPos(), row, L"Log Parking", &_parkingDebugLog);
		auto clipEdit = [&](const wchar_t* label, std::string& field)
		{
			auto* edit = new HexEngine::LineEdit(widget, widget->GetNextPos(), row, label);
			edit->SetValue(std::wstring(field.begin(), field.end()));
			edit->SetDoesCallbackWaitForReturn(false);
			edit->SetOnInputFn([this, &field](HexEngine::LineEdit*, const std::wstring& value) { field = ws2s(value); _locomotionClipCache.clear(); });
		};
		clipEdit(L"Sit Down Clip", _sitDownClipName);
		clipEdit(L"Sitting Clip", _sitIdleClipName);
		clipEdit(L"Stand Up Clip", _standUpClipName);
	}

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
	if (auto sit = _sitStates.find(entity->GetName()); sit != _sitStates.end())
	{
		if (auto* scene = entity->GetScene(); scene != nullptr)
			if (auto* seatEntity = scene->GetEntityByName(sit->second.seatEntity))
				if (auto* seat = seatEntity->GetComponent<SeatComponent>())
					seat->Release(sit->second.slot, entity->GetName());
		_sitStates.erase(sit);
	}
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

	CleanupAgentVehicle(agent, true);
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
		// Runs on the first editor frame, i.e. AFTER the play-stop restore has put
		// every entity back (transforms, visibility). So only reset bookkeeping
		// here - parking/hiding/moving cars at this point changed the EDITOR scene
		// (owned cars parked hidden, then baked in by the next save).
		if (_wasGameRunning)
		{
			CityAssetPins::Clear();
			_vehicleRulesParsed = false;	// re-expand wildcards next play (new variants)
			_ownedCars.clear();
			g_childrenHiddenWithAgent.clear();
		}
		if (_wasGameRunning && GetEntity() != nullptr && GetEntity()->GetScene() != nullptr)
		{
			auto* scene = GetEntity()->GetScene();
			std::vector<RoutineAgentComponent*> agents;
			scene->GetComponents<RoutineAgentComponent>(agents);
			for (auto* agent : agents)
			{
				if (agent == nullptr || agent->GetEntity() == nullptr)
					continue;
				// The trip's car / transit ride is play-time only (its entities are
				// gone); drop the references without touching any entity.
				if (const auto& active = agent->GetActiveTask(); active.has_value() && !active->transitBoardStop.empty())
				{
					if (auto* stopEntity = scene->GetEntityByName(active->transitBoardStop))
						if (auto* stop = stopEntity->GetComponent<TransitStopComponent>())
							stop->RemoveWaiting(agent->GetEntity()->GetName());
				}
				agent->SetTransitVehicleName(std::string());
				agent->SetTransitPhase(RoutineAgentComponent::TransitPhase::None);
				agent->SetCurrentVehicleEntityName(std::string());
				agent->SetConversationPaused(false);	// stopped mid-dialog: not serialised, so release here
				if (auto* nav = agent->GetEntity()->GetComponent<HexEngine::NavigationComponent>(); nav != nullptr)
					nav->ClearPath();
			}
			// Seated agents had their capsule switched off (runtime physics state,
			// which the restore doesn't touch): switch it back on, drop the seats.
			ReleaseAllSits(true);
			ResetParking();
			_agentMotionStates.clear();
			_agentAnimStates.clear();
			_commuteTrips.clear();
			_learnedCommuteHours.clear();
			_gameHoursPerSecond = 0.0f;
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
	// Measured clock rate (the day/night cycle runs day and night at different
	// speeds and a game can sleep-skip): game hours per second of frame time,
	// smoothed. Feeds the commute-lead estimate. Jumps (wrap, sleep) are ignored.
	if (_lastSimulationHour >= 0.0f && frameTime > 1e-4f)
	{
		const float dh = simulationHour - _lastSimulationHour;
		if (dh >= 0.0f && dh < 0.05f)
		{
			const float rate = dh / frameTime;
			_gameHoursPerSecond = _gameHoursPerSecond <= 0.0f ? rate : _gameHoursPerSecond + (rate - _gameHoursPerSecond) * std::min(1.0f, frameTime * 0.5f);
		}
	}
	_lastSimulationHour = simulationHour;

	SweepDriverlessVehicles(frameTime);
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

		// In conversation: the routine clock, walk timeouts and stuck checks all
		// wait (a paused walker would otherwise be declared stuck and re-routed),
		// but locomotion still updates so the body settles into idle.
		if (!agent->IsConversationPaused())
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

	// Sitting: stand up before anything else happens (the next trip, the bus
	// pulling in). While seated the routine waits here.
	if (auto sit = _sitStates.find(agent->GetEntity()->GetName()); sit != _sitStates.end())
	{
		const bool transitSit = sit->second.transit;
		if (transitSit)
		{
			const bool stillWaiting = agent->GetExecutionState() == RoutineExecutionState::WaitForTransit &&
				agent->GetTransitPhase() == RoutineAgentComponent::TransitPhase::Waiting;
			if (!stillWaiting || agent->StateTimerRef() > _transitMaxWaitSeconds)
				RequestStandUp(agent);
		}
		else if (agent->HasQueuedTasks() || agent->GetActiveTask().has_value())
		{
			RequestStandUp(agent);
		}
		if (UpdateSit(agent, frameTime))
		{
			if (transitSit)
			{
				// The wait goes on while seated (give-up timer, boarding window).
				agent->StateTimerRef() += frameTime;
				agent->TransitPhaseTimeRef() += frameTime;
			}
			return;
		}
	}

	if (agent->GetActiveTask().has_value())
	{
		RoutineTaskSpec headTask;
		if (agent->PeekTask(headTask))
		{
			const auto& activeTask = *agent->GetActiveTask();
			if (headTask.preemptive && headTask.priority > activeTask.priority)
			{
				CleanupAgentVehicle(agent, true);
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
		if (task.travelMode == RoutineTravelMode::Transit)
		{
			agent->SetTransitPhase(RoutineAgentComponent::TransitPhase::ToStop);
			agent->SetExecutionState(RoutineExecutionState::WalkToTransitStop);
		}
		else
		{
			agent->SetExecutionState(task.travelMode == RoutineTravelMode::WalkOnly ? RoutineExecutionState::WalkToTaskEntry : RoutineExecutionState::WalkToVehicleAnchor);
		}
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
		else if (auto* temp = GetTempCar(agent, agent->GetEntity()->GetWorldTM().Translation()); temp != nullptr)
			sourceParkingWaypointName = temp->GetName();

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
			else if (vehicle->IsQueuedAtSignal())
			{
				// Waiting at a red (or in the queue behind one): expected, not a
				// jam. Counting it abandoned cars at every long red - the driver
				// got out and walked the rest, arriving late.
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
			const std::string agentName = agent->GetEntity()->GetName();
			math::Vector3 exitPosition = vehicleEntity->GetPosition();	// root entity: local == world; the world TM lags a ForcePosition
			auto tripIt = _tripBays.find(agentName);
			ParkingLotComponent* lot = tripIt != _tripBays.end() ? ResolveLot(tripIt->second.lot) : nullptr;
			if (lot != nullptr)
			{
				// Pulled into its bay: it stays there, parked and visible, until the
				// driver (or, for a spawned car, the same agent) needs it again.
				const BayRef bay = tripIt->second;
				_tripBays.erase(tripIt);
				PlaceCarInBay(vehicleEntity, bay);
				math::Vector3 bayPosition, nose;
				if (lot->GetBay(bay.bay, bayPosition, nose))
					exitPosition = bayPosition + math::Vector3(-nose.z, 0.0f, nose.x) * 1.6f;	// beside the driver's door
				if (vehicleEntity->GetName() == agent->GetOwnedVehicleEntityName())
				{
					auto& carState = OwnedCarStateFor(vehicleEntity);
					if (carState.stored)
						SetAgentHidden(vehicleEntity, false);
					carState.stored = false;
				}
				else
				{
					_tempCars[agentName] = vehicleEntity->GetName();
				}
				agent->SetCurrentVehicleEntityName(std::string());
				if (_parkingDebugLog)
					LOG_INFO("Parking: '%s' parked %s in %s bay %d", agentName.c_str(), vehicleEntity->GetName().c_str(), bay.lot.c_str(), bay.bay);
			}
			else
			{
				// No car park: an own car is put away (driveway at home, else stored
				// out of the lane); a spawned one is removed now - it used to stand in
				// the lane, often in a junction, until its driver had walked inside.
				CleanupAgentVehicle(agent);
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

	case RoutineExecutionState::WalkToTransitStop:
	{
		auto* scene = agent->GetEntity()->GetScene();
		auto* stopEntity = scene->GetEntityByName(task.transitBoardStop);
		auto* stop = stopEntity != nullptr ? stopEntity->GetComponent<TransitStopComponent>() : nullptr;
		if (stop == nullptr)
		{
			AbandonTransit(agent, "No boarding stop");
			break;
		}
		if (agent->StateTimerRef() <= frameTime)
		{
			const uint32_t seed = (uint32_t)(Hash01(agent->GetEntity()->GetName(), 0x57) * 1000.0f);
			if (!BeginWalkToPosition(agent, stop->GetWaitSpot(seed)))
			{
				AbandonTransit(agent, "No path to the stop");
				break;
			}
		}
		// Arrived: navigation says so, or close enough to the waiting area (a
		// wait spot can sit just off the navmesh, so the walk ends short of it).
		math::Vector3 toStop = stop->GetWaitCentre() - agent->GetEntity()->GetWorldTM().Translation();
		toStop.y = 0.0f;
		const bool nearStop = toStop.LengthSquared() < (stop->GetWaitRadius() + 2.5f) * (stop->GetWaitRadius() + 2.5f);
		if (agent->ConsumeNavigationReached() || nearStop)
		{
			TransitStopComponent::Waiting waiting;
			waiting.agentName = agent->GetEntity()->GetName();
			waiting.lineEntityName = task.transitLine;
			waiting.alightStopName = task.transitAlightStop;
			stop->AddWaiting(waiting);
			agent->SetTransitPhase(RoutineAgentComponent::TransitPhase::Waiting);
			agent->SetExecutionState(RoutineExecutionState::WaitForTransit);
			agent->SetStateTimer(0.0f);
			// Face the road (towards the stop's lane node when it has one).
			const auto nodes = stop->GetLaneWaypointCandidates();
			if (auto* node = nodes.empty() ? nullptr : scene->GetEntityByName(nodes.front()); node != nullptr)
			{
				const math::Vector3 d = node->GetWorldTM().Translation() - agent->GetEntity()->GetWorldTM().Translation();
				if (d.x * d.x + d.z * d.z > 0.01f)
					agent->GetEntity()->ForceRotation(math::Quaternion::CreateFromYawPitchRoll(std::atan2(d.x, d.z), 0.0f, 0.0f));
			}
			break;
		}
		if (agent->StateTimerRef() > GetWalkTimeout(agent))
		{
			AbandonTransit(agent, "Timed out walking to the stop");
			break;
		}
		UpdateWalkStuckRecovery(agent, frameTime, task.transitBoardStop, "Stuck walking to the stop");
		break;
	}

	case RoutineExecutionState::WaitForTransit:
	{
		agent->TransitPhaseTimeRef() += frameTime;
		const auto phase = agent->GetTransitPhase();
		auto* scene = agent->GetEntity()->GetScene();
		auto* vehicleEntity = agent->GetTransitVehicleName().empty() ? nullptr : scene->GetEntityByName(agent->GetTransitVehicleName());
		auto* vehicle = vehicleEntity != nullptr ? vehicleEntity->GetComponent<TransitVehicleComponent>() : nullptr;

		if (phase == RoutineAgentComponent::TransitPhase::Riding)
		{
			agent->SetExecutionState(RoutineExecutionState::RideTransit);
			agent->SetStateTimer(0.0f);
			break;
		}
		if (phase == RoutineAgentComponent::TransitPhase::Boarding)
		{
			if (vehicle == nullptr || !vehicle->IsDwelling())
			{
				// Missed it (the vehicle left / vanished): back in the queue.
				agent->SetTransitVehicleName(std::string());
				if (auto* stopEntity = scene->GetEntityByName(task.transitBoardStop))
				{
					if (auto* stop = stopEntity->GetComponent<TransitStopComponent>())
					{
						TransitStopComponent::Waiting waiting;
						waiting.agentName = agent->GetEntity()->GetName();
						waiting.lineEntityName = task.transitLine;
						waiting.alightStopName = task.transitAlightStop;
						stop->AddWaiting(waiting);
					}
				}
				agent->SetTransitPhase(RoutineAgentComponent::TransitPhase::Waiting);
				break;
			}
			if (!agent->IsTransitWalkIssued())
			{
				agent->SetTransitWalkIssued(true);
				BeginWalkToPosition(agent, agent->GetTransitDoor());
			}
			const math::Vector3 toDoor = agent->GetTransitDoor() - agent->GetEntity()->GetWorldTM().Translation();
			if (agent->ConsumeNavigationReached() || (toDoor.x * toDoor.x + toDoor.z * toDoor.z) < 0.8f * 0.8f)
				vehicle->BoardAgent(agent, task.transitAlightStop);
			break;
		}
		if (agent->StateTimerRef() > _transitMaxWaitSeconds)
		{
			AbandonTransit(agent, "Gave up waiting");
			break;
		}
		// Settled into the queue: take a seat in the shelter if one is free.
		if (_sitAtTransitStops && phase == RoutineAgentComponent::TransitPhase::Waiting)
		{
			const float waited = agent->TransitPhaseTimeRef();
			if (waited >= 2.0f && waited - frameTime < 2.0f)
			{
				if (auto* stopEntity = scene->GetEntityByName(task.transitBoardStop))
					if (auto* stop = stopEntity->GetComponent<TransitStopComponent>())
						TryBeginSit(agent, stop->GetWaitCentre(), stop->GetWaitRadius() + _transitSeatRadius, 0.0f, 0.0f, true);
			}
		}
		break;
	}

	case RoutineExecutionState::RideTransit:
	{
		const auto phase = agent->GetTransitPhase();
		auto* scene = agent->GetEntity()->GetScene();
		auto* vehicleEntity = agent->GetTransitVehicleName().empty() ? nullptr : scene->GetEntityByName(agent->GetTransitVehicleName());
		if (phase == RoutineAgentComponent::TransitPhase::Alighted || phase == RoutineAgentComponent::TransitPhase::None ||
			vehicleEntity == nullptr || vehicleEntity->IsPendingDeletion())
		{
			agent->SetTransitPhase(RoutineAgentComponent::TransitPhase::None);
			agent->SetTransitVehicleName(std::string());
			agent->ConsumeNavigationReached();
			agent->SetExecutionState(RoutineExecutionState::WalkToTaskEntry);
			agent->SetStateTimer(0.0f);
			break;
		}
		if (agent->StateTimerRef() > 1200.0f)
		{
			if (auto* vehicle = vehicleEntity->GetComponent<TransitVehicleComponent>())
				vehicle->AlightAgent(agent);
			AbandonTransit(agent, "Ride took too long");
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
		if (!DistrictGateComponent::CanTravel(scene, homePos, pos))
			continue;	// a workplace behind a locked district gate is unreachable for now
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

	// Still at this venue: strolling or taking a seat can carry the agent further than
	// kArrivalRadius from its entry (an office chair, a bench across the plaza). Without
	// this every re-check queued a "commute" back to the same venue, which stood the
	// agent up again (or cancelled the walk to the seat) over and over.
	if (!agent->GetSettledVenueName().empty() && agent->GetSettledVenueName() == task.targetEntityName &&
		(IsSitting(agent) || _onSiteStates.count(agent->GetEntity()->GetName()) != 0))
		return;

	const math::Vector3 agentPos = agent->GetEntity()->GetWorldTM().Translation();
	math::Vector3 destination;
	if (!ResolveAnchorPosition(task.targetEntryWaypointName, destination) &&
		!ResolveAnchorPosition(task.targetEntityName, destination))
		return;

	if ((destination - agentPos).LengthSquared() <= kArrivalRadius * kArrivalRadius)
	{
		// Already there (play started mid-activity, or the next block is the same
		// place): that counts as having arrived, so occupancy (building lights) and
		// at-post checks (jobs) see the agent at this venue.
		if (agent->GetSettledVenueName().empty())
			agent->SetSettledVenueName(task.targetEntityName);
		return;
	}

	task.travelMode = ChooseTravelMode(agent, agentPos, destination);
	// Public transport: people without a car to hand (and some who have one)
	// take the bus / train when it beats walking.
	if (_transitEnabled && agent->GetTravelPreference() == RoutineTravelPreference::Auto &&
		(destination - agentPos).Length() > _walkMaxDistance)
	{
		const bool hasCar = IsOwnedCarAvailable(agent, agentPos);
		const bool ridesAnyway = Hash01(agent->GetEntity()->GetName(), 0x7A11) < _transitShareCarOwners;
		if ((!hasCar || ridesAnyway) && PlanTransitTrip(agent, agentPos, destination, task))
		{
			task.travelMode = RoutineTravelMode::Transit;
			LOG_INFO("CityRoutineSystem: '%s' takes %s from %s to %s (for %s)", agent->GetEntity()->GetName().c_str(),
				task.transitLine.c_str(), task.transitBoardStop.c_str(), task.transitAlightStop.c_str(), task.targetEntityName.c_str());
		}
	}
	if (task.travelMode == RoutineTravelMode::DriveFirst)
	{
		// Buildings rarely name lane waypoints (their "parking" is usually a car
		// park / driveway now): use the nearest quiet lane nodes. The actual ends are
		// picked when the car sets off (ConfigureVehicleRoute).
		if (!IsLaneNode(task.targetParkingWaypointName))
			task.targetParkingWaypointName = FindNearestQuietWaypointName(destination);
		if (!IsLaneNode(task.sourceParkingWaypointName))
			task.sourceParkingWaypointName = FindNearestQuietWaypointName(agentPos);
		if (task.targetParkingWaypointName.empty() || task.sourceParkingWaypointName.empty() ||
			task.targetParkingWaypointName == task.sourceParkingWaypointName)
			task.travelMode = RoutineTravelMode::WalkOnly;
	}

	// Time home->work trips (see EstimateCommuteHours). Only from home, so a
	// hop back from a lunch venue doesn't teach a short commute.
	const std::string& agentName = agent->GetEntity()->GetName();
	if (task.type == RoutineTaskType::GoToWork)
		_commuteTrips[agentName] = CommuteTrip{ simulationHour, task.targetEntityName,
			!agent->GetHomeEntityName().empty() && agent->GetSettledVenueName() == agent->GetHomeEntityName() };
	else
		_commuteTrips.erase(agentName);

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
	// Commute lead: set off for work this many game hours before the shift so
	// the agent is AT its post when it starts (the at-post window job pickups are
	// planned against), rather than starting the trip then.
	const float commuteLead = workplace != nullptr ? EstimateCommuteHours(agent, workplace) : 0.0f;
	bool onShiftNow = false;
	if (schedule.empty())
	{
		onShiftNow = workplace != nullptr && workplace->IsHourInAnyShift(simulationHour);
		const bool leaveForShift = workplace != nullptr && commuteLead > 0.0f &&
			workplace->IsHourInAnyShift(std::fmod(simulationHour + commuteLead, 24.0f));
		activity = (onShiftNow || leaveForShift) ? "Work" : "Home";
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
		onShiftNow = _stricmp(activity.c_str(), "Work") == 0;

		// Not working yet, but a Work block starts within the commute: leave now.
		if (!onShiftNow && workplace != nullptr && commuteLead > 0.0f)
		{
			const float ahead = std::fmod(hour + commuteLead, 24.0f);
			for (size_t i = 0; i < schedule.size(); ++i)
			{
				if (_stricmp(schedule[i].activity.c_str(), "Work") == 0 &&
					HourInWindow(ahead, schedule[i].start, schedule[i].end))
				{
					activity = schedule[i].activity;
					blockKey = ((uint64_t)_dayIndex << 8) | (uint64_t)(i + 1);
					break;
				}
			}
		}
	}

	if (_stricmp(activity.c_str(), "Work") == 0 && workplace == nullptr)
		activity = "Home";
	// On shift = the shift has actually started (the commute before it isn't).
	agent->SetOnShift(onShiftNow && workplace != nullptr);

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

float CityRoutineSystemComponent::EstimateCommuteHours(RoutineAgentComponent* agent, PlaceOfWorkComponent* workplace) const
{
	if (agent == nullptr || agent->GetEntity() == nullptr || workplace == nullptr || workplace->GetEntity() == nullptr)
		return 0.0f;

	// Learned from this agent's own home->work trips: the real figure, with
	// traffic, transit waits and walks to the car included.
	if (auto it = _learnedCommuteHours.find(agent->GetEntity()->GetName()); it != _learnedCommuteHours.end())
		return std::clamp(it->second + _commuteBufferHours, _commuteBufferHours, 6.0f);

	// First trip: estimate from home->work distance. Streets wind (x1.4); beyond
	// walking range the trip is a mix of walking to the car / stop, driving or
	// riding and waiting, ~4 m/s overall. Plus a minute getting going.
	if (_gameHoursPerSecond <= 0.0f)
		return _commuteBufferHours;

	math::Vector3 from = agent->GetEntity()->GetWorldTM().Translation();
	if (!agent->GetHomeEntityName().empty())
		if (auto* home = agent->GetEntity()->GetScene()->GetEntityByName(agent->GetHomeEntityName()); home != nullptr)
			from = home->GetWorldTM().Translation();
	const float distance = (workplace->GetEntity()->GetWorldTM().Translation() - from).Length() * 1.4f;
	const float walk = agent->GetWalkSpeed() > 0.1f ? agent->GetWalkSpeed() : 1.4f;
	const float speed = distance <= _walkMaxDistance * 1.4f ? walk : 4.0f;
	const float seconds = distance / speed + 60.0f;
	return std::clamp(seconds * _gameHoursPerSecond + _commuteBufferHours, _commuteBufferHours, 6.0f);
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
		// Never across a locked district gate (city NPCs stay out, district NPCs stay in).
		if (!DistrictGateComponent::CanTravel(scene, agentPos, place->GetEntity()->GetWorldTM().Translation()))
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
	const LocomotionClips* cachedClips = GetAgentClips(entity);
	if (cachedClips == nullptr)
		return;
	const LocomotionClips clips = *cachedClips;
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
	// Sitting down / seated / standing up: UpdateSit plays those clips.
	if (auto sit = _sitStates.find(entity->GetName()); sit != _sitStates.end() && sit->second.phase != SitState::Phase::Approach)
	{
		state.lastPosition = pos;
		state.hasLastPosition = true;
		state.smoothedSpeed = 0.0f;
		state.requestedClip = -1;
		return;
	}
	// Riding a bus/train: carried, not walking.
	if (agent->GetTransitPhase() == RoutineAgentComponent::TransitPhase::Riding)
	{
		state.lastPosition = pos;
		state.hasLastPosition = true;
		state.smoothedSpeed = 0.0f;
		if ((int32_t)skel->GetTargetAnimationIndex() != clips.idle)
			skel->BlendToAnimationIndex((uint32_t)clips.idle);
		state.requestedClip = clips.idle;
		return;
	}
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
	// Compare with where the skeleton is HEADING (the blend target), every frame.
	// The old check cached the request and compared with the clip currently
	// playing: walk -> (speed dip) idle blend started -> walk again within the
	// ~80 ms crossfade saw "walk" still playing and did nothing, the pending
	// blend then completed into idle, and the NPC walked on in its idle pose
	// until it next stopped. Re-checking the target also heals any outside
	// reset of the clip (mesh swap, vehicle exit).
	if ((int32_t)skel->GetTargetAnimationIndex() != wanted)
		skel->BlendToAnimationIndex((uint32_t)wanted);
	state.requestedClip = wanted;
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

	return BeginWalkToPosition(agent, targetEntity->GetWorldTM().Translation());
}

bool CityRoutineSystemComponent::BeginWalkToPosition(RoutineAgentComponent* agent, const math::Vector3& to)
{
	if (agent == nullptr || agent->GetEntity() == nullptr)
		return false;
	auto* scene = agent->GetEntity()->GetScene();
	if (scene == nullptr)
		return false;

	const math::Vector3 from = agent->GetEntity()->GetWorldTM().Translation();
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

	// Stuck in a navmesh pocket inside a venue (several houses have a kitchen /
	// back-yard half that doesn't connect to the front door): the partial path
	// went nowhere and the agent re-requested it every frame or so for good. Step
	// out to the venue's entry waypoint - home or workplace, whichever is close -
	// when the route from THERE is complete.
	if (nav->HasPath() && nav->IsPathPartial())
	{
		math::Vector3 bestExit;
		float bestD2 = 40.0f * 40.0f;
		bool haveExit = false;
		auto considerVenue = [&](const std::string& venueName)
		{
			auto* venue = venueName.empty() ? nullptr : scene->GetEntityByName(venueName);
			if (venue == nullptr)
				return;
			std::string waypoint;
			if (auto* res = venue->GetComponent<ResidenceComponent>())
				waypoint = res->ResolveEntryWaypointName();
			else if (auto* work = venue->GetComponent<PlaceOfWorkComponent>())
				waypoint = work->ResolveEntryWaypointName();
			math::Vector3 p;
			if (waypoint.empty() || !ResolveAnchorPosition(waypoint, p))
				return;
			const float d2 = (p - from).LengthSquared();
			if (d2 < bestD2 && d2 > 1.0f)
			{
				bestD2 = d2;
				bestExit = p;
				haveExit = true;
			}
		};
		considerVenue(agent->GetSettledVenueName());
		considerVenue(agent->GetHomeEntityName());
		considerVenue(agent->GetAssignedWorkplaceEntityName());
		if (haveExit)
		{
			nav->ClearPath();
			nav->FindPath(scene->GetNavMeshId(), bestExit, to, 5.0f);
			if (nav->HasPath() && !nav->IsPathPartial())
			{
				agent->GetEntity()->ForcePosition(bestExit);
				LOG_DEBUG("CityRoutineSystem: '%s' was in a navmesh pocket - stepped out to its venue entrance", agent->GetEntity()->GetName().c_str());
			}
			else
			{
				nav->ClearPath();
				nav->FindPath(scene->GetNavMeshId(), from, to, 5.0f);
			}
		}

		// No entrance nearby (or it didn't help): search BACKWARDS from the target.
		// That path ends at the point of the target's connected navmesh nearest the
		// agent; if it is close, the agent is on a ledge / roof / island next to
		// walkable ground - step onto it and path from there.
		if (nav->HasPath() && nav->IsPathPartial())
		{
			nav->ClearPath();
			nav->FindPath(scene->GetNavMeshId(), to, from, 5.0f);
			const auto reverse = nav->GetPathPoints();
			nav->ClearPath();
			if (!reverse.empty() && (reverse.back() - from).LengthSquared() < 12.0f * 12.0f)
			{
				nav->FindPath(scene->GetNavMeshId(), reverse.back(), to, 5.0f);
				if (nav->HasPath() && !nav->IsPathPartial())
				{
					agent->GetEntity()->ForcePosition(reverse.back());
					LOG_DEBUG("CityRoutineSystem: '%s' was off the walkable network - stepped %.1f m onto it",
						agent->GetEntity()->GetName().c_str(), (reverse.back() - from).Length());
				}
				else
				{
					nav->ClearPath();
					nav->FindPath(scene->GetNavMeshId(), from, to, 5.0f);
				}
			}
			else
			{
				nav->FindPath(scene->GetNavMeshId(), from, to, 5.0f);
			}
		}
	}

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
		task.sourceParkingWaypointName = FindNearestQuietWaypointName(car->GetPosition());
		agent->SetCurrentVehicleEntityName(car->GetName());
		return car;
	}

	// The car this agent spawned and parked earlier (a car park near work, say):
	// drive it again rather than conjure another. One far away is retired.
	if (task.type != RoutineTaskType::RespondEmergency)
	{
		const std::string& agentName = agent->GetEntity()->GetName();
		if (auto* temp = GetTempCar(agent, agent->GetEntity()->GetWorldTM().Translation()); temp != nullptr)
		{
			if (auto* traffic = temp->GetComponent<TrafficVehicleComponent>(); traffic != nullptr)
				traffic->SetParked(false);
			_tempCars.erase(agentName);
			agent->SetCurrentVehicleEntityName(temp->GetName());
			return temp;
		}
		if (auto it = _tempCars.find(agentName); it != _tempCars.end())
		{
			ReleaseCarBay(it->second);
			if (auto* old = scene->GetEntityByName(it->second); old != nullptr && !old->IsPendingDeletion())
				old->DeleteMe();
			_tempCars.erase(it);
		}
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
		{
			ent->SetFlag(HexEngine::EntityFlags::DoNotSave);
			// Transient movers are not GI geometry (spawning one used to rebuild every
			// GI clipmap's triangle list); flagged in the spawn frame, before the
			// scene resolves the add (Scene::ProcessPendingGiMeshes).
			for (auto* smc : ent->GetComponents<HexEngine::StaticMeshComponent>())
				if (smc != nullptr)
					smc->SetExcludeFromVoxelGI(true);
		}
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
	if (vehicleEntity == nullptr || agent == nullptr || agent->GetEntity() == nullptr)
		return false;

	auto* vehicle = vehicleEntity->GetComponent<TrafficVehicleComponent>();
	if (vehicle == nullptr)
		vehicle = vehicleEntity->AddComponent<TrafficVehicleComponent>();
	auto* scene = vehicleEntity->GetScene();
	if (vehicle == nullptr || scene == nullptr)
		return false;
	const std::string agentName = agent->GetEntity()->GetName();
	const std::string carName = vehicleEntity->GetName();

	// Where the trip ends.
	math::Vector3 destination;
	if (!ResolveAnchorPosition(task.targetEntryWaypointName, destination) &&
		!ResolveAnchorPosition(task.targetEntityName, destination) &&
		!ResolveAnchorPosition(task.targetParkingWaypointName, destination))
		return false;

	// Leaving a bay (driveway / car park): back out into the aisle and drive to the
	// road from the lot's entrance. Otherwise the car pulls out on a quiet lane node.
	std::vector<math::Vector3> leadIn, leadOut;
	int32_t reversePoints = 0;
	// Not in a bay: pull out near the driver (who has walked to the car). A freshly
	// spawned car still stands where the prefab loaded it (the world origin).
	math::Vector3 startAnchor = agent->GetEntity()->GetWorldTM().Translation();
	const BayRef fromBay = _parkingEnabled ? GetCarBay(vehicleEntity) : BayRef{};
	ParkingLotComponent* fromLot = fromBay.IsValid() ? ResolveLot(fromBay.lot) : nullptr;
	if (fromLot != nullptr)
	{
		fromLot->BuildDepartureLeg(fromBay.bay, leadIn, reversePoints);
		if (!leadIn.empty())
			startAnchor = leadIn.back();
	}

	// Arriving: a bay near the destination when there's one free (released again if
	// the trip is abandoned). Without one the car stops on a quiet lane node.
	if (auto it = _tripBays.find(agentName); it != _tripBays.end())
	{
		if (auto* lot = ResolveLot(it->second.lot); lot != nullptr)
			lot->Release(it->second.bay, carName);
		_tripBays.erase(it);
	}
	const BayRef toBay = ReserveDestinationBay(agent, task, vehicleEntity, destination);
	ParkingLotComponent* toLot = toBay.IsValid() ? ResolveLot(toBay.lot) : nullptr;
	if (fromLot != nullptr && toLot == fromLot)
	{
		// The car is already where the trip ends (the owner walked back to it at home,
		// say): nothing to drive. The failed drive leg finishes the trip on foot.
		toLot->Release(toBay.bay, carName);
		return false;
	}
	math::Vector3 endAnchor = destination;
	if (toLot != nullptr)
	{
		toLot->BuildArrivalLeg(toBay.bay, leadOut);
		if (!leadOut.empty())
			endAnchor = leadOut.front();
	}
	auto releaseTripBay = [&]()
	{
		if (toLot != nullptr)
			toLot->Release(toBay.bay, carName);
	};

	// Candidate lane nodes at each end: the lot's own access nodes, else the nearest
	// quiet ones. An emergency vehicle keeps its station's authored node.
	auto gatherCandidates = [&](ParkingLotComponent* lot, const math::Vector3& anchor, const std::string& authored, std::vector<std::string>& out)
	{
		out.clear();
		if (lot != nullptr && !lot->GetAccessWaypointsCsv().empty())
		{
			std::stringstream stream(lot->GetAccessWaypointsCsv());
			std::string token;
			while (std::getline(stream, token, ','))
			{
				token.erase(0, token.find_first_not_of(" \t"));
				token.erase(token.find_last_not_of(" \t") + 1);
				if (IsLaneNode(token))
					out.push_back(token);
			}
		}
		if (out.empty() && task.type == RoutineTaskType::RespondEmergency && IsLaneNode(authored))
			out.push_back(authored);
		// Close to the entrance / pull-out point: the stretch between it and the lane is
		// driven off-road at parking speed (a node 50 m away meant crawling across
		// verges and stacking up behind each other).
		if (out.empty())
			FindQuietWaypointsNear(anchor, 4, 25.0f, out);
		if (out.empty())
			FindQuietWaypointsNear(anchor, 3, 70.0f, out);
		if (out.empty())
		{
			const std::string nearest = FindNearestQuietWaypointName(anchor);
			if (!nearest.empty())
				out.push_back(nearest);
		}
	};
	std::vector<std::string> starts, ends;
	gatherCandidates(fromLot, startAnchor, task.sourceParkingWaypointName, starts);
	gatherCandidates(toLot, endAnchor, task.targetParkingWaypointName, ends);

	auto nodePosition = [&](const std::string& name)
	{
		auto* e = scene->GetEntityByName(name);
		return e != nullptr ? e->GetWorldTM().Translation() : math::Vector3::Zero;
	};
	auto nodeDirection = [&](const std::string& name)
	{
		auto it = _laneNodeDirections.find(name);
		return it != _laneNodeDirections.end() ? it->second : math::Vector3::Zero;
	};

	// The cheapest pair: lane route plus the off-road stretches at each end, with a
	// penalty for joining the lane behind the pull-out point or leaving it past the
	// turn-in (both mean driving against the lane's direction for a bit).
	std::string bestStart, bestEnd;
	float bestCost = std::numeric_limits<float>::max();
	for (const auto& s : starts)
	{
		const math::Vector3 sPos = nodePosition(s);
		for (const auto& e : ends)
		{
			if (s == e)
				continue;
			const float route = TrafficVehicleComponent::EstimateLaneRouteLength(scene, s, e);
			if (route < 0.0f)
				continue;
			const math::Vector3 ePos = nodePosition(e);
			// Off-road metres cost ~4x lane metres (parking speed vs lane speed).
			float cost = route + 4.0f * math::Vector3::Distance(startAnchor, sPos) + 4.0f * math::Vector3::Distance(ePos, endAnchor);
			if ((sPos - startAnchor).Dot(nodeDirection(s)) < -1.0f)
				cost += 40.0f;
			if ((endAnchor - ePos).Dot(nodeDirection(e)) < -1.0f)
				cost += 40.0f;
			if (cost < bestCost)
			{
				bestCost = cost;
				bestStart = s;
				bestEnd = e;
			}
		}
	}
	if (bestStart.empty() || bestEnd.empty())
	{
		releaseTripBay();
		return false;
	}

	if (fromLot != nullptr)
		ReleaseCarBay(carName);	// it drives out of the bay from where it stands
	else
		vehicleEntity->ForcePosition(nodePosition(bestStart));

	vehicle->SetUseWaypointRoute(true);
	vehicle->SetWaypointRouteEndpoints(bestStart, bestEnd);
	vehicle->SetOffLaneLegs(leadIn, reversePoints, leadOut);
	vehicle->SetDespawnAtRouteEnd(false);
	vehicle->RestartPath();
	if (vehicle->GetPlannedRouteLength() <= 0.0f)
	{
		releaseTripBay();
		return false;
	}
	if (toLot != nullptr)
		_tripBays[agentName] = toBay;

	if (_parkingDebugLog)
		LOG_INFO("Parking: '%s' drives %s from %s (%s) to %s (%s)", agentName.c_str(), carName.c_str(),
			fromLot != nullptr ? std::format("{} bay {}", fromBay.lot, fromBay.bay).c_str() : "the road",
			bestStart.c_str(), toLot != nullptr ? std::format("{} bay {}", toBay.lot, toBay.bay).c_str() : "the road", bestEnd.c_str());
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

	// Learn this agent's real commute (game hours, home -> work).
	if (agent->GetEntity() != nullptr)
	{
		const std::string& agentName = agent->GetEntity()->GetName();
		if (auto it = _commuteTrips.find(agentName); it != _commuteTrips.end())
		{
			if (success && completedTask.has_value() && completedTask->type == RoutineTaskType::GoToWork &&
				completedTask->targetEntityName == it->second.target && _lastSimulationHour >= 0.0f)
			{
				const float took = std::fmod(_lastSimulationHour - it->second.departHour + 24.0f, 24.0f);

				// How early / late against the shift start (searched in 3-minute steps).
				PlaceOfWorkComponent* workplace = nullptr;
				if (auto* we = agent->GetEntity()->GetScene()->GetEntityByName(agent->GetAssignedWorkplaceEntityName()); we != nullptr)
					workplace = we->GetComponent<PlaceOfWorkComponent>();
				auto isWorkHour = [&](float h) -> bool
				{
					h = std::fmod(h + 48.0f, 24.0f);
					const auto& schedule = agent->GetSchedule();
					if (schedule.empty())
						return workplace != nullptr && workplace->IsHourInAnyShift(h);
					const float offset = (Hash01(agentName, 0) - 0.5f) * agent->GetScheduleJitterHours();
					const float t = std::fmod(h - offset + 48.0f, 24.0f);
					for (const auto& block : schedule)
						if (HourInWindow(t, block.start, block.end))
							return _stricmp(block.activity.c_str(), "Work") == 0;
					return false;
				};
				float margin = 0.0f;	// + = early, - = late (hours)
				if (isWorkHour(_lastSimulationHour))
				{
					while (margin > -6.0f && isWorkHour(_lastSimulationHour + margin - 0.05f))
						margin -= 0.05f;
				}
				else
				{
					while (margin < 6.0f && !isWorkHour(_lastSimulationHour + margin))
						margin += 0.05f;
				}
				LOG_DEBUG("CityRoutineSystem: commute '%s' -> %s left %.2f arrived %.2f (took %.2f h, %s by %.2f h)",
					agentName.c_str(), completedTask->targetEntityName.c_str(), it->second.departHour, _lastSimulationHour,
					took, margin >= 0.0f ? "EARLY" : "LATE", std::fabs(margin));

				if (it->second.fromHome && took > 0.0f && took < 8.0f)
				{
					auto learned = _learnedCommuteHours.find(agentName);
					if (learned == _learnedCommuteHours.end())
						_learnedCommuteHours[agentName] = took;
					else
						learned->second = learned->second * 0.5f + took * 0.5f;
				}
			}
			_commuteTrips.erase(it);
		}
	}
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
		CleanupAgentVehicle(agent, true);
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
		task.sourceParkingWaypointName = FindNearestQuietWaypointName(agentPos);

		math::Vector3 destinationPos = agentPos;
		auto* scene = agent->GetEntity()->GetScene();
		if (scene != nullptr && !task.targetEntityName.empty())
		{
			auto* target = scene->GetEntityByName(task.targetEntityName);
			if (target != nullptr && !target->IsPendingDeletion())
				destinationPos = target->GetWorldTM().Translation();
		}
		task.targetParkingWaypointName = FindNearestQuietWaypointName(destinationPos);
		if (task.targetEntryWaypointName.empty())
			task.targetEntryWaypointName = task.targetParkingWaypointName;

		CleanupAgentVehicle(agent, true);
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
		CleanupAgentVehicle(agent, true);
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

// ---------------------------------------------------------------------------
// Parking: quiet lane nodes, car parks and driveways
// ---------------------------------------------------------------------------

void CityRoutineSystemComponent::EnsureQuietNodes()
{
	if (_quietNodesBuilt)
		return;
	auto* scene = GetEntity() != nullptr ? GetEntity()->GetScene() : nullptr;
	if (scene == nullptr)
		return;
	std::vector<TrafficVehicleComponent::LaneNodeInfo> nodes;
	TrafficVehicleComponent::GatherLaneNodes(scene, nodes);
	if (nodes.empty())
		return;
	_quietNodesBuilt = true;
	_quietNodes.clear();
	_laneNodeNames.clear();
	_laneNodeDirections.clear();

	// Junction and bus-stop nodes: a car stopping or pulling out on or near one blocks
	// the traffic through it. In this lane graph the node BEFORE a junction branches
	// (one next lane per turn) although it is still mid-street; the turn lanes it
	// branches into, and the nodes lanes merge into, are the junction itself.
	std::unordered_set<std::string> busyNames;
	for (const auto& n : nodes)
	{
		_laneNodeNames.insert(n.name);
		math::Vector3 dir = n.nextPosition - n.position;
		dir.y = 0.0f;
		if (n.outCount > 0 && dir.LengthSquared() > 1e-4f)
		{
			dir.Normalize();
			_laneNodeDirections[n.name] = dir;
		}
		if (n.inCount > 1 || n.outCount == 0 || n.name.rfind("BusStop", 0) == 0)
			busyNames.insert(n.name);
		if (n.outCount > 1)
			busyNames.insert(n.next.begin(), n.next.end());
	}
	std::vector<math::Vector3> busy;
	for (const auto& n : nodes)
		if (busyNames.count(n.name) != 0)
			busy.push_back(n.position);
	constexpr float kClearance = 6.0f;
	for (const auto& n : nodes)
	{
		if (busyNames.count(n.name) != 0 || n.inCount == 0)
			continue;
		bool nearBusy = false;
		for (const auto& b : busy)
		{
			if ((b - n.position).LengthSquared() < kClearance * kClearance)
			{
				nearBusy = true;
				break;
			}
		}
		if (!nearBusy)
			_quietNodes.push_back({ n.name, n.position });
	}
	LOG_INFO("CityRoutineSystem: %zu of %zu lane nodes are quiet enough to stop or pull out on", _quietNodes.size(), nodes.size());
}

bool CityRoutineSystemComponent::IsLaneNode(const std::string& name)
{
	if (name.empty())
		return false;
	EnsureQuietNodes();
	return _laneNodeNames.count(name) != 0;
}

void CityRoutineSystemComponent::FindQuietWaypointsNear(const math::Vector3& worldPosition, size_t maxCount, float maxDistance, std::vector<std::string>& out)
{
	out.clear();
	EnsureQuietNodes();
	std::vector<std::pair<float, const QuietNode*>> found;
	for (const auto& node : _quietNodes)
	{
		const float d = (node.position - worldPosition).LengthSquared();
		if (d <= maxDistance * maxDistance)
			found.emplace_back(d, &node);
	}
	std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
	for (size_t i = 0; i < found.size() && out.size() < maxCount; ++i)
		out.push_back(found[i].second->name);
}

std::string CityRoutineSystemComponent::FindNearestQuietWaypointName(const math::Vector3& worldPosition)
{
	EnsureQuietNodes();
	float best = std::numeric_limits<float>::max();
	const QuietNode* bestNode = nullptr;
	for (const auto& node : _quietNodes)
	{
		const float d = (node.position - worldPosition).LengthSquared();
		if (d < best)
		{
			best = d;
			bestNode = &node;
		}
	}
	return bestNode != nullptr ? bestNode->name : FindNearestWaypointName(worldPosition);
}

ParkingLotComponent* CityRoutineSystemComponent::ResolveLot(const std::string& lotEntityName) const
{
	if (lotEntityName.empty() || GetEntity() == nullptr || GetEntity()->GetScene() == nullptr)
		return nullptr;
	auto* e = GetEntity()->GetScene()->GetEntityByName(lotEntityName);
	if (e == nullptr || e->IsPendingDeletion())
		return nullptr;
	auto* lot = e->GetComponent<ParkingLotComponent>();
	return lot != nullptr && lot->IsEnabled() ? lot : nullptr;
}

ParkingLotComponent* CityRoutineSystemComponent::FindHomeLot(RoutineAgentComponent* agent) const
{
	if (agent == nullptr || agent->GetHomeEntityName().empty() || GetEntity() == nullptr || GetEntity()->GetScene() == nullptr)
		return nullptr;
	auto* scene = GetEntity()->GetScene();
	if (auto* homeEntity = scene->GetEntityByName(agent->GetHomeEntityName()); homeEntity != nullptr)
	{
		if (auto* residence = homeEntity->GetComponent<ResidenceComponent>(); residence != nullptr)
		{
			if (auto* lot = ResolveLot(residence->ResolveParkingWaypointName()); lot != nullptr)
				return lot;
		}
	}
	std::vector<ParkingLotComponent*> lots;
	scene->GetComponents<ParkingLotComponent>(lots);
	for (auto* lot : lots)
	{
		if (lot != nullptr && lot->IsEnabled() && lot->GetPrivateTo() == agent->GetHomeEntityName())
			return lot;
	}
	return nullptr;
}

CityRoutineSystemComponent::BayRef CityRoutineSystemComponent::ReserveDestinationBay(RoutineAgentComponent* agent, const RoutineTaskSpec& task, HexEngine::Entity* car, const math::Vector3& destination)
{
	BayRef out;
	if (!_parkingEnabled || car == nullptr || task.type == RoutineTaskType::RespondEmergency || GetEntity() == nullptr || GetEntity()->GetScene() == nullptr)
		return out;
	const std::string& carName = car->GetName();

	// Home: the household's driveway, the car's own spot first.
	if (task.type == RoutineTaskType::GoHome || (!agent->GetHomeEntityName().empty() && task.targetEntityName == agent->GetHomeEntityName()))
	{
		if (auto* lot = FindHomeLot(agent); lot != nullptr)
		{
			math::Vector3 nearPoint = destination;
			if (auto it = _ownedCars.find(carName); it != _ownedCars.end())
				nearPoint = it->second.homePosition;
			const int32_t bay = lot->FindFreeBay(nearPoint);
			if (bay >= 0 && lot->Reserve(bay, carName))
				return BayRef{ lot->GetEntity()->GetName(), bay };
		}
	}

	// The nearest public car park with room that's within walking distance.
	std::vector<ParkingLotComponent*> lots;
	GetEntity()->GetScene()->GetComponents<ParkingLotComponent>(lots);
	ParkingLotComponent* best = nullptr;
	float bestDistance = std::numeric_limits<float>::max();
	for (auto* lot : lots)
	{
		if (lot == nullptr || !lot->IsEnabled() || lot->IsPrivate() || lot->CountFreeBays() == 0)
			continue;
		const float d = (lot->GetEntrancePoint() - destination).Length();
		if (d <= lot->GetMaxWalkDistance() && d < bestDistance)
		{
			best = lot;
			bestDistance = d;
		}
	}
	if (best != nullptr)
	{
		const int32_t bay = best->FindFreeBay(destination);
		if (bay >= 0 && best->Reserve(bay, carName))
			return BayRef{ best->GetEntity()->GetName(), bay };
	}
	return out;
}

CityRoutineSystemComponent::BayRef CityRoutineSystemComponent::GetCarBay(HexEngine::Entity* car)
{
	if (car == nullptr)
		return {};
	if (auto it = _carBays.find(car->GetName()); it != _carBays.end())
		return it->second;
	if (!_parkingEnabled || GetEntity() == nullptr || GetEntity()->GetScene() == nullptr)
		return {};
	// Not seen in a bay yet this session: a car standing in one (the driveway it was
	// placed on in the scene) claims it.
	const math::Vector3 carPos = car->GetPosition();	// root entity: local == world, and current after a ForcePosition
	std::vector<ParkingLotComponent*> lots;
	GetEntity()->GetScene()->GetComponents<ParkingLotComponent>(lots);
	for (auto* lot : lots)
	{
		if (lot == nullptr || !lot->IsEnabled())
			continue;
		for (int32_t i = 0; i < lot->GetBayCount(); ++i)
		{
			math::Vector3 bayPos, nose;
			if (!lot->GetBay(i, bayPos, nose))
				continue;
			const math::Vector3 delta = bayPos - carPos;
			if (delta.x * delta.x + delta.z * delta.z > 2.0f * 2.0f || std::fabs(delta.y) > 2.0f)
				continue;
			if (!lot->Reserve(i, car->GetName()))
				continue;
			BayRef ref{ lot->GetEntity()->GetName(), i };
			_carBays[car->GetName()] = ref;
			return ref;
		}
	}
	return {};
}

void CityRoutineSystemComponent::ReleaseCarBay(const std::string& carName)
{
	auto it = _carBays.find(carName);
	if (it == _carBays.end())
		return;
	if (auto* lot = ResolveLot(it->second.lot); lot != nullptr)
		lot->Release(it->second.bay, carName);
	_carBays.erase(it);
}

void CityRoutineSystemComponent::PlaceCarInBay(HexEngine::Entity* car, const BayRef& bay)
{
	auto* lot = ResolveLot(bay.lot);
	math::Vector3 position, nose;
	if (car == nullptr || lot == nullptr || !lot->GetBay(bay.bay, position, nose))
		return;
	if (auto it = _carBays.find(car->GetName()); it != _carBays.end() && (it->second.lot != bay.lot || it->second.bay != bay.bay))
		ReleaseCarBay(car->GetName());
	lot->Reserve(bay.bay, car->GetName());
	_carBays[car->GetName()] = bay;
	car->ForcePosition(position);
	car->ForceRotation(math::Quaternion::CreateFromYawPitchRoll(std::atan2(nose.x, nose.z), 0.0f, 0.0f));
	if (auto* traffic = car->GetComponent<TrafficVehicleComponent>(); traffic != nullptr)
	{
		traffic->SetParked(true);
		traffic->SetUseWaypointRoute(false);
	}
}

HexEngine::Entity* CityRoutineSystemComponent::GetTempCar(const RoutineAgentComponent* agent, const math::Vector3& agentPos) const
{
	if (agent == nullptr || agent->GetEntity() == nullptr || GetEntity() == nullptr || GetEntity()->GetScene() == nullptr)
		return nullptr;
	auto it = _tempCars.find(agent->GetEntity()->GetName());
	if (it == _tempCars.end())
		return nullptr;
	auto* car = GetEntity()->GetScene()->GetEntityByName(it->second);
	if (car == nullptr || car->IsPendingDeletion())
		return nullptr;
	auto* traffic = car->GetComponent<TrafficVehicleComponent>();
	if (traffic == nullptr || !traffic->IsParked())
		return nullptr;
	return (car->GetPosition() - agentPos).LengthSquared() <= 300.0f * 300.0f ? car : nullptr;
}

void CityRoutineSystemComponent::SweepDriverlessVehicles(float frameTime)
{
	_driverlessSweepTimer -= frameTime;
	if (_driverlessSweepTimer > 0.0f)
		return;
	_driverlessSweepTimer = 1.0f;
	auto* scene = GetEntity() != nullptr ? GetEntity()->GetScene() : nullptr;
	if (scene == nullptr)
		return;

	std::vector<RoutineAgentComponent*> agents;
	scene->GetComponents<RoutineAgentComponent>(agents);
	std::unordered_set<std::string> driven;
	std::unordered_map<std::string, RoutineAgentComponent*> owners;
	for (auto* agent : agents)
	{
		if (agent == nullptr || agent->GetEntity() == nullptr)
			continue;
		if (!agent->GetCurrentVehicleEntityName().empty())
			driven.insert(agent->GetCurrentVehicleEntityName());
		if (!agent->GetOwnedVehicleEntityName().empty())
			owners[agent->GetOwnedVehicleEntityName()] = agent;
	}

	std::vector<TrafficVehicleComponent*> vehicles;
	scene->GetComponents<TrafficVehicleComponent>(vehicles);
	for (auto* vehicle : vehicles)
	{
		// Routine cars only: a waypoint route that ends in a stop rather than a despawn.
		// Ambient lane traffic despawns at its route end; buses and trains are transit.
		if (vehicle == nullptr || vehicle->IsParked() || !vehicle->IsUsingWaypointRoute() || vehicle->GetDespawnAtRouteEnd())
			continue;
		auto* car = vehicle->GetEntity();
		if (car == nullptr || car->IsPendingDeletion() || car->GetComponent<TransitVehicleComponent>() != nullptr)
			continue;
		if (driven.count(car->GetName()) != 0)
			continue;
		if (owners.count(car->GetName()) != 0)
		{
			LOG_INFO("CityRoutineSystem: '%s' was driving with nobody in it - parked back home", car->GetName().c_str());
			ParkOwnedCar(car, true);
		}
		else
		{
			LOG_INFO("CityRoutineSystem: '%s' was driving with nobody in it - removed", car->GetName().c_str());
			ReleaseCarBay(car->GetName());
			car->DeleteMe();
		}
	}
}

void CityRoutineSystemComponent::ResetParking()
{
	if (GetEntity() != nullptr && GetEntity()->GetScene() != nullptr)
	{
		std::vector<ParkingLotComponent*> lots;
		GetEntity()->GetScene()->GetComponents<ParkingLotComponent>(lots);
		for (auto* lot : lots)
			if (lot != nullptr)
				lot->ReleaseAll();
	}
	_carBays.clear();
	_tripBays.clear();
	_tempCars.clear();
	_ownedCars.clear();
	_quietNodesBuilt = false;	// rebuilt from the (possibly edited) lanes next play
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
		// First sight this play session: wherever it stands now is home - unless it
		// isn't on its household's driveway (a save restored it mid-trip or in a car
		// park across town): then home is a free driveway bay.
		OwnedCarState state;
		state.homePosition = car->GetPosition();	// root entity: local == world, current after a ForcePosition
		state.homeRotation = car->GetRotation();
		RoutineAgentComponent* owner = nullptr;
		std::vector<math::Vector3> otherCars;	// where the other own cars stand now
		if (auto* scene = car->GetScene(); scene != nullptr)
		{
			std::vector<RoutineAgentComponent*> agents;
			scene->GetComponents<RoutineAgentComponent>(agents);
			for (auto* a : agents)
			{
				if (a == nullptr || a->GetOwnedVehicleEntityName().empty())
					continue;
				if (a->GetOwnedVehicleEntityName() == car->GetName())
					owner = a;
				else if (auto* other = scene->GetEntityByName(a->GetOwnedVehicleEntityName()); other != nullptr)
					otherCars.push_back(other->GetPosition());
			}
		}
		if (auto* lot = owner != nullptr ? FindHomeLot(owner) : nullptr; lot != nullptr)
		{
			bool onDriveway = false;
			int32_t freeBay = -1;
			for (int32_t i = 0; i < lot->GetBayCount(); ++i)
			{
				math::Vector3 bayPos, nose;
				if (!lot->GetBay(i, bayPos, nose))
					continue;
				if ((bayPos - state.homePosition).LengthSquared() < 2.5f * 2.5f)
					onDriveway = true;
				bool taken = false;
				for (const auto& [otherName, other] : _ownedCars)
					taken = taken || (other.homePosition - bayPos).LengthSquared() < 1.0f;
				for (const auto& p : otherCars)
					taken = taken || (p - bayPos).LengthSquared() < 2.0f * 2.0f;
				if (!taken && freeBay < 0)
					freeBay = i;
			}
			math::Vector3 bayPos, nose;
			if (!onDriveway && freeBay >= 0 && lot->GetBay(freeBay, bayPos, nose))
			{
				state.homePosition = bayPos;
				state.homeRotation = math::Quaternion::CreateFromYawPitchRoll(std::atan2(nose.x, nose.z), 0.0f, 0.0f);
			}
		}
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
	return it != _ownedCars.end() && (it->second.homePosition - agentPos).LengthSquared() <= 80.0f * 80.0f;
}

void CityRoutineSystemComponent::ParkOwnedCar(HexEngine::Entity* car, bool atHome)
{
	auto& state = OwnedCarStateFor(car);
	if (auto* traffic = car->GetComponent<TrafficVehicleComponent>(); traffic != nullptr)
	{
		traffic->SetParked(true);
		traffic->SetUseWaypointRoute(false);
	}
	// Leaving whatever bay it was in (a car park across town when it's sent home).
	ReleaseCarBay(car->GetName());
	if (atHome)
	{
		car->ForcePosition(state.homePosition);
		car->ForceRotation(state.homeRotation);
		if (state.stored)
			SetAgentHidden(car, false);
		state.stored = false;
		GetCarBay(car);	// back in its driveway bay
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
	if (it == _ownedCars.end())
		return;
	// Stored out of sight, or left in a car park across town (the owner came home
	// another way): it's back on the driveway by the time the owner is home.
	const bool farFromHome = (car->GetPosition() - it->second.homePosition).LengthSquared() > 300.0f * 300.0f;
	if (!it->second.stored && !farFromHome)
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

void CityRoutineSystemComponent::CleanupAgentVehicle(RoutineAgentComponent* agent, bool abandoned)
{
	CleanupAgentTransit(agent);
	if (agent == nullptr || agent->GetEntity() == nullptr)
		return;

	// A bay reserved for this trip's end won't be used now.
	const std::string carName = agent->GetCurrentVehicleEntityName();
	if (auto it = _tripBays.find(agent->GetEntity()->GetName()); it != _tripBays.end())
	{
		if (auto* lot = ResolveLot(it->second.lot); lot != nullptr)
			lot->Release(it->second.bay, carName);
		_tripBays.erase(it);
	}
	if (carName.empty())
		return;

	auto* scene = agent->GetEntity()->GetScene();
	if (scene == nullptr)
		return;

	auto* vehicle = scene->GetEntityByName(carName);
	if (vehicle != nullptr && !vehicle->IsPendingDeletion())
	{
		if (vehicle->GetName() == agent->GetOwnedVehicleEntityName())
		{
			if (abandoned)
			{
				// Given up on mid-road (stuck in a jam, timed out): it used to be
				// hidden right there - often inside a junction - and driven off from
				// there again later. Back to its driveway instead.
				ParkOwnedCar(vehicle, true);
			}
			else
			{
				// Home trip ends on the driveway; anywhere else (no car park nearby)
				// the car is stored out of the lane until the owner comes back for it.
				const auto& active = agent->GetActiveTask();
				const bool goingHome = active.has_value() && active->type == RoutineTaskType::GoHome;
				const auto& state = OwnedCarStateFor(vehicle);
				const bool nearHome = (vehicle->GetWorldTM().Translation() - state.homePosition).LengthSquared() < 60.0f * 60.0f;
				ParkOwnedCar(vehicle, goingHome && nearHome);
			}
		}
		else
		{
			ReleaseCarBay(vehicle->GetName());
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

	// Re-pathing isn't helping (still within 3 m of where the trouble started):
	// after a few tries, step past the obstruction along the path - or, with no
	// usable path, give the task up so it is re-planned - rather than looping.
	if (motion.stuckRecoveries == 0 || (currentPos - motion.stuckAnchor).LengthSquared() > 3.0f * 3.0f)
	{
		motion.stuckAnchor = currentPos;
		motion.stuckRecoveries = 0;
	}
	if (++motion.stuckRecoveries >= 4)
	{
		motion.stuckRecoveries = 0;
		auto* nav = agent->GetEntity()->GetComponent<HexEngine::NavigationComponent>();
		bool stepped = false;
		if (nav != nullptr && nav->HasPath())
		{
			for (const auto& point : nav->GetPathPoints())
			{
				const float d2 = (point - currentPos).LengthSquared();
				if (d2 > 2.5f * 2.5f && d2 < 12.0f * 12.0f)
				{
					agent->GetEntity()->ForcePosition(point);
					stepped = true;
					LOG_WARN("CityRoutineSystem: Agent '%s' was stuck walking toward '%s' - stepped %.1f m past the obstruction",
						agent->GetEntity()->GetName().c_str(), waypointEntityName.c_str(), std::sqrt(d2));
					break;
				}
			}
		}
		if (!stepped)
		{
			HandleTaskRetryOrFailure(agent, failureReason != nullptr ? failureReason : "Stuck while walking");
			return true;
		}
	}

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
	{
		UpdateSit(agent, frameTime);
		return;
	}
	if (!_onSiteWander || agent->GetEntity()->HasFlag(HexEngine::EntityFlags::DoNotRender))
	{
		_onSiteStates.erase(it);
		if (IsSitting(agent))
			AbortSit(agent, false, "hidden / on-site ended");
		return;
	}
	OnSiteState& state = it->second;
	auto* nav = agent->GetEntity()->GetComponent<HexEngine::NavigationComponent>();
	if (nav == nullptr)
		return;

	// Sitting on a bench / chair: that owns the agent until it has stood up again.
	if (IsSitting(agent))
	{
		if (!UpdateSit(agent, frameTime))
			state.pauseSeconds = 3.0f + OnSiteRand01(_onSiteRng) * 6.0f;   // linger a moment after getting up
		return;
	}

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

	// Sometimes take a seat nearby instead of strolling.
	if (_sitEnabled && OnSiteRand01(_onSiteRng) < _sitChanceOnSite &&
		TryBeginSit(agent, state.anchor, _sitSearchRadius, _sitMinSeconds, _sitMaxSeconds, false))
	{
		state.walking = false;
		state.pickFailures = 0;
		return;
	}

	math::Vector3 spot;
	if (PickOnSiteSpot(agent, state.anchor, spot))
	{
		state.pickFailures = 0;
		// Stroll, not commute: a little slower than the agent's walk.
		if (agent->GetWalkSpeed() > 0.0f)
			nav->SetMovementSpeed(agent->GetWalkSpeed() * 0.7f);
		agent->ConsumeNavigationReached();   // clear any stale flag before this stroll
		state.walking = true;
		state.walkSeconds = 0.0f;
	}
	else
	{
		// Cramped spot / no navmesh nearby. Each attempt is up to six FindPaths, and
		// an unreachable candidate makes Detour search the whole connected region:
		// back off (4, 8, 16 .. 60 s) instead of retrying every 4 s forever.
		state.pauseSeconds = std::min(60.0f, 4.0f * (float)(1 << std::min(state.pickFailures, 4))) + OnSiteRand01(_onSiteRng) * 2.0f;
		++state.pickFailures;
	}
}

// ---------------------------------------------------------------------------- sitting

namespace
{
	float FlatDistance(const math::Vector3& a, const math::Vector3& b)
	{
		const float dx = a.x - b.x, dz = a.z - b.z;
		return std::sqrt(dx * dx + dz * dz);
	}

	float SmoothStep01(float t)
	{
		t = std::clamp(t, 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	// The NPC capsule is switched off while seated: it would otherwise sit inside the
	// bench's collider (and block the walkway in front of it).
	void SetAgentCollider(HexEngine::Entity* entity, bool active)
	{
		if (auto* rb = entity->GetComponent<HexEngine::RigidBody>(); rb != nullptr && rb->GetIRigidBody() != nullptr)
			rb->GetIRigidBody()->SetIsSimulated(active);
	}

	SeatComponent* FindSeat(HexEngine::Scene* scene, const std::string& name)
	{
		auto* e = (scene != nullptr && !name.empty()) ? scene->GetEntityByName(name) : nullptr;
		return (e != nullptr && !e->IsPendingDeletion()) ? e->GetComponent<SeatComponent>() : nullptr;
	}
}

const CityRoutineSystemComponent::LocomotionClips* CityRoutineSystemComponent::GetAgentClips(HexEngine::Entity* entity)
{
	auto* skel = entity != nullptr ? entity->GetComponent<HexEngine::SkeletalAnimationComponent>() : nullptr;
	auto data = skel != nullptr ? skel->GetAnimationData() : nullptr;
	if (data == nullptr || data->_animations.empty())
		return nullptr;

	auto cached = _locomotionClipCache.find(data.get());
	if (cached == _locomotionClipCache.end())
	{
		// Exact clip names first, then a loose match ("walk" but not a strafe).
		LocomotionClips clips;
		const auto& anims = data->_animations;
		for (int32_t i = 0; i < (int32_t)anims.size(); ++i)
		{
			const char* n = anims[i].name.c_str();
			if (clips.idle < 0 && _stricmp(n, _idleClipName.c_str()) == 0)
				clips.idle = i;
			if (clips.walk < 0 && _stricmp(n, _walkClipName.c_str()) == 0)
				clips.walk = i;
			if (clips.sitDown < 0 && _stricmp(n, _sitDownClipName.c_str()) == 0)
				clips.sitDown = i;
			if (clips.sitIdle < 0 && _stricmp(n, _sitIdleClipName.c_str()) == 0)
				clips.sitIdle = i;
			if (clips.standUp < 0 && _stricmp(n, _standUpClipName.c_str()) == 0)
				clips.standUp = i;
		}
		for (int32_t i = 0; i < (int32_t)anims.size(); ++i)
		{
			if (clips.idle < 0 && ContainsNoCase(anims[i].name, "idle") && !ContainsNoCase(anims[i].name, "sit"))
				clips.idle = i;
			if (clips.walk < 0 && ContainsNoCase(anims[i].name, "walk") && !ContainsNoCase(anims[i].name, "strafe"))
				clips.walk = i;
		}
		if (clips.idle < 0 || clips.walk < 0)
			LOG_WARN("CityRoutineSystem: '%s' has no idle/walk clips (looked for '%s'/'%s'); locomotion animation disabled for it.", entity->GetName().c_str(), _idleClipName.c_str(), _walkClipName.c_str());
		cached = _locomotionClipCache.emplace(data.get(), clips).first;
	}
	return &cached->second;
}

bool CityRoutineSystemComponent::IsSitting(const RoutineAgentComponent* agent) const
{
	return agent != nullptr && agent->GetEntity() != nullptr && _sitStates.count(agent->GetEntity()->GetName()) != 0;
}

void CityRoutineSystemComponent::RequestStandUp(RoutineAgentComponent* agent)
{
	if (agent == nullptr || agent->GetEntity() == nullptr)
		return;
	if (auto it = _sitStates.find(agent->GetEntity()->GetName()); it != _sitStates.end())
		it->second.standRequested = true;
}

bool CityRoutineSystemComponent::TryBeginSit(RoutineAgentComponent* agent, const math::Vector3& nearPoint, float radius, float minSeconds, float maxSeconds, bool transit)
{
	if (!_sitEnabled || agent == nullptr || agent->GetEntity() == nullptr || IsSitting(agent))
		return false;
	auto* entity = agent->GetEntity();
	auto* scene = entity->GetScene();
	auto* nav = entity->GetComponent<HexEngine::NavigationComponent>();
	const LocomotionClips* clips = GetAgentClips(entity);
	if (scene == nullptr || nav == nullptr || clips == nullptr || clips->sitDown < 0 || clips->sitIdle < 0 || clips->standUp < 0)
	{
		if (_sitDebugLog)
			LOG_INFO("Sit: '%s' can't sit (no nav / no sit clips)", entity->GetName().c_str());
		return false;
	}

	const math::Vector3 from = entity->GetWorldTM().Translation();
	struct Candidate { SeatComponent* seat; int32_t slot; float score; };
	std::vector<Candidate> candidates;
	std::vector<SeatComponent*> seats;
	scene->GetComponents<SeatComponent>(seats);
	for (auto* seat : seats)
	{
		if (seat == nullptr || !seat->IsEnabled() || seat->GetEntity() == nullptr || seat->GetEntity()->IsPendingDeletion())
			continue;
		const int32_t slot = seat->FindFreeSlot(nearPoint);
		if (slot < 0)
			continue;
		math::Vector3 pos, facing;
		if (!seat->GetSlot(slot, pos, facing))
			continue;
		if (std::fabs(pos.y - from.y) > 1.2f || FlatDistance(pos, nearPoint) > radius)
			continue;
		// Nearest first, with a little randomness so a crowd doesn't all pick the same bench.
		candidates.push_back({ seat, slot, FlatDistance(pos, from) + OnSiteRand01(_sitRng) * 4.0f });
	}
	if (candidates.empty())
	{
		if (_sitDebugLog)
			LOG_INFO("Sit: '%s' (%s) no free seat within %.1f m of (%.1f, %.1f, %.1f)", entity->GetName().c_str(), transit ? "transit" : "on site",
				radius, nearPoint.x, nearPoint.y, nearPoint.z);
		return false;
	}
	std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.score < b.score; });

	const int32_t attempts = std::min<int32_t>(3, (int32_t)candidates.size());
	for (int32_t c = 0; c < attempts; ++c)
	{
		SeatComponent* seat = candidates[c].seat;
		const int32_t slot = candidates[c].slot;
		math::Vector3 seatPos, facing;
		seat->GetSlot(slot, seatPos, facing);
		const math::Vector3 approach = seat->GetApproachPoint(slot);

		math::Vector3 standAt = approach;
		if (FlatDistance(from, approach) > 0.3f)
		{
			nav->ClearPath();
			nav->FindPath(scene->GetNavMeshId(), from, approach, 5.0f);
			if (!nav->HasPath())
			{
				if (_sitDebugLog)
					LOG_INFO("Sit: '%s' no path to '%s' slot %d", entity->GetName().c_str(), seat->GetEntity()->GetName().c_str(), slot);
				continue;
			}
			// The walk must end in front of the seat, on the same floor, without a long detour.
			const auto& pts = nav->GetPathPoints();
			float length = 0.0f;
			math::Vector3 prev = from;
			for (const auto& p : pts) { length += (p - prev).Length(); prev = p; }
			const math::Vector3 end = pts.back();
			// On site: a short walk within the venue, not out of the door and round the block.
			const float maxLength = transit ? radius * 2.5f + 5.0f : radius * 1.6f + 3.0f;
			if (FlatDistance(end, approach) > 0.7f || std::fabs(end.y - approach.y) > 0.9f || length > maxLength)
			{
				if (_sitDebugLog)
					LOG_INFO("Sit: '%s' path to '%s' slot %d rejected (end gap %.2f m, dy %.2f, length %.1f m)", entity->GetName().c_str(),
						seat->GetEntity()->GetName().c_str(), slot, FlatDistance(end, approach), end.y - approach.y, length);
				nav->ClearPath();
				continue;
			}
			standAt = end;
		}
		if (!seat->Reserve(slot, entity->GetName()))
		{
			nav->ClearPath();
			continue;
		}

		SitState st;
		st.seatEntity = seat->GetEntity()->GetName();
		st.slot = slot;
		st.seat = seatPos;
		st.approach = standAt;
		st.facing = facing;
		st.transit = transit;
		st.lift = seat->GetSeatLift() + _sitHeightOffset;
		st.seat.y += _sitHeightOffset;	// GetSlot already includes the seat's own lift
		st.seatedFor = minSeconds + OnSiteRand01(_sitRng) * std::max(0.0f, maxSeconds - minSeconds);
		if (agent->GetWalkSpeed() > 0.0f)
			nav->SetMovementSpeed(agent->GetWalkSpeed() * 0.8f);
		agent->ConsumeNavigationReached();   // clear any stale arrival before this walk
		_sitStates[entity->GetName()] = st;
		if (_sitDebugLog)
			LOG_INFO("Sit: '%s' (%s) heading for '%s' slot %d", entity->GetName().c_str(), transit ? "transit" : "on site", st.seatEntity.c_str(), slot);
		return true;
	}
	nav->ClearPath();
	return false;
}

bool CityRoutineSystemComponent::UpdateSit(RoutineAgentComponent* agent, float frameTime)
{
	if (agent == nullptr || agent->GetEntity() == nullptr)
		return false;
	auto* entity = agent->GetEntity();
	auto it = _sitStates.find(entity->GetName());
	if (it == _sitStates.end())
		return false;
	SitState& st = it->second;

	auto* skel = entity->GetComponent<HexEngine::SkeletalAnimationComponent>();
	const LocomotionClips* clips = GetAgentClips(entity);
	SeatComponent* seat = FindSeat(entity->GetScene(), st.seatEntity);
	// Hidden, knocked down (ragdoll), or the seat went away: drop it where we are.
	if (skel == nullptr || clips == nullptr || seat == nullptr || !seat->IsEnabled() ||
		entity->HasFlag(HexEngine::EntityFlags::DoNotRender) || skel->HasPhysicsPose())
	{
		AbortSit(agent, false, "hidden, ragdoll or seat gone");
		return false;
	}

	st.timer += frameTime;
	const math::Vector3 pos = entity->GetWorldTM().Translation();
	auto* nav = entity->GetComponent<HexEngine::NavigationComponent>();

	switch (st.phase)
	{
	case SitState::Phase::Approach:
	{
		if (st.standRequested)
		{
			// Never sat down: just go.
			AbortSit(agent, false, "stand requested before sitting");
			return false;
		}
		const bool arrived = agent->ConsumeNavigationReached() || FlatDistance(pos, st.approach) < 0.35f ||
			(nav != nullptr && !nav->HasPath() && FlatDistance(pos, st.approach) < 1.0f);
		if (!arrived)
		{
			if (st.timer > 25.0f || (nav != nullptr && !nav->HasPath()))
			{
				if (_sitDebugLog)
					LOG_INFO("Sit: '%s' walk to seat stopped %.2f m short (path %s, %.1f s)", entity->GetName().c_str(),
						FlatDistance(pos, st.approach), (nav != nullptr && nav->HasPath()) ? "still set" : "gone", st.timer);
				AbortSit(agent, false, "walk ended short / timed out");
				return false;
			}
			return true;
		}
		if (nav != nullptr)
			nav->ClearPath();
		// Back to the seat, then sit back onto it. Heights come from the SEAT (the floor
		// under its meshes + its lift), not from the walker: the nav ground probe can
		// stand an agent on the front edge of the seat it walked up to, and seating
		// from that height floated people half a metre above waiting-room chairs.
		{
			const float floorY = st.seat.y - st.lift;
			if (std::fabs(floorY - pos.y) > 1.0f)
				st.seat.y = pos.y + st.lift;	// seat bounds disagree with the floor (odd prop): trust the walker
			st.approach = pos;
			st.approach.y = st.seat.y - st.lift;
		}
		entity->ForceRotation(math::Quaternion::CreateFromYawPitchRoll(std::atan2(st.facing.x, st.facing.z), 0.0f, 0.0f));
		SetAgentCollider(entity, false);
		skel->PlayOneShot((uint32_t)clips->sitDown);
		st.transition = std::max(0.5f, skel->GetClipLengthSeconds((uint32_t)clips->sitDown));
		st.phase = SitState::Phase::SittingDown;
		st.timer = 0.0f;
		return true;
	}

	case SitState::Phase::SittingDown:
	{
		// The clip lowers the body in place (root motion is stripped), so slide the
		// root back onto the seat over the first part of it.
		const float s = SmoothStep01(st.timer / (st.transition * 0.75f));
		entity->ForcePosition(math::Vector3::Lerp(st.approach, st.seat, s));
		if (st.timer >= st.transition)
		{
			entity->ForcePosition(st.seat);
			skel->BlendToAnimationIndex((uint32_t)clips->sitIdle);
			st.phase = SitState::Phase::Seated;
			st.timer = 0.0f;
		}
		return true;
	}

	case SitState::Phase::Seated:
	{
		// Moved by something else (a save loaded, a teleport): let go quietly.
		if (FlatDistance(pos, st.seat) > 1.0f)
		{
			AbortSit(agent, false, "moved off the seat");
			return false;
		}
		if ((int32_t)skel->GetTargetAnimationIndex() != clips->sitIdle)
			skel->BlendToAnimationIndex((uint32_t)clips->sitIdle);
		if (st.standRequested || (!st.transit && st.timer >= st.seatedFor))
		{
			skel->PlayOneShot((uint32_t)clips->standUp);
			st.transition = std::max(0.5f, skel->GetClipLengthSeconds((uint32_t)clips->standUp));
			st.phase = SitState::Phase::StandingUp;
			st.timer = 0.0f;
		}
		return true;
	}

	case SitState::Phase::StandingUp:
	{
		// Weight comes forward over the feet in the middle of the clip.
		const float s = SmoothStep01((st.timer - st.transition * 0.15f) / (st.transition * 0.6f));
		entity->ForcePosition(math::Vector3::Lerp(st.seat, st.approach, s));
		if (st.timer >= st.transition)
		{
			entity->ForcePosition(st.approach);
			SetAgentCollider(entity, true);
			seat->Release(st.slot, entity->GetName());
			if (clips->idle >= 0)
				skel->BlendToAnimationIndex((uint32_t)clips->idle);
			// Linger a moment on the spot before the next stroll / seat.
			if (auto onSite = _onSiteStates.find(entity->GetName()); onSite != _onSiteStates.end())
			{
				onSite->second.walking = false;
				onSite->second.pauseSeconds = 3.0f + OnSiteRand01(_onSiteRng) * 6.0f;
			}
			_sitStates.erase(it);
			return false;
		}
		return true;
	}
	}
	return false;
}

void CityRoutineSystemComponent::AbortSit(RoutineAgentComponent* agent, bool moveToApproach, const char* reason)
{
	if (agent == nullptr || agent->GetEntity() == nullptr)
		return;
	auto* entity = agent->GetEntity();
	auto it = _sitStates.find(entity->GetName());
	if (it == _sitStates.end())
		return;
	const SitState st = it->second;
	_sitStates.erase(it);
	if (_sitDebugLog)
		LOG_INFO("Sit: '%s' dropped seat '%s' slot %d (phase %d, %.1f s in): %s", entity->GetName().c_str(), st.seatEntity.c_str(), st.slot, (int)st.phase, st.timer, reason != nullptr ? reason : "");

	if (auto* seat = FindSeat(entity->GetScene(), st.seatEntity))
		seat->Release(st.slot, entity->GetName());
	if (st.phase == SitState::Phase::Approach)
		return;	// still walking over: nothing else changed
	SetAgentCollider(entity, true);
	if (moveToApproach)
		entity->ForcePosition(st.approach);
	auto* skel = entity->GetComponent<HexEngine::SkeletalAnimationComponent>();
	const LocomotionClips* clips = GetAgentClips(entity);
	// A ragdoll owns the pose: leave its clip alone.
	if (skel != nullptr && clips != nullptr && clips->idle >= 0 && !skel->HasPhysicsPose())
		skel->BlendToAnimationIndex((uint32_t)clips->idle);
}

void CityRoutineSystemComponent::ReleaseAllSits(bool restoreColliders)
{
	auto* scene = GetEntity() != nullptr ? GetEntity()->GetScene() : nullptr;
	if (scene != nullptr)
	{
		if (restoreColliders)
		{
			for (const auto& [name, st] : _sitStates)
			{
				if (st.phase == SitState::Phase::Approach)
					continue;
				if (auto* e = scene->GetEntityByName(name); e != nullptr && !e->IsPendingDeletion())
					SetAgentCollider(e, true);
			}
		}
		std::vector<SeatComponent*> seats;
		scene->GetComponents<SeatComponent>(seats);
		for (auto* seat : seats)
			if (seat != nullptr)
				seat->ReleaseAll();
	}
	_sitStates.clear();
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

bool CityRoutineSystemComponent::PlanTransitTrip(RoutineAgentComponent* agent, const math::Vector3& from, const math::Vector3& to, RoutineTaskSpec& task) const
{
	auto* scene = agent->GetEntity()->GetScene();
	if (scene == nullptr)
		return false;
	std::vector<TransitLineComponent*> lines;
	scene->GetComponents<TransitLineComponent>(lines);
	if (lines.empty())
		return false;

	// Times in seconds. Walking is costed at 1.3x the straight line (streets
	// aren't straight); waiting at half a typical headway.
	const float walkSpeed = std::max(0.5f, agent->GetWalkSpeed());
	const float walkDirect = (to - from).Length() * 1.3f / walkSpeed;
	float bestCost = walkDirect * 0.85f;	// must clearly beat walking
	bool found = false;

	for (auto* line : lines)
	{
		if (line == nullptr || line->GetEntity() == nullptr || line->GetStopCount() < 2)
			continue;
		const int32_t n = line->GetStopCount();
		for (int32_t a = 0; a < n; ++a)
		{
			auto* stopA = line->GetStop(a);
			if (stopA == nullptr)
				continue;
			const float walkA = (stopA->GetWaitCentre() - from).Length();
			if (walkA > _transitMaxWalkToStop)
				continue;
			for (int32_t b = 0; b < n; ++b)
			{
				if (b == a)
					continue;
				auto* stopB = line->GetStop(b);
				if (stopB == nullptr)
					continue;
				const float walkB = (to - stopB->GetWaitCentre()).Length();
				if (walkB > _transitMaxWalkToStop)
					continue;
				float ride = line->RideDistance(a, 1, b);
				if (!line->IsClosedLoop())
					ride = std::min(ride, line->RideDistance(a, -1, b));
				if (ride >= 1e20f)
					continue;
				const float cost = (walkA + walkB) * 1.3f / walkSpeed + 60.0f + ride / line->GetPlanningSpeed();
				if (cost < bestCost)
				{
					bestCost = cost;
					found = true;
					task.transitLine = line->GetEntity()->GetName();
					task.transitBoardStop = stopA->GetEntity()->GetName();
					task.transitAlightStop = stopB->GetEntity()->GetName();
				}
			}
		}
	}
	return found;
}

void CityRoutineSystemComponent::CleanupAgentTransit(RoutineAgentComponent* agent)
{
	if (agent == nullptr || agent->GetEntity() == nullptr)
		return;
	const auto phase = agent->GetTransitPhase();
	if (phase == RoutineAgentComponent::TransitPhase::None)
		return;
	auto* scene = agent->GetEntity()->GetScene();
	if (scene != nullptr)
	{
		const auto& active = agent->GetActiveTask();
		if (active.has_value() && !active->transitBoardStop.empty())
		{
			if (auto* stopEntity = scene->GetEntityByName(active->transitBoardStop))
				if (auto* stop = stopEntity->GetComponent<TransitStopComponent>())
					stop->RemoveWaiting(agent->GetEntity()->GetName());
		}
		if (!agent->GetTransitVehicleName().empty())
		{
			if (auto* vehicleEntity = scene->GetEntityByName(agent->GetTransitVehicleName()))
				if (auto* vehicle = vehicleEntity->GetComponent<TransitVehicleComponent>())
					vehicle->AlightAgent(agent);
		}
	}
	agent->SetTransitVehicleName(std::string());
	agent->SetTransitPhase(RoutineAgentComponent::TransitPhase::None);
}

void CityRoutineSystemComponent::AbandonTransit(RoutineAgentComponent* agent, const char* reason)
{
	LOG_DEBUG("CityRoutineSystem: '%s' gives up on public transport (%s) and walks", agent->GetEntity()->GetName().c_str(), reason);
	CleanupAgentTransit(agent);
	agent->ConsumeNavigationReached();
	agent->SetExecutionState(RoutineExecutionState::WalkToTaskEntry);
	agent->SetStateTimer(0.0f);
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
