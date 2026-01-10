

#include "../HexEngine.Core/HexEngine.hpp"

//HVar* HexEngine::g_hvars = nullptr;
//HCommand* HexEngine::g_commands = nullptr;
//int32_t HexEngine::g_numVars = 0;
//int32_t HexEngine::g_numCommands = 0;

IGameExtension* LoadGameExtension()
{
	fs::path gameDllPath = fs::current_path() / fs::path(L"Game.dll");

	HMODULE gameDll = LoadLibraryW(gameDllPath.wstring().c_str());

	LOG_INFO("Loading game DLL from '%S'", gameDllPath.wstring().c_str());

	if (gameDll == nullptr)
	{
		LOG_CRIT("Could not load game DLL. Error: %d", GetLastError());
		return nullptr;
	}

	using tCreateGame = IGameExtension * (*)(IEnvironment* pEnv);
	tCreateGame CreateGame = (tCreateGame)GetProcAddress(gameDll, "CreateGame");

	if (!CreateGame)
	{
		LOG_CRIT("Could not find CreateGame function in Game.dll");
		return nullptr;
	}

	return CreateGame(g_pEnv);
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
	Window::GetDesktopResolution(screenWidth, screenheight);

	

	// Initialise a game window for rendering
	//
	Window* mainWindow = Window::Create(0, 0, screenWidth, screenheight, DisplayMode::FullscreenBorderless, "A Hex Engine Game");

	// Create a new Game3DOptions instance
	//
	Game3DOptions environmentOpts;

	environmentOpts.window = mainWindow;

	// Create a 3D Game environment
	//
	if (Game3DEnvironment::Create(environmentOpts) == nullptr)
	{
		Window::Destroy(mainWindow);

		DestroyEnvironment();

		// Add an error message here?
		return EXIT_FAILURE;
	}

	auto pGameExtension = LoadGameExtension();

	if (pGameExtension)
	{
		// mainWindow->SetTitle ...
		g_pEnv->AddGameExtension(pGameExtension);
		pGameExtension->OnRegisterClasses();
		pGameExtension->OnLoadGameWorld();
		pGameExtension->OnCreateGame();
	}

	//g_pEnv->_inputSystem->EnableRawInput(true);

	// Now that everything is created, enter the game loop
	//
	while (g_pEnv->IsRunning())
	{
		g_pEnv->Run();
	}

	pGameExtension->OnStopGame();

	Window::Destroy(mainWindow);

	// Finally, destroy the environment
	//
	DestroyEnvironment();

	// this will show as a leak, but its not
	return EXIT_SUCCESS;
}

	