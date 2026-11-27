
#pragma once

#include "InteractionComponent.hpp"
#include "../../Math/FloatMath.hpp"

#include <algorithm>
#include <vector>
#include <string>
#include <memory>

namespace HexEngine
{
	class RigidBody;
	class IRigidBody;
	class Camera;
	class Entity;
	class FirstPersonCameraController;
	class SoundEffect;

	// Generic drive intent. Whatever is "driving" the vehicle fills this each
	// frame - the local player controller (V1), the enter/exit routing (V2),
	// or AI traffic later - and DrivableComponent turns it into physics. Keep
	// this the single seam so nothing downstream cares who is steering.
	struct DriveInput
	{
		float throttle = 0.0f;  // 0..1 forward drive
		float brake = 0.0f;     // 0..1 brake / reverse
		float steer = 0.0f;     // -1 (left) .. +1 (right)
		bool  handbrake = false;
	};

	// Per-vehicle handling tuning. Previously global v_bike* cvars; now lives on
	// the component so every vehicle instance can be tuned independently and the
	// values serialize with the prefab/scene. Defaults are the settled arcade
	// bike values. Edit live via the inspector fields.
	struct DriveTuning
	{
		// Grounding / suspension
		bool  hover = false;          // raycast spring lift (arcade float) vs rest on collider
		float groundReach = 1.5f;     // non-hover ground-probe reach below wheel line (m)
		float rideHeight = 0.5f;      // hover ride height / spring rest length (m)
		float wheelbase = 1.25f;      // front-rear wheel spacing (m)
		float suspStiffness = 45.0f;  // hover spring stiffness
		float suspDamping = 12.0f;    // vertical damping (higher = less terrain bob)
		float wheelRadius = 0.35f;    // ground-cast wheel radius (m)

		// Drive
		float driveAccel = 50.0f;     // forward accel (m/s^2)
		float brakeAccel = 50.0f;     // braking decel (m/s^2)
		float maxSpeed = 20.0f;       // top forward speed (m/s)
		float reverseSpeed = 5.0f;    // top reverse speed (m/s)
		float grip = 10.0f;           // lateral grip (kills sideways vel)
		float downforce = 6.0f;       // downforce at speed (m/s^2)

		// Steering / orientation
		// Steering is a turn-RATE controller: holding a direction asks for a yaw
		// rate (tight at walking pace, capped at speed) and `steer` is how hard
		// the body is torqued toward it - so releasing the key stops the turn
		// instead of letting the bike keep spinning on its own momentum.
		float steer = 18.0f;          // yaw-rate tracking gain (1/s)
		float maxYawRate = 55.0f;     // full-lock turn rate at speed (deg/s)
		float minTurnRadius = 4.5f;   // tightest turn radius - limits the rate at low speed (m)
		float standYawRate = 12.0f;   // turn rate available near a standstill (deg/s)
		float lean = 0.5f;            // lean-into-turn angle at speed (rad)
		float upright = 55.0f;        // upright/lean P gain
		float steerSmooth = 8.0f;     // digital steer smoothing rate (1/s)
		float forwardSign = -1.0f;    // +1 / -1: flip if it drives backwards

		// Body
		float mass = 120.0f;          // kg (seeds the body mass if left at default 1)
		float angularDamp = 6.0f;     // angular velocity damping
		float linearDamp = 0.15f;     // linear velocity damping (coast drag)
		float comHeight = -0.25f;     // centre-of-mass height offset (negative = lower)
		float chassisFriction = 0.05f;    // collider friction (low = slides over seams)
		float chassisRestitution = 0.0f;  // collider bounciness

		bool  debug = false;          // log grounded/speed/ray ~1/sec while possessed

		// Hop (Space) + coming off the bike. A hop clears a kerb with a good
		// line; hitting one (or landing badly) throws the rider off - the time
		// lost getting back on is the penalty.
		float hopSpeed = 3.2f;          // upward speed the hop kick reaches (m/s) - ~0.5 m of lift
		float hopCooldown = 0.6f;       // seconds between hops
		float crashSpeed = 4.0f;        // min speed (m/s) before an impact can throw the rider
		float crashSpeedLoss = 0.6f;    // fraction of that speed lost within ~0.15 s = crash
		// Kerb climbing: a low ledge in front of the chassis up to this high (m) is
		// stepped up onto, the way a car's wheels roll over a kerb. 0 = off (the
		// bikes: a kerb is something you hop or crash on).
		float stepHeight = 0.0f;
		float crashLandAngle = 40.0f;   // landing tilted more than this (deg) = crash
		float crashRemountDelay = 2.5f; // seconds before a crashed bike can be remounted

