

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

		// Register a SoundEffect for per-frame 3D updates. Resource-loaded
		// SoundEffects are auto-registered when first loaded from disk,
		// but clones created via SoundEffect::CreatePlaybackClone() need
		// to opt in here - otherwise the per-frame Apply3D() loop skips
		// them and any moving emitter stays stuck at its initial Loop()
		// or Play() position.
		void RegisterPlaybackInstance(const std::shared_ptr<SoundEffect>& effect);

		void SetReverb(dx::AUDIO_ENGINE_REVERB reverb);

		// Underwater muffle (underwater S5). 0 = in air, 1 = listener fully
		// submerged; smoothed in Update() from Scene::IsUnderwater(listener).
		float GetUnderwaterBlend() const { return _underwaterBlend; }
		// Pitch offset (semi-octaves, <= 0) layered on every playing sound.
		float GetUnderwaterPitchOffset() const { return _underwaterPitchOffset; }

	private:
		void UpdateUnderwaterMuffle(const math::Vector3& listenerPosition);

		dx::AudioEngine* _engine;
		dx::AudioListener _listener;
		float _underwaterBlend = 0.0f;
		float _underwaterPitchOffset = 0.0f;
		float _underwaterAppliedBlend = -1.0f; // last blend pushed to the engine/instances

		std::vector<std::weak_ptr<SoundEffect>> _createdSounds;
		
	};
}
