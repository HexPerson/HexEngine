
#pragma once

#include <Windows.h>

namespace HexEngine
{
	/**
	 * @brief RAII owner of a loaded module (HMODULE).
	 *
	 * FreeLibrary()s on destruction unless release()d. Move-only. This fixes the
	 * handle leaks in the plugin loader: previously every failed step after
	 * LoadLibrary (missing CreatePlugin/DestroyPlugin export, null interface,
	 * disabled plugin) returned without FreeLibrary, leaking the mapped module.
	 * Now the module is freed automatically on any early return, and only
	 * release()d into the plugin record on a fully successful load.
	 */
	class ScopedModule
	{
	public:
		ScopedModule() = default;
		explicit ScopedModule(HMODULE handle) : _handle(handle) {}
		~ScopedModule() { reset(); }

		ScopedModule(const ScopedModule&) = delete;
		ScopedModule& operator=(const ScopedModule&) = delete;

		ScopedModule(ScopedModule&& other) noexcept : _handle(other._handle) { other._handle = nullptr; }
		ScopedModule& operator=(ScopedModule&& other) noexcept
		{
			if (this != &other)
			{
				reset();
				_handle = other._handle;
				other._handle = nullptr;
			}
			return *this;
		}

		HMODULE get() const { return _handle; }
		explicit operator bool() const { return _handle != nullptr; }

		// Relinquish ownership without freeing (hand off to the plugin record).
		HMODULE release()
		{
			HMODULE h = _handle;
			_handle = nullptr;
			return h;
		}

		void reset(HMODULE handle = nullptr)
		{
			if (_handle != nullptr)
				FreeLibrary(_handle);
			_handle = handle;
		}

	private:
		HMODULE _handle = nullptr;
	};
}
