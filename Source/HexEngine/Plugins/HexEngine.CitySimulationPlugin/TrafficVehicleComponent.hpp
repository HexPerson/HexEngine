#pragma once

#include <HexEngine.Core/HexEngine.hpp>

class TrafficLaneComponent;

class TrafficVehicleComponent : public HexEngine::UpdateComponent
{
public:
	CREATE_COMPONENT_ID(TrafficVehicleComponent);
	DEFINE_COMPONENT_CTOR(TrafficVehicleComponent);
	virtual ~TrafficVehicleComponent();

	virtual void Update(float frameTime) override;
	virtual void Serialize(json& data, HexEngine::JsonFile* file) override;
	virtual void Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask = 0) override;
	virtual bool CreateWidget(HexEngine::ComponentWidget* widget) override;
	virtual void OnRenderEditorGizmo(bool isSelected, bool& isHovering) override;

	void SetLaneEntityName(const std::string& laneEntityName);
	const std::string& GetLaneEntityName() const { return _laneEntityName; }
	void RestartPath();
	void SetUseWaypointRoute(bool enabled) { _useWaypointRoute = enabled; }
	void SetWaypointRouteEndpoints(const std::string& startWaypointEntityName, const std::string& destinationWaypointEntityName);
	void SetDespawnAtRouteEnd(bool enabled) { _despawnAtRouteEnd = enabled; }
	bool ConsumeRouteEndReachedEvent();

private:
	bool GatherPlannedRoute(std::vector<math::Vector3>& outPoints);
	bool BuildWaypointRoute(std::vector<math::Vector3>& outPoints) const;
	bool AdvancePlannedRouteIndex(size_t numPoints);
	TrafficLaneComponent* ResolveLane();
	bool GatherLanePoints(std::vector<math::Vector3>& outPoints);
	bool TrySwitchToConnectedLane();
	bool AdvanceTargetIndex(size_t numPoints);
	float ComputeAvoidanceSpeed(const math::Vector3& currentPosition, const math::Vector3& moveDirection, float maxSpeed) const;

	static std::vector<TrafficVehicleComponent*> s_allVehicles;

private:
	std::string _laneEntityName;
	size_t _targetIndex = 0;
	float _speed = 5.0f;
	float _acceleration = 2.0f;
	float _rotationLerp = 9.0f;
	float _arrivalDistance = 1.0f;
	float _currentSpeed = 0.0f;
	bool _useLaneSpeedLimit = true;
	bool _invertDirection = false;
	bool _despawnAtRouteEnd = true;
	bool _useWaypointRoute = false;
	std::string _startWaypointEntityName;
	std::string _destinationWaypointEntityName;
	bool _drawDebug = true;
	bool _avoidanceEnabled = true;
	float _avoidanceLookAheadDistance = 10.0f;
	float _avoidanceFollowDistance = 3.0f;
	float _brakingStrength = 6.0f;
	bool _routeEndReachedEvent = false;
	std::vector<math::Vector3> _plannedRoutePoints;
};

