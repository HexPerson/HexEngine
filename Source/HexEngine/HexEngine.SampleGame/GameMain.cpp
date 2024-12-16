
#include "../HexEngine.Core/Environment/Game3DEnvironment.hpp"
#include "../HexEngine.Core/Graphics/Window.hpp"
#include "Game.hpp"

#pragma comment(lib,"HexEngine.Core.lib")

using namespace HexEngine;

IEnvironment* HexEngine::g_pEnv = nullptr;

int WinMain(
	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR     lpCmdLine,
	int       nShowCmd
)
{
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);

	int screenWidth, screenheight;
	Window::GetDesktopResolution(screenWidth, screenheight);

	screenWidth = 2160;
	screenheight = 1440;

	// Initialise a game window for rendering
	//
	Window* mainWindow = Window::Create(0, 0, screenWidth, screenheight, DisplayMode::Windowed, "HexEngine Editor");

	// Instruct the window to append FPS information to the title bar
	//
	mainWindow->_displayFpsInTitle = true;

	CityBuilder::g_pGame = new CityBuilder::Game;

	// Create a new Game3DOptions instance
	//
	Game3DOptions environmentOpts;

	environmentOpts.window = mainWindow;
	//environmentOpts.windowHandle = mainWindow.GetHandle();
	environmentOpts.graphicsEngine = GraphicsEngine::DirectX11;
	environmentOpts.physicsEngine = PhysicsEngine::PhysX;
	environmentOpts.fontEngine = FontEngine::None;// FreeType;
	environmentOpts.gameExtension = CityBuilder::g_pGame;
	//environmentOpts.windowWidth = 1024;
	//environmentOpts.windowHeight = 768;

	

	// Create a 3D Game environment
	//
	if (Game3DEnvironment::Create(environmentOpts) == nullptr)
	{
		Window::Destroy(mainWindow);

		DestroyEnvironment();

		SAFE_DELETE(CityBuilder::g_pGame);

		// Add an error message here?
		return EXIT_FAILURE;
	}

	// Cap the FPS to 30
	//
	//g_pEnv->_timeManager->SetTargetFps(250);

	// Now that everything is created, enter the game loop
	//
	while (g_pEnv->IsRunning())
	{
		g_pEnv->Run();
	}

	Window::Destroy(mainWindow);	

	// Finally, destroy the environment
	//
	DestroyEnvironment();

	// this will show as a leak, but its not
	SAFE_DELETE(CityBuilder::g_pGame);

	return EXIT_SUCCESS;
}