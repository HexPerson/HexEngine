
#include "GameIntegrator.hpp"
#include "Editor.hpp"

#include <array>
#include <deque>
#include <fstream>

namespace HexEditor
{
	constexpr auto GameReloadDebounce = std::chrono::milliseconds(400);

	std::wstring QuoteForCommandLine(const fs::path& path)
	{
		std::wstring quoted(1, L'"');
		quoted += path.wstring();
		quoted += L'"';
		return quoted;
	}

	class IGameBuildService
	{
	public:
		virtual ~IGameBuildService() = default;
		virtual bool Build(const fs::path& projectPath, const fs::path& projectBaseDir, const fs::path& msbuildPath, uint64_t& reloadGeneration) const = 0;
	};

	class MSBuildGameBuildService final : public IGameBuildService
	{
	public:
		bool Build(const fs::path& projectPath, const fs::path& projectBaseDir, const fs::path& msbuildPath, uint64_t& reloadGeneration) const override
		{
			const fs::path logPath = projectBaseDir / L"Build" / L"GameBuild.log";
			fs::create_directories(logPath.parent_path());

			SECURITY_ATTRIBUTES sa = {};
			sa.nLength = sizeof(sa);
			sa.bInheritHandle = TRUE;

			HANDLE logFile = CreateFileW(
				logPath.wstring().c_str(),
				GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				&sa,
				CREATE_ALWAYS,
				FILE_ATTRIBUTE_NORMAL,
				nullptr);
			if (logFile == INVALID_HANDLE_VALUE)
			{
				LOG_CRIT("Could not create game build log file '%S'. Error: %d", logPath.wstring().c_str(), GetLastError());
				return false;
			}

			const fs::path hotReloadDir = projectBaseDir / L"Build" / L"HotReload";
			fs::create_directories(hotReloadDir);

			const auto reloadId = ++reloadGeneration;
			const fs::path pdbPath = hotReloadDir / std::format(L"Game_{:06}.pdb", reloadId);
			const fs::path overridePropsPath = hotReloadDir / std::format(L"HotReload_{:06}.props", reloadId);
			{
				std::wofstream overrideProps(overridePropsPath);
				if (!overrideProps)
				{
					CloseHandle(logFile);
					LOG_CRIT("Could not create hot reload MSBuild overrides file '%S'", overridePropsPath.wstring().c_str());
					return false;
				}

				overrideProps
					<< LR"(<?xml version="1.0" encoding="utf-8"?>)" << L"\n"
					<< LR"(<Project ToolsVersion="Current" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">)" << L"\n"
					<< LR"(  <ItemDefinitionGroup>)" << L"\n"
					<< LR"(    <Link>)" << L"\n"
					<< L"      <ProgramDatabaseFile>" << pdbPath.wstring() << L"</ProgramDatabaseFile>" << L"\n"
					<< LR"(      <LinkIncremental>false</LinkIncremental>)" << L"\n"
					<< LR"(    </Link>)" << L"\n"
					<< LR"(  </ItemDefinitionGroup>)" << L"\n"
					<< LR"(</Project>)" << L"\n";
			}

			const wchar_t* buildConfiguration =
#ifdef _DEBUG
				L"Debug";
#else
				L"Release";
#endif

			std::wstring commandLine = std::format(
				L"{} {} /t:Build /p:Configuration={} /p:Platform=x64 /p:ForceImportAfterCppProps={} /m /nologo /verbosity:minimal",
				QuoteForCommandLine(msbuildPath),
				QuoteForCommandLine(projectPath),
				buildConfiguration,
				QuoteForCommandLine(overridePropsPath));

			STARTUPINFOW si = {};
			si.cb = sizeof(si);
			si.dwFlags = STARTF_USESTDHANDLES;
			si.hStdOutput = logFile;
			si.hStdError = logFile;
			si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

			PROCESS_INFORMATION pi = {};
			std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
			mutableCommand.push_back(L'\0');

			LOG_INFO("Building game code with command: %S", commandLine.c_str());
			LOG_INFO("Writing game build log to '%S'", logPath.wstring().c_str());

			if (!CreateProcessW(
				nullptr,
				mutableCommand.data(),
				nullptr,
				nullptr,
				TRUE,
				CREATE_NO_WINDOW,
				nullptr,
				projectPath.parent_path().wstring().c_str(),
				&si,
				&pi))
			{
				const auto error = GetLastError();
				CloseHandle(logFile);
				LOG_CRIT("Could not start MSBuild. Error: %d", error);
				return false;
			}

