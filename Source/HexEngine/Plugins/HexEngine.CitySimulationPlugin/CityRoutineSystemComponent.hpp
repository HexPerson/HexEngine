#pragma once

#include "ITaskTravelService.hpp"
#include <HexEngine.Core/HexEngine.hpp>
#include <unordered_set>

class PlaceOfWorkComponent;
class ResidenceComponent;
class TrafficVehicleComponent;
class ParkingLotComponent;
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
	// Lane nodes a car may stop on / pull out from: one way in and one way out, not a
	// bus stop, well clear of any junction node. Trips used to start and end on the
	// lane node nearest a building - often a junction or a bus stop - and stood there.
	std::string FindNearestQuietWaypointName(const math::Vector3& worldPosition);
	void FindQuietWaypointsNear(const math::Vector3& worldPosition, size_t maxCount, float maxDistance, std::vector<std::string>& out);
	bool IsLaneNode(const std::string& name);
	CityEmergencyDispatcherSystemComponent* GetDispatcherSystem() const;
	// abandoned: the drive failed mid-road (stuck / timed out). The car must not
	// stay there: an own car is returned to its driveway, a spawned one removed.
	void CleanupAgentVehicle(RoutineAgentComponent* agent, bool abandoned = false);

	// Parking (ParkingLotComponent car parks and house driveways).
	struct BayRef
	{
		std::string lot;		// entity with a ParkingLotComponent
		int32_t bay = -1;
		bool IsValid() const { return bay >= 0 && !lot.empty(); }
	};
	ParkingLotComponent* ResolveLot(const std::string& lotEntityName) const;
	ParkingLotComponent* FindHomeLot(RoutineAgentComponent* agent) const;
	// Pick and reserve a bay for a car arriving near `destination`: the household's
	// driveway on a trip home, else the nearest public car park with room.
	BayRef ReserveDestinationBay(RoutineAgentComponent* agent, const RoutineTaskSpec& task, HexEngine::Entity* car, const math::Vector3& destination);
	// The bay a parked car stands in (claiming its driveway bay on first use).
	BayRef GetCarBay(HexEngine::Entity* car);
	void ReleaseCarBay(const std::string& carName);
	void PlaceCarInBay(HexEngine::Entity* car, const BayRef& bay);
	// The spawned car an agent without one of its own parked earlier (reused).
	HexEngine::Entity* GetTempCar(const RoutineAgentComponent* agent, const math::Vector3& agentPos) const;
	void ResetParking();
	// A routine car still driving with nobody in it - restored from a save mid-trip
	// (the save keeps the car's route but not its driver), or orphaned some other way:
	// it would drive to its old route end (often a junction) and stand there for good.
	// Own cars are parked back home, spawned ones removed. Runs about once a second.
	void SweepDriverlessVehicles(float frameTime);
	float _driverlessSweepTimer = 0.0f;
	// Public transport.
	bool BeginWalkToPosition(RoutineAgentComponent* agent, const math::Vector3& to);
	bool PlanTransitTrip(RoutineAgentComponent* agent, const math::Vector3& from, const math::Vector3& to, RoutineTaskSpec& task) const;
	void CleanupAgentTransit(RoutineAgentComponent* agent);
	// Give up on the bus/train and walk the rest of the way.
	void AbandonTransit(RoutineAgentComponent* agent, const char* reason);
	bool UpdateWalkStuckRecovery(RoutineAgentComponent* agent, float frameTime, const std::string& waypointEntityName, const char* failureReason);
	// On-site behaviour: after arriving at a PlaceOfWork venue (shift or visit),
	// the agent strolls between random reachable spots near the venue entry
	// instead of freezing on the entry waypoint until its next schedule block.
	void BeginOnSite(RoutineAgentComponent* agent, const RoutineTaskSpec& completedTask);
	void UpdateOnSite(RoutineAgentComponent* agent, float frameTime);
	bool PickOnSiteSpot(RoutineAgentComponent* agent, const math::Vector3& anchor, math::Vector3& outSpot);

	// Sitting (SeatComponent benches/chairs). An agent lingering at a venue or
	// waiting at a transit stop may take a free seat nearby: walk to the spot in
	// front of it, sit back onto it (Stand To Sit), sit (Sitting Idle) and stand
	// up again (Sit To Stand) before doing anything else.
	bool TryBeginSit(RoutineAgentComponent* agent, const math::Vector3& nearPoint, float radius, float minSeconds, float maxSeconds, bool transit);
	// Advances the agent's sit; true while sitting owns the agent this frame.
	bool UpdateSit(RoutineAgentComponent* agent, float frameTime);
	bool IsSitting(const RoutineAgentComponent* agent) const;
	void RequestStandUp(RoutineAgentComponent* agent);
	// Drop the sit at once (no stand-up animation): seat released, collider back on.
	void AbortSit(RoutineAgentComponent* agent, bool moveToApproach, const char* reason = nullptr);
	void ReleaseAllSits(bool restoreColliders);

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
		// Walk stuck recovery that isn't working: re-paths without getting
		// anywhere (each re-path also reset the walk timeout, so it never
		// failed - agents looped for a game hour). Counted around an anchor.
		int32_t stuckRecoveries = 0;
		math::Vector3 stuckAnchor = math::Vector3::Zero;
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
		int32_t sitDown = -1;	// optional: bodies without them never sit
		int32_t sitIdle = -1;
		int32_t standUp = -1;
	};
	// Cached clip lookup for an agent's skeleton (null data = none).
	const LocomotionClips* GetAgentClips(HexEngine::Entity* entity);

	struct SitState
	{
		enum class Phase { Approach, SittingDown, Seated, StandingUp };
		Phase phase = Phase::Approach;
		std::string seatEntity;
		int32_t slot = -1;
		math::Vector3 seat = math::Vector3::Zero;		// seated root position
		math::Vector3 approach = math::Vector3::Zero;	// stand here, back to the seat, to sit
		math::Vector3 facing = math::Vector3(0.0f, 0.0f, 1.0f);
		float timer = 0.0f;			// time in the current phase
		float transition = 1.5f;	// length of the current sit-down / stand-up
		float seatedFor = 30.0f;	// planned sitting time
		float lift = 0.0f;			// seated root height above the walker's floor
		bool standRequested = false;
		bool transit = false;		// sitting while waiting for a bus/train
	};

	struct OnSiteState
	{
		math::Vector3 anchor = math::Vector3::Zero;	// venue entry waypoint (stroll centre)
		float pauseSeconds = 0.0f;				// time left standing at the current spot
		float walkSeconds = 0.0f;				// time spent on the current stroll (timeout)
		bool walking = false;
		int32_t pickFailures = 0;				// consecutive failed spot picks (retry backoff)
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
	// Public transport: trips longer than _walkMaxDistance may use a
	// TransitLine when walking to the stop is short and the ride saves time.
	bool _transitEnabled = true;
	float _transitMaxWalkToStop = 320.0f;	// straight-line, each end
	float _transitShareCarOwners = 0.25f;	// fraction of car owners who take it anyway
	float _transitMaxWaitSeconds = 600.0f;	// then give up and walk
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

	// Sitting.
	bool _sitEnabled = true;
	float _sitChanceOnSite = 0.6f;		// chance a lingering agent looks for a seat
	float _sitSearchRadius = 14.0f;		// metres around the venue entry
	float _sitMinSeconds = 20.0f;
	float _sitMaxSeconds = 90.0f;
	bool _sitAtTransitStops = true;
	float _transitSeatRadius = 7.0f;	// metres around the stop's waiting area
	float _sitHeightOffset = 0.0f;		// added to every seat's lift (global tuning)
	bool _sitDebugLog = false;			// log every seat attempt / outcome
	std::string _sitDownClipName = "Stand To Sit";
	std::string _sitIdleClipName = "Sitting Idle";
	std::string _standUpClipName = "Sit To Stand";
	std::unordered_map<std::string, SitState> _sitStates;	// runtime
	uint32_t _sitRng = 0x51A7u;

	// Parking (runtime).
	bool _parkingEnabled = true;
	bool _parkingDebugLog = false;
	std::unordered_map<std::string, BayRef> _carBays;		// car -> bay it is parked in
	std::unordered_map<std::string, BayRef> _tripBays;		// agent -> bay reserved for the trip's end
	std::unordered_map<std::string, std::string> _tempCars;	// agent -> spawned car it parked
	struct QuietNode { std::string name; math::Vector3 position; };
	std::vector<QuietNode> _quietNodes;
	std::unordered_set<std::string> _laneNodeNames;
	std::unordered_map<std::string, math::Vector3> _laneNodeDirections;	// direction of travel at a node
	bool _quietNodesBuilt = false;
	void EnsureQuietNodes();

	// Commute lead: agents set off for work early enough to ARRIVE when the shift
	// starts (they used to leave at the start and turned up an hour+ late, so job
	// pickups at their post sat waiting). The lead is estimated from distance and
	// the measured clock rate, then learned from each agent's real home->work trips.
	float EstimateCommuteHours(RoutineAgentComponent* agent, PlaceOfWorkComponent* workplace) const;
	struct CommuteTrip { float departHour = -1.0f; std::string target; bool fromHome = false; };
	std::unordered_map<std::string, CommuteTrip> _commuteTrips;		// agent -> work trip in progress
	std::unordered_map<std::string, float> _learnedCommuteHours;	// agent -> measured home->work, game hours
	float _gameHoursPerSecond = 0.0f;	// measured clock rate (game hours per second of frame time)
	float _commuteBufferHours = 0.25f;	// arrive this much before the shift
};