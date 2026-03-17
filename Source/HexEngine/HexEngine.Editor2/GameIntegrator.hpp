
#pragma once

#include <HexEngine.Core\HexEngine.hpp>

namespace HexEditor
{
	enum class GameTestState
	{
		None,
		Loaded,
		Stopped,
		Started,
		Max
	};

	class GameIntegrator : public HexEngine::IEntityListener
	{
	public:
		GameIntegrator() = default;
		~GameIntegrator();

		void Update();
		bool BuildGame(const std::wstring& projectFileName = L"");
		bool LoadGame();
		bool RunGame();
		bool StopGame();
		GameTestState GetState() const;

		virtual void OnAddEntity(HexEngine::Entity* entity) override;
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
