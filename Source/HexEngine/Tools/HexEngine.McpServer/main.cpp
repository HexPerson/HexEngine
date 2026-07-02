
// -----------------------------------------------------------------------------
// HexEngine.McpServer - standalone MCP (Model Context Protocol) server.
//
// Speaks MCP JSON-RPC 2.0 over stdio (newline-delimited). Exposes read-only
// tools that either (a) inspect files under a project/repo root, or (b) proxy to
// a running HexEngine editor via the local named-pipe bridge. When no editor is
// connected, live tools fail cleanly with EditorNotConnected; static tools keep
// working. No shell execution, no writes, no mutation - Phase 1 is read-only.
// -----------------------------------------------------------------------------

#include "StaticTools.hpp"
#include "BridgeClient.hpp"
#include "../../Plugins/HexEngine.EditorBridgePlugin/EditorBridgeProtocol.hpp"

#include <cstdio>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <io.h>

using nlohmann::json;
namespace EB = HexEngine::EditorBridge;
namespace Mcp = HexEngine::Mcp;

namespace
{
	std::string g_root = ".";           // project/repo root for static tools
	std::string g_includeDir;           // <root>/Include by default

	struct Tool
	{
		std::string name;
		std::string description;
		json inputSchema;
		bool live = false;
		std::string bridgeMethod;                  // live tools
		std::function<json(const json&)> handler;  // static tools
	};

	json StrSchema(std::initializer_list<std::pair<const char*, const char*>> props, std::initializer_list<const char*> required = {})
	{
		json p = json::object();
		for (auto& kv : props)
			p[kv.first] = json{ {"type", "string"}, {"description", kv.second} };
		json req = json::array();
		for (auto* r : required) req.push_back(r);
		return json{ {"type", "object"}, {"properties", p}, {"required", req} };
	}

	std::vector<Tool> BuildTools()
	{
		std::vector<Tool> t;

		// --- Static (no editor required) ---
		t.push_back({ "hex_validate_nlohmann_staging",
			"Validate that the full nlohmann/json include tree (json.hpp + adl_serializer.hpp + json_fwd.hpp) is staged under <root>/Include.",
			json{ {"type","object"}, {"properties", json::object()} }, false, "",
			[](const json&) { return Mcp::ValidateNlohmannStaging(g_includeDir); } });

		t.push_back({ "hex_parse_msbuild_log",
			"Parse an MSBuild/cl.exe log file into structured error/warning diagnostics. Param: path (relative to root).",
			StrSchema({ {"path","Log file path, relative to the root."} }, { "path" }), false, "",
			[](const json& a) { return Mcp::ParseMsbuildLogFile(g_root, a.value("path", std::string())); } });

		t.push_back({ "hex_read_build_log",
			"Read a build/text log file (capped). Param: path (relative to root).",
			StrSchema({ {"path","Log file path, relative to the root."} }, { "path" }), false, "",
			[](const json& a) { return Mcp::ReadBuildLog(g_root, a.value("path", std::string())); } });

		t.push_back({ "hex_validate_dependency_layout",
			"Validate build/dependencies.lock.json: every dependency has a git url + ref, and report which ThirdParty dirs are cloned.",
			json{ {"type","object"}, {"properties", json::object()} }, false, "",
			[](const json&) { return Mcp::ValidateDependencyLayout(g_root); } });

		t.push_back({ "hex_verify_package_manifest",
			"Verify an asset package against its .hashmanifest sidecar (SHA-256 over the whole .pkg). Param: path (relative to root).",
			StrSchema({ {"path",".pkg path, relative to the root."} }, { "path" }), false, "",
			[](const json& a) { return Mcp::VerifyPackageManifest(g_root, a.value("path", std::string())); } });

		t.push_back({ "hex_list_project_files",
			"List files under root/subdir (capped, read-only). Params: subdir (optional), ext (optional comma-separated extension filter).",
			StrSchema({ {"subdir","Subdirectory relative to root (optional)."}, {"ext","Comma-separated extension filter, e.g. .hmesh,.hmat (optional)."} }), false, "",
			[](const json& a) { return Mcp::ListProjectFiles(g_root, a.value("subdir", std::string()), a.value("ext", std::string())); } });

		t.push_back({ "hex_list_asset_files",
			"List HexEngine asset files under root/subdir (capped). Param: subdir (optional).",
			StrSchema({ {"subdir","Subdirectory relative to root (optional)."} }), false, "",
			[](const json& a) { return Mcp::ListAssetFiles(g_root, a.value("subdir", std::string())); } });

		t.push_back({ "hex_list_editor_sessions",
			"List discovered running HexEngine editor bridge sessions (pid, pipe, project, start time).",
			json{ {"type","object"}, {"properties", json::object()} }, false, "",
			[](const json&) {
				json arr = json::array();
				for (auto& s : Mcp::DiscoverSessions())
					arr.push_back(EB::SessionToJson(s));
				return json{ {"sessionDir", Mcp::SessionDir()}, {"count", arr.size()}, {"sessions", arr} };
			} });

		// --- Live (proxy to editor bridge). Tool name hex_<x> -> bridge method <x>. ---
		struct Live { const char* method; const char* desc; bool takesParams; };
		const Live live[] = {
			{ "get_editor_status",       "Get editor status: running, project, current scene, edit/game mode.", false },
			{ "get_open_project",        "Get open project metadata and root paths.", false },
			{ "get_open_scene",          "Get the current scene name/path and high-level stats.", false },
			{ "get_selected_entity",     "Get the currently selected entity, or null if none.", false },
			{ "inspect_entity",          "Inspect an entity. Params: id (number) or name (string).", true },
			{ "list_entities",           "List entities in the current scene (id/name/components). Params: offset, limit, filter (optional).", true },
			{ "list_components",         "List component types (registered, or found in the current scene).", false },
			{ "inspect_component",       "Inspect one component's safe fields. Params: entity (id/name) + component (type name).", true },
			{ "list_loaded_resources",   "List currently loaded resources (id/path/type/refcount where available).", false },
			{ "find_missing_references", "Find missing material/mesh/texture/script/prefab references if detectable.", false },
			{ "validate_current_scene",  "Run read-only validation of the current scene; returns structured diagnostics.", false },
			{ "get_recent_engine_logs",  "Return recent engine logs (capped). Params: max (optional).", true },
		};
		for (auto& l : live)
		{
			Tool tool;
			tool.name = std::string("hex_") + l.method;
			tool.description = l.desc;
			tool.live = true;
			tool.bridgeMethod = l.method;
			tool.inputSchema = l.takesParams
				? json{ {"type","object"}, {"properties", json::object()}, {"additionalProperties", true} }
				: json{ {"type","object"}, {"properties", json::object()} };
			t.push_back(std::move(tool));
		}

		return t;
	}

