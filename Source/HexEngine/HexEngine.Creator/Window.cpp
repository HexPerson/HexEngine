
#include "Window.hpp"

namespace HexCreator
{
	ultralight::Surface* SurfaceFactory::CreateSurface(uint32_t width, uint32_t height) {
		///
		/// Called by Ultralight when it wants to create a Surface.
		///
		return new Surface(width, height);
	}

	void SurfaceFactory::DestroySurface(ultralight::Surface* surface) {
		///
		/// Called by Ultralight when it wants to destroy a Surface.
		///
		delete static_cast<Surface*>(surface);
	}

	Window::Window(int32_t width, int32_t height)
	{
		_width = width;
		_height = height;
		_title = "HexEngine Creator";
		_displayMode = HexEngine::DisplayMode::Windowed;

		_handle = HexEngine::WindowCreate(0, 0, width, height, HexEngine::DisplayMode::Windowed, "HexEngine Creator");

		ShowWindow(_handle, SW_SHOW);

		RecalculateWindowSize();
	}
}