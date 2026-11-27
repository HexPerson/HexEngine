#pragma once

#include <HexEngine.Core/HexEngine.hpp>

class TransitLineComponent;
class TransitStopComponent;
class RoutineAgentComponent;
namespace HexEngine { class IRigidBody; class SoundEffect; }

// A bus or train carriage in service on a TransitLineComponent (added to the
// line's spawned vehicles at play start; the cabin layout below is authored on
// the vehicle prefab).
//
// Driving: a bus drives the traffic-lane network stop to stop through
// TrafficVehicleComponent waypoint routes; a train runs its line's track with an
// accelerate / cruise / brake profile. At each stop the vehicle DWELLS: the
// doors open, riders for this stop get off, people waiting here for this line
// get on, then the doors close and it leaves.
//
// The cabin is WALKABLE: _cabinBoxes (floor, walls, roof, steps, seat blocks)
// form a kinematic collider that moves with the vehicle, so the player's
// controller rides it (FirstPersonCameraController carries whoever stands on a
// kinematic body). _doorBoxes fill the doorways while the doors are closed.
// Boxes are 6 floats each: local min xyz then max xyz (unscaled entity space).
//
// Riding NPCs stand at _standSpots (3 floats each, local) and are moved with the
// vehicle every frame after it moves.
class TransitVehicleComponent : public HexEngine::UpdateComponent
{
public:
	CREATE_COMPONENT_ID(TransitVehicleComponent);
	DEFINE_COMPONENT_CTOR(TransitVehicleComponent);
	virtual ~TransitVehicleComponent();

	enum class State : int32_t
	{
		Idle = 0,
		Driving,
		Dwelling,
		Departing,
	};

	virtual void Destroy() override;
	virtual void Update(float frameTime) override;
	virtual bool WantsPostUpdate() const override { return _bodiesBuilt || !_passengers.empty(); }
	virtual void PostUpdate(float frameTime) override;
	virtual void Serialize(json& data, HexEngine::JsonFile* file) override;
	virtual void Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask = 0) override;
	virtual bool CreateWidget(HexEngine::ComponentWidget* widget) override;
	virtual void OnRenderEditorGizmo(bool isSelected, bool& isHovering) override;

	// Line hookup (TransitLineComponent::SpawnVehicles).
	void AssignToLine(const std::string& lineEntityName, int32_t stopIndex, int32_t direction, int32_t ordinal);
	// A trailing train carriage: follows `leadEntityName` along the track,
	// `index` carriages behind it, and mirrors its doors.
	void AssignAsCarriage(const std::string& leadEntityName, int32_t index, float spacing);

	const std::string& GetLineEntityName() const { return _lineEntityName; }
	State GetState() const { return _state; }
	bool IsDwelling() const { return _state == State::Dwelling; }
	float GetDoorOpenAmount() const { return _doorOpen; }
	int32_t GetOpenSide() const { return _openSide; }
	// The stop it is at (dwelling) or last left, and the one it is heading to.
	int32_t GetStopIndex() const { return _stopIndex; }
	int32_t GetNextStopIndex() const { return _nextStopIndex; }
	int32_t GetDirection() const { return _direction; }
	float GetTrackDistance() const { return _trackDistance; }
	TransitVehicleComponent* GetLead() const;

	// Passengers (routine agents).
	int32_t GetCapacity() const { return _capacity; }
	int32_t GetPassengerCount() const { return (int32_t)_passengers.size(); }
	bool BoardAgent(RoutineAgentComponent* agent, const std::string& alightStopName);
	void AlightAgent(RoutineAgentComponent* agent);
	// Where a boarding passenger walks to / an alighting one is put: just
	// outside the door nearest `from` (world).
	math::Vector3 GetDoorExitWorld(const math::Vector3& from) const;

	// Is the local player standing in this vehicle (any carriage of a train)?
	bool IsPlayerAboard() const { return _playerAboard; }
	static TransitVehicleComponent* FindPlayerVehicle(HexEngine::Scene* scene);

