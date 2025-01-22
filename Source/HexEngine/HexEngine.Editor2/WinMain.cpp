
#include "Editor.hpp"
#include "UI/EditorUI.hpp"

static std::vector<std::shared_ptr<IShader>> g_hotReloadShaders;

void PrepareShaderHotReload()
{
	// find all the shaders in the shaders dir, and "load" them.
	// the reason we do this is so that change notifications can be received later on
	for (auto const& dir_entry : std::filesystem::recursive_directory_iterator(_SHADERS_LIVE_DIR))
	{
		if (dir_entry.is_directory())
			continue;

		if (dir_entry.is_regular_file() == false)
			continue;

		const auto& path = dir_entry.path();

		if (path.extension() != ".shader")
			continue;


		g_hotReloadShaders.push_back(IShader::Create(path));
	}

	// create a file watch on the shaders folder, so we can catch any modifications and hot reload
	g_pEnv->_fileSystem->CreateChangeNotifier(_SHADERS_LIVE_DIR);
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
	Window::GetDesktopResolution(screenWidth, screenHeight);

#ifdef _DEBUG
	//screenWidth = 2560;
	//screenHeight = 1440;

	screenWidth = DEV_RESOLUTION_X;
	screenHeight = DEV_RESOLUTION_Y;
#endif

	// Create an editor window
	Window* mainWindow = Window::Create(0, 0, screenWidth, screenHeight, DisplayMode::Windowed, "Hex Engine Editor");
	//mainWindow->Maximise();

	mainWindow->_displayFpsInTitle = true;

	// Allow the editor to accept drag&drop files
	DragAcceptFiles(mainWindow->GetHandle(), TRUE);

	fs::path iconDir = fs::current_path() / fs::path(L"Data/Textures/UI/hex_icon.ico");

	if (fs::exists(iconDir))
	{
		HICON icon = (HICON)LoadImageW(NULL, iconDir.wstring().c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE);

		if (icon == 0)
		{
			int err = GetLastError();
			LOG_CRIT("Failed to load editor icon: %d", err);
		}

		mainWindow->SetIcon(icon);
	}

	HexEditor::g_pEditor = new HexEditor::EditorExtension;

	// Create a new Game3DOptions instance
	//
	Game3DOptions environmentOpts;

	environmentOpts.window = mainWindow;
	environmentOpts.applicationName = L"HexEngineEditor";
	environmentOpts.createIconService = true;

	// Create a 3D Game environment
	//
	if (Game3DEnvironment::Create(environmentOpts) == nullptr)
	{
		Window::Destroy(mainWindow);

		DestroyEnvironment();

		return EXIT_FAILURE;
	}

	PrepareShaderHotReload();

	g_pEnv->SetEditorMode(true);
	g_pEnv->AddGameExtension(HexEditor::g_pEditor);
	HexEditor::g_pEditor->OnCreateGame();

	SAFE_DELETE(g_pEnv->_uiManager);
	g_pEnv->_uiManager = new HexEditor::EditorUI;
	g_pEnv->_uiManager->Create(mainWindow->GetClientWidth(), mainWindow->GetClientHeight());

	//g_pEnv->_inputSystem->SetMouseMode(dx::Mouse::Mode::MODE_ABSOLUTE);

	while (g_pEnv->IsRunning())
	{
		g_pEnv->Run();
	}

	// release the hot reloaded shaders, this really just deletes memory
	for (auto& hotReloadShader : g_hotReloadShaders)
	{
		hotReloadShader.reset();
	}

	Window::Destroy(mainWindow);

	// Finally, destroy the environment
	//
	DestroyEnvironment();

	// this will show as a leak, but its not
	SAFE_DELETE(HexEditor::g_pEditor);

	return EXIT_SUCCESS;
}