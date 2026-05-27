#include "MemoryFile.hpp"
#include <cstring>

namespace HexEngine
{
	MemoryFile::MemoryFile(const std::vector<uint8_t>& data, const fs::path& virtualPath) :
		// DiskFile's ctor only stores the path + openMode and (optionally) creates
		// sub-directories; it never opens a stream until DiskFile::Open() is
		// called. We pass std::ios::in purely so the stored openMode is sensible
		// if anything reflects on it - we override IsOpen/Read/Write entirely.
		DiskFile(virtualPath, std::ios::in | std::ios::binary),
		_data(&data),
		_position(0),
		_open(true)
	{
	}

	bool MemoryFile::Open()
	{
		_open = true;
		return _data != nullptr;
	}

	void MemoryFile::Close()
	{
		_open = false;
	}

	bool MemoryFile::DoesExist()
	{
		return _data != nullptr;
	}

	bool MemoryFile::IsOpen() const
	{
		return _open && _data != nullptr;
	}

	uint32_t MemoryFile::GetSize()
	{
		return _data ? static_cast<uint32_t>(_data->size()) : 0u;
	}

	uint32_t MemoryFile::Read(void* outData, uint32_t size)
	{
		if (!IsOpen() || outData == nullptr || size == 0)
			return 0;

		const uint32_t available = static_cast<uint32_t>(_data->size()) - _position;
		const uint32_t toRead = std::min(size, available);
		if (toRead == 0)
			return 0;

		std::memcpy(outData, _data->data() + _position, toRead);
		_position += toRead;
		return toRead;
	}

	uint32_t MemoryFile::Write(void* /*data*/, uint32_t /*size*/)
	{
		// Read-only; writing to a borrowed package buffer is never the right
		// thing. Callers that need to mutate should copy out first.
		return 0;
	}

	void MemoryFile::Flush()
	{
		// no-op
	}

	uint32_t MemoryFile::Tell() const
	{
		return _position;
	}

	void MemoryFile::Seek(uint32_t position)
	{
		if (_data == nullptr)
			return;
		_position = std::min(position, static_cast<uint32_t>(_data->size()));
	}
}
