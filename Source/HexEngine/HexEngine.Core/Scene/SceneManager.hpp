

#pragma once

#include "Scene.hpp"

namespace HexEngine
{
	class IEntityListener;

	class SceneManager : public IResourceLoader
	{
	public:
		SceneManager();

		void Destroy();

		Scene* LoadScene(const fs::path& path);

		std::vector<Entity*> LoadPrefab(Scene* scene, const fs::path& path);

		void UnloadScene(Scene* scene);

		Scene* CreateEmptyScene(bool createSkySphere, IEntityListener* listener = nullptr);

		Scene* GetCurrentScene();

		const std::vector<Scene*>& GetAllScenes() const;

		void Update(float frameTime);

		void FixedUpdate(float frameTime);

		void LateUpdate(float frameTime);

		void Render();

		void SetActiveScene(Scene* scene);

		// IResourceLoader virtual overrides
		virtual IResource* LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;

		virtual IResource* LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;

		virtual void UnloadResource(IResource* resource) override;

		virtual std::vector<std::string> GetSupportedResourceExtensions() override;

		virtual std::wstring GetResourceDirectory() const override;

		virtual void SaveResource(IResource* resource, const fs::path& path) override {}

	private:
		std::vector<Scene*> _scenes;
		std::recursive_mutex _mutex;
		Scene* _currentScene = nullptr;
	};
}