		// Mounted first-person camera. cameraOffset is the rider's eye in the
		// vehicle's LOCAL space (up = +Y, length ~= Z); every vehicle sits the
		// rider differently, so this is per-instance. The smoothing rates damp
		// the physics bobble - higher = snappier/closer to the rigid pose, lower
		// = floatier. They also shape the glide when mounting.
		math::Vector3 cameraOffset = math::Vector3(0.0f, 1.1f, 0.15f);
		float cameraPosSmoothing = 8.0f;   // eye position follow rate (1/s)
		float cameraLookSmoothing = 10.0f; // look-direction follow rate (1/s)

		// --- audio (assign clips per vehicle; empty path = that layer is silent).
		// Engine + wind loop while the vehicle is active; skid loops only while
		// sliding. All three are 3D-positioned at the vehicle.
		std::string engineSoundPath;      // looping engine; pitch + volume track speed
		std::string skidSoundPath;        // looping tyre skid while sliding/handbraking
		std::string windSoundPath;        // looping wind; volume tracks speed
		float audioVolume = 1.0f;         // master multiplier for this vehicle
		float enginePitchIdle = -0.10f;   // engine pitch at rest (-1..1, semi-octave)
		float enginePitchMax = 0.55f;     // engine pitch at top speed
		float engineVolIdle = 0.35f;      // engine volume at rest
		float engineVolMax = 1.0f;        // engine volume at top speed
		float windVolMax = 0.5f;          // wind volume at top speed
		float skidVolume = 0.7f;          // tyre-skid volume when fully sliding
	};

	// A physics-driven, arcade-forward drivable vehicle. The bike is the first
	// implementation; cars slot in by declaring four wheels and different
	// tuning. Requires a sibling RigidBody (Dynamic) with a collider.
	//
	// Physics model (constrained to the engine's force/torque-only rigid body):
	//   - Raycast "wheels" apply spring+damper suspension as force-at-point,
	//     synthesised from ApplyForceToCenterOfMass + ApplyTorque(r x F), so
	//     the body hovers at ride height and pitches/rolls naturally.
	//   - Orientation (upright + bank-into-turns) is driven by a PROPORTIONAL
	//     torque toward a target up-vector, made stable by heavy angular
	//     damping (there is no angular-velocity read to build a true PD).
	//     A bike therefore cannot topple - it is actively held upright - which
	//     is exactly the arcade feel we want.
	//   - Steering is a speed-scaled yaw torque; lateral grip kills sideways
	//     velocity (relaxed on handbrake for drift).
	class HEX_API DrivableComponent : public InteractionComponent
	{
	public:
		CREATE_COMPONENT_ID(DrivableComponent);

		DrivableComponent(Entity* entity);
		DrivableComponent(Entity* entity, DrivableComponent* clone);
		~DrivableComponent();

		virtual void FixedUpdate(float frameTime) override;
		// Per-render-frame: drives the mounted first-person camera (smoothed).
		virtual void Update(float frameTime) override;

		// The generic seam - set by whoever is driving.
		void SetDriveInput(const DriveInput& input) { _input = input; }
		const DriveInput& GetDriveInput() const { return _input; }

		// Current forward ground speed in m/s (signed; negative = reversing).
		float GetForwardSpeed() const { return _forwardSpeed; }
		bool  IsGrounded() const { return _grounded; }

		// Player possession: binds WASD/arrows to this vehicle's intent and
		// feeds it from local input each FixedUpdate. V2's enter/exit flow will
		// call this; for V1 a placed bike self-possesses so it's drivable.
		void SetPlayerControlled(bool possessed);
		bool IsPlayerControlled() const { return _playerControlled; }

		// Digital input flags, poked by the HEX_COMMAND binds.
		void SetThrottle(bool v) { _kThrottle = v; }
		void SetBrake(bool v) { _kBrake = v; }
		void SetSteerLeft(bool v) { _kLeft = v; }
		void SetSteerRight(bool v) { _kRight = v; }
		void SetHandbrake(bool v) { _kHandbrake = v; }
		// Hop is edge-triggered: a press requests one hop (taken if grounded).
		void SetHop(bool v) { if (v && !_kHop) _hopRequested = true; _kHop = v; }

		// Crash state. ConsumeCrash() returns true ONCE per crash so game code can
		// show feedback; IsCrashed() stays true until the bike is remounted.
		bool ConsumeCrash() { const bool c = _crashEvent; _crashEvent = false; return c; }
		bool IsCrashed() const { return _crashed; }
		float CrashRemountSecondsLeft() const { return _crashed ? std::max(0.0f, _crashTimer) : 0.0f; }

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(ComponentWidget* widget) override;

