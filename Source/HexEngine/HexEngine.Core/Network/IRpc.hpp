
#pragma once

#include "../Required.hpp"
#include "NetSerialization.hpp"
#include "../Utility/CRC32.hpp"

#include <vector>
#include <cstdint>
#include <tuple>
#include <utility>
#include <type_traits>

namespace HexEngine
{
	class BaseComponent;
	class IRpcReceiver;

	// Where an RPC runs relative to who invoked it.
	enum class RpcDirection : uint8_t
	{
		Server,     // client -> host: ask the host to run it (host validates). Runs on the host.
		Client,     // host -> the owning client: runs on that one client.
		Multicast,  // host -> everyone: runs on the host and all clients.
	};

	struct RpcEntry
	{
		uint32_t id;              // CRC32 of the method name
		RpcDirection direction;
		bool reliable;
		void (*dispatch)(IRpcReceiver*, NetReader&); // deserialize args + invoke the handler
	};

	/**
	 * @brief Mixin marking a component as having RPCs (see IReplicated for the
	 * netvar equivalent). A component multiply-inherits it and declares its RPCs
	 * with BEGIN_RPCS/RPC/END_RPCS. Discovered via dynamic_cast, like IReplicated.
	 *
	 * Authority: a Server RPC is the ONLY sanctioned way for a client to ask the
	 * host to do something - the host validates (it checks the sender owns the
	 * target entity) and runs it. Client/Multicast RPCs originate only on the host.
	 */
	class IRpcReceiver
	{
	public:
		virtual ~IRpcReceiver() = default;
		virtual const std::vector<RpcEntry>& GetRpcEntries() const = 0;
	};

	namespace detail
	{
		template <typename Tuple, size_t... I>
		inline void RpcReadArgs(NetReader& r, Tuple& t, std::index_sequence<I...>)
		{
			((void)NetDeserialize(r, std::get<I>(t)), ...);
		}
	}

	// Generates the type-erased dispatch thunk for a specific handler method. The
	// method is a non-type template parameter so the argument types are recovered
	// from its signature - args are read via NetDeserialize<T> then applied.
	template <auto Method>
	struct RpcDispatcher;

	template <typename C, typename... Args, void (C::* Method)(Args...)>
	struct RpcDispatcher<Method>
	{
		static void Dispatch(IRpcReceiver* self, NetReader& r)
		{
			std::tuple<std::decay_t<Args>...> args;
			detail::RpcReadArgs(r, args, std::index_sequence_for<Args...>{});
			std::apply([self](auto&... a) { (static_cast<C*>(self)->*Method)(a...); }, args);
		}
	};

	// Serialize args + route via the replication system. Implemented in
	// NetworkReplicationSystem.cpp. Exported so game/plugin code can call RPCs.
	HEX_API void RouteRpc(BaseComponent* comp, uint32_t rpcId, const std::vector<uint8_t>& args);

	// Invoke an RPC. Serializes the args (via NetSerialize<T>) and routes by the
	// RPC's registered direction. In single-player (no session) it just runs the
	// handler locally, so RPC-using gameplay code works offline too.
	template <typename... Args>
	inline void CallRpc(BaseComponent* comp, uint32_t rpcId, const Args&... args)
	{
		std::vector<uint8_t> buf;
		NetWriter w(buf);
		(NetSerialize(w, args), ...);
		RouteRpc(comp, rpcId, buf);
	}

	// Convenience overload: hash the method name at the call site. ConstCRC32
	// (used to register) and CRC32::HashString (used here) are the same algorithm,
	// so the ids match.
	template <typename... Args>
	inline void CallRpc(BaseComponent* comp, const char* rpcName, const Args&... args)
	{
		CallRpc(comp, (uint32_t)CRC32::HashString(rpcName), args...);
	}

	// Declares a component's RPC table. RPC(method, direction, reliable) registers
	// a handler; call it later with CallRpc(this, "method", args...).
#define BEGIN_RPCS(ThisClass) \
	using _RpcThisClass = ThisClass; \
	virtual const std::vector<HexEngine::RpcEntry>& GetRpcEntries() const override \
	{ \
		static const std::vector<HexEngine::RpcEntry> _rpcs = []() \
		{ \
			std::vector<HexEngine::RpcEntry> _r;

#define RPC(method, direction, reliable) \
			_r.push_back(HexEngine::RpcEntry{ \
				(uint32_t)ConstCRC32(#method), (direction), (reliable), \
				&HexEngine::RpcDispatcher<&_RpcThisClass::method>::Dispatch });

#define END_RPCS() \
			return _r; \
		}(); \
		return _rpcs; \
	}
}
