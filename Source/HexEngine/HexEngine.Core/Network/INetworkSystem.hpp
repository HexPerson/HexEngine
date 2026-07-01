
#pragma once

#include "../Plugin/IPlugin.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace HexEngine
{
	/**
	 * @brief Backend-neutral game-networking transport interface.
	 *
	 * Implemented by HexEngine.GameNetworkingSocketsPlugin, which wraps Valve's
	 * GameNetworkingSockets (the open-source standalone of Steam's
	 * ISteamNetworkingSockets). Core/game code talks ONLY to this interface -
	 * no GameNetworkingSockets / protobuf / OpenSSL types leak into the engine,
	 * so the transport is swappable.
	 *
	 * When the plugin DLL (or its vcpkg-built dependencies) isn't present the
	 * provider is simply absent (g_pEnv->_networkSystem == null). Callers must
	 * null-check and treat that as "networking unavailable, run single-player".
	 *
	 * Lifecycle mirrors the other plugin providers: the plugin constructs the
	 * implementation; the engine calls Create() once at boot (initialises the
	 * GNS library) and Destroy() at shutdown. Per-frame Tick() is REQUIRED -
	 * without it no sockets are pumped and nothing sends or receives.
	 *
	 * Connection ids are small opaque handles assigned by the implementation;
	 * 0 is never a valid connection. The host treats every connected client as
	 * a distinct connId; a client has exactly one connId (to the host).
	 */
	class INetworkSystem : public IPluginInterface
	{
	public:
		DECLARE_PLUGIN_INTERFACE(INetworkSystem, 001);

		enum class NetRole : uint8_t
		{
			None = 0,   ///< Not hosting or connected.
			Host,       ///< Authoritative listen server.
			Client      ///< Connected to a remote host.
		};

		/// A single received application message, drained via PollMessage().
		struct NetMessage
		{
			uint32_t connId = 0;            ///< Sender connection (the host, for a client).
			std::vector<uint8_t> bytes;     ///< Raw payload exactly as Send() was given it.
		};

		/// A connection lifecycle change, drained via PollEvent().
		struct NetEvent
		{
			enum class Type : uint8_t { Connected, Disconnected } type = Type::Connected;
			uint32_t connId = 0;
		};

		// === Role / lifecycle ===

		/// Current role. None until StartHost()/Connect() succeeds; returns to
		/// None after Disconnect() (or a fatal transport error).
		virtual NetRole GetRole() const = 0;

		/// Begin listening for clients on the given UDP port. Returns false if a
		/// session is already active or the socket couldn't be created.
		virtual bool StartHost(uint16_t port) = 0;

		/// Connect to a host. `address` is a host/IP string (e.g. "127.0.0.1").
		/// Returns false if a session is already active or the address is invalid;
		/// success here only means the attempt started - watch PollEvent() for the
		/// Connected/Disconnected result.
		virtual bool Connect(const std::string& address, uint16_t port) = 0;

		/// Tear down the listen socket / connection and return to NetRole::None.
		virtual void Disconnect() = 0;

		/// Pump the transport: run socket callbacks and buffer inbound messages /
		/// events for PollMessage()/PollEvent(). Call once per main-thread frame
		/// before the replication pump.
		virtual void Tick() = 0;

		// === Send ===

		/// Send to a specific connection. `reliable` selects ordered+guaranteed
		/// delivery (spawn/despawn) vs fire-and-forget (transform snapshots).
		virtual void Send(uint32_t connId, const void* data, uint32_t size, bool reliable) = 0;

		/// Send to every active connection (host -> all clients; for a client this
		/// is effectively Send() to the host).
		virtual void Broadcast(const void* data, uint32_t size, bool reliable) = 0;

		// === Receive (poll-drain each frame after Tick) ===

		/// Pop the next received message; returns false when the queue is empty.
		virtual bool PollMessage(NetMessage& out) = 0;

		/// Pop the next connection event; returns false when the queue is empty.
		virtual bool PollEvent(NetEvent& out) = 0;

		/// Snapshot of currently-connected connection ids (host: all clients;
		/// client: the single host connection, or empty).
		virtual void GetConnections(std::vector<uint32_t>& out) const = 0;

		// === P2P (identity-based, optional) ===
		// Backends that can NAT-traverse via an identity service override these.
		// The default direct-IP backend (GameNetworkingSockets) leaves them as
		// no-ops; the Steam backend implements them using SteamID + SDR relay, so
		// two peers behind home routers connect without port-forwarding. These are
		// non-pure so a backend only implements what it supports.

		/// True if StartHostP2P / ConnectP2P are supported by this backend.
		virtual bool SupportsP2P() const { return false; }

		/// Host a P2P listen socket on a logical virtual port (0 = default). Peers
		/// reach it via ConnectP2P(thisHost.GetLocalIdentity(), virtualPort).
		virtual bool StartHostP2P(uint16_t virtualPort = 0) { (void)virtualPort; return false; }

		/// Connect to a peer by its backend identity (a SteamID64 for the Steam
		/// backend) - no IP/port, no port-forwarding required.
		virtual bool ConnectP2P(uint64_t identity, uint16_t virtualPort = 0) { (void)identity; (void)virtualPort; return false; }

		/// This peer's identity (SteamID64 for the Steam backend), or 0 when N/A.
		/// Share it via lobby/UI so others can ConnectP2P to you.
		virtual uint64_t GetLocalIdentity() const { return 0; }
	};
}