		// Per-instance handling tuning (read every FixedUpdate; editable live).
		// Runtime gameplay multipliers on top of the serialized tuning (upgrades,
		// a heavy load, ...). Never saved; reset to 1 on play start by the game.
		struct GameplayModifiers
		{
			float speed = 1.0f;       // x maxSpeed
			float accel = 1.0f;       // x driveAccel
			float brake = 1.0f;       // x brakeAccel
			float grip = 1.0f;        // x grip
			float wetPenalty = 1.0f;  // x the wet-road grip/brake loss (0 = weatherproof)
		};
		void SetGameplayModifiers(const GameplayModifiers& m) { _mods = m; }
		const GameplayModifiers& GetGameplayModifiers() const { return _mods; }
		// Current wet-road grip factor (1 = dry) - for HUD hints.
		float GetWetGripFactor() const { return _wetGripFactor; }
		// Kick the mounted camera (decays over ~0.4 s). 1 = a hard hit.
		void AddCameraShake(float amount) { _camShake = std::min(1.5f, _camShake + std::max(0.0f, amount)); }
		// Edge-triggered: true ONCE after the bike lands from a real drop (hop/kerb).
		bool ConsumeLanding(float& impactSpeed) { const bool l = _landingEvent; _landingEvent = false; impactSpeed = _landingSpeed; return l; }
		DriveTuning& GetTuning() { return _tuning; }
		const DriveTuning& GetTuning() const { return _tuning; }

		void Possess();
		void Unpossess();
		// Dismount request from the in-vehicle bind. Deferred to the next
		// FixedUpdate so the look-at interaction listener (which sees the same
		// keypress) can't re-mount in the same frame.
		void RequestDismount();

	private:
		bool ResolveBody();
		// Mounting a bike that's lying over: stand it up in place (heading kept),
		// zero its motion and briefly suppress the crash checks.
		void RightForMount();
		// Collision-aware dismount placement. Probes candidate spots around the
		// vehicle with scene rays (approach clearance from the seat, ground
		// snap, headroom, body clearance) and returns the first clean foot
		// position; false if none of the candidates is safe.
		bool FindSafeDismountPosition(math::Vector3& outFeet) const;
		bool IsDismountSpotClear(const math::Vector3& seat, const math::Vector3& dir, float lateral, math::Vector3& outFeet) const;
		void CreateBinds();
		void RemoveBinds();
		// Throw the rider off: dismount next tick, lock out remounting for
		// crashRemountDelay, and let the bike topple (upright control off).
		void TriggerCrash(const char* reason, float speed);

		// First-person camera mount: hand the main camera to this vehicle (on
		// possess) and give it back to the player's FirstPersonCameraController
		// (on release). Reuses the existing main camera - no new camera created.
		void MountCamera();
		void DismountCamera();

		// Vehicle audio: engine + wind loops start/stop with possession, skid
		// loops on demand; all modulated from speed/slip each FixedUpdate.
		void StartVehicleAudio();
		void StopVehicleAudio();
		void UpdateVehicleAudio();
		// Returns the live main camera only if it's still the one we mounted and
		// alive; otherwise null (and clears the mounted flag). Never dereferences
		// a stale cached pointer - exiting play mode frees it mid-tick.
		Camera* ResolveMountedCamera();

		// Local-space wheel anchors (bike = 2 along the wheel axis). Rebuilt
		// from the wheelbase cvar each frame so live tuning works.
		void RebuildWheels();

		RigidBody* _bodyComp = nullptr;
		IRigidBody* _body = nullptr;

		DriveInput _input;
		DriveTuning _tuning;
		// Default OFF so placing a bike in the editor doesn't hijack the
		// editor camera's WASD. Possess it explicitly - the `possessbike`
		// console command (V1), or the enter/exit flow (V2).
		bool _playerControlled = false;
		bool _bindsActive = false;
		bool _dismountRequested = false;
		int32_t _dismountKeyBound = -1;          // key bound to BikeDismount while possessed
		int64_t _lastPossessChangeFrame = -1;    // same-frame toggle debounce
		bool _resumeRideAfterLoad = false;       // game save had the player riding: Possess() on next Update
		// Where the player stood when they mounted - the dismount fallback when
		// no spot around the vehicle is clear (it was a valid standing spot).
		math::Vector3 _mountFromPos;
		bool _hasMountFromPos = false;

		// digital key state (player path)
		bool _kThrottle = false, _kBrake = false, _kLeft = false, _kRight = false, _kHandbrake = false;
		float _steerSmoothed = 0.0f;
		// Heading at the previous physics step: the rigid-body interface has no
		// angular-velocity read, so the yaw rate is a finite difference.
		math::Vector3 _prevHeading = math::Vector3::Zero;
		bool _hasPrevHeading = false;

