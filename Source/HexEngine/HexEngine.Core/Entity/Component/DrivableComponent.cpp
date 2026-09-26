
#include "DrivableComponent.hpp"
#include "RigidBody.hpp"
#include "Transform.hpp"
#include "Camera.hpp"
#include "FirstPersonCameraController.hpp"
#include "../Entity.hpp"
#include "../../HexEngine.hpp"
#include "../../Physics/IRigidBody.hpp"
#include "../../Physics/IPhysicsSystem.hpp"
#include "../../Physics/PhysUtils.hpp"
#include "../../Input/CommandManager.hpp"
#include "../../Input/HCommand.hpp"
#include "../../Input/HVar.hpp"
#include "../../Scene/SceneManager.hpp"
#include "../../Scene/Scene.hpp"
#include "../../Environment/LogFile.hpp"
#include "../../Environment/TimeManager.hpp"
#include "../../Audio/AudioManager.hpp"
#include "../../Audio/SoundEffect.hpp"
#include "../../GUI/Elements/AssetSearch.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace HexEngine
{

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
	HEX_COMMAND(BikeDismount)
	{
		auto* c = reinterpret_cast<DrivableComponent*>(param);
		if (c && pressed) c->RequestDismount();
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
		LOG_INFO("possessbike: possessed - WASD/arrows to drive, Space = handbrake. (tick 'Debug log' on the DrivableComponent to log grounded/speed while driving.)");
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
		InteractionComponent(entity)
	{
		if (_playerControlled)
			CreateBinds();

		InteractionComponent::SetCallback(std::bind(&DrivableComponent::Possess, this));
	}

	DrivableComponent::DrivableComponent(Entity* entity, DrivableComponent* clone) :
		// Forward the source to the base so the InteractionComponent fields
		// (name/prompt/range/key/...) are copied on a prefab clone; calling the
		// plain InteractionComponent(entity) left them at defaults ("Item" /
		// "Press E to interact") on every spawned instance.
		InteractionComponent(entity, clone)
	{
		if (clone != nullptr)
		{
			_playerControlled = clone->_playerControlled;
			_tuning = clone->_tuning;
		}
		if (_playerControlled)
			CreateBinds();

		InteractionComponent::SetCallback(std::bind(&DrivableComponent::Possess, this));
	}

	DrivableComponent::~DrivableComponent()
	{
		StopVehicleAudio();
		DismountCamera();
		RemoveBinds();
	}

	void DrivableComponent::Possess()
	{
		// The look-at interaction only MOUNTS. Dismounting is the in-vehicle
		// bind (RequestDismount): from the seat the interaction ray starts
		// inside the vehicle's own collider, so toggling on it was unreliable,
		// and both paths firing on one keypress could re-mount immediately.
		if (_playerControlled)
			return;
		SetPlayerControlled(true);
	}

	void DrivableComponent::Unpossess()
	{
		SetPlayerControlled(false);
	}

	void DrivableComponent::RequestDismount()
	{
		if (_playerControlled)
			_dismountRequested = true;
	}

	void DrivableComponent::SetPlayerControlled(bool possessed)
	{
		if (possessed == _playerControlled && _bindsActive == possessed)
			return;
		// One possession change per frame: the dismount bind and the interaction
		// listener can both observe the same keypress.
		const int64_t frameNow = (g_pEnv != nullptr && g_pEnv->_timeManager != nullptr)
			? (int64_t)g_pEnv->_timeManager->_frameCount : -1;
		if (frameNow >= 0 && frameNow == _lastPossessChangeFrame)
			return;
		_lastPossessChangeFrame = frameNow;
		_playerControlled = possessed;
		if (possessed)
		{
			CreateBinds();
			MountCamera();
			StartVehicleAudio();
		}
		else
		{
			RemoveBinds();
			_kThrottle = _kBrake = _kLeft = _kRight = _kHandbrake = false;
			DismountCamera();
			StopVehicleAudio();
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
		// Dismount on the vehicle's own interact key (the key that mounted it).
		_dismountKeyBound = GetInteractKey();
		cm->CreateBind(_dismountKeyBound, "BikeDismount", this);
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
		if (_dismountKeyBound >= 0)
		{
			cm->RemoveBind(_dismountKeyBound);
			_dismountKeyBound = -1;
		}
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

		// Body TYPE is the author's responsibility (a triangle-mesh collider
		// can't be a dynamic sim shape - flipping it crashes PhysX). But MASS
		// is a safe, common footgun: a body left at PhysX's default mass=1 has
		// a tiny inertia tensor, so the steering/upright torques go twitchy and
		// any bump flings it. SetMass is setMassAndUpdateInertia on the EXISTING
		// actor (no recreation, no reattach), so it's safe on an already-dynamic
		// convex body. Only touch it once, and only if it's still the ~1 default
		// so we never override an intentionally-authored mass.
		if (!_ensuredDynamic)
		{
			_ensuredDynamic = true;
			if (_body->GetBodyType() == IRigidBody::BodyType::Dynamic && _body->GetMass() <= 1.001f)
			{
				_body->SetMass(std::max(_tuning.mass, 1.0f));
				LOG_INFO("DrivableComponent: '%s' had default mass 1 - set %.0f kg (tuning Mass) for stable handling.",
					GetEntity() ? GetEntity()->GetName().c_str() : "(null)", _tuning.mass);
			}
			// (Transform interpolation for the vehicle mesh is now enabled centrally
			// by RigidBody for any dynamic body - see RigidBody::UpdateInterpolationState.)
		}
		return true;
	}

	void DrivableComponent::RebuildWheels()
	{
		// Bike: two wheels along the forward (-Z) axis, front and rear. Cars
		// extend this with left/right pairs later.
		const float half = _tuning.wheelbase * 0.5f;
		_wheelsLocal.clear();
		_wheelsLocal.push_back(math::Vector3(0.0f, 0.0f, -half)); // front (-Z)
		_wheelsLocal.push_back(math::Vector3(0.0f, 0.0f,  half)); // rear
	}

	void DrivableComponent::MountCamera()
	{
		if (_cameraMounted)
			return;
		if (g_pEnv == nullptr || g_pEnv->_sceneManager == nullptr)
			return;
		auto scene = g_pEnv->_sceneManager->GetCurrentScene();
		if (scene == nullptr)
			return;
		Camera* cam = scene->GetMainCamera();
		if (cam == nullptr)
			return;
		Entity* camEnt = cam->GetEntity();
		if (camEnt == nullptr)
			return;

		_camMain = cam;
		_camPlayerEntity = camEnt;

		// Remember the player's eye-height view offset so we can restore it on
		// dismount. The ride drives the view offset to the seat each frame; the
		// walk controller relies on this stored offset for eye height and never
		// re-applies it, so we must put back exactly what was here.
		_camSavedViewOffset = cam->GetViewOffset();

		// Suspend the player's walk controller so it stops moving/looking.
		_camFps = camEnt->GetComponent<FirstPersonCameraController>();
		if (_camFps != nullptr)
			_camFps->SetControlEnabled(false);

		// Pause the player's character controller so it doesn't wander off or
		// accumulate gravity while parked. We no longer move the player transform
		// (the eye is placed via the camera's view offset), so there is nothing
		// for it to fight - which is what made the resting eye depend on where the
		// player mounted from.
		_camPlayerBody = nullptr;
		if (auto* rb = camEnt->GetComponent<RigidBody>())
		{
			_camPlayerBody = rb->GetIRigidBody();
			if (_camPlayerBody != nullptr)
			{
				// Pause the CCT (so its parked capsule can't block the bike) and
				// suppress the physics->transform read-back so we can drive the
				// camera transform to the seat ourselves without it fighting us.
				_camPlayerBody->SetIsSimulated(false);
				_camPlayerBody->SetPoseWritebackEnabled(false);
			}
		}

		// Remember where the player stood: the dismount fallback if nothing
		// around the vehicle is clear when they get off.
		if (auto* tf = camEnt->GetComponent<Transform>())
		{
			_mountFromPos = tf->GetPosition();
			_hasMountFromPos = true;
		}

		// Seed the smoothed pose at the camera's current eye/look so mounting
		// reads as a glide from where the player is standing.
		if (auto* tf = camEnt->GetComponent<Transform>())
			_camEyeSmoothed = tf->GetPosition() + _camMain->GetViewOffset();
		_camLookSmoothed = _camMain->GetLookDir();
		if (_camLookSmoothed.LengthSquared() < 1e-6f)
			_camLookSmoothed = math::Vector3::Forward;
		_camSmoothInit = true;
		_cameraMounted = true;
	}

	Camera* DrivableComponent::ResolveMountedCamera()
	{
		if (!_cameraMounted)
			return nullptr;
		if (g_pEnv == nullptr || g_pEnv->_sceneManager == nullptr)
			return nullptr;
		auto scene = g_pEnv->_sceneManager->GetCurrentScene();
		if (scene == nullptr)
			return nullptr;
		Camera* cur = scene->GetMainCamera();
		// Not the camera we mounted (e.g. exited play mode -> editor camera), or
		// gone. Only pointer COMPARISON here - never deref the cached pointer,
		// which may already be freed. Stop mounting.
		if (cur == nullptr || cur != _camMain)
		{
			_cameraMounted = false;
			return nullptr;
		}
		Entity* camEnt = cur->GetEntity();
		if (camEnt == nullptr || camEnt->IsPendingDeletion())
		{
			_cameraMounted = false;
			return nullptr;
		}
		return cur;
	}

	void DrivableComponent::DismountCamera()
	{
		if (!_cameraMounted)
			return;

		// Only touch the player/camera if it's still the live main camera - during
		// play-mode teardown those objects may already be freed.
		Camera* cam = ResolveMountedCamera();
		_cameraMounted = false;

		if (cam != nullptr && _camPlayerEntity != nullptr && GetEntity() != nullptr)
		{
			// Hand the camera back: restore the player's eye-height view offset
			// (captured on mount). Zeroing it here dropped the camera to the player
			// transform origin - i.e. on the floor - because the walk controller
			// never re-applies an eye offset of its own.
			cam->SetViewOffset(_camSavedViewOffset);

			// Set the player down beside the vehicle so they're standing when
			// control returns. The spot is collision-checked (see
			// FindSafeDismountPosition) - a raw lateral offset put the player
			// inside walls/railings the vehicle had stopped against. Re-enable
			// simulation + the pose read-back FIRST - setFootPosition is illegal
			// while DISABLE_SIMULATION is set.
			const math::Matrix bikeTM = GetEntity()->GetWorldTM();
			math::Vector3 dismountPos;
			if (!FindSafeDismountPosition(dismountPos))
			{
				// Nothing clear around the vehicle: fall back to where the player
				// mounted from (a known-valid standing spot, re-snapped to the
				// ground in case the world moved), else on top of the vehicle -
				// never a blind offset into geometry.
				bool placed = false;
				if (_hasMountFromPos && g_pEnv->_physicsSystem != nullptr)
				{
					RayHit hit;
					if (g_pEnv->_physicsSystem->RayCastScene(_mountFromPos + math::Vector3(0.0f, 1.0f, 0.0f),
						math::Vector3(0.0f, -1.0f, 0.0f), 4.0f, &hit, _body) > 0)
					{
						dismountPos = hit.position + math::Vector3(0.0f, 0.05f, 0.0f);
						placed = true;
					}
				}
				if (!placed)
					dismountPos = bikeTM.Translation() + math::Vector3(0.0f, 2.0f, 0.0f);
				LOG_INFO("DrivableComponent: no clear spot beside '%s' to dismount - using %s.",
					GetEntity()->GetName().c_str(), placed ? "the mount position" : "the top of the vehicle");
			}

			if (_camPlayerBody != nullptr)
			{
				_camPlayerBody->SetIsSimulated(true);
				_camPlayerBody->SetPoseWritebackEnabled(true);
				_camPlayerBody->UpdatePosePosition(dismountPos);
			}
			if (auto* tf = _camPlayerEntity->GetComponent<Transform>())
				tf->SetPosition(dismountPos);

			if (_camFps != nullptr)
				_camFps->SetControlEnabled(true);
		}

		_camMain = nullptr;
		_camPlayerEntity = nullptr;
		_camFps = nullptr;
		_camPlayerBody = nullptr;
		_camSmoothInit = false;
	}

	void DrivableComponent::Update(float dt)
	{
		InteractionComponent::Update(dt);

		Camera* cam = ResolveMountedCamera();
		if (cam == nullptr)
			return;
		Entity* camEnt = cam->GetEntity();
		if (camEnt == nullptr)
			return;
		auto* camTf = camEnt->GetComponent<Transform>();
		if (camTf == nullptr)
			return;
		Entity* bike = GetEntity();
		if (bike == nullptr)
			return;

		if (dt <= 0.0f)
			dt = 1.0f / 60.0f;

		const math::Matrix bikeTM = bike->GetWorldTM();

		// Target eye = the local seat offset put through the bike transform.
		const math::Vector3 targetEye = math::Vector3::Transform(_tuning.cameraOffset, bikeTM);
		// Target look = the bike's forward (matching the drive direction).
		math::Vector3 targetLook = math::Vector3::TransformNormal(math::Vector3::Forward, bikeTM) * _tuning.forwardSign;
		if (targetLook.LengthSquared() < 1e-6f)
			targetLook = _camLookSmoothed;
		targetLook.Normalize();

		if (!_camSmoothInit)
		{
			_camEyeSmoothed = targetEye;
			_camLookSmoothed = targetLook;
			_camSmoothInit = true;
		}

		// Frame-rate-independent exponential smoothing damps the physics bobble
		// and shapes the mount glide.
		const float ap = 1.0f - std::exp(-std::max(_tuning.cameraPosSmoothing, 0.0f) * dt);
		const float al = 1.0f - std::exp(-std::max(_tuning.cameraLookSmoothing, 0.0f) * dt);
		_camEyeSmoothed += (targetEye - _camEyeSmoothed) * ap;
		_camLookSmoothed += (targetLook - _camLookSmoothed) * al;
		if (_camLookSmoothed.LengthSquared() < 1e-6f)
			_camLookSmoothed = targetLook;
		_camLookSmoothed.Normalize();

		// Move the camera entity transform to the seat. The physics->transform
		// read-back is suppressed for the player body while mounted (see
		// MountCamera / SetPoseWritebackEnabled), so this SetPosition sticks -
		// camera-following effects (froxel volume, weather particles, audio) read
		// the TRANSFORM, so they now track the bike. We can't move the paused CCT
		// itself (setFootPosition is illegal on a DISABLE_SIMULATION actor), which
		// is fine: its capsule just stays parked and out of the way.
		camTf->SetPosition(_camEyeSmoothed);

		// Correct the rendered eye to EXACTLY the seat regardless of the transform:
		// eye = transformPos + viewOffset (Camera::ConstructViewMatrix, and the
		// frustum in BuildFrustum), so this cancels the CCT foot-vs-eye height and
		// any one-frame read-back lag, keeping the eye consistent no matter where
		// the player mounted from.
		cam->SetViewOffset(_camEyeSmoothed - camTf->GetPosition());
		cam->SetLookDirection(_camLookSmoothed, math::Vector3(0.0f, 1.0f, 0.0f));
	}

	void DrivableComponent::StartVehicleAudio()
	{
		if (_vehicleAudioActive)
			return;
		if (g_pEnv == nullptr || g_pEnv->_audioManager == nullptr)
			return;
		_vehicleAudioActive = true;

		auto* am = g_pEnv->_audioManager;
		const math::Vector3 pos = GetEntity() ? GetEntity()->GetWorldTM().Translation() : math::Vector3::Zero;

		// Start a looping 3D layer from a resource path. Master is cached; a fresh
		// playback clone is what actually loops (registered for per-frame 3D).
		auto startLoop = [&](std::shared_ptr<SoundEffect>& master, std::shared_ptr<SoundEffect>& inst,
			const std::string& path, float vol, float pitch)
		{
			inst.reset();
			if (path.empty())
				return;
			if (master == nullptr)
				master = SoundEffect::Create(path);
			if (master == nullptr)
				return;
			inst = master->CreatePlaybackClone();
			if (inst == nullptr)
				return;
			inst->SetRadius(60.0f);
			inst->SetVolume(vol);
			inst->SetPitch(pitch);
			inst->SetPosition(pos);
			am->Loop(inst, pos);
			am->RegisterPlaybackInstance(inst);
		};

		const float master = std::max(_tuning.audioVolume, 0.0f);
		startLoop(_engineMaster, _engineInst, _tuning.engineSoundPath, _tuning.engineVolIdle * master, _tuning.enginePitchIdle);
		startLoop(_windMaster, _windInst, _tuning.windSoundPath, 0.0f, 0.0f);

		// Skid clip is prepared now but only looped on demand (while sliding).
		_skidInst.reset();
		if (!_tuning.skidSoundPath.empty())
		{
			if (_skidMaster == nullptr)
				_skidMaster = SoundEffect::Create(_tuning.skidSoundPath);
			if (_skidMaster != nullptr)
			{
				_skidInst = _skidMaster->CreatePlaybackClone();
				if (_skidInst != nullptr)
				{
					_skidInst->SetRadius(60.0f);
					_skidInst->SetPosition(pos);
					am->RegisterPlaybackInstance(_skidInst);
				}
			}
		}
		_skidActive = false;
	}

	void DrivableComponent::StopVehicleAudio()
	{
		if (g_pEnv != nullptr && g_pEnv->_audioManager != nullptr)
		{
			auto* am = g_pEnv->_audioManager;
			if (_engineInst) am->Stop(_engineInst);
			if (_windInst)   am->Stop(_windInst);
			if (_skidInst)   am->Stop(_skidInst);
		}
		_engineInst.reset();
		_windInst.reset();
		_skidInst.reset();
		_skidActive = false;
		_vehicleAudioActive = false;
	}

	void DrivableComponent::UpdateVehicleAudio()
	{
		if (!_vehicleAudioActive || g_pEnv == nullptr || g_pEnv->_audioManager == nullptr)
			return;

		const math::Vector3 pos = GetEntity() ? GetEntity()->GetWorldTM().Translation() : math::Vector3::Zero;
		const float maxSpeed = std::max(_tuning.maxSpeed, 1.0f);
		const float speedFactor = std::clamp(std::fabs(_forwardSpeed) / maxSpeed, 0.0f, 1.0f);
		const float master = std::max(_tuning.audioVolume, 0.0f);
		const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };

		if (_engineInst != nullptr)
		{
			// Throttle nudges revs up a touch before the bike actually speeds up.
			const float rev = std::clamp(speedFactor + _input.throttle * 0.15f, 0.0f, 1.0f);
			_engineInst->SetPitch(lerp(_tuning.enginePitchIdle, _tuning.enginePitchMax, rev));
			_engineInst->SetVolume(lerp(_tuning.engineVolIdle, _tuning.engineVolMax, rev) * master);
			_engineInst->SetPosition(pos);
		}
		if (_windInst != nullptr)
		{
			_windInst->SetVolume(_tuning.windVolMax * speedFactor * master);
			_windInst->SetPosition(pos);
		}

		// Skid: slide sideways, or handbrake, at speed on the ground.
		const bool skidding = _grounded && std::fabs(_forwardSpeed) > 1.0f &&
			(_lateralSlip > 2.0f || (_input.handbrake && speedFactor > 0.1f));
		if (skidding && _skidInst != nullptr)
		{
			if (!_skidActive)
			{
				g_pEnv->_audioManager->Loop(_skidInst, pos);
				_skidActive = true;
			}
			const float slipAmt = std::clamp(_lateralSlip / 8.0f, 0.2f, 1.0f);
			_skidInst->SetVolume(_tuning.skidVolume * slipAmt * master);
			_skidInst->SetPosition(pos);
		}
		else if (_skidActive && _skidInst != nullptr)
		{
			g_pEnv->_audioManager->Stop(_skidInst);
			_skidActive = false;
		}
	}

	void DrivableComponent::Serialize(json& data, JsonFile* file)
	{
		InteractionComponent::Serialize(data, file);

		SERIALIZE_VALUE(_playerControlled);

		// Per-vehicle handling tuning.
		json& t = data["tuning"];
		file->Serialize(t, "hover", _tuning.hover);
		file->Serialize(t, "groundReach", _tuning.groundReach);
		file->Serialize(t, "rideHeight", _tuning.rideHeight);
		file->Serialize(t, "wheelbase", _tuning.wheelbase);
		file->Serialize(t, "suspStiffness", _tuning.suspStiffness);
		file->Serialize(t, "suspDamping", _tuning.suspDamping);
		file->Serialize(t, "wheelRadius", _tuning.wheelRadius);
		file->Serialize(t, "driveAccel", _tuning.driveAccel);
		file->Serialize(t, "brakeAccel", _tuning.brakeAccel);
		file->Serialize(t, "maxSpeed", _tuning.maxSpeed);
		file->Serialize(t, "reverseSpeed", _tuning.reverseSpeed);
		file->Serialize(t, "grip", _tuning.grip);
		file->Serialize(t, "downforce", _tuning.downforce);
		file->Serialize(t, "steer", _tuning.steer);
		file->Serialize(t, "lean", _tuning.lean);
		file->Serialize(t, "upright", _tuning.upright);
		file->Serialize(t, "steerSmooth", _tuning.steerSmooth);
		file->Serialize(t, "forwardSign", _tuning.forwardSign);
		file->Serialize(t, "mass", _tuning.mass);
		file->Serialize(t, "angularDamp", _tuning.angularDamp);
		file->Serialize(t, "linearDamp", _tuning.linearDamp);
		file->Serialize(t, "comHeight", _tuning.comHeight);
		file->Serialize(t, "chassisFriction", _tuning.chassisFriction);
		file->Serialize(t, "chassisRestitution", _tuning.chassisRestitution);
		file->Serialize(t, "debug", _tuning.debug);
		file->Serialize(t, "cameraOffset", _tuning.cameraOffset);
		file->Serialize(t, "cameraPosSmoothing", _tuning.cameraPosSmoothing);
		file->Serialize(t, "cameraLookSmoothing", _tuning.cameraLookSmoothing);
		file->Serialize(t, "engineSoundPath", _tuning.engineSoundPath);
		file->Serialize(t, "skidSoundPath", _tuning.skidSoundPath);
		file->Serialize(t, "windSoundPath", _tuning.windSoundPath);
		file->Serialize(t, "audioVolume", _tuning.audioVolume);
		file->Serialize(t, "enginePitchIdle", _tuning.enginePitchIdle);
		file->Serialize(t, "enginePitchMax", _tuning.enginePitchMax);
		file->Serialize(t, "engineVolIdle", _tuning.engineVolIdle);
		file->Serialize(t, "engineVolMax", _tuning.engineVolMax);
		file->Serialize(t, "windVolMax", _tuning.windVolMax);
		file->Serialize(t, "skidVolume", _tuning.skidVolume);
	}

	void DrivableComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		InteractionComponent::Deserialize(data, file, mask);

		DESERIALIZE_VALUE(_playerControlled);

		// Per-vehicle handling tuning. Missing keys keep the struct defaults, so
		// older scenes load fine and only overrides are read back.
		if (data.find("tuning") != data.end())
		{
			json& t = data["tuning"];
			file->Deserialize(t, "hover", _tuning.hover);
			file->Deserialize(t, "groundReach", _tuning.groundReach);
			file->Deserialize(t, "rideHeight", _tuning.rideHeight);
			file->Deserialize(t, "wheelbase", _tuning.wheelbase);
			file->Deserialize(t, "suspStiffness", _tuning.suspStiffness);
			file->Deserialize(t, "suspDamping", _tuning.suspDamping);
			file->Deserialize(t, "wheelRadius", _tuning.wheelRadius);
			file->Deserialize(t, "driveAccel", _tuning.driveAccel);
			file->Deserialize(t, "brakeAccel", _tuning.brakeAccel);
			file->Deserialize(t, "maxSpeed", _tuning.maxSpeed);
			file->Deserialize(t, "reverseSpeed", _tuning.reverseSpeed);
			file->Deserialize(t, "grip", _tuning.grip);
			file->Deserialize(t, "downforce", _tuning.downforce);
			file->Deserialize(t, "steer", _tuning.steer);
			file->Deserialize(t, "lean", _tuning.lean);
			file->Deserialize(t, "upright", _tuning.upright);
			file->Deserialize(t, "steerSmooth", _tuning.steerSmooth);
			file->Deserialize(t, "forwardSign", _tuning.forwardSign);
			file->Deserialize(t, "mass", _tuning.mass);
			file->Deserialize(t, "angularDamp", _tuning.angularDamp);
			file->Deserialize(t, "linearDamp", _tuning.linearDamp);
			file->Deserialize(t, "comHeight", _tuning.comHeight);
			file->Deserialize(t, "chassisFriction", _tuning.chassisFriction);
			file->Deserialize(t, "chassisRestitution", _tuning.chassisRestitution);
			file->Deserialize(t, "debug", _tuning.debug);
			file->Deserialize(t, "cameraOffset", _tuning.cameraOffset);
			file->Deserialize(t, "cameraPosSmoothing", _tuning.cameraPosSmoothing);
			file->Deserialize(t, "cameraLookSmoothing", _tuning.cameraLookSmoothing);
			file->Deserialize(t, "engineSoundPath", _tuning.engineSoundPath);
			file->Deserialize(t, "skidSoundPath", _tuning.skidSoundPath);
			file->Deserialize(t, "windSoundPath", _tuning.windSoundPath);
			file->Deserialize(t, "audioVolume", _tuning.audioVolume);
			file->Deserialize(t, "enginePitchIdle", _tuning.enginePitchIdle);
			file->Deserialize(t, "enginePitchMax", _tuning.enginePitchMax);
			file->Deserialize(t, "engineVolIdle", _tuning.engineVolIdle);
			file->Deserialize(t, "engineVolMax", _tuning.engineVolMax);
			file->Deserialize(t, "windVolMax", _tuning.windVolMax);
			file->Deserialize(t, "skidVolume", _tuning.skidVolume);
		}
	}

	bool DrivableComponent::CreateWidget(ComponentWidget* widget)
	{
		InteractionComponent::CreateWidget(widget);

		const int32_t w = widget->GetSize().x - 20;

		// Mark this vehicle as the player's - possessed on play, and live in
		// the editor via SetPlayerControlled.
		Checkbox* pc = new Checkbox(widget, widget->GetNextPos(), Point(w, 18),
			L"Player controlled (possess on play)", &_playerControlled);
		pc->SetPrefabOverrideBinding(GetComponentName(), "/_playerControlled");
		pc->SetOnCheckFn([this](Checkbox*, bool value) { SetPlayerControlled(value); });

		// Possess this vehicle for a test drive, releasing any other so only
		// one is driven at a time.
		new Button(widget, widget->GetNextPos(), Point(w, 20), L"Possess (drive this)",
			[this](Button*) -> bool
			{
				if (g_pEnv != nullptr && g_pEnv->_sceneManager != nullptr)
				{
					if (auto scene = g_pEnv->_sceneManager->GetCurrentScene())
					{
						std::vector<DrivableComponent*> all;
						scene->GetComponents<DrivableComponent>(all);
						for (auto* d : all)
							if (d != nullptr && d != this)
								d->SetPlayerControlled(false);
					}
				}
				SetPlayerControlled(true);
				return true;
			});

		new Button(widget, widget->GetNextPos(), Point(w, 20), L"Release",
			[this](Button*) -> bool
			{
				SetPlayerControlled(false);
				return true;
			});

		// --- per-vehicle handling tuning (was the global v_bike* cvars) --------
		// Each field is bound straight to a _tuning member; FixedUpdate reads them
		// live, so dragging retunes this instance in real time and the values
		// serialise with the prefab/scene.
		auto addF = [&](const wchar_t* label, float* v, float mn, float mx, float step)
		{
			new DragFloat(widget, widget->GetNextPos(), Point(w, 18), label, v, mn, mx, step);
		};

		new Checkbox(widget, widget->GetNextPos(), Point(w, 18), L"Hover (arcade float) instead of rest on collider", &_tuning.hover);

		addF(L"Ground reach (m)", &_tuning.groundReach, 0.2f, 6.0f, 0.05f);
		addF(L"Ride height (m)", &_tuning.rideHeight, 0.1f, 2.0f, 0.02f);
		addF(L"Wheelbase (m)", &_tuning.wheelbase, 0.4f, 4.0f, 0.02f);
		addF(L"Susp stiffness", &_tuning.suspStiffness, 1.0f, 200.0f, 1.0f);
		addF(L"Susp damping", &_tuning.suspDamping, 0.0f, 60.0f, 0.5f);
		addF(L"Wheel radius (m)", &_tuning.wheelRadius, 0.05f, 1.0f, 0.01f);

		addF(L"Drive accel", &_tuning.driveAccel, 1.0f, 120.0f, 1.0f);
		addF(L"Brake accel", &_tuning.brakeAccel, 1.0f, 120.0f, 1.0f);
		addF(L"Max speed (m/s)", &_tuning.maxSpeed, 1.0f, 90.0f, 1.0f);
		addF(L"Reverse speed (m/s)", &_tuning.reverseSpeed, 0.0f, 20.0f, 0.5f);
		addF(L"Grip", &_tuning.grip, 0.0f, 40.0f, 0.5f);
		addF(L"Downforce", &_tuning.downforce, 0.0f, 40.0f, 0.5f);

		addF(L"Steer authority", &_tuning.steer, 0.1f, 120.0f, 1.0f);
		addF(L"Lean (rad)", &_tuning.lean, 0.0f, 1.2f, 0.02f);
		addF(L"Upright gain", &_tuning.upright, 1.0f, 200.0f, 1.0f);
		addF(L"Steer smoothing", &_tuning.steerSmooth, 1.0f, 30.0f, 0.5f);
		addF(L"Forward sign (-1 / +1)", &_tuning.forwardSign, -1.0f, 1.0f, 1.0f);

		addF(L"Mass (kg)", &_tuning.mass, 5.0f, 2000.0f, 5.0f);
		addF(L"Angular damping", &_tuning.angularDamp, 0.0f, 30.0f, 0.5f);
		addF(L"Linear damping", &_tuning.linearDamp, 0.0f, 4.0f, 0.05f);
		addF(L"CoM height", &_tuning.comHeight, -1.5f, 1.0f, 0.02f);
		addF(L"Chassis friction", &_tuning.chassisFriction, 0.0f, 1.0f, 0.01f);
		addF(L"Chassis restitution", &_tuning.chassisRestitution, 0.0f, 1.0f, 0.01f);

		addF(L"Camera offset X", &_tuning.cameraOffset.x, -3.0f, 3.0f, 0.02f);
		addF(L"Camera offset Y", &_tuning.cameraOffset.y, -1.0f, 3.0f, 0.02f);
		addF(L"Camera offset Z", &_tuning.cameraOffset.z, -3.0f, 3.0f, 0.02f);
		addF(L"Camera pos smoothing", &_tuning.cameraPosSmoothing, 0.5f, 30.0f, 0.5f);
		addF(L"Camera look smoothing", &_tuning.cameraLookSmoothing, 0.5f, 30.0f, 0.5f);

		// --- audio: assign clips + tune. Empty path = that layer stays silent.
		// Changing a path reloads the master and (if currently possessed) restarts
		// the loops so the change previews live.
		auto addSound = [&](const wchar_t* label, std::string* path, std::shared_ptr<SoundEffect>* master)
		{
			auto* search = new AssetSearch(widget, widget->GetNextPos(), Point(w, 84), label, { ResourceType::Audio },
				[this, path, master](AssetSearch*, const AssetSearchResult& result)
				{
					const fs::path& chosen = !result.assetPath.empty() ? result.assetPath : result.absolutePath;
					*path = chosen.string();
					master->reset();
					if (_vehicleAudioActive) { StopVehicleAudio(); StartVehicleAudio(); }
				});
			if (!path->empty())
				search->SetValue(std::wstring(path->begin(), path->end()));
		};

		addSound(L"Engine sound (loop)", &_tuning.engineSoundPath, &_engineMaster);
		addSound(L"Skid sound (loop)", &_tuning.skidSoundPath, &_skidMaster);
		addSound(L"Wind sound (loop)", &_tuning.windSoundPath, &_windMaster);

		addF(L"Audio volume", &_tuning.audioVolume, 0.0f, 2.0f, 0.02f);
		addF(L"Engine pitch idle", &_tuning.enginePitchIdle, -1.0f, 1.0f, 0.02f);
		addF(L"Engine pitch max", &_tuning.enginePitchMax, -1.0f, 1.0f, 0.02f);
		addF(L"Engine vol idle", &_tuning.engineVolIdle, 0.0f, 1.0f, 0.02f);
		addF(L"Engine vol max", &_tuning.engineVolMax, 0.0f, 1.0f, 0.02f);
		addF(L"Wind vol max", &_tuning.windVolMax, 0.0f, 1.0f, 0.02f);
		addF(L"Skid volume", &_tuning.skidVolume, 0.0f, 1.0f, 0.02f);

		new Checkbox(widget, widget->GetNextPos(), Point(w, 18), L"Debug log (grounded/speed/ray)", &_tuning.debug);

		return true;
	}

	// ---- dismount placement ---------------------------------------------------
	//
	// The physics layer exposes rays only (no sweeps/overlaps), so clearance is
	// built from several: every test starts from the SEAT - a point we know is
	// free because the vehicle occupies it - and works outward, so a spot can
	// only be accepted if there is an unobstructed path to it from the vehicle.
	namespace
	{
		// Conservative standing-capsule envelope for the clearance rays. The
		// player's actual CCT is a touch smaller; erring large keeps us out of
		// geometry that a tight fit would graze.
		constexpr float kDismountPlayerRadius = 0.40f;
		constexpr float kDismountPlayerHeight = 1.90f;
		constexpr float kDismountGroundSearch = 3.0f;   // max drop below the seat height to find a floor
		constexpr float kDismountSeatHeight   = 0.90f;  // seat above the vehicle origin
	}

	bool DrivableComponent::IsDismountSpotClear(const math::Vector3& seat, const math::Vector3& dir, float lateral, math::Vector3& outFeet) const
	{
		auto* phys = g_pEnv != nullptr ? g_pEnv->_physicsSystem : nullptr;
		if (phys == nullptr)
			return false;
		RayHit hit;

		// 1. Approach: seat -> candidate must be unobstructed out to where the
		//    capsule's far edge will sit. Anything in the way (wall, railing,
		//    another vehicle) rejects the spot outright.
		const float approach = lateral + kDismountPlayerRadius;
		if (phys->RayCastScene(seat, dir, approach, &hit, _body) > 0)
			return false;

		// 2. Ground: drop from the candidate at seat height and find a floor
		//    within reach. No floor = edge/void, reject rather than fall.
		const math::Vector3 hip = seat + dir * lateral;
		if (phys->RayCastScene(hip, math::Vector3(0.0f, -1.0f, 0.0f), kDismountSeatHeight + kDismountGroundSearch, &hit, _body) == 0)
			return false;
		const math::Vector3 feet = hit.position + math::Vector3(0.0f, 0.05f, 0.0f);

		// 3. Headroom: a full standing capsule above the feet.
		if (phys->RayCastScene(feet + math::Vector3(0.0f, 0.10f, 0.0f), math::Vector3(0.0f, 1.0f, 0.0f), kDismountPlayerHeight, &hit, _body) > 0)
			return false;

		// 4. Body clearance: horizontal rays from the capsule's mid-height in
		//    the four directions around the spot (approach axis + its
		//    perpendicular). Catches thin posts and wall corners the approach
		//    ray passed beside.
		const math::Vector3 mid = feet + math::Vector3(0.0f, kDismountPlayerHeight * 0.5f, 0.0f);
		const math::Vector3 side(-dir.z, 0.0f, dir.x);
		const math::Vector3 probes[4] = { dir, -dir, side, -side };
		for (const math::Vector3& p : probes)
		{
			if (phys->RayCastScene(mid, p, kDismountPlayerRadius + 0.10f, &hit, _body) > 0)
				return false;
		}

		outFeet = feet;
		return true;
	}

	bool DrivableComponent::FindSafeDismountPosition(math::Vector3& outFeet) const
	{
		if (GetEntity() == nullptr)
			return false;

		const math::Matrix tm = GetEntity()->GetWorldTM();
		auto flatten = [](math::Vector3 v, const math::Vector3& fallback)
		{
			v.y = 0.0f;
			if (v.LengthSquared() > 1e-6f) { v.Normalize(); return v; }
			return fallback;
		};
		const math::Vector3 right   = flatten(math::Vector3::TransformNormal(math::Vector3::Right, tm), math::Vector3::Right);
		const math::Vector3 forward = flatten(math::Vector3::TransformNormal(math::Vector3::Forward, tm), math::Vector3::Forward);
		const math::Vector3 seat = tm.Translation() + math::Vector3(0.0f, kDismountSeatHeight, 0.0f);

		// Natural sides first at a comfortable distance, then the ends, then
		// progressively further out. Rear before front: stepping off behind a
		// vehicle that has just stopped against something is the safer habit.
		const math::Vector3 dirs[4] = { right, -right, -forward, forward };
		const float lateral[3] = { 1.2f, 1.8f, 2.5f };
		for (float l : lateral)
		{
			for (const math::Vector3& d : dirs)
			{
				if (IsDismountSpotClear(seat, d, l, outFeet))
					return true;
			}
		}
		return false;
	}

	void DrivableComponent::FixedUpdate(float dt)
	{
		InteractionComponent::FixedUpdate(dt);

		if (_dismountRequested)
		{
			_dismountRequested = false;
			SetPlayerControlled(false);
		}

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

		// Keep the chassis collider slippery + non-bouncy (see the cvar note).
		// Only re-push when a value actually changes - each apply takes a scene
		// write lock and recreates the PhysX material.
		{
			const float fr = _tuning.chassisFriction;
			const float re = _tuning.chassisRestitution;
			if (fr != _appliedFriction || re != _appliedRestitution)
			{
				IRigidBody::PhysicalProperties props;
				props.staticFriction = fr;
				props.dynamicFriction = fr;
				props.restitution = re;
				_body->SetPhysicalProperties(props);
				_appliedFriction = fr;
				_appliedRestitution = re;
			}
		}

		if (_tuning.debug && _playerControlled)
		{
			_debugAccum += dt;
			if (_debugAccum >= 1.0f)
			{
				_debugAccum = 0.0f;
				LOG_INFO("bike: grounded=%d(hits=%d) speed=%.2f  ray[bottomY=%.2f fromY=%.2f toY=%.2f hitY=%.2f dist=%.2f on='%s']  in(thr=%.2f brk=%.2f steer=%.2f hb=%d)",
					_grounded ? 1 : 0, _dbgHits, _forwardSpeed,
					_dbgBottomY, _dbgFromY, _dbgToY, _dbgHitY, _dbgHitDist, _dbgHitEntity.c_str(),
					_input.throttle, _input.brake, _input.steer, _input.handbrake ? 1 : 0);
			}
		}

		// --- player input -> intent (V1 self-drive path) ---
		if (_playerControlled)
		{
			const float steerTarget = (_kRight ? 1.0f : 0.0f) - (_kLeft ? 1.0f : 0.0f);
			const float a = std::clamp(_tuning.steerSmooth * dt, 0.0f, 1.0f);
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

		// Which local axis is the vehicle's nose is model-dependent, so the
		// forward direction is sign-configurable (v_bikeForwardSign). Applied to
		// the whole forward basis so drive, lean and speed stay consistent.
		const math::Vector3 fwd   = math::Vector3::Transform(math::Vector3::Forward, rot) * _tuning.forwardSign;
		const math::Vector3 right = math::Vector3::Transform(math::Vector3::Right, rot);
		const math::Vector3 up    = math::Vector3::Transform(math::Vector3::Up, rot);
		const math::Vector3 worldUp(0.0f, 1.0f, 0.0f);

		// Centre of mass estimate: origin + a downward offset for stability
		// (no CoM accessor on the interface).
		const math::Vector3 com = pos + math::Vector3::Transform(math::Vector3(0.0f, _tuning.comHeight, 0.0f), rot);

		const auto applyForceAtPoint = [&](const math::Vector3& F, const math::Vector3& worldPoint)
		{
			_body->ApplyForceToCenterOfMass(F);
			_body->ApplyTorque((worldPoint - com).Cross(F));
		};

		_body->WakeUp();
		_body->SetLinearVelocityDamping(_tuning.linearDamp);
		_body->SetAngularVelocityDamping(_tuning.angularDamp);
		_body->SetMaxLinearVelocity(_tuning.maxSpeed * 1.4f);

		// --- ground sensing (+ hover suspension when enabled) ---
		// Wheel rays feed two things: the grounded flag / ground normal (always),
		// and - in hover mode only - a spring+damper lift. With hover OFF the bike
		// rests on its physical capsule, so the ray applies at most light vertical
		// bounce damping (never a constant lift), which is why it no longer floats.
		RebuildWheels();
		const bool hover = _tuning.hover;
		const float rideHeight = _tuning.rideHeight;
		const float wheelRadius = _tuning.wheelRadius;
		const float stiffness = _tuning.suspStiffness;
		const float suspDamp = _tuning.suspDamping;

		// Non-hover fires the ground ray from the WHEEL LINE - the bottom of the
		// scaled mesh AABB - not the body origin. On a tall mesh the origin sits
		// well above the ground, so an origin-relative ray only reached the
		// terrain with an absurd wheel radius (the "grounded needs radius 5"
		// symptom). Hover keeps its origin-relative spring.
		float bottomLocalY = 0.0f;
		if (!hover)
		{
			const dx::BoundingBox lb = GetEntity()->GetAABB();
			const math::Vector3 scl = GetEntity()->GetAbsoluteScale();
			bottomLocalY = (lb.Center.y - lb.Extents.y) * scl.y;
		}
		const float maxDrop = hover ? (rideHeight + wheelRadius) : _tuning.groundReach;
		// Start the non-hover ray a little above the wheel line so it straddles
		// the surface even when the capsule is resting right on it.
		const float probeUp = hover ? 0.0f : 0.3f;

		int grounded = 0;
		math::Vector3 groundNormalAccum(0.0f, 0.0f, 0.0f);
		_dbgBottomY = bottomLocalY;
		_dbgHitDist = -1.0f;
		_dbgHitY = 0.0f;
		_dbgHitEntity = "(none)";
		bool capturedRay = false;
		for (const math::Vector3& wl : _wheelsLocal)
		{
			math::Vector3 wlAdj = wl;
			wlAdj.y += bottomLocalY;
			const math::Vector3 anchor = pos + math::Vector3::Transform(wlAdj, rot);
			const math::Vector3 from = anchor + worldUp * probeUp;
			const math::Vector3 to = anchor - worldUp * maxDrop;

			if (!capturedRay) { _dbgFromY = from.y; _dbgToY = to.y; capturedRay = true; }

			// Use the scene-wide PhysX raycast (not PhysUtils::RayCast, which only
			// scans StaticMeshComponent entities and so misses volumetric terrain
			// colliders that live directly in the PhysX scene). Ignore our own body.
			RayHit hit;
			const float rayLen = probeUp + maxDrop;
			if (g_pEnv->_physicsSystem->RayCastScene(from, -worldUp, rayLen, &hit, _body) > 0)
			{
				if (_dbgHitDist < 0.0f)
				{
					_dbgHitDist = hit.distance;
					_dbgHitY = hit.position.y;
					_dbgHitEntity = hit.entity ? hit.entity->GetName() : "(null)";
				}
				if (hover)
				{
					const float d = std::max(hit.distance, 0.0f);
					const float compression = std::clamp((maxDrop - d) / maxDrop, 0.0f, 1.0f);
					// Damp the body's vertical motion so the spring settles.
					const float springAccel = compression * stiffness;
					const float dampAccel = -vel.y * suspDamp;
					const float upAccel = std::max(springAccel + dampAccel, 0.0f);
					applyForceAtPoint(worldUp * (upAccel * mass), anchor);
				}
				else
				{
					// Physical rest: no lift. Damp vertical bounce at the CENTRE of
					// mass (not at-point), so it can't inject a pitching torque -
					// applying it at each wheel is what made the bike bob nose
					// up/down even on flat ground. At rest vel.y~=0, so no force.
					const float dampAccel = -vel.y * suspDamp * 0.5f;
					_body->ApplyForceToCenterOfMass(worldUp * (dampAccel * mass));
				}

				++grounded;
				groundNormalAccum += hit.normal;
			}
		}

		// Coyote grounding: a wheel touching this frame resets the air timer;
		// otherwise we stay "grounded" for a short window so rolling off a curb
		// keeps drive/steer authority (the bike launches over the edge instead
		// of dead-stopping the instant the ray clears the ground).
		_dbgHits = grounded;
		const bool contact = grounded > 0;
		if (contact)
			_airTime = 0.0f;
		else
			_airTime += dt;
		_grounded = contact || (_airTime < 0.2f);

		math::Vector3 groundNormal = worldUp;
		if (contact)
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
		const float maxSpeed = _tuning.maxSpeed;
		const float speedFactor = std::clamp(std::fabs(_forwardSpeed) / std::max(maxSpeed, 1.0f), 0.0f, 1.0f);

		if (_grounded)
		{
			// Drive / brake / reverse.
			if (_input.throttle > 0.001f && _forwardSpeed < maxSpeed)
			{
				applyForceAtPoint(fwdGround * (_input.throttle * _tuning.driveAccel * mass), com);
			}
			if (_input.brake > 0.001f)
			{
				if (_forwardSpeed > 0.3f)
				{
					// Brake: decelerate along travel.
					applyForceAtPoint(fwdGround * (-_input.brake * _tuning.brakeAccel * mass), com);
				}
				else if (_forwardSpeed > -_tuning.reverseSpeed)
				{
					// Reverse (slower than forward).
					applyForceAtPoint(fwdGround * (-_input.brake * _tuning.driveAccel * 0.5f * mass), com);
				}
			}

			// Lateral grip - kill sideways velocity (relaxed on handbrake).
			const float lateral = vel.Dot(right);
			_lateralSlip = std::fabs(lateral); // feeds the tyre-skid audio
			const float gripScale = _input.handbrake ? 0.35f : 1.0f;
			applyForceAtPoint(right * (-lateral * _tuning.grip * gripScale * mass), com);

			// Planted at speed.
			if (_tuning.downforce > 0.0f)
				_body->ApplyForceToCenterOfMass(-groundNormal * (_tuning.downforce * speedFactor * mass));
		}

		// Steering: speed-scaled yaw torque (can turn a little at a standstill).
		// Multiply by the forward sign so the yaw handedness matches the driving
		// direction AND the lean below (the lean is built about `fwd`, which is
		// already signed) - otherwise flipping v_bikeForwardSign turned the bike
		// one way while it leaned the other.
		const float steerAuthority = 0.5f + 1.0f * speedFactor;
		const float steerDir = (_forwardSpeed >= -0.3f) ? 1.0f : -1.0f;
		_body->ApplyTorque(up * (_input.steer * _tuning.steer * steerAuthority * steerDir * mass * _tuning.forwardSign));

		// Upright + lean: proportional torque toward a target up-vector. The
		// heavy angular damping above turns this pure-P controller into a
		// stable spring, so the bike banks into turns and never topples.
		math::Vector3 targetGroundUp = math::Vector3::Lerp(worldUp, groundNormal, 0.5f);
		if (targetGroundUp.LengthSquared() > 1e-5f)
			targetGroundUp.Normalize();
		const float leanRoll = _input.steer * _tuning.lean * speedFactor;
		const math::Quaternion leanQ = math::Quaternion::CreateFromAxisAngle(fwd, leanRoll);
		math::Vector3 targetUp = math::Vector3::Transform(targetGroundUp, leanQ);
		const math::Vector3 uprightAxis = up.Cross(targetUp); // ~sin(angle) * rotation axis
		_body->ApplyTorque(uprightAxis * (_tuning.upright * mass));

		if (!_grounded)
			_lateralSlip = 0.0f; // no skid noise while airborne

		// Drive the engine/wind/skid loops from this frame's speed + slip.
		UpdateVehicleAudio();
	}
}
