
#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace HexEngine
{
	// ==========================================================================
	// TODO(endianness): serialization is raw host byte-order (little-endian on our
	// Windows/x64 targets). Both peers are assumed same-platform for now. Before
	// shipping cross-platform builds, add byte-swapping here (in NetWriter/NetReader
	// or per-type specialization) so a big-endian peer interoperates. Tracked as a
	// known limitation of the v1 replication layer.
	// ==========================================================================

	// Minimal append-only byte writer over a caller-owned buffer.
	class NetWriter
	{
	public:
		explicit NetWriter(std::vector<uint8_t>& buffer) : _buffer(buffer) {}
		void WriteBytes(const void* data, size_t size)
		{
			const uint8_t* p = static_cast<const uint8_t*>(data);
			_buffer.insert(_buffer.end(), p, p + size);
		}
	private:
		std::vector<uint8_t>& _buffer;
	};

	// Minimal bounds-checked byte reader.
	class NetReader
	{
	public:
		NetReader(const uint8_t* data, size_t size) : _p(data), _remaining(size) {}
		bool ReadBytes(void* out, size_t size)
		{
			if (_remaining < size)
				return false;
			std::memcpy(out, _p, size);
			_p += size;
			_remaining -= size;
			return true;
		}
		size_t Remaining() const { return _remaining; }
	private:
		const uint8_t* _p;
		size_t _remaining;
	};

	// Replicated-value (de)serialization dispatches through these templates, so a
	// custom/non-trivial type just adds a specialization (below, for std::string,
	// is the pattern). The primary template raw-copies trivially-copyable types
	// (POD, Vector3, Quaternion, enums) and static_asserts otherwise so a missing
	// specialization is a compile error, not silent corruption.
	template <typename T>
	inline void NetSerialize(NetWriter& w, const T& v)
	{
		static_assert(std::is_trivially_copyable_v<T>,
			"NetSerialize<T>: add a specialization for this non-trivially-copyable type.");
		w.WriteBytes(&v, sizeof(T));
	}

	template <typename T>
	inline bool NetDeserialize(NetReader& r, T& v)
	{
		static_assert(std::is_trivially_copyable_v<T>,
			"NetDeserialize<T>: add a specialization for this non-trivially-copyable type.");
		return r.ReadBytes(&v, sizeof(T));
	}

	// Example custom specialization: length-prefixed std::string.
	template <>
	inline void NetSerialize<std::string>(NetWriter& w, const std::string& v)
	{
		const uint16_t len = (uint16_t)v.size();
		w.WriteBytes(&len, sizeof(len));
		if (len != 0)
			w.WriteBytes(v.data(), len);
	}

	template <>
	inline bool NetDeserialize<std::string>(NetReader& r, std::string& v)
	{
		uint16_t len = 0;
		if (!r.ReadBytes(&len, sizeof(len)))
			return false;
		v.resize(len);
		return (len == 0) || r.ReadBytes(&v[0], len);
	}
}
