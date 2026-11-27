#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include <HexEngine.Core/Entity/Component/IRoutineAgentState.hpp>

enum class RoutineTaskType
{
	None,
	GoHome,
	GoToWork,
	RespondEmergency,
	CustomTravel,
};

enum class RoutineExecutionState
{
	Idle,
	WalkToVehicleAnchor,
	AcquireVehicle,
	DriveToDestinationParking,
	WalkToTaskEntry,
	ExecuteTask,
	ReturnHome,
	Failed,
	// Public transport legs (RoutineTravelMode::Transit).
	WalkToTransitStop,
	WaitForTransit,
	RideTransit,
};

enum class RoutineTravelMode
{
	DriveFirst,
	WalkOnly,
	Transit,	// walk to a stop, ride a TransitLine, walk the rest
};

// How an agent picks between walking and driving for a trip.
enum class RoutineTravelPreference : int32_t
{
	Auto = 0,	// walk trips up to the routine system's walk distance, drive longer ones
	Walk = 1,
	Drive = 2,
};

// One block of an agent's day. activity is "Home", "Work", or a visitor
// service tag offered by PlaceOfWork venues ("Food", "Shop", "Leisure", ...).
struct RoutineScheduleBlock
{
	float start = 0.0f;
	float end = 24.0f;
	std::string activity;
};

struct RoutineTaskSpec
{
	RoutineTaskType type = RoutineTaskType::None;
	RoutineTravelMode travelMode = RoutineTravelMode::DriveFirst;
	std::string targetEntityName;
	std::string targetEntryWaypointName;
	std::string targetParkingWaypointName;
	std::string sourceParkingWaypointName;
	std::string reason;
	int32_t priority = 0;
	int32_t maxRetries = 2;
	int32_t retries = 0;
	int32_t rerouteAttempts = 0;
	int32_t maxReroutes = 1;
	float retryDelaySeconds = 2.0f;
	bool preemptive = false;
	bool autoResolveOnArrival = true;
	uint32_t incidentId = 0;
	// Transit trips: the line entity and the stop entities to board / alight at.
	std::string transitLine;
	std::string transitBoardStop;
	std::string transitAlightStop;
};

class RoutineAgentComponent : public HexEngine::UpdateComponent, public HexEngine::IRoutineAgentState
{
public:
	CREATE_COMPONENT_ID(RoutineAgentComponent);
	DEFINE_COMPONENT_CTOR(RoutineAgentComponent);

