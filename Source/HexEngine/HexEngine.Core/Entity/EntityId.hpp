#pragma once

#include "../Required.hpp"
#include <functional>
#include <limits>

namespace HexEngine
{
	struct EntityId
	{
		static constexpr uint32_t InvalidIndex = std::numeric_limits<uint32_t>::max();

		uint32_t index = InvalidIndex;
		uint32_t generation = 0;

		constexpr bool IsValid() const
		{
			return index != InvalidIndex;
		}

		constexpr explicit operator bool() const
		{
			return IsValid();
		}

		constexpr bool operator==(const EntityId& other) const
		{
			return index == other.index && generation == other.generation;
		}

		constexpr bool operator!=(const EntityId& other) const
		{
			return !(*this == other);
		}
	};

	constexpr EntityId InvalidEntityId{};

	struct EntityIdHasher
	{
		size_t operator()(const EntityId& id) const noexcept
		{
			const uint64_t key = (static_cast<uint64_t>(id.generation) << 32ull) | static_cast<uint64_t>(id.index);
			return static_cast<size_t>(std::hash<uint64_t>{}(key));
		}
	};
}
