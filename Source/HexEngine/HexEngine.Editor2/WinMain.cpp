
#include "Editor.hpp"
#include "UI/EditorUI.hpp"

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

	g_pEnv->_fileSystem->CreateChangeNotifier(g_pEnv->_fileSystem->GetDataDirectory(),
		[](PFILE_NOTIFY_INFORMATION info)
		{
			DWORD name_len = info->FileNameLength / sizeof(wchar_t);

			switch (info->Action) {
			case FILE_ACTION_ADDED: {


			}
			}
		});

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

	Window::Destroy(mainWindow);

	// Finally, destroy the environment
	//
	DestroyEnvironment();

	// this will show as a leak, but its not
	SAFE_DELETE(HexEditor::g_pEditor);

	return EXIT_SUCCESS;
}