		// --- hop / crash runtime state (not serialized) ---
		bool  _kHop = false;
		bool  _hopRequested = false;
		float _hopKickLeft = 0.0f;       // seconds of upward kick still to apply
		float _hopCooldownLeft = 0.0f;
		float _hopAirGrace = 0.0f;       // after a hop: no vertical damping / downforce
		static constexpr int kSpeedWindow = 36;      // ~0.3 s of fixed steps at 120 Hz (a kerb hit decelerates over ~0.3 s as the capsule rides the face)
		float _speedWindow[kSpeedWindow] = {};       // recent horizontal speeds (impact detection)
		int   _speedWindowIdx = 0;
		float _airborneTime = 0.0f;      // wheel line clearly off the ground (landing detection)
		float _prevGroundY = 0.0f;       // seam guard: ground height under the wheels last step
		float _stepCooldown = 0.0f;      // kerb climbing: time until the next step-up is allowed
		bool  _chassisSized = false;     // kerb climbing: half length/width from the mesh parts
		float _chassisHalfLength = 1.0f;
		float _chassisHalfWidth = 0.4f;
		bool  _hasPrevGroundY = false;
		bool  _crashed = false;
		float _crashTimer = 0.0f;        // remount lockout remaining
		float _remountGrace = 0.0f;      // crash checks suppressed after righting on mount
		float _crashTopple = 0.0f;       // seconds of tip-over torque remaining
		float _crashToppleSide = 1.0f;
		bool  _crashEvent = false;

		// per-frame outputs
		float _forwardSpeed = 0.0f;
		bool  _grounded = false;
		// Seconds since the wheels last touched ground. A short "coyote" window
		// keeps the vehicle drivable for a moment after rolling off a lip/curb,
		// so it launches over the edge instead of freezing when the ground ray
		// briefly finds nothing.
		float _airTime = 0.0f;

		// Diagnostics captured during the wheel loop, printed by the v_bikeDebug
		// log so we can see why grounding behaves as it does on terrain.
		float _dbgBottomY = 0.0f;   // wheel-line offset from origin (local, scaled)
		float _dbgFromY = 0.0f;     // ray start Y (world)
		float _dbgToY = 0.0f;       // ray end Y (world)
		float _dbgHitY = 0.0f;      // hit point Y (world), or 0 if none
		float _dbgHitDist = -1.0f;  // hit distance, or -1 if none
		int   _dbgHits = 0;         // wheels that hit this frame
		std::string _dbgHitEntity;  // name of the entity the first ray hit
		float _debugAccum = 0.0f;
		bool  _ensuredDynamic = false;

		// Last chassis material values pushed to the collider, so we only touch
		// the PhysX material (a scene-write-locked recreate) when they change.
		float _appliedFriction = -1.0f;
		float _appliedRestitution = -1.0f;

		std::vector<math::Vector3> _wheelsLocal;

		// --- mounted camera state ---
		bool _cameraMounted = false;
		Entity* _camPlayerEntity = nullptr;          // entity owning the main camera
		Camera* _camMain = nullptr;                  // the main camera we drive
		FirstPersonCameraController* _camFps = nullptr; // suspended while mounted
		IRigidBody* _camPlayerBody = nullptr;        // player CCT, sim paused while mounted
		bool _camSmoothInit = false;
		math::Vector3 _camEyeSmoothed;               // smoothed world eye position
		math::Vector3 _camLookSmoothed;              // smoothed world look direction
		math::Vector3 _camSavedViewOffset;           // player's eye-height view offset, restored on dismount
		float _camBaseFov = 0.0f;                    // main camera FOV at mount, restored on dismount
		float _camFovBoost = 0.0f;                   // smoothed speed-reactive FOV widening (deg)
		float _camShake = 0.0f;                      // camera shake energy (decays)
		float _camShakeTime = 0.0f;                  // shake noise phase
		GameplayModifiers _mods;                     // runtime (not serialized)
		float _wetGripFactor = 1.0f;                 // 1 dry .. v_bikeWetGrip at full wetness
		float _wetBrakeFactor = 1.0f;
		bool  _landingEvent = false;
		float _landingSpeed = 0.0f;
		float _lastVerticalSpeed = 0.0f;

		// --- audio playback state (runtime; not serialized) ---
		std::shared_ptr<SoundEffect> _engineMaster, _engineInst;
		std::shared_ptr<SoundEffect> _skidMaster, _skidInst;
		std::shared_ptr<SoundEffect> _windMaster, _windInst;
		bool _vehicleAudioActive = false;
		bool _skidActive = false;
		float _lateralSlip = 0.0f;                   // |sideways speed| this frame, for skid
	};
}
