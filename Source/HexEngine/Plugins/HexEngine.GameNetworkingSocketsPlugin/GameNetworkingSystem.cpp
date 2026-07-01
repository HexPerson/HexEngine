
#include "Plugin.hpp"
#include "GameNetworkingSystem.hpp"

#include <cstdio>

GameNetworkingSystem* GameNetworkingSystem::s_instance = nullptr;

bool GameNetworkingSystem::Create()
{
	SteamNetworkingErrMsg errMsg = {};
	if (!GameNetworkingSockets_Init(nullptr, errMsg))
	{
		LOG_WARN("GameNetworkingSockets_Init failed: %s", errMsg);
		return false;
	}

	_sockets = SteamNetworkingSockets();
	if (_sockets == nullptr)
	{
		LOG_WARN("SteamNetworkingSockets() returned null after init.");
		GameNetworkingSockets_Kill();
		return false;
	}

	// Route connection-state changes back to this instance. GNS standalone uses
	// a global callback (no Steam client running to dispatch per-connection ones).
	s_instance = this;
	SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(&GameNetworkingSystem::OnConnectionStatusChangedStatic);

	_initialised = true;
	LOG_INFO("GameNetworkingSockets initialised.");
	return true;
}

void GameNetworkingSystem::Destroy()
{
	if (_initialised)
	{
		Disconnect();
		GameNetworkingSockets_Kill();
		_initialised = false;
	}
	if (s_instance == this)
		s_instance = nullptr;
	_sockets = nullptr;
}

bool GameNetworkingSystem::StartHost(uint16_t port)
{
	if (_role != NetRole::None)
	{
		LOG_WARN("StartHost ignored: a network session is already active.");
		return false;
	}
	if (_sockets == nullptr)
		return false;

	SteamNetworkingIPAddr addr;
	addr.Clear();
	addr.m_port = port; // bind to any local address on this UDP port

	_listenSocket = _sockets->CreateListenSocketIP(addr, 0, nullptr);
	if (_listenSocket == k_HSteamListenSocket_Invalid)
	{
		LOG_WARN("Failed to create listen socket on port %u.", (uint32_t)port);
		return false;
	}

	_pollGroup = _sockets->CreatePollGroup();
	_role = NetRole::Host;
	LOG_INFO("Network host listening on UDP port %u.", (uint32_t)port);
	return true;
}

bool GameNetworkingSystem::Connect(const std::string& address, uint16_t port)
{
	if (_role != NetRole::None)
	{
		LOG_WARN("Connect ignored: a network session is already active.");
		return false;
	}
	if (_sockets == nullptr)
		return false;

	char buf[256];
	std::snprintf(buf, sizeof(buf), "%s:%u", address.c_str(), (uint32_t)port);

	SteamNetworkingIPAddr addr;
	addr.Clear();
	if (!addr.ParseString(buf))
	{
		LOG_WARN("Invalid network address '%s' (expected ip:port, e.g. 127.0.0.1:27015).", buf);
		return false;
	}

	_clientConnection = _sockets->ConnectByIPAddress(addr, 0, nullptr);
	if (_clientConnection == k_HSteamNetConnection_Invalid)
	{
		LOG_WARN("ConnectByIPAddress failed for '%s'.", buf);
		return false;
	}

	_role = NetRole::Client;
	LOG_INFO("Connecting to %s ...", buf);
	return true;
}

void GameNetworkingSystem::CloseAllConnections()
{
	if (_sockets == nullptr)
		return;
	for (uint32_t c : _connections)
		_sockets->CloseConnection((HSteamNetConnection)c, 0, "shutdown", false);
	_connections.clear();
}

void GameNetworkingSystem::Disconnect()
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

void GameNetworkingSystem::Tick()
{
	if (_sockets == nullptr || _role == NetRole::None)
		return;

	_sockets->RunCallbacks(); // dispatches OnConnectionStatusChanged
	DrainMessages();
}

void GameNetworkingSystem::DrainMessages()
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
			break; // queue drained
	}
}

void GameNetworkingSystem::OnConnectionStatusChangedStatic(SteamNetConnectionStatusChangedCallback_t* info)
{
	if (s_instance != nullptr)
		s_instance->OnConnectionStatusChanged(info);
}

void GameNetworkingSystem::OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info)
{
	const HSteamNetConnection conn = info->m_hConn;

	switch (info->m_info.m_eState)
	{
	case k_ESteamNetworkingConnectionState_Connecting:
		// An inbound client is knocking on the host's listen socket: accept it
		// and route its messages through our poll group. (On the client side the
		// outbound connection never reports Connecting here.)
		if (_role == NetRole::Host)
		{
			if (_sockets->AcceptConnection(conn) != k_EResultOK)
			{
				_sockets->CloseConnection(conn, 0, nullptr, false);
				break;
			}
			_sockets->SetConnectionPollGroup(conn, _pollGroup);
			// Connected event is pushed when the state reaches Connected below.
		}
		break;

	case k_ESteamNetworkingConnectionState_Connected:
	{
		_connections.insert((uint32_t)conn);
		NetEvent e;
		e.type = NetEvent::Type::Connected;
		e.connId = (uint32_t)conn;
		_events.push(e);
		LOG_INFO("Network connection %u established.", (uint32_t)conn);
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
			LOG_INFO("Network connection %u closed.", (uint32_t)conn);
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

void GameNetworkingSystem::Send(uint32_t connId, const void* data, uint32_t size, bool reliable)
{
	if (_sockets == nullptr || connId == 0 || size == 0)
		return;
	_sockets->SendMessageToConnection(
		(HSteamNetConnection)connId, data, size,
		reliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_Unreliable,
		nullptr);
}

void GameNetworkingSystem::Broadcast(const void* data, uint32_t size, bool reliable)
{
	for (uint32_t c : _connections)
		Send(c, data, size, reliable);
}

bool GameNetworkingSystem::PollMessage(NetMessage& out)
{
	if (_messages.empty())
		return false;
	out = std::move(_messages.front());
	_messages.pop();
	return true;
}

bool GameNetworkingSystem::PollEvent(NetEvent& out)
{
	if (_events.empty())
		return false;
	out = _events.front();
	_events.pop();
	return true;
}

void GameNetworkingSystem::GetConnections(std::vector<uint32_t>& out) const
{
	out.assign(_connections.begin(), _connections.end());
}
