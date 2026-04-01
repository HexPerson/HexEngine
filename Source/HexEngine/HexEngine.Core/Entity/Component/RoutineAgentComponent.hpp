#pragma once

#include "UpdateComponent.hpp"
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace HexEngine
{
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
	};

	enum class RoutineTravelMode
	{
		DriveFirst,
		WalkOnly,
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
	};

	class HEX_API RoutineAgentComponent : public UpdateComponent
	{
	public:
		CREATE_COMPONENT_ID(RoutineAgentComponent);
		DEFINE_COMPONENT_CTOR(RoutineAgentComponent);

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(ComponentWidget* widget) override;
		virtual void OnMessage(Message* message, MessageListener* sender) override;

		const std::vector<std::string>& GetRoleTags() const { return _roleTags; }
		const std::string& GetRoleTagsCsv() const { return _roleTagsCsv; }
		const std::string& GetHomeEntityName() const { return _homeEntityName; }
		const std::string& GetAssignedWorkplaceEntityName() const { return _assignedWorkplaceEntityName; }
		const std::string& GetPreferredVehiclePrefabPath() const { return _preferredVehiclePrefabPath; }
		const std::string& GetCurrentVehicleEntityName() const { return _currentVehicleEntityName; }
		bool IsEmergencyEligible() const { return _isEmergencyEligible; }
		bool IsBusy() const { return _isBusy; }
		bool IsOnShift() const { return _isOnShift; }

		void SetAssignedWorkplaceEntityName(const std::string& value) { _assignedWorkplaceEntityName = value; }
		void SetCurrentVehicleEntityName(const std::string& value) { _currentVehicleEntityName = value; }
		void SetBusy(bool value) { _isBusy = value; }
		void SetOnShift(bool value) { _isOnShift = value; }

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

	private:
		void RebuildRoleTags();

	private:
		std::string _roleTagsCsv = "Citizen";
		std::vector<std::string> _roleTags;
		std::string _homeEntityName;
		std::string _assignedWorkplaceEntityName;
		std::string _preferredVehiclePrefabPath;
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
	};
}
