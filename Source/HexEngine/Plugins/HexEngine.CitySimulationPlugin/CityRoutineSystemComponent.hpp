#pragma once

#include "ITaskTravelService.hpp"
#include <HexEngine.Core/HexEngine.hpp>

class PlaceOfWorkComponent;
class ResidenceComponent;
class TrafficVehicleComponent;
class CityEmergencyDispatcherSystemComponent;

class CityRoutineSystemComponent : public HexEngine::UpdateComponent, public HexEngine::IEntityListener, public ITaskTravelService
{
public:
	CREATE_COMPONENT_ID(CityRoutineSystemComponent);
	DEFINE_COMPONENT_CTOR(CityRoutineSystemComponent);
	virtual ~CityRoutineSystemComponent();

	virtual void Destroy() override;
	virtual void Update(float frameTime) override;
	virtual void Serialize(json& data, HexEngine::JsonFile* file) override;
	virtual void Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask = 0) override;
	virtual bool CreateWidget(HexEngine::ComponentWidget* widget) override;
	virtual void OnDebugRender() override;

	virtual void OnAddEntity(HexEngine::Entity* entity) override {}
	virtual void OnRemoveEntity(HexEngine::Entity* entity) override;
	virtual void OnAddComponent(HexEngine::Entity* entity, BaseComponent* component) override {}
	virtual void OnRemoveComponent(HexEngine::Entity* entity, BaseComponent* component) override {}

	virtual bool EnqueueTask(HexEngine::Entity* agentEntity, const RoutineTaskSpec& spec) override;
	virtual bool RequestTravel(HexEngine::Entity* agentEntity, const std::string& destinationAnchorWaypointEntityName, RoutineTravelMode travelMode = RoutineTravelMode::DriveFirst) override;
	virtual bool CancelTask(HexEngine::Entity* agentEntity, const std::string& reason) override;

	uint32_t ReportEmergency(const math::Vector3& worldPosition, const std::string& requiredServiceTag = "Medical");
	bool ResolveEmergency(uint32_t incidentId);

	void SetNavMeshId(uint32_t id) { _navMeshId = id; }
	uint32_t GetNavMeshId() const { return _navMeshId; }

private:
	void RegisterEntityListener();
	void UnregisterEntityListener();
	float GetSimulationHour() const;
	void UpdateAgents(float frameTime, float simulationHour);
	void UpdateAgentState(RoutineAgentComponent* agent, float frameTime, float simulationHour, const std::unordered_map<std::string, int32_t>& workplaceOccupancy);
	void EnsureWorkplaceAssignment(RoutineAgentComponent* agent, float simulationHour, const std::unordered_map<std::string, int32_t>& workplaceOccupancy);
	void SeedBaselineTaskIfIdle(RoutineAgentComponent* agent, float simulationHour);
	// The task the agent's day schedule (or, without one, its shift) wants
	// right now. False = nothing to do (no home, etc).
	bool BuildScheduledTask(RoutineAgentComponent* agent, float simulationHour, RoutineTaskSpec& outTask);
	std::string PickVenue(RoutineAgentComponent* agent, const std::string& serviceTag, float simulationHour, uint64_t blockKey) const;
	RoutineTravelMode ChooseTravelMode(RoutineAgentComponent* agent, const math::Vector3& from, const math::Vector3& to) const;
	bool ResolveAnchorPosition(const std::string& entityName, math::Vector3& outPosition) const;
	float GetWalkTimeout(RoutineAgentComponent* agent);
	void UpdateLocomotionAnimation(RoutineAgentComponent* agent, float frameTime);
	bool BeginWalkTo(RoutineAgentComponent* agent, const std::string& waypointEntityName);
	HexEngine::Entity* AcquireVehicleForTask(RoutineAgentComponent* agent, RoutineTaskSpec& task);
	bool ConfigureVehicleRoute(HexEngine::Entity* vehicleEntity, RoutineAgentComponent* agent, const RoutineTaskSpec& task);
	void FinishTask(RoutineAgentComponent* agent, bool success);
	void HandleTaskRetryOrFailure(RoutineAgentComponent* agent, const std::string& failureReason);
	bool IsRoleMatch(const std::vector<std::string>& a, const std::vector<std::string>& b) const;
	std::string FindNearestWaypointName(const math::Vector3& worldPosition) const;
	CityEmergencyDispatcherSystemComponent* GetDispatcherSystem() const;
	void CleanupAgentVehicle(RoutineAgentComponent* agent);
	bool UpdateWalkStuckRecovery(RoutineAgentComponent* agent, float frameTime, const std::string& waypointEntityName, const char* failureReason);
	// On-site behaviour: after arriving at a PlaceOfWork venue (shift or visit),
	// the agent strolls between random reachable spots near the venue entry
	// instead of freezing on the entry waypoint until its next schedule block.
	void BeginOnSite(RoutineAgentComponent* agent, const RoutineTaskSpec& completedTask);
	void UpdateOnSite(RoutineAgentComponent* agent, float frameTime);
	bool PickOnSiteSpot(RoutineAgentComponent* agent, const math::Vector3& anchor, math::Vector3& outSpot);

	struct AgentMotionState
	{
		math::Vector3 lastPosition = math::Vector3::Zero;
		bool hasLastPosition = false;
		float lowMotionAccumSeconds = 0.0f;
		float repathCooldownSeconds = 0.0f;
		float walkTimeoutSeconds = 0.0f;	// per-walk, from the distance and walk speed
		math::Vector3 lastVehiclePosition = math::Vector3::Zero;
		bool hasLastVehiclePosition = false;
		float vehicleStillSeconds = 0.0f;	// how long the agent's car hasn't moved
	};

	struct AgentAnimState
	{
		math::Vector3 lastPosition = math::Vector3::Zero;
		bool hasLastPosition = false;
		float smoothedSpeed = 0.0f;
		int32_t requestedClip = -1;
	};

	struct LocomotionClips
	{
		int32_t idle = -1;
		int32_t walk = -1;
	};

	struct OnSiteState
	{
		math::Vector3 anchor = math::Vector3::Zero;	// venue entry waypoint (stroll centre)
		float pauseSeconds = 0.0f;				// time left standing at the current spot
		float walkSeconds = 0.0f;				// time spent on the current stroll (timeout)
		bool walking = false;
	};

