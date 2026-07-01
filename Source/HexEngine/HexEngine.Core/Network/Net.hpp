
#pragma once

#include "../Required.hpp"

namespace HexEngine
{
	// Authority helpers for gameplay code. The rule for replicated state / RPCs is
	// simple: write authoritative state only when IsServer() is true.
	namespace Net
	{
		// A networking session is active (hosting or connected).
		HEX_API bool HasSession();

		// This peer is the authority: the host, OR there's no session at all
		// (single-player - you own everything). Guard server-side gameplay writes
		// (health, score, ...) with this so they run on the host and offline, but
		// never on a connected client (where they'd just be overwritten anyway).
		HEX_API bool IsServer();

		// This peer is a connected client (not the authority).
		HEX_API bool IsClient();
	}
}
