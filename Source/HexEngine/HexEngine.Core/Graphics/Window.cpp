

#include "Window.hpp"
#include "../HexEngine.hpp"
#include "LogWindowMessages.hpp"

EXTERN_C IMAGE_DOS_HEADER __ImageBase;
#define HINST_THISCOMPONENT ((HINSTANCE)&__ImageBase)

namespace HexEngine
{
	Window::Window() :
		_handle(nullptr),
		_width(0),
		_height(0)
	{}

	Window::~Window()
	{
		if (_displayMode == DisplayMode::Fullscreen)
		{
			ChangeDisplaySettings(NULL, 0);
		}
	}

	static LRESULT CALLBACK HexWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if(g_pEnv && g_pEnv->_inputSystem)
			return g_pEnv->_inputSystem->HandleWindowMessage(hWnd, message, wParam, lParam);

		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	void Window::GetDesktopResolution(int& width, int& height)
	{
		width = GetSystemMetrics(SM_CXSCREEN);
		height = GetSystemMetrics(SM_CYSCREEN);
	}

	HWND HEX_API WindowCreate(int32_t xpos, int32_t ypos, int32_t width, int32_t height, DisplayMode displayMode, const std::string& windowTitle)
	{
		WNDCLASSEXA wcex = { sizeof(wcex) };

		wcex.style = 0;
		wcex.lpfnWndProc = HexWndProc;
		wcex.cbWndExtra = sizeof(LONG_PTR);
		wcex.hInstance = HINST_THISCOMPONENT;
		wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
		wcex.hIcon = 0;
		wcex.hIconSm = 0;
		wcex.lpszClassName = "HexEngine";
		wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
		wcex.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC | CS_DBLCLKS;

		RegisterClassExA(&wcex);

		auto screenWidth = GetSystemMetrics(SM_CXSCREEN);
		auto screenHeight = GetSystemMetrics(SM_CYSCREEN);

		// Force the x and y position of the window to the top left if we're using any kind of fullscreen
		//
		if (displayMode == DisplayMode::Fullscreen || displayMode == DisplayMode::FullscreenBorderless)
			xpos = ypos = 0;

		// Handle switching to fullscreen
		//
		if (displayMode == DisplayMode::Fullscreen)
		{
			DEVMODE dmScreenSettings;

			// If full screen set the screen to maximum size of the users desktop and 32bit.
			//
			memset(&dmScreenSettings, 0, sizeof(dmScreenSettings));
			dmScreenSettings.dmSize = sizeof(dmScreenSettings);
			dmScreenSettings.dmPelsWidth = (unsigned long)screenWidth;
			dmScreenSettings.dmPelsHeight = (unsigned long)screenHeight;
			dmScreenSettings.dmBitsPerPel = 32;
			dmScreenSettings.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;

			// Change the display settings to full screen.
			//
			ChangeDisplaySettings(&dmScreenSettings, CDS_FULLSCREEN);
		}

		HWND wnd;

		if (displayMode == DisplayMode::Windowed)
		{
			wnd = CreateWindowExA(
				WS_EX_APPWINDOW,//WS_EX_CLIENTEDGE,
				"HexEngine",
				windowTitle.c_str(),
				WS_OVERLAPPEDWINDOW,//WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_POPUP,
				0,//(screenWidth/2)-(width/2),
				0,//(screenHeight/2)-(height/2),
				width, height,
				nullptr,
				nullptr,
				wcex.hInstance,
				0);
		}
		else
		{
			wnd = CreateWindowExA(
				WS_EX_APPWINDOW,
				"HexEngine",
				windowTitle.c_str(),
				WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_POPUP,
				xpos, ypos,
				width, height,
				nullptr,
				nullptr,
				wcex.hInstance,
				0);
		}

		if (!wnd)
		{
			return nullptr;
		}

		

		//ShowCursor(FALSE);

		return wnd;
	}

	Window* Window::Create(int32_t xpos, int32_t ypos, int32_t width, int32_t height, DisplayMode displayMode, const std::string& windowTitle)
	{
		Window* window = new Window;

		window->_width = width;
		window->_height = height;
		window->_title = windowTitle;
		window->_displayMode = displayMode;

		window->_handle = WindowCreate(xpos, ypos, width, height, displayMode, windowTitle);

		if (!window->_handle || window->_handle == INVALID_HANDLE_VALUE)
		{
			LOG_CRIT("Failed to create window: %d", GetLastError());
			delete window;
			return nullptr;
		}

		ShowWindow(window->_handle, SW_SHOW);

		window->RecalculateWindowSize();		

		return window;
	}

	Window* Window::FindFromHandle(HWND handle)
	{
		for (auto& win : g_pEnv->_windows)
		{
			if (win->GetHandle() == handle)
				return win;
		}
		return nullptr;
	}

	void Window::RecalculateWindowSize()
	{
		GetClientRect(_handle, &_clientRect);

		RECT windowRect;
		GetWindowRect(_handle, &windowRect);

		_width = windowRect.right - windowRect.left;
		_height = windowRect.bottom - windowRect.top;

		_clientWidth = _clientRect.right - _clientRect.left;
		_clientHeight = _clientRect.bottom - _clientRect.top;
	}

	void Window::SetIcon(HICON icon)
	{
		SendMessage(_handle, WM_SETICON, ICON_SMALL, (LPARAM)icon);
		SendMessage(_handle, WM_SETICON, ICON_BIG, (LPARAM)icon);
	}

	void Window::Maximise()
	{
		ShowWindow(_handle, SW_MAXIMIZE);
		//GetClientRect(_handle, &_clientRect);

		//
		//ClientToScreen(_handle, (LPPOINT)&_clientRect.left); // get the top left in screen coords

		//



		////ScreenToClient(_handle, (LPPOINT)&_clientRect.left);
		//ScreenToClient(_handle, (LPPOINT)&_clientRect.right);

		RecalculateWindowSize();
	}

	void Window::Minimise()
	{
		ShowWindow(_handle, SW_MINIMIZE);
		//GetClientRect(_handle, &_clientRect);

		RecalculateWindowSize();
	}

	void Window::Destroy(Window* window)
	{
		// Destroy the handle
		//
		DestroyWindow(window->_handle);

		// Free the memory
		//
		SAFE_DELETE(window);
	}

	HWND Window::GetHandle() const
	{
		return _handle;
	}

	int32_t Window::GetWidth() const
	{
		return _width;
	}

	int32_t Window::GetHeight() const
	{
		return _height;
	}

	int32_t Window::GetClientWidth() const
	{
		return _clientWidth;
	}

	int32_t Window::GetClientHeight() const
	{
		return _clientHeight;
	}

	float Window::GetAspectRatio()
	{
		return (float)GetClientWidth() / (float)GetClientHeight();
	}

	DisplayMode Window::GetDisplayMode()
	{
		return _displayMode;
	}

	void Window::Update()
	{
		if (_displayFpsInTitle && g_pEnv->_timeManager->_hasFpsUpdated == true)
		{
			std::string windowText = _title;

			std::stringstream sstream;

			sstream << _title << " [" << g_pEnv->_timeManager->_fps << "fps, " << g_pEnv->_timeManager->_frameTimeMS << "ms ]";

			if (g_pEnv->_sceneManager->GetCurrentScene())
			{
				sstream << " Entities: " << g_pEnv->_sceneManager->GetCurrentScene()->GetNumberOfEntitiesDrawn();
			}

			SetWindowText(_handle, sstream.str().c_str());
		}
	}
}