

#include "AudioManager.hpp"
#include "SoundEffect.hpp"
#include "../HexEngine.hpp"
#include <WAVFileReader.h>

namespace HexEngine
{
	// Underwater muffle (underwater S5). DirectXTK's per-voice filters are only
	// reachable through the environmental-reverb path, which this engine creates
	// the AudioEngine WITHOUT (see Create()) - so a true low-pass is an audio-
	// architecture change, not a polish item. What sells "head under water"
	// almost as well is the pair the weather plugin already uses for "indoors":
	// duck the level and drop the pitch. Both are smoothed so a camera bobbing
	// at the waterline doesn't chatter.
	HVar snd_underwater("snd_underwater", "Muffle audio (level duck + pitch drop) while the listener is below the sea surface", true, false, true);
	HVar snd_underwaterVolume("snd_underwaterVolume", "Master volume multiplier while fully submerged", 0.42f, 0.0f, 1.0f);
	HVar snd_underwaterPitch("snd_underwaterPitch", "Pitch offset (octaves, negative = lower) layered on every sound while fully submerged", -0.22f, -1.0f, 0.0f);

	AudioManager::AudioManager()
	{
		g_pEnv->GetResourceSystem().RegisterResourceLoader(this);
	}

	std::shared_ptr<IResource> AudioManager::LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		std::shared_ptr<SoundEffect> effect = std::shared_ptr<SoundEffect>(new SoundEffect, ResourceDeleter());

		effect->_effect = std::make_shared<dx::SoundEffect>(this->_engine, absolutePath.c_str());
		effect->_instance = effect->_effect->CreateInstance(dx::SoundEffectInstance_Use3D /*| dx::SoundEffectInstance_ReverbUseFilters*/);

		_createdSounds.push_back(effect);