private:
	bool _enabled = true;
	bool _logMissingClockWarning = true;
	uint32_t _navMeshId = 0;
	float _walkStateTimeoutSeconds = 45.0f;
	float _driveStateTimeoutSeconds = 240.0f;
	bool _drawSelectedAgentHud = true;
	bool _drawRoutineWalkPaths = false;
	bool _enableWalkStuckRecovery = true;
	float _walkStuckSpeedThreshold = 0.1f;
	float _walkStuckDurationSeconds = 2.5f;
	float _walkStuckRepathCooldownSeconds = 1.0f;
	bool _isListeningForEntityEvents = false;
	std::unordered_map<std::string, AgentMotionState> _agentMotionStates;

	// Schedules / travel / animation.
	float _walkMaxDistance = 150.0f;			// Auto travel: walk up to this straight-line distance, drive beyond
	std::string _defaultVehiclePrefabPath = "GameData.Prefabs/SmallCar01.hprefab";
	// Job-relevant commuter vehicles, so a driver's job reads from their car.
	// Ordered, first match wins. One rule per line:
	//   Role1,Role2[@Workplace1,Workplace2] = GameData.Prefabs/Vehicles/V_Sedan_01*.hprefab, ...
	// Roles match the agent's role tags ('*' = anyone); the optional @list matches the
	// assigned workplace entity name. Prefab entries may end their file name in '*'
	// to take every colour variant. Each agent always gets the same pick (hashed
	// from its entity name). The agent's own preferred prefab still wins.
	std::vector<std::string> _roleVehicleRules;
	// Agents WITHOUT a car of their own only get a spawned one for trips longer
	// than this (or emergencies / an explicit Drive preference); otherwise they walk.
	float _spawnVehicleMinDistance = 400.0f;
	// Owned cars (RoutineAgentComponent::GetOwnedVehicleEntityName), runtime only.
	struct OwnedCarState
	{
		math::Vector3 homePosition;
		math::Quaternion homeRotation;
		bool stored = false;		// hidden at a destination (implicit car park)
	};
	std::unordered_map<std::string, OwnedCarState> _ownedCars;
	HexEngine::Entity* GetOwnedCar(const class RoutineAgentComponent* agent) const;
	OwnedCarState& OwnedCarStateFor(HexEngine::Entity* car);
	bool IsOwnedCarAvailable(const class RoutineAgentComponent* agent, const math::Vector3& agentPos) const;
	void ParkOwnedCar(HexEngine::Entity* car, bool atHome);
	// A car left in the (implicit) car park somewhere else - e.g. abandoned when a
	// drive got stuck - is back on the driveway by the time its owner is home.
	void RehomeOwnedCarIfOwnerHome(RoutineAgentComponent* agent);
	void ResetOwnedCars();
	struct ParsedVehicleRule
	{
		std::vector<std::string> roles;
		std::vector<std::string> workplaces;
		std::vector<std::string> prefabs;	// wildcards expanded
	};
	std::vector<ParsedVehicleRule> _parsedVehicleRules;
	bool _vehicleRulesParsed = false;
	void ParseVehicleRules();
	std::string PickRoleVehiclePrefab(const class RoutineAgentComponent* agent);
	bool _hideAgentsAtDestination = false;		// legacy: vanish on arriving at work
	float _failedTaskCooldownSeconds = 15.0f;	// after a permanent failure, wait before re-seeding
	float _vehicleStuckSeconds = 20.0f;		// a car that hasn't moved this long is abandoned (agent walks on)
	bool _animateLocomotion = true;
	std::string _idleClipName = "idle";
	std::string _walkClipName = "walking";
	float _lastSimulationHour = -1.0f;
	bool _wasGameRunning = false;
	uint32_t _dayIndex = 0;
	std::unordered_map<std::string, AgentAnimState> _agentAnimStates;
	std::unordered_map<std::string, OnSiteState> _onSiteStates;
	bool _onSiteWander = true;			// stroll around the venue between schedule blocks
	float _onSiteWanderRadius = 5.0f;	// metres around the entry (stays inside kArrivalRadius = 8)
	uint32_t _onSiteRng = 0x9E3779B9u;
	std::unordered_map<const void*, LocomotionClips> _locomotionClipCache;
};