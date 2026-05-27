
#pragma once

#include "DiskFile.hpp"

namespace HexEngine
{
	/**
	 * @brief Read-only IFile backed by an in-memory byte span.
	 *
	 * Inherits DiskFile purely so the templated `Read<T>()`, `Read<T>(T*)`,
	 * and `ReadString()` helpers compile against this type unchanged - those
	 * templates go through the virtual `Read(void*, uint32_t)`, which we
	 * override to advance a cursor through the borrowed buffer. The
	 * inherited fstream is never opened, so `_stream.is_open()` would
	 * naturally return false; we override `IsOpen()` to always return true
	 * (the buffer is "always open" once constructed).
	 *
	 * Used by resource loaders' LoadResourceFromMemory entry points so the
	 * same binary-parsing code that drives LoadResourceFromFile works
	 * verbatim against bytes streamed out of an AssetPackage (.pkg).
	 *
	 * The buffer is borrowed, NOT owned - callers must keep the underlying
	 * vector alive for the lifetime of the MemoryFile.
	 */
	class HEX_API MemoryFile : public DiskFile
	{
	public:
		MemoryFile(const std::vector<uint8_t>& data, const fs::path& virtualPath = {});

		// IFile / DiskFile overrides
		virtual bool		Open() override;
		virtual void		Close() override;
		virtual bool		DoesExist() override;
		virtual bool		IsOpen() const override;
		virtual uint32_t	GetSize() override;
		virtual uint32_t	Read(void* outData, uint32_t size) override;
		virtual uint32_t	Write(void* data, uint32_t size) override;
		virtual void		Flush() override;

		// Position-tracking helpers (handy when the caller wants to know
		// how much of the buffer was consumed - e.g. partial parse).
		uint32_t			Tell() const;
		void				Seek(uint32_t position);

	private:
		const std::vector<uint8_t>* _data = nullptr;
		uint32_t _position = 0;
		bool _open = true;
	};
}
