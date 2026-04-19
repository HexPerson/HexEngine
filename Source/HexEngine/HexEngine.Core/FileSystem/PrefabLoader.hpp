
#pragma once

#include "../Environment/IEnvironment.hpp"

namespace HexEngine
{
	class Scene;
	class Entity;

	class HEX_API PrefabLoader : public IResourceLoader
	{
	public:
		PrefabLoader();
		~PrefabLoader();

		std::vector<Entity*>				LoadPrefab(const std::shared_ptr<Scene>& scene, const fs::path& path);
		bool								LoadPrefabAssetToScene(const fs::path& path, const std::shared_ptr<Scene>& targetScene);

		virtual std::shared_ptr<IResource>	LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
		virtual std::shared_ptr<IResource>	LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
		virtual void						OnResourceChanged(std::shared_ptr<IResource> resource) override;
		virtual void						UnloadResource(IResource* resource) override;
		virtual std::vector<std::string>	GetSupportedResourceExtensions() override;
		virtual std::wstring				GetResourceDirectory() const override;
		virtual Dialog*						CreateEditorDialog(const std::vector<fs::path>& paths) override { return nullptr; }
		virtual void						SaveResource(IResource* resource, const fs::path& path) override;
		virtual bool						DoesSupportHotLoading() override { return true; }
	};
}
