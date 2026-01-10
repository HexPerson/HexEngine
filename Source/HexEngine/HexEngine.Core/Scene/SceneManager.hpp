

#pragma once

#include "Scene.hpp"

namespace HexEngine
{
	class IEntityListener;

	class HEX_API SceneManager : public IResourceLoader
	{
	public:
		SceneManager();

		void Destroy();

		bool LoadScene(const fs::path& path, std::shared_ptr<Scene>& scene);

		std::shared_ptr<Scene> LoadPrefab(std::shared_ptr<Scene> scene, const fs::path& path);

		void UnloadScene(Scene* scene);

		std::shared_ptr<Scene> CreateEmptyScene(bool createSkySphere, IEntityListener* listener = nullptr, bool registerScene = false);

		std::shared_ptr<Scene> GetCurrentScene();

		const std::vector<std::shared_ptr<Scene>>& GetAllScenes() const;

		void Update(float frameTime);

		void FixedUpdate(float frameTime);

		void LateUpdate(float frameTime);

		void Render();

		void SetActiveScene(const std::shared_ptr<Scene>& scene);

		// IResourceLoader virtual overrides
		virtual std::shared_ptr<IResource>	LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
		virtual std::shared_ptr<IResource>	LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
		virtual void						OnResourceChanged(std::shared_ptr<IResource> resource) override {}
		virtual void						UnloadResource(IResource* resource) override;
		virtual std::vector<std::string>	GetSupportedResourceExtensions() override;
		virtual std::wstring				GetResourceDirectory() const override;
		virtual void						SaveResource(IResource* resource, const fs::path& path) override {}

	private:
		std::vector<std::shared_ptr<Scene>> _scenes;
		std::recursive_mutex _mutex;
		std::shared_ptr<Scene> _currentScene;
	};
}