	// Call a live tool: discover newest session, proxy the bridge method.
	json CallLive(const std::string& bridgeMethod, const json& args, bool& isError)
	{
		isError = false;
		auto sessions = Mcp::DiscoverSessions();
		if (sessions.empty())
		{
			isError = true;
			return json{ {"error", { {"code","EditorNotConnected"},
				{"message","No running HexEngine editor bridge was found. Start the editor with the bridge enabled (--enable-editor-bridge or HEXENGINE_EDITOR_BRIDGE=1)."} }} };
		}

		json request{ {"id", 1}, {"method", bridgeMethod}, {"params", args.is_object() ? args : json::object()} };
		json response;
		std::string err;
		if (!Mcp::CallBridge(sessions.front(), request, response, err))
		{
			isError = true;
			return json{ {"error", { {"code","BridgeCallFailed"}, {"message", err} }} };
		}
		if (!response.value("ok", false))
			isError = true; // bridge reported a structured error; surface it as-is
		return response;
	}

	// Wrap a tool result object into an MCP tools/call content payload.
	json ToContent(const json& obj, bool isError)
	{
		return json{
			{"content", json::array({ json{ {"type","text"}, {"text", obj.dump(2)} } })},
			{"isError", isError},
		};
	}

	json RpcResult(const json& id, json result) { return json{ {"jsonrpc","2.0"}, {"id", id}, {"result", std::move(result)} }; }
	json RpcError(const json& id, int code, const std::string& msg)
	{
		return json{ {"jsonrpc","2.0"}, {"id", id}, {"error", { {"code", code}, {"message", msg} }} };
	}
}

int main(int argc, char** argv)
{
	for (int i = 1; i < argc; ++i)
	{
		std::string a = argv[i];
		if (a == "--root" && i + 1 < argc) g_root = argv[++i];
	}
	if (const char* envRoot = std::getenv("HEXENGINE_MCP_ROOT")) g_root = envRoot;
	g_includeDir = (std::filesystem::path(g_root) / "Include").string();

	// Binary stdio so newline framing isn't corrupted by CRLF translation.
	_setmode(_fileno(stdin), _O_BINARY);
	_setmode(_fileno(stdout), _O_BINARY);

	const std::vector<Tool> tools = BuildTools();

	std::string line;
	while (std::getline(std::cin, line))
	{
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty()) continue;

		json msg;
		try { msg = json::parse(line); }
		catch (...) { std::cout << RpcError(nullptr, -32700, "Parse error").dump() << "\n" << std::flush; continue; }

		const json id = msg.contains("id") ? msg["id"] : json(nullptr);
		const std::string method = msg.value("method", std::string());

		// Notifications (no id) get no response.
		const bool isNotification = !msg.contains("id");

		json out;
		if (method == "initialize")
		{
			out = RpcResult(id, json{
				{"protocolVersion", "2024-11-05"},
				{"capabilities", { {"tools", json::object()} }},
				{"serverInfo", { {"name","HexEngine.McpServer"}, {"version","0.1.0"} }},
			});
		}
		else if (method == "tools/list")
		{
			json list = json::array();
			for (const auto& tl : tools)
				list.push_back(json{ {"name", tl.name}, {"description", tl.description}, {"inputSchema", tl.inputSchema} });
			out = RpcResult(id, json{ {"tools", list} });
		}
		else if (method == "tools/call")
		{
			const json params = msg.value("params", json::object());
			const std::string name = params.value("name", std::string());
			const json args = params.value("arguments", json::object());

			const Tool* found = nullptr;
			for (const auto& tl : tools) if (tl.name == name) { found = &tl; break; }

			if (!found)
			{
				out = RpcResult(id, ToContent(json{ {"error", { {"code","UnknownTool"}, {"message","No tool named " + name} }} }, true));
			}
			else if (found->live)
			{
				bool isError = false;
				json r = CallLive(found->bridgeMethod, args, isError);
				out = RpcResult(id, ToContent(r, isError));
			}
			else
			{
				json r;
				bool isError = false;
				try { r = found->handler(args); }
				catch (const std::exception& e) { r = json{ {"error", { {"code","Internal"}, {"message", e.what()} }} }; isError = true; }
				if (r.is_object() && r.contains("error")) isError = true;
				out = RpcResult(id, ToContent(r, isError));
			}
		}
		else if (method == "ping")
		{
			out = RpcResult(id, json::object());
		}
		else if (isNotification)
		{
			continue; // e.g. notifications/initialized
		}
		else
		{
			out = RpcError(id, -32601, "Method not found: " + method);
		}

		std::cout << out.dump() << "\n" << std::flush;
	}
	return 0;
}
