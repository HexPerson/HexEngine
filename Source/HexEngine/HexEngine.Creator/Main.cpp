
#include <HexEngine.Core/HexEngine.hpp>
#include <Ultralight/Ultralight.h>
#include "Creator.hpp"
#include "CreatorApp.hpp"

#include <csignal>

using namespace ultralight;

void signal_handler(int signal)
{
	bool a = false;
}

int WinMain(
	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR     lpCmdLine,
	int       nShowCmd
)
{
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);


	int screenWidth, screenHeight;
	HexEngine::Window::GetDesktopResolution(screenWidth, screenHeight);

#ifdef _DEBUG
		//screenWidth = 2560;
		//screenHeight = 1440;

	screenWidth = DEV_RESOLUTION_X;
	screenHeight = DEV_RESOLUTION_Y;
#endif

	fs::path binpath = (fs::current_path() / "Bin");

	SetDllDirectoryW(binpath.wstring().c_str());

	// Install a signal handler
	std::signal(SIGTERM, signal_handler);

	//HexCreator::CreatorApp app;
	//app.Run();
	
	// Allow the editor to accept drag&drop files
	//DragAcceptFiles(mainWindow->GetHandle(), TRUE);

	fs::path iconDir = fs::current_path() / fs::path(L"Data/Textures/UI/hex_icon.ico");

	if (fs::exists(iconDir))
	{
		HICON icon = (HICON)LoadImageW(NULL, iconDir.wstring().c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE);

		if (icon == 0)
		{
			int err = GetLastError();
			LOG_CRIT("Failed to load editor icon: %d", err);
		}

		//mainWindow->SetIcon(icon);
	}

	HexCreator::g_pCreator = new HexCreator::Creator;

	// Create a new Game3DOptions instance
	//
	HexEngine::Game3DOptions environmentOpts;

	environmentOpts.window = HexCreator::g_pCreator->_app._window;
	//environmentOpts.windowWidth = app.window()->width();
	//environmentOpts.windowHeight = app.window()->height();
	environmentOpts.applicationName = L"HexEngineEditor";
	environmentOpts.createIconService = true;
	//environmentOpts.flags = GameOptions_NoRenderer;

	// Create a 3D Game environment
	//
	if (HexEngine::Game3DEnvironment::Create(environmentOpts) == nullptr)
	{
		//Window::Destroy(mainWindow);

		HexEngine::DestroyEnvironment();

		return EXIT_FAILURE;
	}

	HexCreator::g_pCreator->_app.CreateTile(512, 512, 1.25);
	HexEngine::g_pEnv->_inputSystem->AddInputListener(&HexCreator::g_pCreator->_app, InputEventMaskAllDesktop);

	//PrepareShaderHotReload();

	HexEngine::g_pEnv->SetEditorMode(true);
	HexEngine::g_pEnv->AddGameExtension(HexCreator::g_pCreator);
	HexCreator::g_pCreator->OnCreateGame();

	//SAFE_DELETE(g_pEnv->_uiManager);
	//g_pEnv->_uiManager = new HexEditor::EditorUI;
	//g_pEnv->_uiManager->Create(mainWindow->GetClientWidth(), mainWindow->GetClientHeight());

	//g_pEnv->_inputSystem->SetMouseMode(dx::Mouse::Mode::MODE_ABSOLUTE);

	while (HexEngine::g_pEnv->IsRunning())
	{
		HexEngine::g_pEnv->Run();
	}

	// release the hot reloaded shaders, this really just deletes memory
	/*for (auto& hotReloadShader : g_hotReloadShaders)
	{
		hotReloadShader.reset();
	}*/

	//Window::Destroy(mainWindow);

	// Finally, destroy the environment
	//
	HexEngine::DestroyEnvironment();

	// this will show as a leak, but its not
	SAFE_DELETE(HexCreator::g_pCreator);

	return EXIT_SUCCESS;
}