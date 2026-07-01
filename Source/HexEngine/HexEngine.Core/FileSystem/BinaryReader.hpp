
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>
#include <type_traits>

namespace HexEngine
{
	// -------------------------------------------------------------------------
	// BinaryReader - bounds-checked reader over a borrowed contiguous byte span.
	//
	// Every read validates that enough bytes remain BEFORE touching memory, so a
	// corrupt or hostile file can never drive an out-of-bounds read. Length and
	// count prefixes are additionally clamped against an absolute cap AND against
	// the bytes actually remaining, so a header claiming (say) 4 billion elements
	// in a 100-byte file fails cleanly instead of triggering a 4 GB allocation.
	//
	// Failure is sticky (like std::istream): once a read runs past the end or a
	// validation trips, the reader latches into a failed state and every
	// subsequent read is a no-op returning false / nullptr. Check Good() - or the
	// bool return of each call - to detect it. This means callers can chain a
	// sequence of reads and test once at the end.
	//
	// The span is BORROWED - the caller owns the underlying storage and must keep
	// it alive for the reader's lifetime. The reader never allocates except when
	// materialising a validated string.
	//
	// Header-only and dependency-free (only <cstdint>/<string>/<vector>) so it can
	// be unit-tested in isolation and reused by any loader (MeshLoader / AssetFile
	// hardening lands in PR3). TODO(endianness): all multi-byte reads are
	// host-endian; add byte-swapping helpers when cross-endian assets are needed.
	// -------------------------------------------------------------------------
	class BinaryReader
	{
	public:
		// Absolute sanity caps for length/count prefixes. Generous enough for any
		// legitimate asset string or array, small enough to refuse a hostile
		// header outright. Callers that know a tighter bound should pass it.
		static constexpr size_t kDefaultMaxStringLength = 64u * 1024u * 1024u; // 64 MiB
		static constexpr size_t kDefaultMaxElementCount = 64u * 1024u * 1024u; // 64 Mi elems

		BinaryReader(const void* data, size_t size)
			: _data(static_cast<const uint8_t*>(data))
			, _size(data ? size : 0)
		{
		}

		explicit BinaryReader(const std::vector<uint8_t>& data)
			: _data(data.empty() ? nullptr : data.data())
			, _size(data.size())
		{
		}

		size_t Size()      const { return _size; }
		size_t Position()  const { return _pos; }
		size_t Remaining() const { return _pos <= _size ? _size - _pos : 0; }
		bool   Eof()       const { return _pos >= _size; }
		bool   Good()      const { return !_failed; }
		explicit operator bool() const { return !_failed; }

		// Reset the failure latch (the cursor is unchanged). Rarely needed - only
		// when a caller deliberately probes an optional field and wants to recover.
		void ClearFailure() { _failed = false; }

		// Move the cursor to an absolute position. An out-of-range seek latches
		// failure and clamps to the end.
		bool Seek(size_t pos)
		{
			if (pos > _size) { _failed = true; _pos = _size; return false; }
			_pos = pos;
			return true;
		}

		// Advance the cursor by `count` bytes. Fails (and latches) if fewer remain.
		bool Skip(size_t count)
		{
			if (_failed || count > Remaining()) { _failed = true; return false; }
			_pos += count;
			return true;
		}

		// Copy `count` raw bytes into `out`. False (and latches) if fewer remain
		// or `out` is null; the destination is left untouched on failure.
		bool ReadBytes(void* out, size_t count)
		{
			if (_failed || out == nullptr || count > Remaining()) { _failed = true; return false; }
			if (count > 0)
				std::memcpy(out, _data + _pos, count);
			_pos += count;
			return true;
		}

		// Return a pointer to `count` in-place bytes and advance, without copying.
		// nullptr (and latches) if fewer remain. The pointer is valid only while
		// the borrowed span is alive.
		const uint8_t* ReadInPlace(size_t count)
		{
			if (_failed || count > Remaining()) { _failed = true; return nullptr; }
			const uint8_t* p = _data + _pos;
			_pos += count;
			return p;
		}

		// Read a single trivially-copyable POD. False (and latches) at EOF.
		template<typename T>
		bool Read(T& out)
		{
			static_assert(std::is_trivially_copyable<T>::value,
				"BinaryReader::Read<T> requires a trivially-copyable T");
			return ReadBytes(&out, sizeof(T));
		}

		// Read a POD, returning `fallback` (and latching) if insufficient bytes.
		template<typename T>
		T ReadOr(const T& fallback)
		{
			T v{};
			if (!Read(v))
				return fallback;
			return v;
		}

		// Read a uint32 length prefix, validate it against `maxLength` and the
		// bytes remaining, then materialise that many bytes into `out`. On any
		// violation `out` is cleared and failure latches. Because the length is
		// checked against Remaining() before the resize, a bogus huge length can
		// never allocate more than the span actually holds.
		bool ReadString(std::string& out, size_t maxLength = kDefaultMaxStringLength)
		{
			out.clear();
			uint32_t len = 0;
			if (!Read(len))                                     // truncated prefix
				return false;
			if (len > maxLength || len > Remaining()) { _failed = true; return false; }
			if (len == 0)
				return true;
			out.resize(len);
			return ReadBytes(&out[0], len);
		}

		// Read a uint32 element count and validate that count*elementSize fits in
		// the bytes remaining (and count <= maxCount). Prevents a bogus count from
		// driving an over-allocation BEFORE the caller reserves/reads the elements.
		// Does not read the elements themselves - the caller loops. A zero
		// elementSize is rejected (a caller passing 0 is a programming error).
		bool ReadCount(uint32_t& outCount, size_t elementSize, size_t maxCount = kDefaultMaxElementCount)
		{
			outCount = 0;
			if (elementSize == 0) { _failed = true; return false; }
			uint32_t count = 0;
			if (!Read(count))
				return false;
			if (count > maxCount) { _failed = true; return false; }
			// Widen to 64-bit so count*elementSize can't wrap; compare against the
			// bytes actually left in the span.
			if (static_cast<uint64_t>(count) * static_cast<uint64_t>(elementSize) > static_cast<uint64_t>(Remaining()))
			{
				_failed = true;
				return false;
			}
			outCount = count;
			return true;
		}

	private:
		const uint8_t* _data = nullptr;
		size_t         _size = 0;
		size_t         _pos = 0;
		bool           _failed = false;
	};
}