	virtual void Serialize(json& data, HexEngine::JsonFile* file) override;
	virtual void Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask = 0) override;
	virtual bool CreateWidget(HexEngine::ComponentWidget* widget) override;
	virtual void OnMessage(HexEngine::Message* message, MessageListener* sender) override;

	const std::vector<std::string>& GetRoleTags() const { return _roleTags; }
	const std::string& GetRoleTagsCsv() const { return _roleTagsCsv; }
	virtual const std::string& GetHomeEntityName() const override { return _homeEntityName; }
	virtual const std::string& GetAssignedWorkplaceEntityName() const override { return _assignedWorkplaceEntityName; }
	const std::string& GetPreferredVehiclePrefabPath() const { return _preferredVehiclePrefabPath; }
	// The agent's own car (an entity in the scene, normally parked on its home
	// driveway). Trips use it instead of spawning one.
	const std::string& GetOwnedVehicleEntityName() const { return _ownedVehicleEntityName; }
	void SetOwnedVehicleEntityName(const std::string& value) { _ownedVehicleEntityName = value; }
	const std::string& GetCurrentVehicleEntityName() const { return _currentVehicleEntityName; }
	bool IsEmergencyEligible() const { return _isEmergencyEligible; }
	bool IsBusy() const { return _isBusy; }
	bool IsOnShift() const { return _isOnShift; }

	// Day schedule: "start-end:Activity" entries separated by ',' or ';',
	// hours 0-24, wrapping allowed ("22-6:Home"). Empty = legacy behaviour
	// (at work during the workplace's shifts, otherwise home).
	const std::string& GetScheduleCsv() const { return _scheduleCsv; }
	const std::vector<RoutineScheduleBlock>& GetSchedule() const { return _schedule; }
	void SetScheduleCsv(const std::string& value) { _scheduleCsv = value; RebuildSchedule(); }
	RoutineTravelPreference GetTravelPreference() const { return (RoutineTravelPreference)_travelPreference; }
	float GetWalkSpeed() const { return _walkSpeed; }
	// Block boundaries shift by up to +/- half this many hours, deterministically
	// per agent and block, so a crowd does not move in lockstep.
	float GetScheduleJitterHours() const { return _scheduleJitterHours; }

	// Runtime: the venue chosen for the current schedule block (re-picked when
	// the block changes).
	uint64_t GetPlannedBlockKey() const { return _plannedBlockKey; }
	const std::string& GetPlannedTargetName() const { return _plannedTargetName; }
	void SetPlannedBlock(uint64_t key, const std::string& target) { _plannedBlockKey = key; _plannedTargetName = target; }
	float& SeedCooldownRef() { return _seedCooldown; }

	void SetAssignedWorkplaceEntityName(const std::string& value) { _assignedWorkplaceEntityName = value; }
	void SetCurrentVehicleEntityName(const std::string& value) { _currentVehicleEntityName = value; }
	void SetBusy(bool value) { _isBusy = value; }
	void SetOnShift(bool value) { _isOnShift = value; }
	void SetHomeEntityName(const std::string& value) { _homeEntityName = value; }
	void SetRoleTagsCsv(const std::string& value) { _roleTagsCsv = value; RebuildRoleTags(); }
	void SetPreferredVehiclePrefabPath(const std::string& value) { _preferredVehiclePrefabPath = value; }
	void SetEmergencyEligible(bool value) { _isEmergencyEligible = value; }

	void EnqueueTask(const RoutineTaskSpec& task);
	bool HasQueuedTasks() const { return !_taskQueue.empty(); }
	bool PeekTask(RoutineTaskSpec& outTask) const;
	bool TryPopTask(RoutineTaskSpec& outTask);
	void ClearQueuedTasks();
	void SetActiveTask(const std::optional<RoutineTaskSpec>& task) { _activeTask = task; }
	const std::optional<RoutineTaskSpec>& GetActiveTask() const { return _activeTask; }

	void SetExecutionState(RoutineExecutionState state) { _executionState = state; }
	RoutineExecutionState GetExecutionState() const { return _executionState; }
	void SetStateTimer(float seconds) { _stateTimer = seconds; }
	float& StateTimerRef() { return _stateTimer; }
	float& RetryTimerRef() { return _retryTimer; }
	void SuspendCurrentTask();
	bool RestoreSuspendedTask();
	void ClearSuspendedTask() { _suspendedTask.reset(); }

	bool ConsumeNavigationReached();

	// IRoutineAgentState: the venue the agent arrived at (set by the routine
	// system when a trip completes, cleared when the next trip starts) and the
	// game-side "stay here" hold.
	virtual const std::string& GetSettledVenueName() const override { return _settledVenueName; }
	void SetSettledVenueName(const std::string& venue) { _settledVenueName = venue; }
	virtual void SetHeldAtVenue(bool held) override { _heldAtVenue = held; }
	virtual bool IsHeldAtVenue() const override { return _heldAtVenue; }
	virtual void SetConversationPaused(bool paused) override;
	virtual bool IsConversationPaused() const override { return _conversationPaused; }
	virtual bool WorkplaceOffersService(const std::string& tag) const override;
	virtual bool IsScheduledAtPost(float hour) const override;

	// Public transport (runtime). The routine system walks the agent to the stop
	// (ToStop) and queues it there (Waiting); an arriving vehicle calls it to the
	// door (Boarding), takes it aboard (Riding) and lets it off (Alighted).
	enum class TransitPhase : int32_t
	{
		None = 0,
		ToStop,
		Waiting,
		Boarding,
		Riding,
		Alighted,
	};
	TransitPhase GetTransitPhase() const { return _transitPhase; }
	void SetTransitPhase(TransitPhase phase) { _transitPhase = phase; _transitPhaseTime = 0.0f; _transitWalkIssued = false; }
	float& TransitPhaseTimeRef() { return _transitPhaseTime; }
	bool IsTransitWalkIssued() const { return _transitWalkIssued; }
	void SetTransitWalkIssued(bool issued) { _transitWalkIssued = issued; }
	const std::string& GetTransitVehicleName() const { return _transitVehicleName; }
	void SetTransitVehicleName(const std::string& name) { _transitVehicleName = name; }
	void SetTransitBoardTarget(const math::Vector3& door, const std::string& alightStop) { _transitDoor = door; _transitAlightStop = alightStop; }
	const math::Vector3& GetTransitDoor() const { return _transitDoor; }
	const std::string& GetTransitAlightStop() const { return _transitAlightStop; }

private:
	void RebuildRoleTags();
	void RebuildSchedule();

private:
	std::string _roleTagsCsv = "Citizen";
	std::vector<std::string> _roleTags;
	std::string _homeEntityName;
	std::string _assignedWorkplaceEntityName;
	std::string _preferredVehiclePrefabPath;
	std::string _ownedVehicleEntityName;
	bool _isEmergencyEligible = false;

	bool _isBusy = false;
	bool _isOnShift = false;
	std::string _currentVehicleEntityName;
	std::deque<RoutineTaskSpec> _taskQueue;
	std::optional<RoutineTaskSpec> _activeTask;
	std::optional<RoutineTaskSpec> _suspendedTask;
	RoutineExecutionState _executionState = RoutineExecutionState::Idle;
	float _stateTimer = 0.0f;
	float _retryTimer = 0.0f;
	bool _navigationReachedFlag = false;

	std::string _scheduleCsv;
	std::vector<RoutineScheduleBlock> _schedule;
	int32_t _travelPreference = (int32_t)RoutineTravelPreference::Auto;
	float _walkSpeed = 1.6f;
	float _scheduleJitterHours = 0.5f;
	uint64_t _plannedBlockKey = ~0ull;
	std::string _plannedTargetName;
	float _seedCooldown = 0.0f;
	std::string _settledVenueName;	// runtime
	bool _heldAtVenue = false;		// runtime
	bool _conversationPaused = false;	// runtime
	TransitPhase _transitPhase = TransitPhase::None;	// runtime
	float _transitPhaseTime = 0.0f;
	bool _transitWalkIssued = false;
	std::string _transitVehicleName;
	std::string _transitAlightStop;
	math::Vector3 _transitDoor = math::Vector3::Zero;
};