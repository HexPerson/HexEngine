#pragma once

#include "IResourceLoader.hpp"

namespace HexEngine
{
	class ParticleEffect;

	class HEX_API ParticleEffectLoader : public IResourceLoader
	{
	public:
		ParticleEffectLoader();
		~ParticleEffectLoader();

		virtual std::shared_ptr<IResource> LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
		virtual std::shared_ptr<IResource> LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
		virtual void OnResourceChanged(std::shared_ptr<IResource> resource) override;
		virtual void UnloadResource(IResource* resource) override;
		virtual std::vector<std::string> GetSupportedResourceExtensions() override;
		virtual std::wstring GetResourceDirectory() const override;
		virtual Dialog* CreateEditorDialog(const std::vector<fs::path>& paths) override;
		virtual void SaveResource(IResource* resource, const fs::path& path) override;
		virtual bool DoesSupportHotLoading() override { return true; }

	private:
		bool ParseEffectJson(const json& root, ParticleEffect& outEffect);
		void SerializeEffectJson(const ParticleEffect& effect, json& outRoot);
	};
}
