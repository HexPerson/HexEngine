
#pragma once

#include <HexEngine.Core/Network/INetworkSystem.hpp>

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>

#include <queue>
#include <unordered_set>

/// <summary>
/// INetworkSystem implementation backed by Valve's GameNetworkingSockets (the
/// open-source standalone of Steam's ISteamNetworkingSockets). Wraps a single
/// session that is either a listen server (Host) or one connection (Client).
/// Connection ids surfaced to the engine are just the HSteamNetConnection
/// handle values (uint32, unique, never 0).
/// </summary>
class GameNetworkingSystem : public HexEngine::INetworkSystem
{
public:
	virtual bool Create() override;
	virtual void Destroy() override;

	virtual NetRole GetRole() const override { return _role; }
	virtual bool StartHost(uint16_t port) override;
	virtual bool Connect(const std::string& address, uint16_t port) override;
	virtual void Disconnect() override;
	virtual void Tick() override;

	virtual void Send(uint32_t connId, const void* data, uint32_t size, bool reliable) override;
	virtual void Broadcast(const void* data, uint32_t size, bool reliable) override;

	virtual bool PollMessage(NetMessage& out) override;
	virtual bool PollEvent(NetEvent& out) override;
	virtual void GetConnections(std::vector<uint32_t>& out) const override;

private:
	// GNS delivers connection-state transitions through a global C callback; we
	// route it back to this instance via s_instance.
	static void OnConnectionStatusChangedStatic(SteamNetConnectionStatusChangedCallback_t* info);
	void OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info);

	void DrainMessages();
	void CloseAllConnections();

	static GameNetworkingSystem* s_instance;

	ISteamNetworkingSockets* _sockets = nullptr;
	NetRole _role = NetRole::None;
	bool _initialised = false;

	HSteamListenSocket  _listenSocket     = k_HSteamListenSocket_Invalid;
	HSteamNetPollGroup  _pollGroup        = k_HSteamNetPollGroup_Invalid;
	HSteamNetConnection _clientConnection = k_HSteamNetConnection_Invalid;

	// All currently-connected handles (host: every accepted client; client: the
	// single host connection). Used by Broadcast() and GetConnections().
	std::unordered_set<uint32_t> _connections;

	std::queue<NetMessage> _messages;
	std::queue<NetEvent>   _events;
};