			WaitForSingleObject(pi.hProcess, INFINITE);

			DWORD exitCode = 1;
			GetExitCodeProcess(pi.hProcess, &exitCode);
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
			CloseHandle(logFile);

			if (exitCode != 0)
			{
				std::ifstream logStream(logPath);
				if (logStream)
				{
					std::deque<std::string> tail;
					std::string line;
					while (std::getline(logStream, line))
					{
						if (!line.empty())
						{
							tail.push_back(line);
							if (tail.size() > 12)
								tail.pop_front();
						}
					}

					for (const auto& tailLine : tail)
					{
						LOG_WARN("MSBuild: %s", tailLine.c_str());
					}
				}

				LOG_CRIT("Game code build failed with exit code %d. See '%S'", exitCode, logPath.wstring().c_str());
				return false;
			}

			LOG_INFO("Game code build finished successfully");
			return true;
		}
	};

	GameIntegrator::~GameIntegrator()
	{
		UnloadGame();
	}

	void GameIntegrator::Update()
	{
		bool shouldReload = false;
		{
			std::scoped_lock lock(_reloadMutex);
			if (_reloadRequested && !_isReloading && (std::chrono::steady_clock::now() - _lastCodeChange) >= GameReloadDebounce)
			{
				_reloadRequested = false;
				_isReloading = true;
				shouldReload = true;
			}
		}

		if (!shouldReload)
			return;

		HotReloadGame();

		std::scoped_lock lock(_reloadMutex);
		_isReloading = false;
	}

	bool GameIntegrator::LoadGame()
	{
		EnsureCodeWatcher();

		if (_gameDll && _gameExtension)
		{
			if (!_runtimeFS)
			{
				_runtimeFS = new HexEngine::FileSystem(L"RuntimeGameData");
				_runtimeFS->SetBaseDirectory(g_pEditor->_projectFS->GetBaseDirectory() / L"Build");
			}

			return true;
		}

		const fs::path builtGameDllPath = g_pEditor->_projectFS->GetBaseDirectory() / L"Build" / L"Game.dll";
		if (!BuildGame(_lastBuildProjectFileName))
		{
			LOG_CRIT("Could not build the game before loading '%S'", builtGameDllPath.wstring().c_str());
			return false;
		}

		if (!fs::exists(builtGameDllPath))
		{
			LOG_CRIT("Built game DLL was not found at '%S'", builtGameDllPath.wstring().c_str());
			return false;
		}

		if (!_runtimeFS)
		{
			_runtimeFS = new HexEngine::FileSystem(L"RuntimeGameData");
			_runtimeFS->SetBaseDirectory(g_pEditor->_projectFS->GetBaseDirectory() / L"Build");
		}

		_loadedGameDllPath = StageRuntimeGameDllCopy(builtGameDllPath);
		if (_loadedGameDllPath.empty())
		{
			LOG_CRIT("Could not stage a hot reload copy of '%S'", builtGameDllPath.wstring().c_str());
			SAFE_DELETE(_runtimeFS);
			return false;
		}

		LOG_INFO("Loading game DLL from '%S'", _loadedGameDllPath.wstring().c_str());
		_gameDll = LoadLibraryW(_loadedGameDllPath.wstring().c_str());

		if (_gameDll == nullptr)
		{
			LOG_CRIT("Could not load game DLL. Error: %d", GetLastError());
			SAFE_DELETE(_runtimeFS);
			return false;
		}

		using tCreateGame = HexEngine::IGameExtension * (*)();
		auto CreateGame = reinterpret_cast<tCreateGame>(GetProcAddress(_gameDll, "CreateGame"));
		if (!CreateGame)
		{
			LOG_CRIT("Could not find CreateGame function in Game.dll");
			UnloadGame();
			return false;
		}

		_gameExtension = CreateGame();
		if (!_gameExtension)
		{
			LOG_CRIT("Could not create IGameExtension, something is probably wrong in the game code!");
			UnloadGame();
			return false;
		}

		_gameExtension->OnRegisterClasses();
		_state = GameTestState::Loaded;
		return true;
	}

	bool GameIntegrator::BuildGame(const std::wstring& projectFileName)
	{
		_buildProjectPath = ResolveBuildProjectPath(projectFileName);
		if (_buildProjectPath.empty())
		{
			LOG_CRIT("Could not locate a Visual Studio solution or project to build in the project Code directory");
			return false;
		}

		_lastBuildProjectFileName = projectFileName;

		auto msbuildPath = FindMSBuildExecutable();
		if (msbuildPath.empty())
		{
			LOG_CRIT("Could not locate MSBuild.exe");
			return false;
		}

		MSBuildGameBuildService buildService;
		return buildService.Build(_buildProjectPath, g_pEditor->_projectFS->GetBaseDirectory(), msbuildPath, _reloadGeneration);
	}

	bool GameIntegrator::RunGame()
	{
		if ((!_gameExtension || !_runtimeFS || !_gameDll) && !LoadGame())
			return false;

		if (_state == GameTestState::Started)
			return true;

		HexEngine::g_pEnv->GetResourceSystem().AddFileSystem(_runtimeFS);
		HexEngine::g_pEnv->AddGameExtension(_gameExtension);
		HexEngine::g_pEnv->SetEditorMode(false);
		HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->AddEntityListener(this);

		// make a backup of the main camera for restoring after the game stops
		_origMainCamera = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera();

		_gameExtension->OnCreateGame();

		_state = GameTestState::Started;
		return true;
	}

	bool GameIntegrator::StopGame()
	{
		if (_state != GameTestState::Started || !_gameExtension)
			return false;

		if (_runtimeFS)
		{
			HexEngine::g_pEnv->GetResourceSystem().RemoveFileSystem(_runtimeFS);
			delete _runtimeFS;
			_runtimeFS = nullptr;
		}

		HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->RemoveEntityListener(this);

		for (auto& tempEnt : _tempEntitiesCreated)
		{
			HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->DestroyEntity(tempEnt);
		}
		_tempEntitiesCreated.clear();

		_gameExtension->OnStopGame();

		HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->SetMainCamera(_origMainCamera);
		HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->ForceRebuildPVS();

		HexEngine::g_pEnv->SetEditorMode(true);
		HexEngine::g_pEnv->_inputSystem->SetMouseLockMode(HexEngine::MouseLockMode::Free);
		HexEngine::g_pEnv->RemoveGameExtension(_gameExtension);

		_state = GameTestState::Stopped;
		return true;
	}

	GameTestState GameIntegrator::GetState() const
	{
		return _state;
	}

	void GameIntegrator::OnAddEntity(HexEngine::Entity* entity)
	{
		_tempEntitiesCreated.push_back(entity);
	}

	void GameIntegrator::OnRemoveEntity(HexEngine::Entity* entity)
	{
		_tempEntitiesCreated.erase(std::remove(_tempEntitiesCreated.begin(), _tempEntitiesCreated.end(), entity), _tempEntitiesCreated.end());
	}

	void GameIntegrator::EnsureCodeWatcher()
	{
		if (_isWatchingCode || !g_pEditor || !g_pEditor->_projectFS)
			return;

		_codeDirectory = g_pEditor->_projectFS->GetBaseDirectory() / L"Code";
		if (!fs::is_directory(_codeDirectory))
			return;

		_isWatchingCode = g_pEditor->_projectFS->CreateChangeNotifier(
			_codeDirectory,
			std::bind(&GameIntegrator::OnCodeFileChangeEvent, this, std::placeholders::_1, std::placeholders::_2));
	}

	void GameIntegrator::OnCodeFileChangeEvent(const HexEngine::DirectoryWatchInfo& info, const HexEngine::FileChangeActionMap& actionMap)
	{
		for (const auto& action : actionMap)
		{
			for (const auto& fileInfo : action.second)
			{
				if (!IsWatchedCodeFile(fileInfo.path) || IsIntermediateCodePath(fileInfo.path))
					continue;

				std::scoped_lock lock(_reloadMutex);
				_reloadRequested = true;
				_lastCodeChange = std::chrono::steady_clock::now();
				LOG_INFO("Queued hot reload for game code change: %S", fileInfo.path.wstring().c_str());
				return;
			}
		}
	}

	bool GameIntegrator::HotReloadGame()
	{
		if (_gameDll == nullptr && _gameExtension == nullptr)
			return false;

		const bool wasStarted = (_state == GameTestState::Started);
		if (wasStarted)
			StopGame();

		if (!UnloadGame())
			return false;

		if (!BuildGame(_lastBuildProjectFileName))
			return false;

		if (!LoadGame())
			return false;

		if (wasStarted)
			return RunGame();

		return true;
	}

	bool GameIntegrator::UnloadGame()
	{
		if (_state == GameTestState::Started)
			StopGame();

		if (_runtimeFS)
		{
			delete _runtimeFS;
			_runtimeFS = nullptr;
		}

		if (_gameExtension)
		{
			using tDestroyGame = void (*)(HexEngine::IGameExtension*);
			auto DestroyGame = reinterpret_cast<tDestroyGame>(GetProcAddress(_gameDll, "DestroyGame"));
			if (DestroyGame)
				DestroyGame(_gameExtension);
			else
				delete _gameExtension;

			_gameExtension = nullptr;
		}

		if (_gameDll)
		{
			FreeLibrary(_gameDll);
			_gameDll = nullptr;
		}

		if (!_loadedGameDllPath.empty())
		{
			std::error_code ec;
			fs::remove(_loadedGameDllPath, ec);
			_loadedGameDllPath.clear();
		}

		_state = GameTestState::None;
		return true;
	}

	fs::path GameIntegrator::StageRuntimeGameDllCopy(const fs::path& sourceDllPath)
	{
		if (!fs::exists(sourceDllPath))
			return {};

		const fs::path hotReloadDir = sourceDllPath.parent_path() / L"HotReload";
		std::error_code ec;
		fs::create_directories(hotReloadDir, ec);
		if (ec)
			return {};

		const fs::path stagedDllPath = hotReloadDir / std::format(L"Game_Loaded_{:06}.dll", _reloadGeneration);
		fs::copy_file(sourceDllPath, stagedDllPath, fs::copy_options::overwrite_existing, ec);
		if (ec)
			return {};

		return stagedDllPath;
	}

	fs::path GameIntegrator::ResolveBuildProjectPath(const std::wstring& projectFileName) const
	{
		if (!g_pEditor || !g_pEditor->_projectFS)
			return {};

		const fs::path codeDir = g_pEditor->_projectFS->GetBaseDirectory() / L"Code";
		if (!fs::is_directory(codeDir))
			return {};

		if (!projectFileName.empty())
		{
			fs::path directPath = codeDir / projectFileName;
			if (fs::exists(directPath))
				return directPath;
		}

		for (const auto& entry : fs::directory_iterator(codeDir))
		{
			if (entry.path().extension() == L".sln")
				return entry.path();
		}

		for (const auto& entry : fs::recursive_directory_iterator(codeDir))
		{
			if (entry.path().extension() == L".vcxproj")
				return entry.path();
		}

		return {};
	}

	fs::path GameIntegrator::FindMSBuildExecutable() const
	{
		static const std::array<fs::path, 4> candidates = {
			fs::path(L"C:/Program Files/Microsoft Visual Studio/2022/Enterprise/MSBuild/Current/Bin/MSBuild.exe"),
			fs::path(L"C:/Program Files/Microsoft Visual Studio/2022/Professional/MSBuild/Current/Bin/MSBuild.exe"),
			fs::path(L"C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe"),
			fs::path(L"C:/Program Files/Microsoft Visual Studio/2022/BuildTools/MSBuild/Current/Bin/MSBuild.exe")
		};

		for (const auto& candidate : candidates)
		{
			if (fs::exists(candidate))
				return candidate;
		}

		return {};
	}

	bool GameIntegrator::IsWatchedCodeFile(const fs::path& path)
	{
		const auto ext = path.extension().wstring();
		return ext == L".cpp" || ext == L".cxx" || ext == L".cc" || ext == L".h" || ext == L".hpp" || ext == L".inl";
	}

	bool GameIntegrator::IsIntermediateCodePath(const fs::path& path)
	{
		std::wstring lowered = path.wstring();
		std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
		return lowered.find(L"\\x64\\") != std::wstring::npos
			|| lowered.find(L"\\win32\\") != std::wstring::npos
			|| lowered.find(L"\\.vs\\") != std::wstring::npos
			|| lowered.find(L"\\build\\") != std::wstring::npos;
	}
}
