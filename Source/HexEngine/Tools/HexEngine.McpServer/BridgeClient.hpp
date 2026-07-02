
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
}
}
