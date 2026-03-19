
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

	private:
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
	};
}