private:
	TransitLineComponent* GetLine() const;
	void EnsureBodies();
	void DestroyBodies();
	void CollectDoorMeshes();
	void ApplyDoorMeshes();
	void BeginLeg();
	void ArriveAtStop();
	void UpdateTrainMotion(float dt);
	void PlaceOnTrack(float distance);
	void ProcessStopArrival();
	void UpdatePassengers();
	bool IsPlayerInDoorway() const;
	static bool ParseBox(const std::vector<float>& v, size_t i, math::Vector3& mn, math::Vector3& mx);

	// --- authored (prefab) ---
	std::vector<float> _cabinBoxes;
	std::vector<float> _doorBoxes;
	std::vector<float> _doorExits;		// 3 per door: local point just outside it
	std::vector<float> _standSpots;		// 3 per spot: local floor position
	std::vector<float> _doorSlides;		// 3 per door mesh: local offset when fully open
	std::string _doorMeshPrefix = "VehicleGen_Door";
	// Alternative to _doorMeshPrefix + _doorSlides for models whose door parts
	// keep their mesh names (kit trains): "namePrefix=dx,dy,dz" per entry; every
	// child whose name starts with namePrefix slides by that local offset.
	std::vector<std::string> _doorPartSlides;
	int32_t _capacity = 30;
	float _doorOpenSeconds = 0.9f;
	float _yawOffsetDegrees = 0.0f;		// trains: model forward vs the track direction
	float _trackHeightOffset = 0.0f;	// trains: root height above the track points
	float _halfLength = 6.0f;			// trains: bogie half-spacing for the heading

	// --- runtime ---
	std::string _lineEntityName;
	std::string _currentNodeName;		// bus: the lane node it is stopped at
	std::string _targetNodeName;		// bus: the lane node it is heading for
	std::string _leadEntityName;		// trailing carriage: the lead's name
	int32_t _carriageIndex = 0;
	float _carriageSpacing = 0.0f;
	int32_t _stopIndex = -1;
	int32_t _nextStopIndex = -1;
	int32_t _direction = 1;
	int32_t _ordinal = 0;
	State _state = State::Idle;
	State _loggedState = State::Idle;
	float _stateTime = 0.0f;
	bool _arrivalProcessed = false;
	float _doorOpen = 0.0f;
	float _doorTarget = 0.0f;
	float _trackDistance = 0.0f;
	float _speed = 0.0f;
	bool _bodiesBuilt = false;
	HexEngine::IRigidBody* _cabinBody = nullptr;
	HexEngine::IRigidBody* _doorBody = nullptr;		// doors on the vehicle's -X side
	HexEngine::IRigidBody* _doorBodyPos = nullptr;	// doors on the +X side
	int32_t _openSide = 0;	// side facing the platform at this stop: -1 / +1 (0 = both)
	bool _giExcluded = false;
	bool _wheelsUnmirrored = false;
	bool _playerAboard = false;
	bool _wasPlayerAboard = false;
	// Sounds (GameData paths; a comma list = random takes). Doors are 3D at the
	// vehicle, the stop chime is 2D and only for a rider.
	std::string _doorOpenSoundPath;
	std::string _doorCloseSoundPath;
	std::string _stopChimePath;
	float _soundVolume = 0.8f;
	float _lastDoorTarget = -1.0f;	// -1 = not seen yet (no sound on spawn)
	std::vector<std::shared_ptr<HexEngine::SoundEffect>> _activeSounds;
	void PlayVehicleSound(const std::string& paths, bool positional);
	float _stuckTime = 0.0f;
	int32_t _reopenCount = 0;
	math::Vector3 _lastPosition = math::Vector3::Zero;

	struct DoorMesh
	{
		std::string entityName;
		math::Vector3 closedLocal;
		math::Vector3 slide;
	};
	std::vector<DoorMesh> _doorMeshes;
	bool _doorMeshesCollected = false;

	struct Passenger
	{
		std::string agentName;
		std::string alightStopName;
		int32_t spot = 0;
		float yaw = 0.0f;
	};
	std::vector<Passenger> _passengers;
	std::vector<std::string> _pendingBoarders;
};
