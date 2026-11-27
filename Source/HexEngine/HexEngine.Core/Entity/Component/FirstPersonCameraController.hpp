

#pragma once

#include "UpdateComponent.hpp"
#include "../../Input/InputSystem.hpp"
#include <string>
#include <memory>
#include <vector>
#include <map>

namespace HexEngine
{
	enum MoveFlag
	{
		MoveNone = 0,
		MoveForwards = HEX_BITSET(0),
		MoveBackwards = HEX_BITSET(1),
		MoveLeft = HEX_BITSET(2),
		MoveRight = HEX_BITSET(3),
		MoveUp = HEX_BITSET(4),
		MoveDown = HEX_BITSET(5),
		MoveSprint = HEX_BITSET(6),
	};
	DEFINE_ENUM_FLAG_OPERATORS(MoveFlag);

	class HEX_API FirstPersonCameraController : public UpdateComponent, public IInputListener
	{
	public:
		CREATE_COMPONENT_ID(FirstPersonCameraController);

		FirstPersonCameraController(Entity* entity);

		FirstPersonCameraController(Entity* entity, FirstPersonCameraController* clone);

		virtual ~FirstPersonCameraController();

		virtual bool OnInputEvent(InputEvent event, InputData* data) override;

		virtual void Update(float frameTime) override;
		virtual void FixedUpdate(float frameTime) override;
		virtual bool WantsPostUpdate() const override { return !_carrierName.empty(); }
		virtual void PostUpdate(float frameTime) override;

		// The moving platform (kinematic bus / train floor) the player is riding, or null.
		Entity* GetCarrier() const;

		void AddInputFlag(MoveFlag flag);
		void RemoveInputFlag(MoveFlag flag);

		// Suspend player movement + mouse-look without destroying the component,
		// so another system (e.g. DrivableComponent while the player is riding a
		// vehicle) can take over the camera. Clears pending move flags on disable.
		void SetControlEnabled(bool enabled);
		bool IsControlEnabled() const { return _controlEnabled; }

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(class ComponentWidget* widget) override;

	private:
		// (Re)binds the WASD/Space/Ctrl/Shift movement keys to this controller.
		// Called at construction and again whenever control is re-enabled, so
		// returning from something that stole the keys (e.g. a mounted vehicle,
		// which unbinds them on dismount) restores walking control.
		void CreateMovementBinds();
		// Plays one footstep clone (pitch-randomised) and keeps it alive.
		void PlayFootstep();
		// Drops finished footstep clones (AudioManager only holds weak refs).
		void PruneFinishedFootsteps();
		// Samples a material's surface-ID map (red channel) at a UV and returns the
		// surface id, or -1 if the map can't be loaded. Decoded pixels are cached.
		int32_t SampleFootstepSurfaceId(const std::string& mapPath, float u, float v);

		// Moving platforms. Standing on a KINEMATIC body (a bus or train floor
		// moved by game code) makes it the carrier: the player keeps their spot in
		// the carrier's local frame and turns with it. Applied every frame in
		// PostUpdate (after the vehicle moved, before rendering - no lag/jitter)
		// and again at the start of each physics tick. Survives short hops.
		static Entity* FindCarrierFor(class IRigidBody* groundBody);
		static float CarrierYaw(const math::Matrix& worldTM);
		void UpdateCarrierAfterMove(class IRigidBody* body, bool onGround, float frameTime);
		void FollowCarrier(class IRigidBody* body);
		void ClearCarrier(const char* reason);

	private:
		float _movementSpeed = 4.0f;
		float _runMultiplier = 2.0f;
		float _strafeMovementSpeed = 3.0f;
		float _verticalMovementSpeed = 4.0f;
		float _gravityAcceleration = 9.81f * 2.0f;
		float _verticalVelocity = 0.0f;
		MoveFlag _flags = MoveNone;
		bool _controlEnabled = true;
		//float _pitchSensitivity = 200.0f;
		//float _yawSensitivity = 200.0f;

		// --- Footsteps (surface-aware) ---
		// A step plays every _strideLength metres of grounded horizontal travel,
		// so cadence rises naturally with speed (sprinting covers more ground).
		// Each step raycasts down _footstepRayDistance metres; if the hit
		// material specifies its own footstep sound that one is used, otherwise
		// _footstepSoundPath (the controller default) plays.
		std::string _footstepSoundPath;               // default sound (resource path), editor-assignable
		float       _strideLength = 2.0f;             // metres of grounded travel between steps
		float       _footstepVolume = 0.5f;
		float       _footstepPitchVariation = 0.08f;  // +/- DirectX semi-octave pitch units per step
		float       _footstepRayDistance = 3.0f;      // down-ray length to find the surface under the player
		// Master sounds keyed by resource path (controller default + each
		// material's), loaded on first use and reused.
		std::map<std::string, std::shared_ptr<class SoundEffect>> _footstepSoundCache;
		std::vector<std::shared_ptr<class SoundEffect>> _activeFootsteps; // in-flight clones held alive
		float _distanceSinceStep = 0.0f;

		// CPU-decoded surface-ID maps (atlas / splatmap footstep regions), keyed by
		// texture path. width/height + RGBA pixels; sampled in SampleFootstepSurfaceId.
		struct FootstepSurfaceMap
		{
			int32_t width = 0;
			int32_t height = 0;
			bool isBgra = false;
			bool loaded = false;   // decode attempted (success or failure)
			bool valid = false;    // pixels usable
			std::vector<uint8_t> pixels;
		};
		std::map<std::string, FootstepSurfaceMap> _footstepSurfaceMapCache;

		std::string   _carrierName;              // entity name (entities can be despawned - never hold the pointer)
		math::Vector3 _carrierLocalFoot;         // foot position in the carrier's local frame
		float         _carrierYaw = 0.0f;        // carrier yaw (rad) when the camera yaw was last synced
		float         _carrierAirTime = 0.0f;    // seconds off its floor (a hop inside the bus)
		bool          _interpolationBeforeCarry = true;
		// Low cabins (the kit bus has ~1.6 m of headroom): the eye is lowered to
		// fit under the ceiling while riding and restored on getting off.
		bool          _eyeLowered = false;
		float         _defaultEyeHeight = 0.0f;
		void FitEyeUnderCeiling();
		void RestoreEyeHeight();
	};
}
