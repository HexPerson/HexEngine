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

private:
	void RegisterEntityListener();
	void UnregisterEntityListener();
	float GetSimulationHour() const;
	void UpdateAgents(float frameTime, float simulationHour);
	void UpdateAgentState(RoutineAgentComponent* agent, float frameTime, float simulationHour, const std::unordered_map<std::string, int32_t>& workplaceOccupancy);
	void EnsureWorkplaceAssignment(RoutineAgentComponent* agent, float simulationHour, const std::unordered_map<std::string, int32_t>& workplaceOccupancy);
	void SeedBaselineTaskIfIdle(RoutineAgentComponent* agent, float simulationHour);
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

	struct AgentMotionState
	{
		math::Vector3 lastPosition = math::Vector3::Zero;
		bool hasLastPosition = false;
		float lowMotionAccumSeconds = 0.0f;
		float repathCooldownSeconds = 0.0f;
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
};