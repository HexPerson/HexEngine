
#pragma once

#include "JsonFile.hpp"

namespace HexEngine
{
	enum class SceneFileFlags
	{
		None = 0,
		DontSaveVariables = HEX_BITSET(0),
		DontSaveHierarchy = HEX_BITSET(1)
	};

	class Scene;

	class SceneSaveFile : public JsonFile
	{
	public:
		using SceneSaveProgressCallback = std::function<void(const std::wstring& entityName, int32_t loaded, int32_t total)>;

		SceneSaveFile(const fs::path& absolutePath, std::ios_base::openmode openMode, Scene* scene, SceneFileFlags flags = SceneFileFlags::None);

		const int Version = 2;

		bool Load(SceneSaveProgressCallback callback = nullptr);

		bool Save();

		Scene* GetScene() const;

		bool IsSceneAttached() const;

	public:
		Scene* _scene = nullptr;
		SceneFileFlags _flags;
	};
}