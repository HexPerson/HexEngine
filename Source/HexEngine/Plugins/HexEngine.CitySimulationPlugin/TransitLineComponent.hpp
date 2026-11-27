#pragma once

#include <HexEngine.Core/HexEngine.hpp>

class TransitStopComponent;

// One public-transport line: an ordered list of TransitStop entities served by
// _vehicleCount vehicles spawned from _vehiclePrefab when the game starts
// (transient - never saved). The vehicles carry TransitVehicleComponent, which
// does the driving, the dwell at each stop, the doors and the passengers.
//
//  - BUS lines drive the traffic-lane network stop to stop (each stop names the
//    lane node the bus pulls up at), so buses obey signals and queue in traffic.
//  - TRAIN lines run along _trackPathEntityName: a path entity whose children are
//    the track's waypoints in name order (like PathMoverComponent). Each stop is
//    the point of the track closest to its stop entity.
//
// _closedLoop: after the last stop the line continues to the first (a circle);
// otherwise vehicles reverse at the ends (ping-pong).
class TransitLineComponent : public HexEngine::UpdateComponent
{
public:
	CREATE_COMPONENT_ID(TransitLineComponent);
	DEFINE_COMPONENT_CTOR(TransitLineComponent);

	enum class Mode : int32_t
	{
		Bus = 0,
		Train = 1,
	};

	virtual void Update(float frameTime) override;
	virtual void Serialize(json& data, HexEngine::JsonFile* file) override;
	virtual void Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask = 0) override;
	virtual bool CreateWidget(HexEngine::ComponentWidget* widget) override;
	virtual void OnRenderEditorGizmo(bool isSelected, bool& isHovering) override;

	Mode GetMode() const { return (Mode)_mode; }
	const std::string& GetDisplayName() const;
	int32_t GetStopCount() const { return (int32_t)_stops.size(); }
	const std::string& GetStopEntityName(int32_t index) const;
	TransitStopComponent* GetStop(int32_t index) const;
	int32_t FindStopIndex(const std::string& stopEntityName) const;
	bool IsClosedLoop() const { return _closedLoop; }
	float GetDwellSeconds() const { return _dwellSeconds; }
	float GetTrainSpeed() const { return _trainSpeed; }
	float GetTrainAcceleration() const { return _trainAcceleration; }
	float GetTrainDeceleration() const { return _trainDeceleration; }
	// Average speed used to plan trips (includes stops).
	float GetPlanningSpeed() const { return _mode == (int32_t)Mode::Train ? 11.0f : 6.5f; }

	// The stop after `index` travelling in `direction` (+1/-1); reverses the
	// direction at the ends of a ping-pong line.
	int32_t NextStop(int32_t index, int32_t& direction) const;
	// Will a vehicle at stop `from` heading `direction` reach `to` before
	// turning round? (Always true on a loop.)
	bool IsAhead(int32_t from, int32_t direction, int32_t to) const;
	// Straight-line length of the ride from one stop to another in that direction.
	float RideDistance(int32_t from, int32_t direction, int32_t to) const;

	// Trains: the track polyline (world) and each stop's distance along it.
	bool EnsureTrack();
	const std::vector<math::Vector3>& GetTrackPoints() const { return _trackPoints; }
	float GetTrackLength() const { return _trackLength; }
	float GetStopTrackDistance(int32_t index) const;
	math::Vector3 SampleTrack(float distance) const;

	// Bus: the lane node(s) a bus can pull up at for this stop (resolved once;
	// the first is the default).
	const std::string& GetStopLaneWaypoint(int32_t index);
	std::vector<std::string> GetStopLaneWaypointCandidates(int32_t index);

	// Every vehicle of every line, for the HUD / player boarding.
	static TransitLineComponent* FindLine(HexEngine::Scene* scene, const std::string& lineEntityName);

private:
	void RebuildStops();
	void SpawnVehicles();

	std::string _displayName;
	int32_t _mode = (int32_t)Mode::Bus;
	std::string _stopsCsv;
	bool _closedLoop = true;
	std::string _vehiclePrefab;
	int32_t _vehicleCount = 2;
	float _dwellSeconds = 10.0f;
	std::string _trackPathEntityName;
	float _trainSpeed = 16.0f;
	float _trainAcceleration = 0.9f;
	float _trainDeceleration = 1.1f;
	int32_t _carriagesPerTrain = 1;
	float _carriageSpacing = 16.0f;	// centre-to-centre along the track
	bool _enabled = true;

	// runtime
	std::vector<std::string> _stops;
	std::vector<std::string> _stopLaneWaypoints;
	std::vector<math::Vector3> _trackPoints;
	std::vector<float> _trackCumulative;
	std::vector<float> _stopTrackDistances;
	float _trackLength = 0.0f;
	bool _trackBuilt = false;
	bool _spawned = false;
	std::vector<std::string> _vehicleNames;
};
