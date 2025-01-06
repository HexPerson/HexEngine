

#include "SceneManager.hpp"
#include "../HexEngine.hpp"
#include "../FileSystem/SceneSaveFile.hpp"

namespace HexEngine
{
	SceneManager::SceneManager()
	{
		g_pEnv->_resourceSystem->RegisterResourceLoader(this);
	}

	void SceneManager::Destroy()
	{
		std::unique_lock lock(_mutex);

		for (auto&& scene : _scenes)
		{
			UnloadScene(scene.get());
		}

		_scenes.clear();

		g_pEnv->_resourceSystem->UnregisterResourceLoader(this);
	}

	std::shared_ptr<Scene> SceneManager::LoadScene(const fs::path& path)
	{
		/*if (auto scene = GetCurrentScene(); scene != nullptr)
		{
			UnloadScene(scene);
		}*/

		auto newScene = CreateEmptyScene(false);

		SceneSaveFile file(path, std::ios::in, newScene.get());

		if (!file.Load())
		{
			LOG_CRIT("Failed to load scene");
			return nullptr;
		}

		file.Close();

		return newScene;
	}

	const std::vector<std::shared_ptr<Scene>>& SceneManager::GetAllScenes() const
	{
		return _scenes;
	}

	void SceneManager::UnloadScene(Scene* scene)
	{
		std::unique_lock lock(_mutex);

		scene->Destroy();

		_scenes.erase(std::remove_if(_scenes.begin(), _scenes.end(),
			[scene](std::shared_ptr<Scene> sp) {
				return sp.get() == scene;
			}));

		delete scene;					

		if (scene == _currentScene.get())
			_currentScene = nullptr;
	}

	std::vector<Entity*> SceneManager::LoadPrefab(Scene* scene, const fs::path& path)
	{
		std::vector<Entity*> ents;

		auto prefabScene = CreateEmptyScene(false);

		SceneSaveFile file(path, std::ios::in, prefabScene.get());

		if (!file.Load())
		{
			LOG_CRIT("Failed to load scene");
			return ents;
		}

		file.Close();

		scene->MergeFrom(prefabScene.get());

		//_currentScene = scene;

		return ents;
	}

	std::shared_ptr<Scene> SceneManager::CreateEmptyScene(bool createSkySphere, IEntityListener* listener)
	{
		std::unique_lock lock(_mutex);

		std::shared_ptr<Scene> scene = std::shared_ptr<Scene>(new Scene, ResourceDeleter());

		if (_currentScene == nullptr)
			_currentScene = scene;

		scene->CreateEmpty(createSkySphere, listener);

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
				D3DPERF_BeginEvent(0xffffffff, std::format(L"Rendering scene '{}'", scene->GetName()).c_str());

				_currentScene = scene;
				g_pEnv->_sceneRenderer->RenderScene(scene.get(), scene->GetMainCamera(), scene->GetFlags());

				D3DPERF_EndEvent();
			}
		}
	}

	void SceneManager::SetActiveScene(const std::shared_ptr<Scene>& scene)
	{
		_currentScene = scene;
	}

	std::shared_ptr<IResource> SceneManager::LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		if (absolutePath.extension() == ".prefab")
		{
			LoadPrefab(GetCurrentScene().get(), absolutePath);
			return nullptr;
		}
		else
			return LoadScene(absolutePath);
	}

	std::shared_ptr<IResource> SceneManager::LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		return nullptr;
	}

	void SceneManager::UnloadResource(IResource* resource)
	{
		UnloadScene(reinterpret_cast<Scene*>(resource));
	}

	std::vector<std::string> SceneManager::GetSupportedResourceExtensions()
	{
		return { ".scene", ".prefab" };
	}

	std::wstring SceneManager::GetResourceDirectory() const
	{
		return L"Scenes";
	}
}