

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

		void AddInputFlag(MoveFlag flag);
		void RemoveInputFlag(MoveFlag flag);

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(class ComponentWidget* widget) override;

	private:
		// Plays one footstep clone (pitch-randomised) and keeps it alive.
		void PlayFootstep();
		// Drops finished footstep clones (AudioManager only holds weak refs).
		void PruneFinishedFootsteps();
		// Samples a material's surface-ID map (red channel) at a UV and returns the
		// surface id, or -1 if the map can't be loaded. Decoded pixels are cached.
		int32_t SampleFootstepSurfaceId(const std::string& mapPath, float u, float v);

	private:
		float _movementSpeed = 4.0f;
		float _runMultiplier = 2.0f;
		float _strafeMovementSpeed = 3.0f;
		float _verticalMovementSpeed = 4.0f;
		float _gravityAcceleration = 9.81f * 2.0f;
		float _verticalVelocity = 0.0f;
		MoveFlag _flags = MoveNone;
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
	};
}
