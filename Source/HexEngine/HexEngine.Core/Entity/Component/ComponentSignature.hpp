#pragma once

#include <cstdint>

namespace HexEngine
{
	using ComponentId = uint32_t;

	// One bit per component type id (ClassRegistry hands ids out in registration
	// order; game and plugin DLLs share the registry). It used to be a uint64_t, but
	// the engine + plugins + a game passed 64 types and `(uint64_t)1 << id` with
	// id >= 64 wraps on x64 - ids 64+ aliased bits 0..n, so HasA<T>() could answer
	// for the wrong component. There is deliberately no integer conversion and no
	// shift operator: build bits with ComponentSignature::Bit(id), so any leftover
	// `1 << id` style code stops compiling instead of silently aliasing again.
	// MAX_COMPONENTS is the hard limit; ClassRegistry reports going past it.
	const ComponentId MAX_COMPONENTS = 128;

	struct ComponentSignature
	{
		static constexpr uint32_t kWords = MAX_COMPONENTS / 64;
		uint64_t words[kWords] = {};

		constexpr ComponentSignature() = default;

		static constexpr ComponentSignature Bit(ComponentId id)
		{
			ComponentSignature s;
			if (id < MAX_COMPONENTS)
				s.words[id / 64] = uint64_t(1) << (id % 64);
			return s;
		}

		constexpr bool Has(ComponentId id) const
		{
			return id < MAX_COMPONENTS && ((words[id / 64] >> (id % 64)) & 1u) != 0;
		}
		constexpr void Set(ComponentId id)
		{
			if (id < MAX_COMPONENTS)
				words[id / 64] |= uint64_t(1) << (id % 64);
		}
		constexpr void Clear(ComponentId id)
		{
			if (id < MAX_COMPONENTS)
				words[id / 64] &= ~(uint64_t(1) << (id % 64));
		}

		constexpr bool Any() const
		{
			for (uint32_t i = 0; i < kWords; ++i)
				if (words[i] != 0)
					return true;
			return false;
		}
		// Any bit in common with `other`.
		constexpr bool Intersects(const ComponentSignature& other) const
		{
			for (uint32_t i = 0; i < kWords; ++i)
				if ((words[i] & other.words[i]) != 0)
					return true;
			return false;
		}
		// The id of the single set bit, or -1 when zero or several bits are set.
		int32_t SingleBitIndex() const
		{
			int32_t found = -1;
			for (uint32_t i = 0; i < kWords; ++i)
			{
				const uint64_t w = words[i];
				if (w == 0)
					continue;
				if (found >= 0 || (w & (w - 1)) != 0)
					return -1;
				uint32_t bit = 0;
				while (((w >> bit) & 1u) == 0)
					++bit;
				found = (int32_t)(i * 64 + bit);
			}
			return found;
		}

		constexpr ComponentSignature operator|(const ComponentSignature& o) const
		{
			ComponentSignature r;
			for (uint32_t i = 0; i < kWords; ++i)
				r.words[i] = words[i] | o.words[i];
			return r;
		}
		constexpr ComponentSignature operator&(const ComponentSignature& o) const
		{
			ComponentSignature r;
			for (uint32_t i = 0; i < kWords; ++i)
				r.words[i] = words[i] & o.words[i];
			return r;
		}
		constexpr ComponentSignature operator~() const
		{
			ComponentSignature r;
			for (uint32_t i = 0; i < kWords; ++i)
				r.words[i] = ~words[i];
			return r;
		}
		constexpr ComponentSignature& operator|=(const ComponentSignature& o)
		{
			for (uint32_t i = 0; i < kWords; ++i)
				words[i] |= o.words[i];
			return *this;
		}
		constexpr ComponentSignature& operator&=(const ComponentSignature& o)
		{
			for (uint32_t i = 0; i < kWords; ++i)
				words[i] &= o.words[i];
			return *this;
		}
		constexpr bool operator==(const ComponentSignature& o) const
		{
			for (uint32_t i = 0; i < kWords; ++i)
				if (words[i] != o.words[i])
					return false;
			return true;
		}
		constexpr bool operator!=(const ComponentSignature& o) const { return !(*this == o); }
		// Strict weak order (Scene groups entities in a std::map by signature).
		constexpr bool operator<(const ComponentSignature& o) const
		{
			for (uint32_t i = kWords; i-- > 0;)
				if (words[i] != o.words[i])
					return words[i] < o.words[i];
			return false;
		}
	};
	static_assert(MAX_COMPONENTS % 64 == 0, "ComponentSignature is whole 64-bit words");
}
