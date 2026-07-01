

#pragma once

#include "../FileSystem/IResource.hpp"
#include <Audio.h>

namespace HexEngine
{
	class HEX_API SoundEffect : public IResource
	{
		friend class AudioManager;

	public:
		SoundEffect();

		static std::shared_ptr<SoundEffect> Create(const fs::path& path);
		std::shared_ptr<SoundEffect> CreatePlaybackClone() const;

		virtual void Destroy() override
		{
			_wavData.release();
			_effect.reset();
			_instance.reset();
		}

		void SetVolume(float volume);
		void SetPitch(float pitch);
		void SetRadius(float radius);
		float GetRadius() const;
		// Update the 3D emitter position WITHOUT restarting playback.
		// AudioManager::Update() re-applies the 3D parameters from the
		// emitter each frame, so just writing the new position here is
		// enough to move a playing/looping sound (e.g. attaching it to
		// a moving traffic vehicle). Calling Play()/Loop() again to
		// "move" a sound would restart it from the beginning.
		void SetPosition(const math::Vector3& position);

		float GetDuration();

		bool IsInUse() const;
		bool IsPlaying() const;

	private:
		std::shared_ptr<dx::SoundEffect> _effect;
		std::unique_ptr<dx::SoundEffectInstance> _instance;
		float _volume = 1.0f;
		dx::AudioEmitter _emitter;
		bool _is3D = false;
		std::unique_ptr<uint8_t[]> _wavData;
		float _radius = 0.0f;
	};
}
