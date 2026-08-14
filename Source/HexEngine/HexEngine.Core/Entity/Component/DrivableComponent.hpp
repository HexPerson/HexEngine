
#pragma once

#include "InteractionComponent.hpp"
#include "../../Math/FloatMath.hpp"

#include <vector>
#include <string>

namespace HexEngine
{
	class RigidBody;
	class IRigidBody;
	class Camera;
	class Entity;
	class FirstPersonCameraController;

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
		float steer = 18.0f;          // yaw authority (torque scale)
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

		// Mounted first-person camera. cameraOffset is the rider's eye in the
		// vehicle's LOCAL space (up = +Y, length ~= Z); every vehicle sits the
		// rider differently, so this is per-instance. The smoothing rates damp
		// the physics bobble - higher = snappier/closer to the rigid pose, lower
		// = floatier. They also shape the glide when mounting.
		math::Vector3 cameraOffset = math::Vector3(0.0f, 1.1f, 0.15f);
		float cameraPosSmoothing = 8.0f;   // eye position follow rate (1/s)
		float cameraLookSmoothing = 10.0f; // look-direction follow rate (1/s)
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

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(ComponentWidget* widget) override;

		// Per-instance handling tuning (read every FixedUpdate; editable live).
		DriveTuning& GetTuning() { return _tuning; }
		const DriveTuning& GetTuning() const { return _tuning; }

		void Possess();
		void Unpossess();

	private:
		bool ResolveBody();
		void CreateBinds();
		void RemoveBinds();

		// First-person camera mount: hand the main camera to this vehicle (on
		// possess) and give it back to the player's FirstPersonCameraController
		// (on release). Reuses the existing main camera - no new camera created.
		void MountCamera();
		void DismountCamera();
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

		// digital key state (player path)
		bool _kThrottle = false, _kBrake = false, _kLeft = false, _kRight = false, _kHandbrake = false;
		float _steerSmoothed = 0.0f;

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
	};
}
