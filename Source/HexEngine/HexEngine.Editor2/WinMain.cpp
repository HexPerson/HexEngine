
#include "Editor.hpp"
#include "UI/EditorUI.hpp"

static std::vector<std::shared_ptr<HexEngine::IShader>> g_hotReloadShaders;

void PrepareShaderHotReload()
{
	// _SHADERS_LIVE_DIR is the SOURCE tree's HexEngine.Shaders folder, baked in at
	// compile time. It only exists on a developer machine: an installed or
	// portable build has no source tree, and iterating a missing directory
	// throws - which crashed the editor on startup. No sources, no hot reload.
	{
		std::error_code ec;
		if (!std::filesystem::is_directory(_SHADERS_LIVE_DIR, ec))
			return;
	}

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


		g_hotReloadShaders.push_back(HexEngine::IShader::Create(path));
	}

	// create a file watch on the shaders folder, so we can catch any modifications and hot reload
	HexEngine::g_pEnv->GetFileSystem().CreateChangeNotifier(_SHADERS_LIVE_DIR);
}

int WinMain(
	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR     lpCmdLine,
	int       nShowCmd
)
{
	// The engine resolves Data\, Plugins\, Bin\ and the helper tools relative to
	// the WORKING directory. An installed build can be started from anywhere
	// (a pinned taskbar icon, a file association, another folder's shell), so
	// pin it to the install folder rather than trust the caller's. Dev builds
	// keep their existing behaviour (Visual Studio sets the working dir).
	if (HexEngine::FileSystem::IsInstalledBuild())
	{
		wchar_t exePath[MAX_PATH] = {};
		if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0)
			SetCurrentDirectoryW(fs::path(exePath).parent_path().c_str());
	}

	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);


	int screenWidth, screenHeight;
	HexEngine::Window::GetDesktopResolution(screenWidth, screenHeight);

#ifdef _DEBUG
	//screenWidth = 2560;
	//screenHeight = 1440;

	screenWidth = DEV_RESOLUTION_X;
	screenHeight = DEV_RESOLUTION_Y;
#endif

	// Create an editor window
	HexEngine::Window* mainWindow = HexEngine::Window::Create(0, 0, screenWidth, screenHeight, HexEngine::DisplayMode::Windowed, "Hex Engine Studio");
	mainWindow->Maximise();

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
	HexEngine::Game3DOptions environmentOpts;

	environmentOpts.window = mainWindow;
	environmentOpts.applicationName = L"HexEngineStudio";
	environmentOpts.createIconService = true;

	// Create a 3D Game environment
	//
	if (HexEngine::Game3DEnvironment::Create(environmentOpts) == nullptr)
	{
		HexEngine::Window::Destroy(mainWindow);

		HexEngine::DestroyEnvironment();

		return EXIT_FAILURE;
	}

	PrepareShaderHotReload();

	HexEngine::g_pEnv->SetEditorMode(true);
	HexEngine::g_pEnv->AddGameExtension(HexEditor::g_pEditor);
	HexEditor::g_pEditor->OnCreateGame();

	HexEditor::EditorUI* uiManager = new HexEditor::EditorUI;
	uiManager->Create(mainWindow->GetClientWidth(), mainWindow->GetClientHeight());
	HexEngine::g_pEnv->SetUIManager(uiManager);

	// Expose editor-only state (selection / open project) to Core plugins such as
	// the read-only MCP editor bridge. EditorUI implements IEditorContext.
	HexEngine::g_pEnv->_editorContext = uiManager;

	//g_pEnv->_inputSystem->SetMouseMode(dx::Mouse::Mode::MODE_ABSOLUTE);

	while (HexEngine::g_pEnv->IsRunning())
	{
		HexEngine::g_pEnv->Run();
	}

	// release the hot reloaded shaders, this really just deletes memory
	for (auto& hotReloadShader : g_hotReloadShaders)
	{
		hotReloadShader.reset();
	}

	HexEngine::Window::Destroy(mainWindow);

	// Finally, destroy the environment
	//
	HexEngine::g_pEnv->_editorContext = nullptr; // stop exposing editor state before teardown
	HexEngine::DestroyEnvironment();

	// this will show as a leak, but its not
	SAFE_DELETE(HexEditor::g_pEditor);

	return EXIT_SUCCESS;
}