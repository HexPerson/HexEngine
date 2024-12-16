

#pragma once

#include "../Required.hpp"
#include "../FileSystem/ResourceSystem.hpp"
#include <Audio.h>

namespace HexEngine
{
	class SoundEffect;
	class AudioManager : public IResourceLoader
	{
	public:
		AudioManager();

		virtual IResource* LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;

		virtual IResource* LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;

		virtual void UnloadResource(IResource* resource) override;

		virtual std::vector<std::string> GetSupportedResourceExtensions() override;

		virtual std::wstring GetResourceDirectory() const override;

		virtual void SaveResource(IResource* resource, const fs::path& path) override {}

		bool Create();
		void Destroy();
		void Update();

		void Play(SoundEffect* effect);
		void Play(SoundEffect* effect, const math::Vector3& position);

		void Loop(SoundEffect* effect);
		void Loop(SoundEffect* effect, const math::Vector3& position);

		void Stop(SoundEffect* effect);

		void SetReverb(dx::AUDIO_ENGINE_REVERB reverb);

	private:
		dx::AudioEngine* _engine;
		dx::AudioListener _listener;

		std::vector<SoundEffect*> _createdSounds;
		
	};
}
