

#include "PrefabLoader.hpp"
#include "SceneSaveFile.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Environment/LogFile.hpp"

namespace HexEngine
{
	PrefabLoader::PrefabLoader()
	{
		//g_pEnv->_resourceSystem->RegisterResourceLoader(this);
	}

	PrefabLoader::~PrefabLoader()
	{
		//g_pEnv->_resourceSystem->UnregisterResourceLoader(this);
	}

	std::shared_ptr<IResource> PrefabLoader::LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		SceneSaveFile prefabFile(absolutePath, std::ios::in, g_pEnv->_sceneManager->GetCurrentScene(), SceneFileFlags::IsPrefab);

		if (prefabFile.Load() == false)
		{
			LOG_CRIT("Failed to load prefab file: %s", absolutePath.filename().c_str());
			return nullptr;
		}

		return g_pEnv->_sceneManager->GetCurrentScene();
	}

	std::shared_ptr<IResource> PrefabLoader::LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		return nullptr;
	}

	void PrefabLoader::OnResourceChanged(std::shared_ptr<IResource> resource)
	{

	}

	void PrefabLoader::UnloadResource(IResource* resource)
	{
		//g_pEnv->_sceneManager->UnloadScene()
	}

	std::vector<std::string> PrefabLoader::GetSupportedResourceExtensions()
	{
		return { ".hprefab" };
	}

	std::wstring PrefabLoader::GetResourceDirectory() const
	{
		return L"Prefabs";
	}

	void PrefabLoader::SaveResource(IResource* resource, const fs::path& path)
	{
		SceneSaveFile prefabFile(path, std::ios::in, g_pEnv->_sceneManager->GetCurrentScene(), SceneFileFlags::IsPrefab);

		if (prefabFile.Save() == false)
		{
			LOG_CRIT("Failed to load prefab file: %s", path.filename().c_str());
			return;
		}
	}

}