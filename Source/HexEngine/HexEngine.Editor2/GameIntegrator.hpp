
#pragma once

#include <HexEngine.Core\HexEngine.hpp>

namespace HexEditor
{
	/** @brief Runtime state of the editor-integrated game module. */
	enum class GameTestState
	{
		None,
		Loaded,
		Stopped,
		Started,
		Max
	};

	/**
	 * @brief Bridges editor play mode with game DLL lifecycle and hot reload.
	 *
	 * This class owns loading/unloading the game extension DLL, running/stopping
	 * play mode, and watching `Code` files to trigger rebuild + reload.
	 */
	class GameIntegrator : public HexEngine::IEntityListener
	{
	public:
		GameIntegrator() = default;
		/** @brief Ensures currently loaded game module resources are released. */
		~GameIntegrator();

		/** @brief Processes delayed hot-reload requests queued by the file watcher. */
		void Update();
		/**
		 * @brief Builds the active game solution/project.
		 * @param projectFileName Optional `.sln`/`.vcxproj` under the project `Code` directory.
		 * @return True when build succeeded.
		 */
		bool BuildGame(const std::wstring& projectFileName = L"");

		/**
		 * @brief Packages the project's Data/ folder into GameData.pkg via the
		 *        HexEngine.AssetPacker.exe tool.
		 *
		 * The output `.pkg` is dropped at `<projectBaseDir>/Build/GameData.pkg`
		 * unless a custom output path is provided. The shipped game runtime
		 * auto-mounts any GameData.pkg sitting next to its executable.
		 *
		 * @param compress  Pass-through to AssetPacker's `--compression` flag.
		 *                  Brotli-compresses the final blob; trades startup
		 *                  decode time for on-disk size.
		 * @param outputPkg Optional override for the .pkg destination. Empty
		 *                  uses the default `<project>/Build/GameData.pkg`.
		 * @return True if AssetPacker exits 0 AND the output file exists.
		 */
		bool PackageAssets(bool compress = true, const fs::path& outputPkg = {});

		/**
		 * @brief Deploys engine-side runtime binaries to a target folder so a
		 *        shipped launcher can run cleanly without the user manually
		 *        copying DLLs / packages each time engine code changes.
		 *
		 * Copies, from the editor's runtime layout:
		 *   - HexEngine.Core.dll (sourced from the currently-loaded module
		 *     path so we always get the bits the editor is actually using,
		 *     not a possibly-stale post-build copy)
		 *   - All *.dll under <editorCwd>/Plugins/ into <destDir>/Plugins/
		 *   - <editorCwd>/Data/AssetPackages/EngineAssets.pkg into
		 *     <destDir>/Data/AssetPackages/ (if it exists)
		 *
		 * Does NOT touch the launcher executable, the game DLL, or
		 * GameData.pkg - those are produced/owned by other steps (the
		 * launcher is the user's own exe, the others come from BuildGame /
		 * PackageAssets).
		 *
		 * @param destDir Destination root (typically the project's Build/).
		 * @return True on full success, false if any required file was
		 *         missing or could not be copied.
		 */
		bool DeployEngineBinaries(const fs::path& destDir);

		/** @brief Loads the game DLL and creates the game extension instance. */
		bool LoadGame();
		/** @brief Enters play mode and invokes game startup callbacks. */
		bool RunGame();
		/** @brief Leaves play mode and invokes game shutdown callbacks. */
		bool StopGame();
		/** @brief Returns the current game integration state. */
		GameTestState GetState() const;

		/** @brief Tracks temporary entities created during play mode. */
		virtual void OnAddEntity(HexEngine::Entity* entity) override;
		/** @brief Removes tracked temporary entities when deleted. */
		virtual void OnRemoveEntity(HexEngine::Entity* entity) override;
		virtual void OnAddComponent(HexEngine::Entity* entity, HexEngine::BaseComponent* component) override {};
		virtual void OnRemoveComponent(HexEngine::Entity* entity, HexEngine::BaseComponent* component) override {};

	private:
		void EnsureCodeWatcher();
		void OnCodeFileChangeEvent(const HexEngine::DirectoryWatchInfo& info, const HexEngine::FileChangeActionMap& actionMap);
		bool HotReloadGame();
		bool UnloadGame();
		fs::path StageRuntimeGameDllCopy(const fs::path& sourceDllPath);
		fs::path ResolveBuildProjectPath(const std::wstring& projectFileName) const;
		fs::path FindMSBuildExecutable() const;
		static bool IsWatchedCodeFile(const fs::path& path);
		static bool IsIntermediateCodePath(const fs::path& path);

		// Capture every entity's authored transform at play start and put it back on
		// stop, so a run can't leave the scene mutated (doors left open, props
		// shoved, the camera at the spawn point, ...). The editor doesn't otherwise
		// snapshot the scene across play/stop.
		void SnapshotSceneState();
		void RestoreSceneState();

	private:
		// Temp .hscene written at play start; on stop it's reloaded over the live
		// scene via SceneSaveFile with a load override that restores into existing
		// entities (keeping editor-held pointers valid) instead of recreating the
		// scene. Empty when no snapshot is held.
		fs::path _playSnapshotPath;

		std::vector<HexEngine::Entity*> _tempEntitiesCreated;
		HexEngine::FileSystem* _runtimeFS = nullptr;
		HMODULE _gameDll = 0;
		HexEngine::IGameExtension* _gameExtension = 0;
		GameTestState _state = GameTestState::None;
		fs::path _codeDirectory;
		fs::path _buildProjectPath;
		std::wstring _lastBuildProjectFileName;
		bool _isWatchingCode = false;
		bool _reloadRequested = false;
		bool _isReloading = false;
		uint64_t _reloadGeneration = 0;
		std::chrono::steady_clock::time_point _lastCodeChange = std::chrono::steady_clock::now();
		std::mutex _reloadMutex;
		fs::path _loadedGameDllPath;
		HexEngine::Camera* _origMainCamera = nullptr;
	};
}
