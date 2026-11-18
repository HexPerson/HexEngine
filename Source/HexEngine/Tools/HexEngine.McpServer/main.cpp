
// -----------------------------------------------------------------------------
// HexEngine.McpServer - standalone MCP (Model Context Protocol) server.
//
// Speaks MCP JSON-RPC 2.0 over stdio (newline-delimited). Exposes tools that
// either (a) inspect files under a project/repo root, or (b) proxy to a running
// HexEngine editor via the local named-pipe bridge over ONE persistent
// connection. When no editor is connected, live tools fail cleanly with
// EditorNotConnected; static tools keep working. Mutating live tools only work
// when the editor was launched with HEXENGINE_EDITOR_BRIDGE_WRITE=1.
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

	// ---- JSON-schema building blocks for the live tools ----

	json Obj(json props, std::initializer_list<const char*> required = {})
	{
		json req = json::array();
		for (auto* r : required) req.push_back(r);
		json s = json{ {"type", "object"}, {"properties", std::move(props)} };
		if (!req.empty()) s["required"] = req;
		return s;
	}

	json Str(const char* desc)  { return json{ {"type", "string"},  {"description", desc} }; }
	json Int(const char* desc)  { return json{ {"type", "integer"}, {"description", desc} }; }
	json Bool(const char* desc) { return json{ {"type", "boolean"}, {"description", desc} }; }
	json Num(const char* desc)  { return json{ {"type", "number"},  {"description", desc} }; }

	json NumArray(size_t n, const char* desc)
	{
		return json{ {"type", "array"}, {"items", json{ {"type", "number"} }}, {"minItems", n}, {"maxItems", n}, {"description", desc} };
	}

	json ColourSchema(const char* desc)
	{
		return json{ {"type", "array"}, {"items", json{ {"type", "number"} }}, {"minItems", 3}, {"maxItems", 4}, {"description", desc} };
	}

	json EntityRefSchema(const char* desc = "Target entity: name (string), live index (integer), or exact id object {index, generation} as returned by other tools.")
	{
		return json{
			{"description", desc},
			{"anyOf", json::array({
				json{ {"type", "string"} },
				json{ {"type", "integer"}, {"minimum", 0} },
				json{ {"type", "object"},
					{"properties", json{ {"index", json{ {"type", "integer"} }}, {"generation", json{ {"type", "integer"} }} }},
					{"required", json::array({ "index" })} },
			})},
		};
	}

	json FieldsSchema()
	{
		return json{ {"type", "object"}, {"additionalProperties", true},
			{"description", "Field:value patch. Names/shapes come from hex_inspect_component (the component's Serialize() output). Unknown names are rejected before anything applies; object values merge recursively, scalars/arrays replace."} };
	}

	json TransformProps(json props)
	{
		props["position"] = NumArray(3, "[x, y, z] world position.");
		props["rotation"] = NumArray(4, "Quaternion [x, y, z, w]. Takes precedence over eulerDegrees.");
		props["eulerDegrees"] = NumArray(3, "[pitch, yaw, roll] in degrees.");
		props["scale"] = NumArray(3, "[x, y, z] scale.");
		return props;
	}

	struct LiveTool { std::string method; std::string description; json schema; };

	std::vector<LiveTool> LiveTools()
	{
		const char* WRITE = " (write opt-in required: HEXENGINE_EDITOR_BRIDGE_WRITE=1)";
		auto w = [&](const std::string& d) { return d + WRITE; };
		const json componentSpec = Obj(json{
			{"component", Str("Registered component class name (see hex_list_component_types), e.g. \"PointLight\".")},
			{"fields", FieldsSchema()},
		}, { "component" });

		std::vector<LiveTool> v;

		// --- read ---
		v.push_back({ "get_editor_status", "Editor status: scene name, edit/play mode, project, protocol version, whether writes are enabled, frame counter.", Obj(json::object()) });
		v.push_back({ "get_open_project", "Open project metadata and root paths.", Obj(json::object()) });
		v.push_back({ "get_open_scene", "Current scene name and entity count.", Obj(json::object()) });
		v.push_back({ "get_selected_entity", "The entity selected in the editor (full detail), or null.", Obj(json::object()) });
		v.push_back({ "inspect_entity", "Entity detail: id, name, parent, children, component types, AABB.",
			Obj(json{ {"entity", EntityRefSchema()} }, { "entity" }) });
		v.push_back({ "list_entities", "List entities (id/name/components), paginated. Filters: name substring and/or component type.",
			Obj(json{ {"offset", Int("Skip this many matches.")}, {"limit", Int("Max results (<= 1000, default 200).")},
				{"filter", Str("Name substring filter.")}, {"component", Str("Only entities that have this component type.")} }) });
		v.push_back({ "list_components", "Component types present in the current scene.", Obj(json::object()) });
		v.push_back({ "list_component_types", "Every registered component class (anything hex_add_component accepts).", Obj(json::object()) });
		v.push_back({ "inspect_component", "A component's serialised fields - the exact names/shapes hex_set_component_field accepts.",
			Obj(json{ {"entity", EntityRefSchema()}, {"component", Str("Component type name, e.g. \"Transform\".")} }, { "entity", "component" }) });
		v.push_back({ "list_loaded_resources", "Currently loaded resources (id/path/refcount).", Obj(json{ {"limit", Int("Max results (<= 2000).")} }) });
		v.push_back({ "find_missing_references", "Find missing asset references (not implemented yet).", Obj(json::object()) });
		v.push_back({ "validate_current_scene", "Basic scene validation diagnostics.", Obj(json::object()) });
		v.push_back({ "get_recent_engine_logs", "Recent engine log lines with sequence numbers. Pass the previous lastSeq as sinceSeq to get only NEW lines.",
			Obj(json{ {"max", Int("Max lines (<= 1000, default 200).")}, {"sinceSeq", Int("Only lines with seq greater than this.")},
				{"minLevel", json{ {"type", "string"}, {"enum", json::array({ "debug", "info", "warn", "crit" })}, {"description", "Minimum level."} }} }) });
		v.push_back({ "list_projects", "The editor's recent projects (path/name/exists).", Obj(json::object()) });

		// --- write ---
		v.push_back({ "open_project", w("Open a project at the project browser. Async: poll hex_get_editor_status until sceneName is set. Not batchable."),
			Obj(json{ {"path", Str("Project .json path.")}, {"name", Str("Project name (stem from hex_list_projects).")} }) });
		v.push_back({ "exec_console", w("Run an editor console command / set a cvar, e.g. \"r_bloomIntensity 0.1\"."),
			Obj(json{ {"command", Str("Console command line.")} }, { "command" }) });
		v.push_back({ "create_entity", w("Create an entity, optionally with a transform, parent and components (each with initial fields) in ONE call. Returns {id, name} - the name may be de-duplicated."),
			Obj(TransformProps(json{
				{"name", Str("Desired entity name.")},
				{"parent", EntityRefSchema("Optional parent entity.")},
				{"persistent", Bool("Save with the scene (default true).")},
				{"components", json{ {"type", "array"}, {"items", componentSpec}, {"description", "Components to add, in order."} }},
			}), { "name" }) });
		v.push_back({ "add_component", w("Add a component (by registered class name) to an entity, optionally setting its fields. An entity holds one component per type: adding an existing type returns alreadyExisted=true."),
			Obj(json{ {"entity", EntityRefSchema()}, {"component", Str("Registered component class name.")}, {"fields", FieldsSchema()} }, { "entity", "component" }) });
		v.push_back({ "remove_component", w("Remove a component from an entity (Transform cannot be removed)."),
			Obj(json{ {"entity", EntityRefSchema()}, {"component", Str("Component type name.")} }, { "entity", "component" }) });
		v.push_back({ "set_component_field", w("Patch a component's serialised fields (read-modify-write through Serialize/Deserialize)."),
			Obj(json{ {"entity", EntityRefSchema()}, {"component", Str("Component type name.")}, {"fields", FieldsSchema()} }, { "entity", "component", "fields" }) });
		v.push_back({ "set_entity_transform", w("Set position / rotation / scale. CAMERA entities are driven through the camera's own angles: degrees, pitch + = up (clamped to +/-89), yaw 0 faces -Z and yaw + turns toward -X."),
			Obj(TransformProps(json{ {"entity", EntityRefSchema()} }), { "entity" }) });
		v.push_back({ "rename_entity", w("Rename an entity; returns the final (possibly de-duplicated) name."),
			Obj(json{ {"entity", EntityRefSchema()}, {"newName", Str("New name.")} }, { "entity", "newName" }) });
		v.push_back({ "set_parent", w("Re-parent an entity; parent null un-parents it."),
			Obj(json{ {"entity", EntityRefSchema()},
				{"parent", json{ {"description", "New parent entity reference, or null to un-parent."}, {"anyOf", json::array({ EntityRefSchema(), json{ {"type", "null"} } })} }},
				{"preserveWorldPosition", Bool("Keep the world position (default true).")} }, { "entity" }) });
		v.push_back({ "set_entity_flags", w("Set/clear entity flags."),
			Obj(json{ {"entity", EntityRefSchema()},
				{"set", json{ {"type", "array"}, {"items", json{ {"type", "string"}, {"enum", json::array({ "DoNotSave", "DoNotRender", "DoNotBlockNavMesh", "ExcludeFromHLOD", "DoNotPickInEditor" })} }} }},
				{"clear", json{ {"type", "array"}, {"items", json{ {"type", "string"}, {"enum", json::array({ "DoNotSave", "DoNotRender", "DoNotBlockNavMesh", "ExcludeFromHLOD", "DoNotPickInEditor" })} }} }} }, { "entity" }) });
		v.push_back({ "delete_entity", w("Delete an entity (and its children)."),
			Obj(json{ {"entity", EntityRefSchema()} }, { "entity" }) });
		v.push_back({ "clone_entity", w("Clone an entity with its full hierarchy. For world-baked kit meshes (entity at origin) position acts as a world OFFSET."),
			Obj(TransformProps(json{ {"entity", EntityRefSchema("Source entity.")}, {"newName", Str("Name for the clone.")},
				{"persistent", Bool("Save with the scene (default false).")} }), { "entity", "newName" }) });
		v.push_back({ "select_entity", w("Select an entity in the editor (the editor also frames the camera on it), or clear the selection."),
			Obj(json{ {"entity", EntityRefSchema()}, {"clear", Bool("Clear the selection instead.")} }) });
		v.push_back({ "create_mesh_entity", w("Create an entity with an in-memory procedural mesh. Front faces are COUNTER-clockwise seen from outside. Normals are computed if omitted."),
			Obj(TransformProps(json{
				{"name", Str("Entity name.")},
				{"vertices", json{ {"type", "array"}, {"description", "[[x,y,z],...] or flat [x,y,z,...] (local space)."} }},
				{"indices", json{ {"type", "array"}, {"items", json{ {"type", "integer"} }}, {"description", "Flat triangle list."} }},
				{"normals", json{ {"type", "array"}, {"description", "Optional, same layout/count as vertices."} }},
				{"uvs", json{ {"type", "array"}, {"description", "Optional [[u,v],...] or flat."} }},
				{"parent", EntityRefSchema("Optional parent entity.")},
				{"persistent", Bool("Save with the scene (default false; the mesh has no asset file).")},
				{"material", Obj(json{
					{"path", Str("Existing .hmat to copy (default EngineData.Materials/Default.hmat).")},
					{"diffuseColour", ColourSchema("[r,g,b] or [r,g,b,a], 0..1.")},
					{"emissiveColour", ColourSchema("[r,g,b] or [r,g,b,a]; values > 1 glow.")},
					{"metallic", Num("0..1")}, {"roughness", Num("0..1")}, {"smoothness", Num("0..1")},
					{"affectsGI", Bool("Contribute to voxel GI.")} })},
			}), { "name", "vertices", "indices" }) });
		v.push_back({ "capture_frame", w("Save the main camera's rendered frame to an image file. Waits waitFrames (default 2) editor frames first so recent edits are visible. Not batchable."),
			Obj(json{ {"path", Str("Absolute output path (.png / .jpg / .dds).")}, {"waitFrames", Int("Frames to wait before capturing (default 2).")} }, { "path" }) });

		// --- batch ---
		v.push_back({ "batch", "FAST PATH: run many bridge methods (any of the above except open_project/capture_frame, names WITHOUT the hex_ prefix) in ONE editor frame. "
			"An op's params may reference an earlier op's result with {\"$ref\": \"<opIndexOrLabel>.<path>\"}, e.g. {\"$ref\": \"house.id\"} where op had \"as\": \"house\". "
			"Ops run in order; with stopOnError (default) later ops are skipped after a failure. Not transactional: completed ops stay applied." + std::string(" Write ops need the write opt-in."),
			Obj(json{
				{"ops", json{ {"type", "array"}, {"minItems", 1}, {"maxItems", 5000},
					{"items", Obj(json{
						{"method", Str("Bridge method name, e.g. \"create_entity\".")},
						{"params", json{ {"type", "object"}, {"additionalProperties", true}, {"description", "That method's params (same as its hex_ tool)."} }},
						{"as", Str("Optional label for $ref.")} }, { "method" })} }},
				{"stopOnError", Bool("Skip remaining ops after the first failure (default true).")},
			}, { "ops" }) });

		return v;
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
		for (auto& l : LiveTools())
		{
			Tool tool;
			tool.name = std::string("hex_") + l.method;
			tool.description = l.description;
			tool.live = true;
			tool.bridgeMethod = l.method;
			tool.inputSchema = l.schema;
			t.push_back(std::move(tool));
		}

		return t;
	}

	// One long-lived pipe connection for the whole MCP session: no per-call
	// discovery or connect. Re-discovers automatically if the editor restarts.
	Mcp::BridgeConnection g_bridge;

	// Generous: the bridge cancels anything the editor hasn't STARTED within
	// 10 s, but a started batch/mesh upload may legitimately take longer.
	constexpr unsigned kBridgeCallTimeoutMs = 120000;

	// Call a live tool over the persistent bridge connection. Returns the
	// bridge's `result` object on success, or {"error": {...}} on failure.
	json CallLive(const std::string& bridgeMethod, const json& args, bool& isError)
	{
		isError = false;
		if (!g_bridge.IsConnected() && Mcp::DiscoverSessions().empty())
		{
			isError = true;
			return json{ {"error", { {"code","EditorNotConnected"},
				{"message","No running HexEngine editor bridge was found. Start the editor with the bridge enabled (--enable-editor-bridge or HEXENGINE_EDITOR_BRIDGE=1; writes also need HEXENGINE_EDITOR_BRIDGE_WRITE=1)."} }} };
		}

		json request{ {"method", bridgeMethod}, {"params", args.is_object() ? args : json::object()} };
		json response;
		std::string err;
		if (!g_bridge.Call(request, response, err, kBridgeCallTimeoutMs))
		{
			isError = true;
			return json{ {"error", { {"code","BridgeCallFailed"}, {"message", err} }} };
		}
		if (!response.value("ok", false))
		{
			isError = true;
			return json{ {"error", response.contains("error") ? response["error"] : json("unknown bridge error")} };
		}
		return response.contains("result") ? response["result"] : json::object();
	}

	// Wrap a tool result object into an MCP tools/call content payload. Compact
	// JSON: pretty-printing roughly doubles the tokens an agent pays per call.
	json ToContent(const json& obj, bool isError)
	{
		return json{
			{"content", json::array({ json{ {"type","text"}, {"text", obj.dump()} } })},
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
				{"serverInfo", { {"name","HexEngine.McpServer"}, {"version","0.2.0"} }},
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
