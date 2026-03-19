

#include "AudioManager.hpp"
#include "SoundEffect.hpp"
#include "../HexEngine.hpp"
#include <WAVFileReader.h>

namespace HexEngine
{
	AudioManager::AudioManager()
	{
		g_pEnv->GetResourceSystem().RegisterResourceLoader(this);
	}

	std::shared_ptr<IResource> AudioManager::LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		std::shared_ptr<SoundEffect> effect = std::shared_ptr<SoundEffect>(new SoundEffect, ResourceDeleter());

		effect->_effect = new dx::SoundEffect(this->_engine, absolutePath.c_str());
		effect->_instance = effect->_effect->CreateInstance(dx::SoundEffectInstance_Use3D /*| dx::SoundEffectInstance_ReverbUseFilters*/);

		_createdSounds.push_back(effect);

		return effect;
	}

	std::shared_ptr<IResource> AudioManager::LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		std::shared_ptr<SoundEffect> effect = std::shared_ptr<SoundEffect>(new SoundEffect, ResourceDeleter());

		effect->_wavData = std::make_unique<uint8_t[]>(data.size());
		memcpy(effect->_wavData.get(), data.data(), data.size());

		effect->_wavInfo = malloc(sizeof(dx::WAVData));

		dx::WAVData* wd = (dx::WAVData*)effect->_wavInfo;

		HRESULT hr = dx::LoadWAVAudioInMemoryEx(effect->_wavData.get(), data.size(), *wd);

		if (FAILED(hr))
		{
			LOG_CRIT("Failed to load audio file from memory! 0x%X", hr);
			effect.reset();
			return nullptr;
		}		

		effect->_effect = new dx::SoundEffect(this->_engine, effect->_wavData, wd->wfx, wd->startAudio, wd->audioBytes);// , wavInfo.seek, wavInfo.seekCount);
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

				for (auto& sound : _createdSounds)
				{
					auto sp = sound.lock();

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
				}
			}
		}
	}

	void AudioManager::Play(const std::shared_ptr<SoundEffect>& effect)
	{
		//effect->_effect->Play(effect->_volume, 0.0f, 0.0f);
		effect->_instance->Stop(true);
		effect->_instance->SetVolume(effect->_volume);
		effect->_instance->Play(false);
	}

	void AudioManager::Loop(const std::shared_ptr<SoundEffect>& effect)
	{
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
		effect->_instance->Stop(true);
	}

	void AudioManager::SetReverb(dx::AUDIO_ENGINE_REVERB reverb)
	{
		_engine->SetReverb(reverb);
	}
}