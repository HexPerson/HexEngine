
#pragma once

#include "../../Plugins/HexEngine.EditorBridgePlugin/EditorBridgeProtocol.hpp"

#include <string>
#include <vector>

namespace HexEngine
{
namespace Mcp
{
	using HexEngine::EditorBridge::SessionInfo;
	using HexEngine::EditorBridge::json;

	// Directory where editor bridge session files live. Honors the
	// HEXENGINE_BRIDGE_SESSION_DIR override (used by tests + custom setups),
	// otherwise %TEMP%/HexEngine/EditorBridge.
	std::string SessionDir();

	// Discover live editor bridge sessions, newest first (by startedAtUnix).
	// Invalid/unreadable session files are skipped. Never launches anything.
	std::vector<SessionInfo> DiscoverSessions();

	// One-shot named-pipe request/response against a specific session. Connects,
	// writes one request line, reads one response line, all bounded by timeouts,
	// then closes the handle (RAII). Returns false + a human-readable error on
	// connect/IO/timeout/parse failure.
	bool CallBridge(const SessionInfo& session, const json& request, json& response,
		std::string& error, unsigned int timeoutMs = 5000);

	// A long-lived connection to the newest editor bridge session. Connects
	// lazily, keeps the pipe open across calls (no per-call discovery or
	// connect), and transparently re-discovers + reconnects when the pipe is
	// found dead BEFORE a request was sent (editor restarted). It never resends a
	// request whose write succeeded, so a read timeout can't cause a duplicate
	// mutation. Not thread-safe: use one connection per thread.
	class BridgeConnection
	{
	public:
		BridgeConnection() = default;
		~BridgeConnection();
		BridgeConnection(const BridgeConnection&) = delete;
		BridgeConnection& operator=(const BridgeConnection&) = delete;

		// `request` needs only method + params; id and token are filled in.
		bool Call(const json& request, json& response, std::string& error, unsigned int timeoutMs);

		void Close();
		bool IsConnected() const;
		const SessionInfo& Session() const { return _session; }

	private:
		bool Connect(std::string& error);

		void*       _handle = nullptr; // HANDLE, nullptr when disconnected
		SessionInfo _session;
		std::string _buffer;           // bytes read past the last newline
		int64_t     _nextId = 1;
	};
}
}
