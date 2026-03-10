
#include "GameIntegrator.hpp"
#include "Editor.hpp"

namespace HexEditor
{
	bool GameIntegrator::LoadGame()
	{
		fs::path gameDllPath = g_pEditor->_projectFS->GetBaseDirectory() / L"Build";

		_runtimeFS = new HexEngine::FileSystem(L"RuntimeGameData");
		_runtimeFS->SetBaseDirectory(gameDllPath);

		

		gameDllPath /= fs::path(L"Game.dll");

		_gameDll = LoadLibraryW(gameDllPath.wstring().c_str());

		LOG_INFO("Loading game DLL from '%S'", gameDllPath.wstring().c_str());

		if (_gameDll == nullptr)
		{
			LOG_CRIT("Could not load game DLL. Error: %d", GetLastError());
			return false;
		}

		using tCreateGame = HexEngine::IGameExtension * (*)();
		tCreateGame CreateGame = (tCreateGame)GetProcAddress(_gameDll, "CreateGame");

		if (!CreateGame)
		{
			LOG_CRIT("Could not find CreateGame function in Game.dll");
			return false;
		}

		_gameExtension =  CreateGame();

		if (!_gameExtension)
		{
			LOG_CRIT("Could not create IGameExtension, something is probably wrong in the game code!");
			return false;
		}

		_gameExtension->OnRegisterClasses();

		_state = GameTestState::Loaded;

		return true;
	}

	bool GameIntegrator::BuildGame(const std::wstring& projectFileName)
	{
		fs::path slnPath = g_pEditor->_projectFS->GetBaseDirectory() / L"Code" / projectFileName;

		// msbuild myProject.vcxproj /p:PlatformToolset=v140

		return true;
	}

	bool GameIntegrator::RunGame()
	{
		HexEngine::g_pEnv->_resourceSystem->AddFileSystem(_runtimeFS);
		HexEngine::g_pEnv->AddGameExtension(_gameExtension);
		HexEngine::g_pEnv->SetEditorMode(false);
		HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->AddEntityListener(this);

		_gameExtension->OnCreateGame();

		_state = GameTestState::Started;

		return true;
	}

	bool GameIntegrator::StopGame()
	{
		if (_runtimeFS)
		{
			HexEngine::g_pEnv->_resourceSystem->RemoveFileSystem(_runtimeFS);
			delete _runtimeFS;
			_runtimeFS = nullptr;
		}

		HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->RemoveEntityListener(this);

		for (auto& tempEnt : _tempEntitiesCreated)
		{
			HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->DestroyEntity(tempEnt);
		}

		_gameExtension->OnStopGame();

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
}