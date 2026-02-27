
#pragma once

#include "JsonFile.hpp"

namespace HexEngine
{
	enum class SceneFileFlags
	{
		None = 0,
		DontSaveVariables = HEX_BITSET(0),
		DontSaveHierarchy = HEX_BITSET(1),
		IsPrefab		  = HEX_BITSET(2)
	};

	class Scene;

	class HEX_API SceneSaveFile : public JsonFile
	{
	public:
		using SceneSaveProgressCallback = std::function<void(const std::wstring& entityName, int32_t loaded, int32_t total)>;

		SceneSaveFile(const fs::path& absolutePath, std::ios_base::openmode openMode, std::shared_ptr<Scene> scene, SceneFileFlags flags = SceneFileFlags::None);

		const int Version = 2;

		bool Load(SceneSaveProgressCallback callback = nullptr);
		bool Load(std::shared_ptr<Scene> loadIntoExistingScene, SceneSaveProgressCallback callback = nullptr);

		// Save all entities in the scene
		bool Save();

		// Save a specific set of entities only
		bool Save(const std::vector<HexEngine::Entity*>& entities);

		std::shared_ptr<Scene> GetScene() const;

		bool IsSceneAttached() const;

		const std::vector<Entity*>& GetLoadedEntities() const;

	public:
		std::shared_ptr<Scene> _scene;
		SceneFileFlags _flags;
		std::vector<Entity*> _loadedEntities;
	};
}