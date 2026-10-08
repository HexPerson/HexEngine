

#pragma once

#include "Scene.hpp"
#include <atomic>

namespace HexEngine
{
	class IEntityListener;

	class HEX_API SceneManager : public IResourceLoader
	{
	public:
		SceneManager();

		void Destroy();

		std::shared_ptr<Scene> LoadScene(const fs::path& path);

		void UnloadScene(Scene* scene);

		std::shared_ptr<Scene> CreateEmptyScene(bool createSkySphere, IEntityListener* listener = nullptr, bool registerScene = false);

		std::shared_ptr<Scene> GetCurrentScene();

		const std::vector<std::shared_ptr<Scene>>& GetAllScenes() const;

		void Update(float frameTime);

		void FixedUpdate(float frameTime);

		void LateUpdate(float frameTime);

		void Render();

		void SetActiveScene(const std::shared_ptr<Scene>& scene);

		// A scene is being deserialized. The editor loads a level on a worker thread
		// (minutes for a big one) while the main loop keeps running, so until the
		// load finishes the world is only partly there: a dynamic body created early
		// has no ground under it yet. The frame loop holds physics and the fixed
		// update while this is true. Calls nest; SceneSaveFile brackets its own loads.
		void BeginSceneLoad() { ++_scenesLoading; }
		void EndSceneLoad() { --_scenesLoading; }
		bool IsLoadingScene() const { return _scenesLoading.load() > 0; }

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
		std::atomic<int32_t> _scenesLoading{ 0 };
	};
}
