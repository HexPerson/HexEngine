

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
