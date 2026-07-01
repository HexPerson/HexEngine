
#include "SteamNetworkingSystem.hpp"
#include <HexEngine.Core/HexEngine.hpp>

#include <cstdio>

SteamNetworkingSystem* SteamNetworkingSystem::s_instance = nullptr;

bool SteamNetworkingSystem::Create()
{
	// Steam itself is initialised by the Steamworks provider (SteamAPI_Init),
	// which Game3DEnvironment creates before the network system. If Steam isn't
	// running, SteamNetworkingSockets() is null and we bail (the engine then
	// drops the provider, leaving g_pEnv->_networkSystem null).
	_sockets = SteamNetworkingSockets();
	if (_sockets == nullptr)
	{
		LOG_WARN("SteamNetworkingSystem: Steam is not initialised (SteamNetworkingSockets() == null); networking disabled.");
		return false;
	}

	// Begin acquiring an SDR relay network route up front so the first P2P
	// connection isn't stalled bootstrapping the relay network.
	if (SteamNetworkingUtils() != nullptr)
		SteamNetworkingUtils()->InitRelayNetworkAccess();

	s_instance = this;
	if (SteamNetworkingUtils() != nullptr)
		SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(&SteamNetworkingSystem::OnConnectionStatusChangedStatic);

	_initialised = true;
	LOG_INFO("SteamNetworkingSystem initialised (Steam P2P + direct IP).");
	return true;
}

void SteamNetworkingSystem::Destroy()
{
	if (_initialised)
		Disconnect();
	if (s_instance == this)
		s_instance = nullptr;
	_sockets = nullptr;
	_initialised = false;
}

bool SteamNetworkingSystem::StartHost(uint16_t port)
{
	if (_role != NetRole::None || _sockets == nullptr)
		return false;

	SteamNetworkingIPAddr addr;
	addr.Clear();
	addr.m_port = port;

	_listenSocket = _sockets->CreateListenSocketIP(addr, 0, nullptr);
	if (_listenSocket == k_HSteamListenSocket_Invalid)
	{
		LOG_WARN("SteamNetworkingSystem: failed to create IP listen socket on port %u.", (uint32_t)port);
		return false;
	}
	_pollGroup = _sockets->CreatePollGroup();
	_role = NetRole::Host;
	LOG_INFO("SteamNetworkingSystem: hosting (direct IP) on UDP port %u.", (uint32_t)port);
	return true;
}

bool SteamNetworkingSystem::Connect(const std::string& address, uint16_t port)
{
	if (_role != NetRole::None || _sockets == nullptr)
		return false;

	char buf[256];
	std::snprintf(buf, sizeof(buf), "%s:%u", address.c_str(), (uint32_t)port);

	SteamNetworkingIPAddr addr;
	addr.Clear();
	if (!addr.ParseString(buf))
	{
		LOG_WARN("SteamNetworkingSystem: invalid address '%s'.", buf);
		return false;
	}

	_clientConnection = _sockets->ConnectByIPAddress(addr, 0, nullptr);
	if (_clientConnection == k_HSteamNetConnection_Invalid)
		return false;
	_role = NetRole::Client;
	LOG_INFO("SteamNetworkingSystem: connecting (direct IP) to %s ...", buf);
	return true;
}

bool SteamNetworkingSystem::StartHostP2P(uint16_t virtualPort)
{
	if (_role != NetRole::None || _sockets == nullptr)
		return false;

	_listenSocket = _sockets->CreateListenSocketP2P((int)virtualPort, 0, nullptr);
	if (_listenSocket == k_HSteamListenSocket_Invalid)
	{
		LOG_WARN("SteamNetworkingSystem: failed to create P2P listen socket (virtual port %u).", (uint32_t)virtualPort);
		return false;
	}
	_pollGroup = _sockets->CreatePollGroup();
	_role = NetRole::Host;
	LOG_INFO("SteamNetworkingSystem: hosting P2P on virtual port %u (local SteamID %llu). Share that id so peers can ConnectP2P.",
		(uint32_t)virtualPort, (unsigned long long)GetLocalIdentity());
	return true;
}

bool SteamNetworkingSystem::ConnectP2P(uint64_t identity, uint16_t virtualPort)
{
	if (_role != NetRole::None || _sockets == nullptr)
		return false;

	SteamNetworkingIdentity ident;
	ident.Clear();
	ident.SetSteamID64(identity);

	_clientConnection = _sockets->ConnectP2P(ident, (int)virtualPort, 0, nullptr);
	if (_clientConnection == k_HSteamNetConnection_Invalid)
	{
		LOG_WARN("SteamNetworkingSystem: ConnectP2P to SteamID %llu failed.", (unsigned long long)identity);
		return false;
	}
	_role = NetRole::Client;
	LOG_INFO("SteamNetworkingSystem: connecting P2P to SteamID %llu (virtual port %u) ...",
		(unsigned long long)identity, (uint32_t)virtualPort);
	return true;
}

uint64_t SteamNetworkingSystem::GetLocalIdentity() const
{
	return (SteamUser() != nullptr) ? SteamUser()->GetSteamID().ConvertToUint64() : 0ull;
}

