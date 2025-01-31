

#pragma once

#include "../Required.hpp"

namespace HexEngine
{
	enum class DisplayMode
	{
		Windowed,
		Fullscreen,
		FullscreenBorderless
	};

	HWND WindowCreate(int32_t xpos, int32_t ypos, int32_t width, int32_t height, DisplayMode displayMode, const std::string& windowTitle);

	class Window
	{
	public:				
		Window();
		~Window();

		static Window* Create(int32_t xpos, int32_t ypos, int32_t width, int32_t height, DisplayMode displayMode, const std::string& windowTitle);
		static void Destroy(Window* window);

		static void GetDesktopResolution(int& width, int& height);

		HWND	GetHandle() const;
		int32_t GetWidth() const;
		int32_t GetHeight() const;
		int32_t GetClientWidth() const;
		int32_t GetClientHeight() const;

		DisplayMode GetDisplayMode();

		float GetAspectRatio();

		void Update();

		void SetIcon(HICON icon);

		void Maximise();
		void Minimise();
		void RecalculateWindowSize();

	protected:
		HWND _handle;
		int32_t _width;
		int32_t _height;
		int32_t _clientWidth;
		int32_t _clientHeight;
		std::string _title;
		DisplayMode _displayMode;
		RECT _clientRect;

	public:
		bool _displayFpsInTitle = false;
	};
}
