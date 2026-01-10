

#pragma once

#include "../Required.hpp"
#include "../FileSystem/ResourceSystem.hpp"
#include <Audio.h>

namespace HexEngine
{
	class SoundEffect;
	class HEX_API AudioManager : public IResourceLoader
	{
	public:
		AudioManager();

		virtual std::shared_ptr<IResource>	LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
		virtual std::shared_ptr<IResource>	LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
		virtual void						OnResourceChanged(std::shared_ptr<IResource> resource) override {}
		virtual void						UnloadResource(IResource* resource) override;
		virtual std::vector<std::string>	GetSupportedResourceExtensions() override;
		virtual std::wstring				GetResourceDirectory() const override;
		virtual void						SaveResource(IResource* resource, const fs::path& path) override {}

		bool Create();
		void Destroy();
		void Update();

		void Play(const std::shared_ptr<SoundEffect>& effect);
		void Play(const std::shared_ptr<SoundEffect>& effect, const math::Vector3& position);

		void Loop(const std::shared_ptr<SoundEffect>& effect);
		void Loop(const std::shared_ptr<SoundEffect>& effect, const math::Vector3& position);

		void Stop(const std::shared_ptr<SoundEffect>& effect);

		void SetReverb(dx::AUDIO_ENGINE_REVERB reverb);

	private:
		dx::AudioEngine* _engine;
		dx::AudioListener _listener;

		std::vector<std::weak_ptr<SoundEffect>> _createdSounds;
		
	};
}
