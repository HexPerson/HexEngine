
#pragma once

#include "NetSerialization.hpp"
#include "../Utility/CRC32.hpp"

#include <vector>
#include <cstdint>

namespace HexEngine
{
	class IReplicated;

	// One server-authoritative replicated member. `id` is the CRC32 of the member
	// name (stable across builds while the name is unchanged). The thunks route
	// through NetSerialize/NetDeserialize<T>, so the property system never needs to
	// know the concrete type.
	struct ReplicatedProperty
	{
		uint32_t id;
		void (*serialize)(IReplicated*, NetWriter&);
		void (*deserialize)(IReplicated*, NetReader&);
	};

	/**
	 * @brief Mixin interface marking a component as carrying replicated variables.
	 *
	 * Intentionally an interface, NOT a registered component - it adds no entry to
	 * the (capped) component signature table; a gameplay component multiply-inherits
	 * it. The NetworkReplicationSystem discovers it via dynamic_cast on the entity's
	 * components.
	 *
	 * Authority: only the HOST writes the authoritative value; clients receive and
	 * apply. A client editing its own copy of the member is harmless - the server
	 * overwrites it on the next update. (Memory tampering can't be *prevented*; it's
	 * made inconsequential by authoritative overwrite.)
	 *
	 * Usage:
	 *   class HealthComponent : public UpdateComponent, public HexEngine::IReplicated {
	 *       int32_t _health = 100; bool _isDead = false;
	 *       BEGIN_REPLICATED(HealthComponent)
	 *           REPLICATE(_health)
	 *           REPLICATE(_isDead)
	 *       END_REPLICATED()
	 *   };
	 */
	class IReplicated
	{
	public:
		virtual ~IReplicated() = default;
		virtual const std::vector<ReplicatedProperty>& GetReplicatedProperties() const = 0;
	};

	// Declares the replicated-property table for a component. Property ids are
	// CRC32 name hashes; (de)serialization dispatches through NetSerialize/
	// NetDeserialize<T> (specialize those for custom types). The captureless thunks
	// downcast the IReplicated* back to the concrete component to reach the member.
#define BEGIN_REPLICATED(ThisClass) \
	using _ReplicatedThisClass = ThisClass; \
	virtual const std::vector<HexEngine::ReplicatedProperty>& GetReplicatedProperties() const override \
	{ \
		static const std::vector<HexEngine::ReplicatedProperty> _props = []() \
		{ \
			std::vector<HexEngine::ReplicatedProperty> _p;

#define REPLICATE(member) \
			_p.push_back(HexEngine::ReplicatedProperty{ \
				(uint32_t)ConstCRC32(#member), \
				[](HexEngine::IReplicated* c, HexEngine::NetWriter& w) { HexEngine::NetSerialize(w, static_cast<_ReplicatedThisClass*>(c)->member); }, \
				[](HexEngine::IReplicated* c, HexEngine::NetReader& r) { HexEngine::NetDeserialize(r, static_cast<_ReplicatedThisClass*>(c)->member); } });

#define END_REPLICATED() \
			return _p; \
		}(); \
		return _props; \
	}
}