		return effect;
	}

	std::shared_ptr<IResource> AudioManager::LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		std::shared_ptr<SoundEffect> effect = std::shared_ptr<SoundEffect>(new SoundEffect, ResourceDeleter());

		effect->_wavData = std::make_unique<uint8_t[]>(data.size());
		memcpy(effect->_wavData.get(), data.data(), data.size());

		dx::WAVData wavData;

		HRESULT hr = dx::LoadWAVAudioInMemoryEx(effect->_wavData.get(), data.size(), wavData);

		if (FAILED(hr))
		{
			LOG_CRIT("Failed to load audio file from memory! 0x%X", hr);
			effect.reset();
			return nullptr;
		}		

		effect->_effect = std::make_shared<dx::SoundEffect>(this->_engine, effect->_wavData, wavData.wfx, wavData.startAudio, wavData.audioBytes);
		effect->_instance = effect->_effect->CreateInstance(dx::SoundEffectInstance_Use3D /*| dx::SoundEffectInstance_ReverbUseFilters*/);

		_createdSounds.push_back(effect);

		return effect;
	}

	void AudioManager::UnloadResource(IResource* resource)
	{
		for (auto it = _createdSounds.begin(); it != _createdSounds.end(); it++)
		{
			auto sound = it->lock();

			if (sound.get() == dynamic_cast<SoundEffect*>(resource))
			{
				_createdSounds.erase(it);
				break;
			}
		}

		SAFE_DELETE(resource);
	}

	std::vector<std::string> AudioManager::GetSupportedResourceExtensions()
	{
		return { ".wav", ".mp3" };
	}

	std::wstring AudioManager::GetResourceDirectory() const
	{
		return L"Audio";
	}

	bool AudioManager::Create()
	{
		dx::AUDIO_ENGINE_FLAGS flags = dx::AudioEngine_Default | dx::AudioEngine_UseMasteringLimiter;// | dx::AudioEngine_EnvironmentalReverb | dx::AudioEngine_ReverbUseFilters;;

		

#ifdef _DEBUG
		flags |= dx::AudioEngine_Debug;
#endif
		_engine = new dx::AudioEngine(flags);

		return true;
	}

	void AudioManager::Destroy()
	{
		SAFE_DELETE(_engine);
	}

	void AudioManager::Update()
	{
		if (_engine)
		{
			if (!_engine->Update())
			{
				if (_engine->IsCriticalError())
				{
					LOG_CRIT("Audio engine critical error!");
				}
				
			}

			if (g_pEnv->_sceneManager->GetCurrentScene())
			{
				auto mainCamera = g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera();

				if (!mainCamera)
					return;

				_listener.SetPosition(mainCamera->GetEntity()->GetPosition());
				_listener.SetOrientation(mainCamera->GetEntity()->GetComponent<Transform>()->GetForward(), math::Vector3::Up);

				UpdateUnderwaterMuffle(mainCamera->GetEntity()->GetPosition() + mainCamera->GetViewOffset());
				// Re-layer the pitch offset only while it is changing or active:
				// one SetPitch per playing sound per frame, and nothing at all in
				// the (overwhelmingly common) dry steady state.
				const bool relayerPitch = _underwaterBlend > 0.0f || _underwaterAppliedBlend != _underwaterBlend;

				for (auto it = _createdSounds.begin(); it != _createdSounds.end(); )
				{
					auto sp = it->lock();
					if (!sp)
					{
						it = _createdSounds.erase(it);
						continue;
					}

					if (relayerPitch && sp->_instance && sp->IsPlaying())
						sp->_instance->SetPitch(std::clamp(sp->_pitch + _underwaterPitchOffset, -1.0f, 1.0f));

					if (sp->_is3D && sp->IsPlaying())
					{
						/*if (sound->GetRadius() != 0.0f)
						{
							auto distance = std::clamp((math::Vector3(sound->_emitter.Position.x, sound->_emitter.Position.y, sound->_emitter.Position.z) - mainCamera->GetEntity()->GetPosition()).Length(), 0.0f, sound->GetRadius());

							auto attenuation = 1.0f - (distance / sound->GetRadius());

							sound->SetVolume(attenuation);
						}*/
						//sound->_emitter.SetPosition(sound->_emitter.Position);// .x, math::Vector3::Up, g_pEnv->_timeManager->_frameTime);
						sp->_instance->Apply3D(_listener, sp->_emitter);
					}

					++it;
				}

				_underwaterAppliedBlend = _underwaterBlend;
			}
		}
	}

	void AudioManager::UpdateUnderwaterMuffle(const math::Vector3& listenerPosition)
	{
		auto scene = g_pEnv->_sceneManager->GetCurrentScene();

		float target = 0.0f;
		if (snd_underwater._val.b && scene != nullptr && scene->HasOcean())
		{
			// Ramp over the top 25 cm so ears-at-the-waterline is a partial
			// muffle, not a switch.
			target = std::clamp(scene->GetDepthBelowWater(listenerPosition) / 0.25f, 0.0f, 1.0f);
		}

		const float dt = (g_pEnv->_timeManager != nullptr) ? std::clamp(g_pEnv->_timeManager->GetFrameTime(), 0.0f, 0.1f) : 0.0f;
		const float alpha = 1.0f - std::exp(-9.0f * dt); // ~0.25 s to settle
		_underwaterBlend += (target - _underwaterBlend) * alpha;
		if (std::abs(target - _underwaterBlend) < 0.002f)
			_underwaterBlend = target;

		_underwaterPitchOffset = snd_underwaterPitch._val.f32 * _underwaterBlend;

		if (_engine != nullptr && _underwaterBlend != _underwaterAppliedBlend)
			_engine->SetMasterVolume(1.0f + (snd_underwaterVolume._val.f32 - 1.0f) * _underwaterBlend);
	}

	void AudioManager::Play(const std::shared_ptr<SoundEffect>& effect)
	{
		// Null-safe like the positional overloads: game code plays event
		// sounds from serialized slots that may legitimately be unset (or
		// whose resource failed to load), and a null here was a hard crash
		// on the first job-seed of the Whereabouts project.
		if (!effect || !effect->_instance)
			return;
		//effect->_effect->Play(effect->_volume, 0.0f, 0.0f);
		effect->_is3D = false;
		effect->_instance->Stop(true);
		effect->_instance->SetVolume(effect->_volume);
		effect->_instance->Play(false);
	}

	void AudioManager::Loop(const std::shared_ptr<SoundEffect>& effect)
	{
		if (!effect || !effect->_instance)
			return;
		effect->_is3D = false;
		effect->_instance->Stop(true);
		effect->_instance->Play(true);
	}

	void AudioManager::Play(const std::shared_ptr<SoundEffect>& effect, const math::Vector3& position)
	{
		if (!effect)
			return;

		auto mainCamera = g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera();
		
		effect->_emitter.SetPosition(position);

		//if (effect->_instance->GetState() == dx::SoundState::PLAYING)
		//	effect->_effect+

		effect->_is3D = true;
		
		effect->_instance->Stop(true);
		effect->_instance->SetVolume(effect->_volume);
		effect->_instance->Play(false);
		effect->_instance->Apply3D(_listener, effect->_emitter);
	}

	void AudioManager::Loop(const std::shared_ptr<SoundEffect>& effect, const math::Vector3& position)
	{
		if (!effect)
			return;

		auto mainCamera = g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera();

		effect->_is3D = true;

		effect->_emitter.SetPosition(position);
		//effect->_emitter.ChannelCount = effect->_effect->GetFormat()->nChannels;

		effect->_instance->Stop(true);
		effect->_instance->SetVolume(effect->_volume);
		effect->_instance->Play(true);
		effect->_instance->Apply3D(_listener, effect->_emitter);
	}

	void AudioManager::Stop(const std::shared_ptr<SoundEffect>& effect)
	{
		if (!effect || !effect->_instance)
			return;
		effect->_instance->Stop(true);
	}

	void AudioManager::RegisterPlaybackInstance(const std::shared_ptr<SoundEffect>& effect)
	{
		if (!effect) return;
		// Dedup against existing weak refs - the same shared_ptr being
		// registered twice would just get Apply3D'd twice per frame for
		// no benefit. Cheap linear scan; the vector is short.
		for (const auto& existing : _createdSounds)
		{
			if (existing.lock() == effect)
				return;
		}
		_createdSounds.push_back(effect);
	}

	void AudioManager::SetReverb(dx::AUDIO_ENGINE_REVERB reverb)
	{
		_engine->SetReverb(reverb);
	}
}
