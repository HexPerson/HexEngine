
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

		// Load entry points that bypass disk I/O - used by PrefabLoader's
		// LoadResourceFromMemory path so prefabs streamed out of an
		// AssetPackage (.pkg) can be deserialized without ever touching the
		// disk. Internally route to the same LoadFromJson body the disk
		// Load() does.
		bool LoadFromMemory(const std::vector<uint8_t>& data, std::shared_ptr<Scene> loadIntoExistingScene, SceneSaveProgressCallback callback = nullptr);
		// Non-const json& because the downstream Scene::Load(json&, JsonFile*)
		// hook takes a mutable reference (deserialization sometimes mutates the
		// document, e.g. fixing up legacy fields in-place). Callers always
		// construct a local json from the buffer/file so handing it through
		// mutable is fine.
		bool LoadFromJson(json& sceneData, std::shared_ptr<Scene> loadIntoExistingScene, SceneSaveProgressCallback callback = nullptr);

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