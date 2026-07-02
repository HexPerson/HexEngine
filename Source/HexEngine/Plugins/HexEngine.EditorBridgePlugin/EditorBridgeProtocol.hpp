
#pragma once

// -----------------------------------------------------------------------------
// HexEngine Editor Bridge - shared wire protocol (header-only, nlohmann only).
//
// Deliberately boring, explicit JSON. Shared verbatim by the editor-only bridge
// plugin (server side) and the standalone HexEngine.McpServer (client side), and
// unit-tested directly. No engine headers here - only <nlohmann/json.hpp> + std,
// so it compiles in the dependency-light test target and the tool exe.
//
// Request:   { "id": <int>, "method": "<name>", "params": { ... } }
// Success:   { "id": <int>, "ok": true,  "result": { ... } }
// Failure:   { "id": <int>, "ok": false, "error": { "code": "...", "message": "..." } }
//
// Framing on the pipe / stdio is newline-delimited JSON (one message per line).
// -----------------------------------------------------------------------------

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

namespace HexEngine
{
namespace EditorBridge
{
	using json = nlohmann::json;

	// Current bridge protocol version. Bumped on breaking wire changes.
	inline constexpr int kProtocolVersion = 1;

	// Cap on a single response payload so a runaway handler can't flood the pipe
	// or the AI client. Handlers should paginate/truncate below this.
	inline constexpr size_t kMaxResponseBytes = 1u * 1024u * 1024u; // 1 MiB

	// Fail-closed, string error codes (stable identifiers for clients).
	namespace ErrorCode
	{
		inline constexpr const char* UnknownMethod  = "UnknownMethod";
		inline constexpr const char* InvalidRequest = "InvalidRequest";
		inline constexpr const char* InvalidParams  = "InvalidParams";
		inline constexpr const char* NotImplemented = "NotImplemented";
		inline constexpr const char* NotAvailable   = "NotAvailable";   // e.g. no scene open
		inline constexpr const char* Internal       = "Internal";
		inline constexpr const char* ResponseTooLarge = "ResponseTooLarge";
		inline constexpr const char* Unauthorized   = "Unauthorized";  // missing/wrong session token
	}

	// Constant-time-ish string compare for the session token (avoids trivially
	// leaking length/prefix via early-out). Local IPC, so this is belt-and-braces.
	inline bool TokensMatch(const std::string& a, const std::string& b)
	{
		if (a.empty() || a.size() != b.size())
			return false;
		unsigned char diff = 0;
		for (size_t i = 0; i < a.size(); ++i)
			diff |= (unsigned char)(a[i] ^ b[i]);
		return diff == 0;
	}

	// A parsed, validated request.
	struct Request
	{
		int64_t     id = 0;
		std::string method;
		std::string token;   // per-session auth token (optional in the wire form)
		json        params = json::object();
	};

	// Parse + validate a raw request object. Fail closed: id must be an integer,
	// method a non-empty string, params (if present) an object. Returns false and
	// fills `error` on any violation.
	inline bool ParseRequest(const json& j, Request& out, std::string& error)
	{
		if (!j.is_object())
		{
			error = "request must be a JSON object";
			return false;
		}
		if (!j.contains("id") || !j["id"].is_number_integer())
		{
			error = "request is missing an integer 'id'";
			return false;
		}
		if (!j.contains("method") || !j["method"].is_string())
		{
			error = "request is missing a string 'method'";
			return false;
		}
		out.id     = j["id"].get<int64_t>();
		out.method = j["method"].get<std::string>();
		if (out.method.empty())
		{
			error = "'method' must be non-empty";
			return false;
		}
		if (j.contains("token"))
		{
			if (!j["token"].is_string())
			{
				error = "'token' must be a string when present";
				return false;
			}
			out.token = j["token"].get<std::string>();
		}
		if (j.contains("params"))
		{
			if (!j["params"].is_object())
			{
				error = "'params' must be an object when present";
				return false;
			}
			out.params = j["params"];
		}
		else
		{
			out.params = json::object();
		}
		return true;
	}

	// Convenience: parse from a raw text line. Catches JSON syntax errors.
	inline bool ParseRequestText(const std::string& text, Request& out, std::string& error)
	{
		json j;
		try
		{
			j = json::parse(text);
		}
		catch (const std::exception& e)
		{
			error = std::string("invalid JSON: ") + e.what();
			return false;
		}
		return ParseRequest(j, out, error);
	}

	inline json MakeResult(int64_t id, json result)
	{
		return json{ {"id", id}, {"ok", true}, {"result", std::move(result)} };
	}

	inline json MakeError(int64_t id, const std::string& code, const std::string& message)
	{
		return json{ {"id", id}, {"ok", false}, {"error", { {"code", code}, {"message", message} }} };
	}

	// Enforce the response cap. If `response` serialises larger than the cap, it is
	// replaced with a ResponseTooLarge error so we never emit an oversized frame.
	inline std::string SerializeResponseCapped(const json& response, int64_t id)
	{
		std::string text = response.dump();
		if (text.size() > kMaxResponseBytes)
		{
			return MakeError(id, ErrorCode::ResponseTooLarge,
				"response exceeded the " + std::to_string(kMaxResponseBytes) + "-byte cap").dump();
		}
		return text;
	}

	// ---- Discovery session file -------------------------------------------------
	// Each running editor bridge writes one session file so the MCP server can
	// discover it without a fixed port. Location:
	//   %TEMP%/HexEngine/EditorBridge/session-<pid>.json
	// Schema below. Kept tiny + explicit.
	struct SessionInfo
	{
		uint32_t    pid = 0;
		std::string pipeName;      // e.g. \\.\pipe\HexEngine.EditorBridge.1234
		std::string projectName;   // best-effort; may be empty
		uint64_t    startedAtUnix = 0;
		int         protocolVersion = kProtocolVersion;
		std::string token;         // per-session secret; only a process that can read
		                           // this file (same user) learns it, and every
		                           // request must echo it back.
	};

	inline std::string PipeNameForPid(uint32_t pid)
	{
		return "\\\\.\\pipe\\HexEngine.EditorBridge." + std::to_string(pid);
	}

	inline std::string SessionFileName(uint32_t pid)
	{
		return "session-" + std::to_string(pid) + ".json";
	}

	inline json SessionToJson(const SessionInfo& s)
	{
		return json{
			{"pid", s.pid},
			{"pipe", s.pipeName},
			{"project", s.projectName},
			{"startedAtUnix", s.startedAtUnix},
			{"protocolVersion", s.protocolVersion},
			{"token", s.token},
		};
	}

	inline bool SessionFromJson(const json& j, SessionInfo& out)
	{
		if (!j.is_object())
			return false;
		out.pid             = j.value("pid", 0u);
		out.pipeName        = j.value("pipe", std::string());
		out.projectName     = j.value("project", std::string());
		out.startedAtUnix   = j.value("startedAtUnix", (uint64_t)0);
		out.protocolVersion = j.value("protocolVersion", 0);
		out.token           = j.value("token", std::string());
		return out.pid != 0 && !out.pipeName.empty();
	}
}
}
