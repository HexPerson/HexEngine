

#pragma once

#include "InteractionComponent.hpp"
#include <string>
#include <vector>
#include <memory>

namespace HexEngine
{
	class SoundEffect;

	// An interactable door. Inherits InteractionComponent, so it gets the look-at
	// SDF outline glow, on-screen name + prompt, and interact key for free. On
	// interact it swings open/closed and plays optional open/close sounds.
	//
	// The door swings around a vertical hinge located at _hingeOffset (a point in
	// the door's local space, relative to the entity origin). This means the mesh
	// origin does NOT need to be at the hinge - common when a door was authored as
	// part of a larger house mesh and its origin sits elsewhere. Set _hingeOffset
	// to the hinge edge (the editor gizmo draws where it currently is). An offset
	// of zero reduces to a plain in-place rotation about the origin.
	//
	// Collision moves with it if the entity also has a (kinematic) RigidBody; a
	// plain visual door just swings.
	class HEX_API DoorComponent : public InteractionComponent
	{
	public:
		CREATE_COMPONENT_ID(DoorComponent);
		DEFINE_COMPONENT_CTOR(DoorComponent);

		virtual void Update(float frameTime) override;
		// Fired by the interaction system when the player presses the interact key
		// while looking at this door. Toggles open/closed.
		virtual void Callback() override;

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(ComponentWidget* widget) override;
		// Draws the hinge point + swing axis so the offset can be placed by eye.
		virtual void OnRenderEditorGizmo(bool isSelected, bool& isHovering) override;

		void Open();
		void Close();
		void Toggle();
		bool IsDoorOpen() const { return _isOpen; }

	private:
		// Applies the current eased progress (_t) to the transform: slerps the
		// rotation from closed->open and orbits the position around the hinge.
		void ApplyAngle();
		// Removes/restores the door mesh from GI for the duration of a swing so the
		// per-frame movement doesn't re-voxelise the clipmap every frame.
		void SetSwingGiExclusion(bool excluded);
		void PlayDoorSound(const std::string& path, std::shared_ptr<SoundEffect>& master);

	private:
		// --- Config (serialised + editor-exposed) ---
		float         _openAngle   = 90.0f;  // swing angle, degrees (sign = swing direction)
		float         _swingSpeed  = 220.0f; // degrees per second (sets swing duration)
		int32_t       _easing      = 5;      // easing_functions value (5 = EaseInOutQuad); -1 = linear. See Math/easing.h
		bool          _startOpen   = false;  // initial state when play begins
		float         _soundVolume = 0.8f;
		math::Vector3 _hingeOffset = math::Vector3::Zero; // hinge point in local space (origin->hinge)
		std::string   _openSoundPath;        // resource path, editor-assignable
		std::string   _closeSoundPath;

		// --- Runtime ---
		bool             _isOpen         = false; // target state
		float            _t              = 0.0f;  // swing progress, 0 = closed .. 1 = open
		bool             _closedCaptured = false; // captured the closed pose this play session
		math::Vector3    _closedPosition;         // authored (closed) local position to swing from
		math::Quaternion _closedRotation;         // authored (closed) local rotation to swing from
		math::Quaternion _openRotation;           // fully-open local rotation (slerp endpoint)

		// While the door is mid-swing it's pulled out of GI so its per-frame motion
		// doesn't thrash the voxel clipmap; restored to its authored GI state once
		// it settles. _meshOriginalExclude remembers that authored state.
		bool             _giExcludedForSwing  = false;
		bool             _meshOriginalExclude = false;

		// Master sounds (lazy-loaded) + in-flight clones held alive (AudioManager
		// keeps only weak refs).
		std::shared_ptr<SoundEffect> _openSound;
		std::shared_ptr<SoundEffect> _closeSound;
		std::vector<std::shared_ptr<SoundEffect>> _activeSounds;
	};
}
