
#pragma once

#define _CRT_SECURE_NO_WARNINGS

#include <HexEngine.Core/Network/INetworkSystem.hpp>
#include "sdk/steam_api.h"
#include "sdk/isteamnetworkingsockets.h"
#include "sdk/isteamnetworkingutils.h"

#include <queue>
#include <unordered_set>

/// <summary>
/// INetworkSystem implementation backed by the Steam client's
/// ISteamNetworkingSockets. Supports both direct IP (StartHost/Connect) and
/// identity-based P2P (StartHostP2P/ConnectP2P by SteamID64), the latter using
/// Steam's SDR relay + NAT punch so peers behind home routers connect without
/// port-forwarding.
///
/// Steam itself is initialised by the Steamworks provider (SteamAPI_Init); this
/// class does NOT init/shutdown the Steam API - it just grabs SteamNetworkingSockets()
/// after the provider is up. Connection ids surfaced to the engine are the
/// HSteamNetConnection handle values.
/// </summary>
class SteamNetworkingSystem : public HexEngine::INetworkSystem
{
public:
	virtual bool Create() override;
	virtual void Destroy() override;

	virtual NetRole GetRole() const override { return _role; }
	virtual bool StartHost(uint16_t port) override;                                   // direct IP listen
	virtual bool Connect(const std::string& address, uint16_t port) override;         // direct IP
	virtual void Disconnect() override;
	virtual void Tick() override;

	virtual void Send(uint32_t connId, const void* data, uint32_t size, bool reliable) override;
	virtual void Broadcast(const void* data, uint32_t size, bool reliable) override;

	virtual bool PollMessage(NetMessage& out) override;
	virtual bool PollEvent(NetEvent& out) override;
	virtual void GetConnections(std::vector<uint32_t>& out) const override;

	// P2P (SteamID-based, SDR relay).
	virtual bool SupportsP2P() const override { return true; }
	virtual bool StartHostP2P(uint16_t virtualPort = 0) override;
	virtual bool ConnectP2P(uint64_t identity, uint16_t virtualPort = 0) override;
	virtual uint64_t GetLocalIdentity() const override;

private:
	static void OnConnectionStatusChangedStatic(SteamNetConnectionStatusChangedCallback_t* info);
	void OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info);
	void DrainMessages();
	void CloseAllConnections();

	static SteamNetworkingSystem* s_instance;

	ISteamNetworkingSockets* _sockets = nullptr;
	NetRole _role = NetRole::None;
	bool _initialised = false;

	HSteamListenSocket  _listenSocket     = k_HSteamListenSocket_Invalid;
	HSteamNetPollGroup  _pollGroup        = k_HSteamNetPollGroup_Invalid;
	HSteamNetConnection _clientConnection = k_HSteamNetConnection_Invalid;

	std::unordered_set<uint32_t> _connections;
	std::queue<NetMessage> _messages;
	std::queue<NetEvent>   _events;
};