void SteamNetworkingSystem::CloseAllConnections()
{
	if (_sockets == nullptr)
		return;
	for (uint32_t c : _connections)
		_sockets->CloseConnection((HSteamNetConnection)c, 0, "shutdown", false);
	_connections.clear();
}

void SteamNetworkingSystem::Disconnect()
{
	if (_sockets == nullptr)
	{
		_role = NetRole::None;
		return;
	}

	CloseAllConnections();

	if (_clientConnection != k_HSteamNetConnection_Invalid)
	{
		_sockets->CloseConnection(_clientConnection, 0, "disconnect", false);
		_clientConnection = k_HSteamNetConnection_Invalid;
	}
	if (_pollGroup != k_HSteamNetPollGroup_Invalid)
	{
		_sockets->DestroyPollGroup(_pollGroup);
		_pollGroup = k_HSteamNetPollGroup_Invalid;
	}
	if (_listenSocket != k_HSteamListenSocket_Invalid)
	{
		_sockets->CloseListenSocket(_listenSocket);
		_listenSocket = k_HSteamListenSocket_Invalid;
	}

	std::queue<NetMessage>().swap(_messages);
	std::queue<NetEvent>().swap(_events);
	_role = NetRole::None;
}

void SteamNetworkingSystem::Tick()
{
	if (_sockets == nullptr || _role == NetRole::None)
		return;

	// Connection-status callbacks are delivered through SteamAPI_RunCallbacks
	// (also pumped by the Steamworks provider; calling it again is harmless).
	SteamAPI_RunCallbacks();
	DrainMessages();
}

void SteamNetworkingSystem::DrainMessages()
{
	ISteamNetworkingMessage* msgs[64];
	for (;;)
	{
		int n = 0;
		if (_role == NetRole::Host)
			n = _sockets->ReceiveMessagesOnPollGroup(_pollGroup, msgs, 64);
		else if (_clientConnection != k_HSteamNetConnection_Invalid)
			n = _sockets->ReceiveMessagesOnConnection(_clientConnection, msgs, 64);

		if (n <= 0)
			break;

		for (int i = 0; i < n; ++i)
		{
			ISteamNetworkingMessage* m = msgs[i];
			NetMessage nm;
			nm.connId = (uint32_t)m->m_conn;
			const uint8_t* p = (const uint8_t*)m->m_pData;
			nm.bytes.assign(p, p + m->m_cbSize);
			_messages.push(std::move(nm));
			m->Release();
		}

		if (n < 64)
			break;
	}
}

void SteamNetworkingSystem::OnConnectionStatusChangedStatic(SteamNetConnectionStatusChangedCallback_t* info)
{
	if (s_instance != nullptr)
		s_instance->OnConnectionStatusChanged(info);
}

void SteamNetworkingSystem::OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info)
{
	const HSteamNetConnection conn = info->m_hConn;

	switch (info->m_info.m_eState)
	{
	case k_ESteamNetworkingConnectionState_Connecting:
		if (_role == NetRole::Host)
		{
			if (_sockets->AcceptConnection(conn) != k_EResultOK)
			{
				_sockets->CloseConnection(conn, 0, nullptr, false);
				break;
			}
			_sockets->SetConnectionPollGroup(conn, _pollGroup);
		}
		break;

	case k_ESteamNetworkingConnectionState_Connected:
	{
		_connections.insert((uint32_t)conn);
		NetEvent e;
		e.type = NetEvent::Type::Connected;
		e.connId = (uint32_t)conn;
		_events.push(e);
		LOG_INFO("SteamNetworkingSystem: connection %u established.", (uint32_t)conn);
		break;
	}

	case k_ESteamNetworkingConnectionState_ClosedByPeer:
	case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
	{
		const bool wasKnown = (_connections.erase((uint32_t)conn) > 0) || (conn == _clientConnection);
		if (wasKnown)
		{
			NetEvent e;
			e.type = NetEvent::Type::Disconnected;
			e.connId = (uint32_t)conn;
			_events.push(e);
			LOG_INFO("SteamNetworkingSystem: connection %u closed.", (uint32_t)conn);
		}
		_sockets->CloseConnection(conn, 0, nullptr, false);
		if (conn == _clientConnection)
			_clientConnection = k_HSteamNetConnection_Invalid;
		break;
	}

	default:
		break;
	}
}

void SteamNetworkingSystem::Send(uint32_t connId, const void* data, uint32_t size, bool reliable)
{
	if (_sockets == nullptr || connId == 0 || size == 0)
		return;
	_sockets->SendMessageToConnection(
		(HSteamNetConnection)connId, data, size,
		reliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_Unreliable,
		nullptr);
}

void SteamNetworkingSystem::Broadcast(const void* data, uint32_t size, bool reliable)
{
	for (uint32_t c : _connections)
		Send(c, data, size, reliable);
}

bool SteamNetworkingSystem::PollMessage(NetMessage& out)
{
	if (_messages.empty())
		return false;
	out = std::move(_messages.front());
	_messages.pop();
	return true;
}

bool SteamNetworkingSystem::PollEvent(NetEvent& out)
{
	if (_events.empty())
		return false;
	out = _events.front();
	_events.pop();
	return true;
}

void SteamNetworkingSystem::GetConnections(std::vector<uint32_t>& out) const
{
	out.assign(_connections.begin(), _connections.end());
}
