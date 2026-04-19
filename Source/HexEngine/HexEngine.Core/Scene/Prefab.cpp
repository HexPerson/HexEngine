
#include "Prefab.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../FileSystem/SceneSaveFile.hpp"
#include "SceneManager.hpp"
#include "../Environment/LogFile.hpp"

namespace HexEngine
{
	std::shared_ptr<Prefab> Prefab::Create(const fs::path& path)
	{
		return dynamic_pointer_cast<Prefab>(g_pEnv->GetResourceSystem().LoadResource(path));
	}

	void Prefab::Save()
	{
		auto scene = g_pEnv->_sceneManager->CreateEmptyScene(false);

		SceneSaveFile saveFile(GetAbsolutePath(), std::ios::out | std::ios::trunc, scene, HexEngine::SceneFileFlags::IsPrefab);
		if (!saveFile.Save(_entities))
		{
			LOG_WARN("Failed to save prefab stage: %s", GetAbsolutePath().string().c_str());
			return;
		}
	}

	void Prefab::Destroy()
	{
		_entities.clear();
		_scene.reset();
	}

	ResourceType Prefab::GetResourceType() const
	{
		return ResourceType::Prefab;
	}

	const std::vector<Entity*>& Prefab::GetLoadedEntities() const
	{
		return _entities;
	}
}
