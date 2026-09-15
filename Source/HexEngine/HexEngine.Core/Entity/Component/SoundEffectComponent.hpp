#pragma once

#include "UpdateComponent.hpp"
#include "../../Audio/SoundEffect.hpp"

namespace HexEngine
{
	/**
	 * @brief Places a SoundEffect in the world on an entity.
	 *
	 * Editor-authored wrapper over the AudioManager / SoundEffect playback API:
	 * picks a sound asset, plays it 2D or as a 3D emitter that follows the
	 * entity, with volume / pitch / falloff radius / loop / play-on-start all
	 * exposed in the inspector and serialized with the entity. Play / Stop are
	 * also available as preview buttons in edit mode.
	 *
	 * Each component owns its own playback clone of the (shared, cached)
	 * master resource, so many entities can play the same .wav independently.
	 */
	class HEX_API SoundEffectComponent : public UpdateComponent
	{
	public:
		CREATE_COMPONENT_ID(SoundEffectComponent);
		DEFINE_COMPONENT_CTOR(SoundEffectComponent);

		virtual ~SoundEffectComponent();
		virtual void Destroy() override;

		virtual void Update(float frameTime) override;

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(ComponentWidget* widget) override;
		virtual void OnRenderEditorGizmo(bool isSelected, bool& isHovering) override;

		// Playback control (usable from scripts / other components).
		void Play();
		void Stop();
		bool IsPlaying() const;

		void SetSoundPath(const std::string& path);
		const std::string& GetSoundPath() const { return _soundPath; }

		void SetIs3D(bool is3D);
		bool GetIs3D() const { return _is3D; }
		void SetLoop(bool loop);
		bool GetLoop() const { return _loop; }
		void SetPlayOnStart(bool playOnStart) { _playOnStart = playOnStart; }
		bool GetPlayOnStart() const { return _playOnStart; }

		// Linear gain, 0..1.
		void SetVolume(float volume);
		float GetVolume() const { return _volume; }
		// Pitch in octaves, -1..1 (DirectXTK's range: -1 = one octave down,
		// 0 = original, +1 = one octave up).
		void SetPitch(float pitch);
		float GetPitch() const { return _pitch; }
		// 3D falloff radius in metres (distance at which the sound reaches silence).
		void SetRadius(float radius);
		float GetRadius() const { return _radius; }

	private:
		bool EnsureInstance();
		void ReleaseInstance();
		void ApplyParameters();
		math::Vector3 GetEmitterPosition() const;

	private:
		// Authored (serialized).
		std::string _soundPath;
		bool _is3D = true;
		bool _loop = false;
		bool _playOnStart = true;
		float _volume = 1.0f;
		float _pitch = 0.0f;
		float _radius = 20.0f;

		// Runtime.
		std::shared_ptr<SoundEffect> _master;
		std::shared_ptr<SoundEffect> _instance;
		bool _startedThisRun = false;
		bool _wasGameRunning = false;
	};
}
