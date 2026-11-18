
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
// A pipe connection is PERSISTENT: a client may send any number of request
// lines over one connection (responses come back in order); one-shot clients
// that connect, send one line and disconnect keep working.
// -----------------------------------------------------------------------------

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace HexEngine
{
namespace EditorBridge
{
	using json = nlohmann::json;

	// Current bridge protocol version. Bumped on breaking wire changes.
	// v2: persistent connections, `batch`, entity references by name/index/id.
	inline constexpr int kProtocolVersion = 2;

	// Cap on a single response payload so a runaway handler can't flood the pipe
	// or the AI client. Handlers should paginate/truncate below this.
	inline constexpr size_t kMaxResponseBytes = 8u * 1024u * 1024u; // 8 MiB

	// Cap on a single request line (batches and procedural meshes are large).
	inline constexpr size_t kMaxRequestBytes = 16u * 1024u * 1024u; // 16 MiB

	// Upper bound on ops in one `batch` request.
	inline constexpr size_t kMaxBatchOps = 5000;

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

	// ---- Entity references --------------------------------------------------------
	// Every method that targets an existing entity accepts an entity reference:
	//   "Crate_01"                       -> by name
	//   42                               -> by live slot index
	//   {"index": 42, "generation": 3}   -> by exact id (stale ids are rejected)
	//   {"index": 42}                    -> by live slot index
	// Legacy params (`name` string / `index` integer / `id` object) are still
	// accepted where a method historically used them.
	struct EntityRef
	{
		enum class Kind { None, Name, Index, Id };
		Kind        kind = Kind::None;
		std::string name;
		uint32_t    index = 0;
		uint32_t    generation = 0;
	};

	// Parse one reference value. Returns false + error on a malformed value.
	inline bool ParseEntityRefValue(const json& v, EntityRef& out, std::string& error)
	{
		out = EntityRef{};
		if (v.is_string())
		{
			out.name = v.get<std::string>();
			if (out.name.empty()) { error = "entity name must be non-empty"; return false; }
			out.kind = EntityRef::Kind::Name;
			return true;
		}
		if (v.is_number_unsigned() || (v.is_number_integer() && v.get<int64_t>() >= 0))
		{
			out.kind = EntityRef::Kind::Index;
			out.index = v.get<uint32_t>();
			return true;
		}
		if (v.is_object() && v.contains("index") && v["index"].is_number_integer() && v["index"].get<int64_t>() >= 0)
		{
			out.index = v["index"].get<uint32_t>();
			if (v.contains("generation"))
			{
				if (!v["generation"].is_number_integer() || v["generation"].get<int64_t>() < 0)
				{
					error = "entity id 'generation' must be a non-negative integer";
					return false;
				}
				out.generation = v["generation"].get<uint32_t>();
				out.kind = EntityRef::Kind::Id;
			}
			else
			{
				out.kind = EntityRef::Kind::Index;
			}
			return true;
		}
		error = "entity reference must be a name (string), an index (integer) or an id object {index, generation}";
		return false;
	}

	// Find an entity reference in `params` under `key`; when absent and
	// `allowLegacy`, fall back to the historical `id` / `name` / `index` params.
	// Returns false + error when nothing usable is present or a value is malformed.
	inline bool GetEntityRef(const json& params, const char* key, bool allowLegacy, EntityRef& out, std::string& error)
	{
		if (params.contains(key))
			return ParseEntityRefValue(params[key], out, error);
		if (allowLegacy)
		{
			if (params.contains("id") && params["id"].is_object())
				return ParseEntityRefValue(params["id"], out, error);
			if (params.contains("name") && params["name"].is_string())
				return ParseEntityRefValue(params["name"], out, error);
			if (params.contains("index") && params["index"].is_number_integer())
				return ParseEntityRefValue(params["index"], out, error);
		}
		error = std::string("missing entity reference '") + key + "' (name string, index integer or {index, generation})";
		return false;
	}

	// ---- Batch result references ---------------------------------------------------
	// Inside a `batch`, any params value of the exact form {"$ref": "<op>.<path>"}
	// is replaced by a value from an EARLIER op's result before that op runs.
	// <op> is an op index or the label an earlier op declared with "as":
	//   {"$ref": "0"}            -> op 0's whole result object
	//   {"$ref": "0.id"}         -> op 0's result["id"]
	//   {"$ref": "house.id"}     -> result["id"] of the op declared "as": "house"
	//   {"$ref": "2.entities.1.name"}  (numeric segments index arrays)
	// Referencing the current/a later op, a failed op, or a missing path is an error.

	using BatchLabels = std::map<std::string, size_t>;

	inline bool LookupRefPath(const std::string& ref, const std::vector<json>& results, size_t currentOp,
		const BatchLabels& labels, json& out, std::string& error)
	{
		std::vector<std::string> parts;
		{
			size_t start = 0;
			for (;;)
			{
				const size_t dot = ref.find('.', start);
				parts.push_back(ref.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
				if (dot == std::string::npos) break;
				start = dot + 1;
			}
		}
		auto isIndex = [](const std::string& s) {
			if (s.empty() || s.size() > 9) return false;
			for (char c : s) if (c < '0' || c > '9') return false;
			return true;
		};
		size_t op = 0;
		if (isIndex(parts[0]))
			op = (size_t)std::stoul(parts[0]);
		else if (auto it = labels.find(parts[0]); it != labels.end())
			op = it->second;
		else
		{
			error = "$ref '" + ref + "' must start with an op index or an earlier op's \"as\" label";
			return false;
		}
		if (op >= currentOp || op >= results.size())
		{
			error = "$ref '" + ref + "' must reference an EARLIER op";
			return false;
		}
		const json* cur = &results[op];
		if (cur->is_null())
		{
			error = "$ref '" + ref + "' references op " + parts[0] + ", which failed or produced no result";
			return false;
		}
		for (size_t i = 1; i < parts.size(); ++i)
		{
			const std::string& p = parts[i];
			if (cur->is_object() && cur->contains(p))
				cur = &(*cur)[p];
			else if (cur->is_array() && isIndex(p) && (size_t)std::stoul(p) < cur->size())
				cur = &(*cur)[(size_t)std::stoul(p)];
			else
			{
				error = "$ref '" + ref + "': path segment '" + p + "' not found";
				return false;
			}
		}
		out = *cur;
		return true;
	}

	// Recursively substitute {"$ref": "..."} objects in `value`. `results[i]` is
	// op i's result object (null if it failed).
	inline bool ResolveBatchRefs(json& value, const std::vector<json>& results, size_t currentOp,
		const BatchLabels& labels, std::string& error)
	{
		if (value.is_object())
		{
			if (value.size() == 1 && value.contains("$ref"))
			{
				if (!value["$ref"].is_string())
				{
					error = "$ref must be a string like \"0.id\"";
					return false;
				}
				json resolved;
				if (!LookupRefPath(value["$ref"].get<std::string>(), results, currentOp, labels, resolved, error))
					return false;
				value = std::move(resolved);
				return true;
			}
			for (auto& [k, v] : value.items())
				if (!ResolveBatchRefs(v, results, currentOp, labels, error))
					return false;
			return true;
		}
		if (value.is_array())
		{
			for (auto& v : value)
				if (!ResolveBatchRefs(v, results, currentOp, labels, error))
					return false;
		}
		return true;
	}

	// ---- Component field typing (shared by set_component_field / describe) ----

	inline bool IsNumericVector(const json& v)
	{
		if (!v.is_array() || v.size() < 2 || v.size() > 4)
			return false;
		for (const auto& e : v)
			if (!e.is_number()) return false;
		return true;
	}

	// A short type name for a serialised value (what agents see).
	inline std::string JsonFieldType(const json& v)
	{
		if (v.is_boolean()) return "bool";
		if (v.is_number_integer()) return "int";
		if (v.is_number()) return "float";
		if (v.is_string()) return "string";
		if (IsNumericVector(v)) return "vec" + std::to_string(v.size());
		if (v.is_array()) return "array";
		if (v.is_object()) return "object";
		return "null";
	}

	// Check that `patch` has the same shape as the field's current value, so
	// a wrong type can't throw half-way through a component's Deserialize
	// (or silently store garbage). Numeric vectors keep their length; ints
	// stay integral; nested objects are checked key-by-key for keys that
	// already exist (new nested keys are allowed, e.g. switching collider
	// shapeData from box to sphere).
	inline bool CheckFieldShape(const json& current, const json& patch, const std::string& path, std::string& msg)
	{
		if (current.is_null())
			return true;
		if (patch.is_null())
		{
			msg = "'" + path + "' cannot be set to null";
			return false;
		}
		auto fail = [&](const std::string& expected) {
			msg = "'" + path + "' expects " + expected + ", got " + JsonFieldType(patch) + " " + patch.dump();
			return false;
		};
		if (current.is_boolean())
			return patch.is_boolean() ? true : fail("a bool");
		if (current.is_number())
		{
			if (!patch.is_number() || patch.is_boolean())
				return fail("a number");
			if (current.is_number_integer() && !patch.is_number_integer())
			{
				const double d = patch.get<double>();
				if (std::floor(d) != d)
					return fail("an integer");
			}
			return true;
		}
		if (current.is_string())
			return patch.is_string() ? true : fail("a string");
		if (current.is_array())
		{
			if (!patch.is_array())
				return fail("an array");
			if (IsNumericVector(current))
			{
				if (!IsNumericVector(patch) || patch.size() != current.size())
					return fail("an array of " + std::to_string(current.size()) + " numbers");
			}
			return true;
		}
		if (current.is_object())
		{
			if (!patch.is_object())
				return fail("an object");
			for (auto& [k, v] : patch.items())
				if (current.contains(k) && !CheckFieldShape(current[k], v, path + "." + k, msg))
					return false;
			return true;
		}
		return true;
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
