
#pragma once

#include "UpdateComponent.hpp"
#include "../../Math/FloatMath.hpp"

#include <vector>
#include <string>

namespace HexEngine
{
	class RigidBody;
	class IRigidBody;

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
	class HEX_API DrivableComponent : public UpdateComponent
	{
	public:
		CREATE_COMPONENT_ID(DrivableComponent);

		DrivableComponent(Entity* entity);
		DrivableComponent(Entity* entity, DrivableComponent* clone);
		~DrivableComponent();

		virtual void FixedUpdate(float frameTime) override;

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

	private:
		bool ResolveBody();
		void CreateBinds();
		void RemoveBinds();

		// Local-space wheel anchors (bike = 2 along the wheel axis). Rebuilt
		// from the wheelbase cvar each frame so live tuning works.
		void RebuildWheels();

		RigidBody* _bodyComp = nullptr;
		IRigidBody* _body = nullptr;

		DriveInput _input;
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
	};
}
