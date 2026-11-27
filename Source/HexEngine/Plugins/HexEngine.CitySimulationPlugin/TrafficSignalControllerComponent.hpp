#pragma once

#include <HexEngine.Core/HexEngine.hpp>

// A signal-controlled junction. Sits at the junction centre. Each arm listed in
// _armsCsv (N/E/S/W in the entity's local frame: N = -Z, E = +X, S = +Z,
// W = -X) gets a stop line _stopLineDistance out along it.
//
// Split phasing: arms take turns at green in _armsCsv order (green -> amber ->
// all-red), then a pedestrian stage keeps every arm at red while the crossings
// in _crossingsCsv show WALK. One approach moving at a time means no turning
// conflicts, so vehicles need no give-way logic.
//
// Consumers:
//  - TrafficVehicleComponent asks ComputeVehicleSpeedCap() every frame and
//    brakes for its stop line on amber (when it can stop) and red.
//  - Walking agents are held at the kerb through NavigationComponent's
//    segment gate (IsPedestrianStepAllowed), installed by the plugin.
//  - TrafficSignalComponent heads read GetArmState()/IsPedestrianWalk() to
//    swap their lit material / walk-don't-walk mesh.
class TrafficSignalControllerComponent : public HexEngine::UpdateComponent
{
public:
	CREATE_COMPONENT_ID(TrafficSignalControllerComponent);
	DEFINE_COMPONENT_CTOR(TrafficSignalControllerComponent);
	virtual ~TrafficSignalControllerComponent();

	enum class SignalState : int32_t
	{
		Red = 0,
		Amber = 1,
		Green = 2,
	};

	virtual void Update(float frameTime) override;
	virtual void Serialize(json& data, HexEngine::JsonFile* file) override;
	virtual void Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask = 0) override;
	virtual bool CreateWidget(HexEngine::ComponentWidget* widget) override;
	virtual void OnDebugRender() override;

	// State of one arm ('N', 'E', 'S', 'W'); arms this junction doesn't list are red.
	SignalState GetArmState(char arm) const;
	// True during the pedestrian stage (every arm red, crossings show WALK).
	bool IsPedestrianWalk() const;
	float GetCycleLength() const;

	// Speed limit a vehicle at `position`, travelling along `heading` at
	// `currentSpeed`, must respect for the signals ahead of it (maxSpeed when
	// unaffected). Sets outHeldBySignal when a signal is the reason it slows.
	static float ComputeVehicleSpeedCap(const math::Vector3& position, const math::Vector3& heading,
		float currentSpeed, float maxSpeed, float maxDeceleration, bool& outHeldBySignal);
	// NavigationComponent segment gate: false while `from` -> `to` steps onto a
	// controlled crossing that is showing DON'T WALK.
	static bool IsPedestrianStepAllowed(const math::Vector3& from, const math::Vector3& to);

private:
	void RebuildArms();
	void ResolveCrossings();
	bool GetArmOutward(char arm, math::Vector3& outOutward) const;
	float GetCycleTime() const;

	static std::vector<TrafficSignalControllerComponent*> s_controllers;

private:
	// config (serialized)
	std::string _armsCsv = "N,E,S,W";
	std::string _crossingsCsv;
	float _stopLineDistance = 11.5f;
	float _approachHalfWidth = 5.5f;
	float _greenSeconds = 9.0f;
	float _amberSeconds = 3.0f;
	float _allRedSeconds = 1.5f;
	float _pedestrianWalkSeconds = 8.0f;
	float _pedestrianClearSeconds = 3.0f;
	float _cycleOffsetSeconds = 0.0f;
	bool _drawDebug = true;

	// runtime
	std::vector<char> _arms;
	std::vector<std::string> _crossingNames;
	struct CrossingEnds
	{
		math::Vector3 a;
		math::Vector3 b;
	};
	std::vector<CrossingEnds> _crossings;
	bool _crossingsResolved = false;
	float _clock = 0.0f;

	// Demand-actuated stage machine (replaces the fixed clock: every junction
	// ran a 65 s cycle of four separate greens plus a walk stage whether anyone
	// was waiting or not, so cars sat ~1 min per junction and pedestrians missed
	// the 8 s walk). Stage index 0..arms-1 = that arm's green, == arms.size() =
	// the pedestrian stage. Demand is reported by the callers themselves:
	// ComputeVehicleSpeedCap (a car approaching an arm) and
	// IsPedestrianStepAllowed (someone held at the kerb).
	enum class Phase : int32_t { Green, Amber, AllRed, Walk, Clear, Idle };
	void StartNextStage();
	bool HasArmDemand(size_t armIndex) const;
	bool HasPedestrianDemand() const;
	size_t _stageIndex = 0;
	Phase _phase = Phase::Green;
	float _phaseTimer = 0.0f;
	std::vector<float> _armDemandTime;	// per arm: _clock when a vehicle last approached
	float _pedDemandTime = -1e9f;		// _clock when a pedestrian was last held
	// cs_signalLog diagnostics: when the current wait at each stage began (-1 = none).
	std::vector<float> _armWaitSince;
	float _pedWaitSince = -1.0f;
};
