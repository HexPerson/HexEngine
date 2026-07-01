

#include "../HexEngine.Core/HexEngine.hpp"
#include "../HexEngine.Core/FileSystem/AssetPackage.hpp"
#include "../HexEngine.Core/Entity/Component/PlayerStartComponent.hpp"

//HVar* HexEngine::g_hvars = nullptr;
//HCommand* HexEngine::g_commands = nullptr;
//int32_t HexEngine::g_numVars = 0;
//int32_t HexEngine::g_numCommands = 0;

// Holds the lifetime of the GameData mount so the AssetPackage isn't freed
// while the game is still loading resources from it. Kept at file scope rather
// than as a local in WinMain because the package needs to outlive the game
// loop without forcing every game-side resource load to keep its own ref.
static std::shared_ptr<HexEngine::AssetPackage> g_gameDataPackage;
static HexEngine::FileSystem* g_gameDataDiskFs = nullptr;

// Auto-mounts the game's data so resource loads find their assets. Two
// scenarios:
//   1. Shipped game next to GameData.pkg -> mount the pkg under the
//      "GameData" filesystem name; resource loads stream from the package.
//   2. Dev / loose-asset launch with a Data/ folder next to the exe ->
//      mount that folder as a regular FileSystem under "GameData". The
//      editor takes a different path (Editor.cpp creates a "GameData"
//      FileSystem rooted at the project folder), so neither double-mounts.
static void MountGameData()
{
	if (HexEngine::g_pEnv == nullptr)
		return;

	const fs::path exeDir = fs::current_path();
	const fs::path packagePath = exeDir / L"Data" / L"AssetPackages" / L"GameData.pkg";

	if (fs::exists(packagePath))
	{
		// A bootstrap FileSystem rooted at exeDir gives AssetPackage::Create
		// a place to resolve the loader path; the pkg's loader needs a
		// hosting FileSystem entry the resource system can walk.
		HexEngine::FileSystem* bootstrapFs = new HexEngine::FileSystem(L"GameDataBootstrap");
		bootstrapFs->SetBaseDirectory(exeDir);
		HexEngine::g_pEnv->GetResourceSystem().AddFileSystem(bootstrapFs);

		LOG_INFO("Loading game asset package from '%S'", packagePath.wstring().c_str());
		g_gameDataPackage = HexEngine::AssetPackage::Create(L"GameDataBootstrap.AssetPackages/GameData.pkg", L"GameData");

		HexEngine::g_pEnv->GetResourceSystem().RemoveFileSystem(bootstrapFs);
		delete bootstrapFs;

		if (!g_gameDataPackage)
		{
			LOG_CRIT("Failed to load GameData.pkg from '%S'; game asset lookups will fail until a Data/ folder is mounted.", packagePath.wstring().c_str());
		}
		return;
	}

	const fs::path looseDataDir = exeDir / L"Data";
	if (fs::exists(looseDataDir) && fs::is_directory(looseDataDir))
	{
		g_gameDataDiskFs = new HexEngine::FileSystem(L"GameData");
		g_gameDataDiskFs->SetBaseDirectory(exeDir);
		HexEngine::g_pEnv->GetResourceSystem().AddFileSystem(g_gameDataDiskFs);
		LOG_INFO("Loading game data from loose directory '%S'", looseDataDir.wstring().c_str());
		return;
	}

	LOG_WARN("No GameData.pkg or Data/ directory found next to the executable. Game.dll resource lookups will fail.");
}

HexEngine::IGameExtension* LoadGameExtension()
{
	fs::path gameDllPath = fs::current_path() / fs::path(L"Game.dll");

	HMODULE gameDll = LoadLibraryW(gameDllPath.wstring().c_str());

	LOG_INFO("Loading game DLL from '%S'", gameDllPath.wstring().c_str());

	if (gameDll == nullptr)
	{
		LOG_CRIT("Could not load game DLL. Error: %d", GetLastError());
		return nullptr;
	}

	using tCreateGame = HexEngine::IGameExtension * (*)(HexEngine::IEnvironment* pEnv);
	tCreateGame CreateGame = (tCreateGame)GetProcAddress(gameDll, "CreateGame");

	if (!CreateGame)
	{
		LOG_CRIT("Could not find CreateGame function in Game.dll");
		return nullptr;
	}

	return CreateGame(HexEngine::g_pEnv);
}

int WinMain(
	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR     lpCmdLine,
	int       nShowCmd
)
{
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);

	// Get the desktop resolution
	int screenWidth, screenheight;
	HexEngine::Window::GetDesktopResolution(screenWidth, screenheight);

	

	// Initialise a game window for rendering
	//
	HexEngine::Window* mainWindow = HexEngine::Window::Create(0, 0, screenWidth, screenheight, HexEngine::DisplayMode::FullscreenBorderless, "A Hex Engine Game");

	// Create a new Game3DOptions instance
	//
	HexEngine::Game3DOptions environmentOpts;

	environmentOpts.window = mainWindow;

	// Create a 3D Game environment
	//
	if (HexEngine::Game3DEnvironment::Create(environmentOpts) == nullptr)
	{
		HexEngine::Window::Destroy(mainWindow);

		HexEngine::DestroyEnvironment();

		// Add an error message here?
		return EXIT_FAILURE;
	}

	// Mount the game's data BEFORE the game extension's OnRegisterClasses /
	// OnLoadGameWorld runs so resource lookups it kicks off can resolve
	// against the package (or loose Data/ fallback) immediately.
	MountGameData();

	auto pGameExtension = LoadGameExtension();

	if (pGameExtension)
	{
		// mainWindow->SetTitle ...
		HexEngine::g_pEnv->AddGameExtension(pGameExtension);
		pGameExtension->OnRegisterClasses();
		pGameExtension->OnLoadGameWorld();

		// Spawn the player at the scene's Player Start (if any) before
		// OnCreateGame, mirroring the editor's GameIntegrator::RunGame. We move
		// the main camera's entity to the marker's transform; a game that
		// attaches a camera controller to the main camera in OnCreateGame then
		// begins play there, and can still override it.
		if (auto scene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene().get(); scene != nullptr)
		{
			if (auto* cam = scene->GetMainCamera(); cam != nullptr && cam->GetEntity() != nullptr)
			{
				std::vector<HexEngine::PlayerStartComponent*> starts;
				if (scene->GetComponents<HexEngine::PlayerStartComponent>(starts) && !starts.empty())
				{
					HexEngine::PlayerStartComponent* chosen = starts.front();
					for (auto* s : starts) { if (s != nullptr && s->IsPrimary()) { chosen = s; break; } }
					if (chosen != nullptr && chosen->GetEntity() != nullptr)
					{
						cam->GetEntity()->SetPosition(chosen->GetEntity()->GetWorldTM().Translation());
						cam->GetEntity()->SetRotation(chosen->GetEntity()->GetRotation());
					}
				}
			}
		}

		HexEngine::g_pEnv->SetGameRunning(true);
		pGameExtension->OnCreateGame();
	}

	// Rebuild the PVS after game launch
	HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->ForceRebuildPVS();

	//g_pEnv->_inputSystem->EnableRawInput(true);

	// Now that everything is created, enter the game loop
	//
	while (HexEngine::g_pEnv->IsRunning())
	{
		HexEngine::g_pEnv->Run();
	}

	pGameExtension->OnStopGame();
	HexEngine::g_pEnv->SetGameRunning(false);

	HexEngine::Window::Destroy(mainWindow);

	// Finally, destroy the environment
	//
	HexEngine::DestroyEnvironment();

	// this will show as a leak, but its not
	return EXIT_SUCCESS;
}

	