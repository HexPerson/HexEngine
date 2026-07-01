#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include <HexEngine.Core/Audio/SoundEffect.hpp>

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

public:
	// Spatial grid maintenance. TrafficManagerComponent::Update calls
	// RebuildSpatialGrid() once per frame; vehicle avoidance and spawn
	// clearance then query the grid via QueryNearbyVehicles(), avoiding
	// the previous O(N^2) full-list scan per vehicle per frame.
	static void RebuildSpatialGrid();
	static void QueryNearbyVehicles(const math::Vector3& center, float radius, std::vector<TrafficVehicleComponent*>& out);
	// Cheap count of live vehicles - replaces scene->GetComponents() per
	// frame in the manager's global-cap check.
	static size_t GetAliveVehicleCount();

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
	// Audio: lazy-loaded engine loop + on-demand honk one-shots. Both are
	// distance-culled - vehicles outside _audioCullDistance from the
	// camera don't hold a live audio voice. Engine pitch + volume track
	// vehicle speed for the classic accel/idle/cruise feel.
	void UpdateAudio(const math::Vector3& worldPos, float frameTime);
	void StartEngineSoundIfNeeded(const math::Vector3& worldPos);
	void StopEngineSound();
	void TryHonk(const math::Vector3& worldPos);

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

	// --- Audio ---
	// Asset paths (serialized). One engine clip, multiple honk variants
	// picked at random per honk so a traffic jam doesn't sound monotonic.
	std::string _engineSoundPath;
	std::vector<std::string> _honkSoundPaths;
	// Audible range. Vehicles further from the camera than _audioCullDistance
	// stop their engine sound (saves audio voices for distant traffic).
	float _audioCullDistance = 60.0f;
	float _engineSoundRadius = 35.0f;
	float _honkSoundRadius = 80.0f;
	// Engine pitch envelope in DirectX SEMI-OCTAVE units (range [-1, 1]):
	//   -1 = down one octave (half frequency)
	//    0 = original pitch
	//   +1 = up one octave (double frequency)
	// Internally `pitch * 12` semitones gets converted to a frequency
	// ratio - so 0.5 = 6 semitones = ratio ~1.41, audibly higher.
	// NOT a frequency multiplier (don't pass 0.85 / 1.55 like Unity).
	//
	// With _engineNumGears > 1, the pitch SWEEPS from idle to max within
	// each gear's speed band, then "shifts down" sharply at gear
	// boundaries. So a car accelerating from 0 to top speed cycles
	// idle->max N times instead of a single linear ramp - much more
	// realistic, and the pitch keeps moving even at constant cruise
	// speed (you sit inside one gear's pitch range rather than at the
	// max-pitch ceiling).
	float _engineIdlePitch = -0.50f;
	float _engineMaxPitch = 0.50f;
	float _engineIdleVolume = 0.25f;
	float _engineMaxVolume = 0.85f;
	// Number of simulated gears for the engine pitch envelope. 1 =
	// linear speed->pitch (old behaviour); 3-5 sounds like a realistic
	// gearbox; high values sound like a CVT / sci-fi engine.
	int32_t _engineNumGears = 4;
	// Damping factor for shift smoothing. Higher = snappier shift,
	// lower = softer transition. 0 = no smoothing (raw step). 12 is
	// a good "automatic-transmission" feel.
	float _engineShiftDamping = 12.0f;
	// Persistent smoothed pitch (runtime only) so gear shifts don't
	// crackle - we lerp the live pitch toward the target each frame.
	float _engineSmoothedPitch = 0.0f;
	// Honk trigger: vehicle must be stopped (or near-stopped) due to
	// avoidance for this long before honking. Cooldown prevents spam.
	float _honkBlockedThreshold = 2.0f;
	float _honkCooldownMin = 3.5f;
	float _honkCooldownMax = 8.0f;
	// Random per-vehicle "personality" multipliers initialised on spawn,
	// so traffic doesn't honk in perfect unison.
	float _honkPersonality = 1.0f;   // <1 = patient, >1 = honky
	float _enginePitchOffset = 0.0f; // small per-vehicle pitch shift

	// Cached each frame for audio. The vehicle's effective top speed
	// is min(_speed, laneSpeedLimit) when _useLaneSpeedLimit is on -
	// using _speed alone as the pitch denominator means cars on slow
	// lanes never reach max pitch even at their actual cruise speed.
	float _audioReferenceMaxSpeed = 5.0f;

	// Runtime audio state. `_engineSound` is the per-vehicle CLONE of
	// the cached master SoundEffect for this path - each vehicle needs
	// its own SoundEffectInstance so multiple cars can play the same
	// engine clip simultaneously without stomping each other's playback.
	// `_activeHonks` keeps fire-and-forget honk clones alive until they
	// finish (AudioManager holds only weak refs); pruned each frame.
	std::shared_ptr<HexEngine::SoundEffect> _engineSound;
	std::vector<std::shared_ptr<HexEngine::SoundEffect>> _activeHonks;
	bool _engineSoundPlaying = false;
	float _blockedTime = 0.0f;
	float _honkCooldown = 0.0f;

	// Set when the component is DESERIALIZED (loaded from a saved scene).
	// Spawner-created vehicles are explicitly placed onto the lane via
	// Entity::ForcePosition(lanePoints.front()); loaded vehicles never were,
	// so their saved/prefab transform can be anywhere - commonly buried under
	// the map. On the first Update where the lane resolves we snap the vehicle
	// onto the nearest lane point and then drive from there. Cleared after the
	// snap so it only happens once per load.
	bool _snapToLaneOnLoad = false;
};

