

#pragma once

#include "../FileSystem/IResource.hpp"
#include <Audio.h>


namespace HexEngine
{
	class SoundEffect : public IResource
	{
		friend class AudioManager;

	public:
		SoundEffect();

		static SoundEffect* Create(const fs::path& path);

		virtual void Destroy() override
		{
			_wavData.release();
			SAFE_DELETE(_effect);

			if (_wavInfo)
			{
				free(_wavInfo);
				_wavInfo = nullptr;
			}
		}

		void SetVolume(float volume);
		void SetPitch(float pitch);
		void SetRadius(float radius);
		float GetRadius() const;

		float GetDuration();

		bool IsInUse() const;
		bool IsPlaying() const;

	private:
		dx::SoundEffect* _effect = nullptr;
		std::unique_ptr<dx::SoundEffectInstance> _instance;
		float _volume = 1.0f;
		dx::AudioEmitter _emitter;
		bool _is3D = false;
		std::unique_ptr<uint8_t[]> _wavData;
		void* _wavInfo;
		float _radius = 0.0f;
	};
}
