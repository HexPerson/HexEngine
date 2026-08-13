
#include "DrivableComponent.hpp"
#include "RigidBody.hpp"
#include "Transform.hpp"
#include "../Entity.hpp"
#include "../../HexEngine.hpp"
#include "../../Physics/IRigidBody.hpp"
#include "../../Physics/PhysUtils.hpp"
#include "../../Input/CommandManager.hpp"
#include "../../Input/HCommand.hpp"
#include "../../Input/HVar.hpp"
#include "../../Scene/SceneManager.hpp"
#include "../../Scene/Scene.hpp"
#include "../../Environment/LogFile.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace HexEngine
{
	// -----------------------------------------------------------------------
	// Handling tuning as live cvars (arcade-forward). Iterating on feel is far
	// faster through the console than through prefab fields; V3 can promote the
	// settled values to serialized per-vehicle overrides.
	// -----------------------------------------------------------------------
	namespace
	{
		HVar v_bikeRideHeight("v_bikeRideHeight", "Bike suspension ride height / rest length (m)", 0.5f, 0.1f, 2.0f);
		HVar v_bikeWheelbase("v_bikeWheelbase", "Bike front-rear wheel spacing (m)", 1.25f, 0.4f, 4.0f);
		HVar v_bikeSuspStiffness("v_bikeSuspStiffness", "Suspension spring stiffness (accel per unit compression)", 45.0f, 1.0f, 200.0f);
		HVar v_bikeSuspDamping("v_bikeSuspDamping", "Suspension vertical damping", 6.0f, 0.0f, 40.0f);
		HVar v_bikeWheelRadius("v_bikeWheelRadius", "Wheel radius used for ground casts (m)", 0.35f, 0.05f, 1.0f);
		HVar v_bikeDriveAccel("v_bikeDriveAccel", "Forward drive acceleration (m/s^2)", 16.0f, 1.0f, 80.0f);
		HVar v_bikeBrakeAccel("v_bikeBrakeAccel", "Braking deceleration (m/s^2)", 26.0f, 1.0f, 120.0f);
		HVar v_bikeMaxSpeed("v_bikeMaxSpeed", "Top forward speed (m/s)", 20.0f, 1.0f, 90.0f);
		HVar v_bikeReverseSpeed("v_bikeReverseSpeed", "Top reverse speed (m/s)", 5.0f, 0.0f, 20.0f);
		HVar v_bikeGrip("v_bikeGrip", "Lateral grip (kills sideways velocity; lower = slidey)", 10.0f, 0.0f, 40.0f);
		HVar v_bikeSteer("v_bikeSteer", "Steering yaw authority (torque scale)", 3.0f, 0.1f, 12.0f);
		HVar v_bikeLean("v_bikeLean", "Lean-into-turn angle at speed (radians)", 0.5f, 0.0f, 1.2f);
		HVar v_bikeUpright("v_bikeUpright", "Upright/lean correction stiffness (orientation P gain)", 55.0f, 1.0f, 200.0f);
		HVar v_bikeDownforce("v_bikeDownforce", "Downforce at speed (m/s^2)", 6.0f, 0.0f, 40.0f);
		HVar v_bikeAngularDamp("v_bikeAngularDamp", "Angular velocity damping (stabilises orientation)", 6.0f, 0.0f, 30.0f);
		HVar v_bikeLinearDamp("v_bikeLinearDamp", "Linear velocity damping (coast drag)", 0.15f, 0.0f, 4.0f);
		HVar v_bikeComHeight("v_bikeComHeight", "Centre-of-mass height offset from origin (negative = lower = stabler)", -0.25f, -1.5f, 1.0f);
		HVar v_bikeSteerSmooth("v_bikeSteerSmooth", "Digital steer smoothing rate (1/s)", 8.0f, 1.0f, 30.0f);
		HVar v_bikeDebug("v_bikeDebug", "Log DrivableComponent state (grounded/speed/input) ~1/sec while possessed", false, false, true);
		HVar v_bikeMass("v_bikeMass", "Vehicle mass in kg (sets mass + inertia when the body is made dynamic)", 120.0f, 5.0f, 2000.0f);
	}

	// --- player input binds -> intent flags -------------------------------
	HEX_COMMAND(BikeThrottle)
	{
		auto* c = reinterpret_cast<DrivableComponent*>(param);
		if (c) c->SetThrottle(pressed);
	}
	HEX_COMMAND(BikeBrake)
	{
		auto* c = reinterpret_cast<DrivableComponent*>(param);
		if (c) c->SetBrake(pressed);
	}
	HEX_COMMAND(BikeSteerLeft)
	{
		auto* c = reinterpret_cast<DrivableComponent*>(param);
		if (c) c->SetSteerLeft(pressed);
	}
	HEX_COMMAND(BikeSteerRight)
	{
		auto* c = reinterpret_cast<DrivableComponent*>(param);
		if (c) c->SetSteerRight(pressed);
	}
	HEX_COMMAND(BikeHandbrake)
	{
		auto* c = reinterpret_cast<DrivableComponent*>(param);
		if (c) c->SetHandbrake(pressed);
	}

	// Console: possess the first DrivableComponent in the current scene (and
	// release any others), so a placed bike becomes drivable with WASD/arrows
	// without hijacking the editor camera until you ask. V2's enter/exit flow
	// supersedes this.
	HEX_COMMAND(possessbike)
	{
		(void)args; (void)param; (void)pressed;
		if (g_pEnv == nullptr || g_pEnv->_sceneManager == nullptr)
			return;
		auto scene = g_pEnv->_sceneManager->GetCurrentScene();
		if (scene == nullptr)
			return;
		std::vector<DrivableComponent*> bikes;
		scene->GetComponents<DrivableComponent>(bikes);
		LOG_INFO("possessbike: current scene has %zu DrivableComponent(s)", bikes.size());
		if (bikes.empty())
		{
			LOG_INFO("possessbike: none found. Add a DrivableComponent to an entity that also has a RigidBody (Dynamic) + a collider, then Play and re-run possessbike.");
			return;
		}
		bool first = true;
		for (auto* b : bikes)
		{
			if (b == nullptr) continue;
			// Diagnose the FIRST (the one we possess) so setup mistakes are obvious.
			if (first)
			{
				Entity* e = b->GetEntity();
				auto* rb = e ? e->GetComponent<RigidBody>() : nullptr;
				IRigidBody* irb = rb ? rb->GetIRigidBody() : nullptr;
				LOG_INFO("possessbike: target entity='%s' RigidBody=%s IRigidBody=%s%s",
					e ? e->GetName().c_str() : "(null)",
					rb ? "yes" : "NO (add a RigidBody component)",
					irb ? "yes" : "NO (add a collider - the body is created lazily on first collider)",
					irb ? "" : "");
				if (irb != nullptr)
				{
					const char* bt =
						irb->GetBodyType() == IRigidBody::BodyType::Dynamic ? "Dynamic" :
						irb->GetBodyType() == IRigidBody::BodyType::Kinematic ? "Kinematic (must be DYNAMIC to drive)" :
						irb->GetBodyType() == IRigidBody::BodyType::Static ? "Static (must be DYNAMIC to drive)" : "None";
					LOG_INFO("possessbike: body type=%s mass=%.2f", bt, irb->GetMass());
				}
			}
			b->SetPlayerControlled(first);
			first = false;
		}
		LOG_INFO("possessbike: possessed - WASD/arrows to drive, Space = handbrake. (v_bikeDebug 1 to log grounded/speed while driving.)");
	}

	HEX_COMMAND(unpossessbike)
	{
		(void)args; (void)param; (void)pressed;
		if (g_pEnv == nullptr || g_pEnv->_sceneManager == nullptr)
			return;
		auto scene = g_pEnv->_sceneManager->GetCurrentScene();
		if (scene == nullptr)
			return;
		std::vector<DrivableComponent*> bikes;
		scene->GetComponents<DrivableComponent>(bikes);
		for (auto* b : bikes) { if (b) b->SetPlayerControlled(false); }
		LOG_INFO("unpossessbike: released all drivable vehicles");
	}

	DrivableComponent::DrivableComponent(Entity* entity) :
		UpdateComponent(entity)
	{
		if (_playerControlled)
			CreateBinds();
	}

	DrivableComponent::DrivableComponent(Entity* entity, DrivableComponent* clone) :
		UpdateComponent(entity)
	{
		if (clone != nullptr)
			_playerControlled = clone->_playerControlled;
		if (_playerControlled)
			CreateBinds();
	}

	DrivableComponent::~DrivableComponent()
	{
		RemoveBinds();
	}

	void DrivableComponent::SetPlayerControlled(bool possessed)
	{
		if (possessed == _playerControlled && _bindsActive == possessed)
			return;
		_playerControlled = possessed;
		if (possessed)
			CreateBinds();
		else
		{
			RemoveBinds();
			_kThrottle = _kBrake = _kLeft = _kRight = _kHandbrake = false;
		}
	}

	void DrivableComponent::CreateBinds()
	{
		if (_bindsActive || g_pEnv == nullptr || g_pEnv->_commandManager == nullptr)
			return;
		auto* cm = g_pEnv->_commandManager;
		cm->CreateBind('W', "BikeThrottle", this);
		cm->CreateBind(VK_UP, "BikeThrottle", this);
		cm->CreateBind('S', "BikeBrake", this);
		cm->CreateBind(VK_DOWN, "BikeBrake", this);
		cm->CreateBind('A', "BikeSteerLeft", this);
		cm->CreateBind(VK_LEFT, "BikeSteerLeft", this);
		cm->CreateBind('D', "BikeSteerRight", this);
		cm->CreateBind(VK_RIGHT, "BikeSteerRight", this);
		cm->CreateBind(VK_SPACE, "BikeHandbrake", this);
		_bindsActive = true;
	}

	void DrivableComponent::RemoveBinds()
	{
		if (!_bindsActive || g_pEnv == nullptr || g_pEnv->_commandManager == nullptr)
			return;
		auto* cm = g_pEnv->_commandManager;
		for (int32_t k : { (int32_t)'W', (int32_t)VK_UP, (int32_t)'S', (int32_t)VK_DOWN,
		                   (int32_t)'A', (int32_t)VK_LEFT, (int32_t)'D', (int32_t)VK_RIGHT, (int32_t)VK_SPACE })
			cm->RemoveBind(k);
		_bindsActive = false;
	}

	bool DrivableComponent::ResolveBody()
	{
		if (_body == nullptr)
		{
			if (_bodyComp == nullptr)
				_bodyComp = GetEntity() ? GetEntity()->GetComponent<RigidBody>() : nullptr;
			if (_bodyComp != nullptr)
				_body = _bodyComp->GetIRigidBody();
		}
		if (_body == nullptr)
			return false;

		// A drivable vehicle is inherently a DYNAMIC body. Enforce it once
		// (the PhysX plugin recreates the actor on the type flip but leaves
		// PhysX's default mass=1/unit-inertia, which drives twitchily), then
		// set a real mass - SetMass uses setMassAndUpdateInertia, so this also
		// gives a correct inertia tensor from the collider shape. Removes the
		// "placed a bike but its RigidBody was Static/mass 1" footgun.
		if (!_ensuredDynamic)
		{
			if (_body->GetBodyType() != IRigidBody::BodyType::Dynamic)
				_body->SetBodyType(IRigidBody::BodyType::Dynamic);
			_body->SetMass(std::max(v_bikeMass._val.f32, 1.0f));
			_body->SetGravityEnabled(true);
			_ensuredDynamic = true;
			LOG_INFO("DrivableComponent: body made Dynamic (mass %.0f kg) on '%s'",
				v_bikeMass._val.f32, GetEntity() ? GetEntity()->GetName().c_str() : "(null)");
		}
		return true;
	}

	void DrivableComponent::RebuildWheels()
	{
		// Bike: two wheels along the forward (-Z) axis, front and rear. Cars
		// extend this with left/right pairs later.
		const float half = v_bikeWheelbase._val.f32 * 0.5f;
		_wheelsLocal.clear();
		_wheelsLocal.push_back(math::Vector3(0.0f, 0.0f, -half)); // front (-Z)
		_wheelsLocal.push_back(math::Vector3(0.0f, 0.0f,  half)); // rear
	}

	void DrivableComponent::Serialize(json& data, JsonFile* file)
	{
		SERIALIZE_VALUE(_playerControlled);
	}

	void DrivableComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		DESERIALIZE_VALUE(_playerControlled);
	}

	void DrivableComponent::FixedUpdate(float dt)
	{
		if (dt <= 0.0f)
			return;
		if (!ResolveBody())
		{
			// Ticking but no usable body - the #1 setup mistake. Log once so
			// it's obvious the component runs but has nothing to push.
			static bool loggedNoBody = false;
			if (_playerControlled && !loggedNoBody)
			{
				loggedNoBody = true;
				LOG_WARN("DrivableComponent: FixedUpdate running but no RigidBody/collider resolved on entity '%s' - add a RigidBody(Dynamic) + collider.",
					GetEntity() ? GetEntity()->GetName().c_str() : "(null)");
			}
			return;
		}

		if (v_bikeDebug._val.b && _playerControlled)
		{
			_debugAccum += dt;
			if (_debugAccum >= 1.0f)
			{
				_debugAccum = 0.0f;
				LOG_INFO("bike: grounded=%d speed=%.2f m/s  in(thr=%.2f brk=%.2f steer=%.2f hb=%d)",
					_grounded ? 1 : 0, _forwardSpeed,
					_input.throttle, _input.brake, _input.steer, _input.handbrake ? 1 : 0);
			}
		}

		// --- player input -> intent (V1 self-drive path) ---
		if (_playerControlled)
		{
			const float steerTarget = (_kRight ? 1.0f : 0.0f) - (_kLeft ? 1.0f : 0.0f);
			const float a = std::clamp(v_bikeSteerSmooth._val.f32 * dt, 0.0f, 1.0f);
			_steerSmoothed += (steerTarget - _steerSmoothed) * a;
			_input.throttle = _kThrottle ? 1.0f : 0.0f;
			_input.brake = _kBrake ? 1.0f : 0.0f;
			_input.steer = _steerSmoothed;
			_input.handbrake = _kHandbrake;
		}

		const float mass = std::max(_body->GetMass(), 1.0f);
		const math::Vector3 pos = _body->GetPhysicsPosition();
		const math::Quaternion rot = _body->GetPhysicsRotation();
		const math::Vector3 vel = _body->GetLinearVelocity();

		// Sanity: bail on a non-finite pose (a spawned-inside-geometry blow-up).
		if (!std::isfinite(pos.x) || !std::isfinite(vel.x))
			return;

		const math::Vector3 fwd   = math::Vector3::Transform(math::Vector3::Forward, rot);
		const math::Vector3 right = math::Vector3::Transform(math::Vector3::Right, rot);
		const math::Vector3 up    = math::Vector3::Transform(math::Vector3::Up, rot);
		const math::Vector3 worldUp(0.0f, 1.0f, 0.0f);

		// Centre of mass estimate: origin + a downward offset for stability
		// (no CoM accessor on the interface).
		const math::Vector3 com = pos + math::Vector3::Transform(math::Vector3(0.0f, v_bikeComHeight._val.f32, 0.0f), rot);

		const auto applyForceAtPoint = [&](const math::Vector3& F, const math::Vector3& worldPoint)
		{
			_body->ApplyForceToCenterOfMass(F);
			_body->ApplyTorque((worldPoint - com).Cross(F));
		};

		_body->WakeUp();
		_body->SetLinearVelocityDamping(v_bikeLinearDamp._val.f32);
		_body->SetAngularVelocityDamping(v_bikeAngularDamp._val.f32);
		_body->SetMaxLinearVelocity(v_bikeMaxSpeed._val.f32 * 1.4f);

		// --- suspension: raycast wheels, spring+damper as force-at-point ---
		RebuildWheels();
		const float rideHeight = v_bikeRideHeight._val.f32;
		const float wheelRadius = v_bikeWheelRadius._val.f32;
		const float maxDrop = rideHeight + wheelRadius;
		const float stiffness = v_bikeSuspStiffness._val.f32;
		const float suspDamp = v_bikeSuspDamping._val.f32;
		const LayerMask groundMask =
			LAYERMASK(Layer::StaticGeometry) | LAYERMASK(Layer::DynamicGeometry) | LAYERMASK(Layer::Decorative);
		const std::vector<Entity*> ignore { GetEntity() };

		int grounded = 0;
		math::Vector3 groundNormalAccum(0.0f, 0.0f, 0.0f);
		for (const math::Vector3& wl : _wheelsLocal)
		{
			const math::Vector3 anchor = pos + math::Vector3::Transform(wl, rot);
			const math::Vector3 from = anchor;
			const math::Vector3 to = anchor - worldUp * maxDrop;

			RayHit hit;
			if (PhysUtils::RayCast(from, to, groundMask, &hit, ignore))
			{
				const float d = std::max(hit.distance, 0.0f);
				const float compression = std::clamp((maxDrop - d) / maxDrop, 0.0f, 1.0f);
				// Damp the body's vertical motion so the spring settles.
				const float springAccel = compression * stiffness;
				const float dampAccel = -vel.y * suspDamp;
				const float upAccel = std::max(springAccel + dampAccel, 0.0f);
				applyForceAtPoint(worldUp * (upAccel * mass), anchor);

				++grounded;
				groundNormalAccum += hit.normal;
			}
		}

		_grounded = grounded > 0;
		math::Vector3 groundNormal = worldUp;
		if (_grounded)
		{
			groundNormalAccum.Normalize();
			if (std::isfinite(groundNormalAccum.x) && groundNormalAccum.y > 0.1f)
				groundNormal = groundNormalAccum;
		}

		// Forward direction projected onto the ground plane.
		math::Vector3 fwdGround = fwd - groundNormal * fwd.Dot(groundNormal);
		if (fwdGround.LengthSquared() > 1e-5f)
			fwdGround.Normalize();
		else
			fwdGround = fwd;

		_forwardSpeed = vel.Dot(fwdGround);
		const float maxSpeed = v_bikeMaxSpeed._val.f32;
		const float speedFactor = std::clamp(std::fabs(_forwardSpeed) / std::max(maxSpeed, 1.0f), 0.0f, 1.0f);

		if (_grounded)
		{
			// Drive / brake / reverse.
			if (_input.throttle > 0.001f && _forwardSpeed < maxSpeed)
			{
				applyForceAtPoint(fwdGround * (_input.throttle * v_bikeDriveAccel._val.f32 * mass), com);
			}
			if (_input.brake > 0.001f)
			{
				if (_forwardSpeed > 0.3f)
				{
					// Brake: decelerate along travel.
					applyForceAtPoint(fwdGround * (-_input.brake * v_bikeBrakeAccel._val.f32 * mass), com);
				}
				else if (_forwardSpeed > -v_bikeReverseSpeed._val.f32)
				{
					// Reverse (slower than forward).
					applyForceAtPoint(fwdGround * (-_input.brake * v_bikeDriveAccel._val.f32 * 0.5f * mass), com);
				}
			}

			// Lateral grip - kill sideways velocity (relaxed on handbrake).
			const float lateral = vel.Dot(right);
			const float gripScale = _input.handbrake ? 0.35f : 1.0f;
			applyForceAtPoint(right * (-lateral * v_bikeGrip._val.f32 * gripScale * mass), com);

			// Planted at speed.
			if (v_bikeDownforce._val.f32 > 0.0f)
				_body->ApplyForceToCenterOfMass(-groundNormal * (v_bikeDownforce._val.f32 * speedFactor * mass));
		}

		// Steering: speed-scaled yaw torque (can turn a little at a standstill).
		const float steerAuthority = 0.35f + 0.65f * speedFactor;
		const float steerDir = (_forwardSpeed >= -0.3f) ? 1.0f : -1.0f;
		_body->ApplyTorque(up * (_input.steer * v_bikeSteer._val.f32 * steerAuthority * steerDir * mass));

		// Upright + lean: proportional torque toward a target up-vector. The
		// heavy angular damping above turns this pure-P controller into a
		// stable spring, so the bike banks into turns and never topples.
		math::Vector3 targetGroundUp = math::Vector3::Lerp(worldUp, groundNormal, 0.5f);
		if (targetGroundUp.LengthSquared() > 1e-5f)
			targetGroundUp.Normalize();
		const float leanRoll = -_input.steer * v_bikeLean._val.f32 * speedFactor;
		const math::Quaternion leanQ = math::Quaternion::CreateFromAxisAngle(fwd, leanRoll);
		math::Vector3 targetUp = math::Vector3::Transform(targetGroundUp, leanQ);
		const math::Vector3 uprightAxis = up.Cross(targetUp); // ~sin(angle) * rotation axis
		_body->ApplyTorque(uprightAxis * (v_bikeUpright._val.f32 * mass));
	}
}
