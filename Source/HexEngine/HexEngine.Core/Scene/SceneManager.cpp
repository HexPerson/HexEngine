

#include "SceneManager.hpp"
#include "../HexEngine.hpp"
#include "../FileSystem/PrefabLoader.hpp"
#include "../FileSystem/SceneSaveFile.hpp"
#include <algorithm>

namespace HexEngine
{
	SceneManager::SceneManager()
	{
		g_pEnv->GetResourceSystem().RegisterResourceLoader(this);
	}

	void SceneManager::Destroy()
	{
		std::unique_lock lock(_mutex);

		while(_scenes.size() > 0)
		{
			UnloadScene(_scenes[0].get());
		}

		_scenes.clear();

		g_pEnv->GetResourceSystem().UnregisterResourceLoader(this);
	}

	std::shared_ptr<Scene> SceneManager::LoadScene(const fs::path& path)
	{
		auto scene = CreateEmptyScene(false, nullptr, true);

		SceneSaveFile file(path, std::ios::in, scene);

		if (!file.Load())
		{
			scene.reset();
			LOG_CRIT("Failed to load scene");
			return nullptr;
		}

		file.Close();

		return scene;
	}

	const std::vector<std::shared_ptr<Scene>>& SceneManager::GetAllScenes() const
	{
		return _scenes;
	}

	void SceneManager::UnloadScene(Scene* scene)
	{
		std::unique_lock lock(_mutex);

		if (scene == nullptr)
			return;

		scene->Destroy();

		_scenes.erase(std::remove_if(_scenes.begin(), _scenes.end(),
			[scene](std::shared_ptr<Scene> sp) {
				return sp.get() == scene;
			}), _scenes.end());

		delete scene;					

		if (scene == _currentScene.get())
			_currentScene = nullptr;
	}

	std::shared_ptr<Scene> SceneManager::CreateEmptyScene(bool createSkySphere, IEntityListener* listener, bool registerScene)
	{
		std::unique_lock lock(_mutex);

		std::shared_ptr<Scene> scene = std::shared_ptr<Scene>(new Scene, ResourceDeleter());

		if (_currentScene == nullptr && registerScene == true)
			_currentScene = scene;

		scene->CreateEmpty(createSkySphere, listener);

		if(registerScene)
			_scenes.push_back(scene);		

		return scene;
	}

	std::shared_ptr<Scene> SceneManager::GetCurrentScene()
	{
		return _currentScene;
	}

	void SceneManager::Update(float frameTime)
	{
		std::unique_lock lock(_mutex);

		for (auto&& scene : _scenes)
		{
			if (HEX_HASFLAG(scene->GetFlags(), SceneFlags::Updateable))
			{
				_currentScene = scene;
				_currentScene->Update(frameTime);
			}
		}
	}

	void SceneManager::FixedUpdate(float frameTime)
	{
		std::unique_lock lock(_mutex);

		for (auto&& scene : _scenes)
		{
			if (HEX_HASFLAG(scene->GetFlags(), SceneFlags::Updateable))
			{
				_currentScene = scene;
				scene->FixedUpdate(frameTime);
			}
		}
	}

	void SceneManager::LateUpdate(float frameTime)
	{
		std::unique_lock lock(_mutex);

		for (auto&& scene : _scenes)
		{
			if (HEX_HASFLAG(scene->GetFlags(), SceneFlags::Updateable))
			{
				_currentScene = scene;
				scene->LateUpdate(frameTime);
			}
		}
	}

	void SceneManager::Render()
	{
		std::unique_lock lock(_mutex);

		for (auto&& scene : _scenes)
		{
			if (HEX_HASFLAG(scene->GetFlags(), SceneFlags::Renderable))
			{
				GFX_PERF_BEGIN(0xffffffff, std::format(L"Rendering scene '{}'", scene->GetName()).c_str());

				_currentScene = scene;
				g_pEnv->_sceneRenderer->RenderScene(scene.get(), scene->GetMainCamera(), scene->GetFlags());

				GFX_PERF_END();
			}
		}
	}

	void SceneManager::SetActiveScene(const std::shared_ptr<Scene>& scene)
	{
		_currentScene = scene;
	}

	std::shared_ptr<IResource> SceneManager::LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		if (absolutePath.extension() == ".hprefab")
		{
			// Defensive fallback for .hprefab requests that somehow end up
			// in this loader (PrefabLoader is the canonical handler, but we
			// stay registered as a backstop). Delegate to PrefabLoader so
			// the caller gets a proper Prefab resource back.
			//
			// The previous behaviour here was to call
			// _prefabLoader->LoadPrefab(GetCurrentScene(), absolutePath)
			// which DUMPED prefab entities into whatever scene happened to
			// be active right now. That side-effect was correct for game-
			// scene loads but catastrophic for IconService: while it's
			// rendering it sets the icon scene as active, so ANY async
			// .hprefab resource resolution (asset explorer enumeration,
			// dependency walk, etc.) would inject the prefab's entities
			// into the icon scene mid-render. Result: the next icon (e.g.
			// a mesh thumbnail) shows the previously-rendered prefab
			// merged in. Returning a normal Prefab resource leaves
			// instantiation to whoever explicitly asks for it.
			if (g_pEnv != nullptr && g_pEnv->_prefabLoader != nullptr)
			{
				return g_pEnv->_prefabLoader->LoadResourceFromFile(absolutePath, fileSystem, options);
			}
			return nullptr;
		}
		else
		{
			std::shared_ptr<Scene> scene = LoadScene(absolutePath);
			if (scene == nullptr)
			{
				LOG_CRIT("Failed to load scene %s", absolutePath.string().c_str());
				return nullptr;
			}

			return scene;
		}

		return nullptr;
	}

	std::shared_ptr<IResource> SceneManager::LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		// Mirror LoadResourceFromFile's branching: the loader is registered
		// only for ".hscene", but the file path also handles ".hprefab"
		// defensively, so do the same here for symmetry.
		if (relativePath.extension() == ".hprefab")
		{
			if (g_pEnv != nullptr && g_pEnv->_prefabLoader != nullptr)
			{
				// Delegate to the prefab loader's own memory path so prefabs
				// streamed from an AssetPackage don't fall back to disk.
				return g_pEnv->_prefabLoader->LoadResourceFromMemory(data, relativePath, fileSystem, options);
			}
			return nullptr;
		}

		// Standard .hscene path. SceneSaveFile::LoadFromMemory already exists
		// (it was added when PrefabLoader was wired up for memory loads); it
		// internally builds a json document from the bytes and routes through
		// the same LoadFromJson body the disk Load() ends up at, so packaged
		// scenes deserialise identically to disk scenes.
		auto scene = CreateEmptyScene(false, nullptr, true);

		SceneSaveFile file(relativePath, std::ios::in, scene);
		if (!file.LoadFromMemory(data, scene))
		{
			LOG_CRIT("Failed to load scene from memory: %s", relativePath.string().c_str());
			UnloadScene(scene.get());
			return nullptr;
		}

		return scene;
	}

	void SceneManager::UnloadResource(IResource* resource)
	{
		UnloadScene(reinterpret_cast<Scene*>(resource));
	}

	std::vector<std::string> SceneManager::GetSupportedResourceExtensions()
	{
		return { ".hscene" };
	}

	std::wstring SceneManager::GetResourceDirectory() const
	{
		return L"Scenes";
	}
}
