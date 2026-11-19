
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
			{"description", "Field:value patch. Names/shapes come from hex_inspect_component (the component's Serialize() output). Unknown names, wrong types/shapes and unknown enum names are rejected before anything applies. Enum fields take value names, flag fields arrays of names (see hex_describe_component_type). Object values merge recursively; scalars/arrays replace."} };
	}

	json TransformProps(json props)
	{
		props["position"] = NumArray(3, "[x, y, z] world position.");
		props["rotation"] = NumArray(4, "Quaternion [x, y, z, w]. Takes precedence over eulerDegrees.");
		props["eulerDegrees"] = NumArray(3, "[pitch, yaw, roll] in degrees.");
		props["scale"] = NumArray(3, "[x, y, z] scale.");
		return props;
	}

	// Material vocabulary shared by create_material / edit_material / mesh tools.
	json MaterialProps(json props)
	{
		props["diffuseColour"] = ColourSchema("Base colour [r,g,b] or [r,g,b,a], 0..1.");
		props["emissiveColour"] = ColourSchema("Emission [r,g,b] or [r,g,b,a]; values > 1 glow.");
		props["metallic"] = Num("0..1.");
		props["roughness"] = Num("0..1.");
		props["smoothness"] = Num("0..1.");
		props["transparent"] = Bool("Alpha-blended (uses diffuseColour alpha / the opacity texture).");
		props["isWater"] = Bool("Water shading.");
		props["affectsGI"] = Bool("Contributes to voxel GI (default true).");
		props["emissiveAffectsGI"] = Bool("Emission lights the GI (default true).");
		props["receivesSnow"] = Bool("Snow accumulates on it (up-facing ground).");
		props["cullMode"] = json{ {"type", "string"}, {"enum", json::array({ "back", "front", "none" })}, {"description", "\"none\" = two-sided."} };
		props["rainDripIntensity"] = Num("0..1 wet-droplet effect when it rains.");
		props["materialModel"] = Int("Shading model index (0 = standard).");
		props["modelParams"] = NumArray(4, "Shading-model parameters.");
		props["windSwayParams"] = NumArray(4, "Vegetation wind sway [bend, flutter, charHeight, mode].");
		props["cullDistance"] = Num("Stop drawing beyond this distance (0 = never).");
		props["textures"] = json{ {"type", "object"}, {"additionalProperties", json{ {"type", json::array({ "string", "null" })} }},
			{"description", "{slot: texture asset path or null to clear}. Slots: albedo, normal, roughness, metallic, height, emission, opacity, ambientOcclusion."} };
		return props;
	}

	// `material` on mesh-creating tools.
	json MeshMaterialSchema()
	{
		json obj = Obj(MaterialProps(json{
			{"path", Str("Base .hmat (default EngineData.Materials/Default.hmat). With no properties the entity uses this shared asset directly.")},
			{"saveAs", Str("Write the result as a NEW project .hmat and use it (otherwise property overrides make a private, unsaved copy).")},
			{"overwrite", Bool("Allow saveAs to replace an existing file.")},
		}));
		return json{ {"description", "Material: an asset path (ref like \"GameData.Materials/Rock.hmat\" or a Data-relative path), or an object with a base path and property overrides."},
			{"anyOf", json::array({ json{ {"type", "string"} }, obj })} };
	}

	// Ray filters shared by raycast / ground_height / snap_to_ground.
	json RayOptionProps(json props)
	{
		const json refs = json{ {"type", "array"}, {"items", EntityRefSchema()} };
		props["maxDistance"] = Num("Ray length (default 10000).");
		props["ignore"] = refs; props["ignore"]["description"] = "Entities (with their children) the ray passes through.";
		props["only"] = refs; props["only"]["description"] = "Hit ONLY these entities (and their children), e.g. the terrain.";
		props["minNormalY"] = Num("Pass through surfaces whose normal.y is below this (e.g. 0.6 = ignore walls/steep faces).");
		props["includeHidden"] = Bool("Also hit DoNotRender entities (default false).");
		props["meshes"] = Bool("Test mesh triangles (default true).");
		props["physics"] = Bool("Test physics colliders, e.g. terrain chunks without a mesh component (default true).");
		return props;
	}

	// Sent as the MCP server instructions: the condensed agent playbook
	// (full version: docs/MCP-agent-playbook.md).
	const char* const kAgentInstructions =
		"HexEngine editor bridge. Loop: observe (get_editor_status, list_scenes, get_environment, capture_view, validate_current_scene) -> act -> verify (capture_view, validate_current_scene groundCheck, get_recent_engine_logs sinceSeq).\n"
		"Speed: every call costs >= 1 editor frame; use hex_batch for multiple edits (one frame, one undo step; chain with \"as\" + {\"$ref\": \"label.id\"}). ground_height takes up to 20000 points per call.\n"
		"Conventions: right-handed, Y up, metres; camera yaw 0 looks along -Z; eulerDegrees [pitch, yaw, roll]. Entity names are unique (duplicates get suffixes) - use the returned ids. Assets are refs like GameData.Materials/X.hmat (list_assets).\n"
		"Levels: new_scene / open_scene (switching clears undo). set_environment for time of day, weather, sea level and look cvars. Water = a plane with a material based on EngineData.Materials/Water.hmat (check with ground_height that the basin is below it). Emissive materials light the world via GI - keep emission modest or set emissiveAffectsGI false.\n"
		"Geometry: create_primitive / create_mesh_entity are IN-MEMORY until saveAs or save_mesh_asset (needed to save, prefab or undo them). Share materials (create_material + assign_material) rather than per-object overrides.\n"
		"Placement: ground_height / raycast with only=[terrain] or minNormalY 0.7 to skip canopies and roofs; snap_to_ground; query_region to avoid overlaps. Reuse via create_prefab + instantiate_prefab; edit all copies in open_prefab_stage.\n"
		"Components: describe_component_type before adding (fields, enum names, ranges).\n"
		"Game logic, fastest: Lua scripts (no build). write_script Data path e.g. Scripts/Door.lua (returns compile errors at once), attach with add_component ScriptComponent {script: \"GameData.Scripts/Door.lua\", properties: {...}}, play. Edits hot-reload within 0.25 s even while playing; runtime errors are [Script] warnings in get_recent_engine_logs. API: docs/Scripting.md.\n"
		"Game C++: set_game_auto_reload false, write_game_source, build_game (errors come back with file/line), then play.\n"
		"Play-testing: play needs Build/Game.dll; stop restores the pre-play scene (play-time edits are discarded). set_simulation paused + step_simulation for exact ticks; send_input drives key binds like real input.\n"
		"Safety: undo reverts the last call/batch. While a prefab stage is open every tool edits the prefab. save_scene only when you mean to keep the work.\n"
		"Full guide: docs/MCP-agent-playbook.md.";

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
		const json validateProps = json{
			{"limit", Int("Max issues listed (default 200; counts always cover everything).")},
			{"minSeverity", json{ {"type", "string"}, {"enum", json::array({ "info", "warning", "error" })}, {"description", "Lowest severity to report (default info)."} }},
		};
		v.push_back({ "find_missing_references", "Asset-reference check only: meshes/materials whose files are missing, and in-memory (unsaved) meshes/materials on entities that save with the scene.", Obj(validateProps) });
		json validateAll = validateProps;
		validateAll["killY"] = Num("World height below which an entity counts as fallen out of the world (default -500).");
		validateAll["groundCheck"] = Bool("Also flag top-level mesh entities floating above the surface below them (raycasts; default false).");
		validateAll["floatTolerance"] = Num("groundCheck: gap that counts as floating (default 0.25).");
		v.push_back({ "validate_current_scene", "Check the scene for problems an agent should fix: NaN/zero-scale transforms, entities fallen out of the world, missing or unsaved mesh/material assets, double-spawned duplicates, out-of-date prefab instances, scripts that are missing or don't compile, no camera / lights / PlayerStart, and (groundCheck) floating objects. Issues come sorted error > warning > info, with codes and the entity.", Obj(validateAll) });
		v.push_back({ "get_recent_engine_logs", "Recent engine log lines with sequence numbers. Pass the previous lastSeq as sinceSeq to get only NEW lines.",
			Obj(json{ {"max", Int("Max lines (<= 1000, default 200).")}, {"sinceSeq", Int("Only lines with seq greater than this.")},
				{"minLevel", json{ {"type", "string"}, {"enum", json::array({ "debug", "info", "warn", "crit" })}, {"description", "Minimum level."} }} }) });
		v.push_back({ "list_projects", "The editor's recent projects (path/name/exists).", Obj(json::object()) });
		v.push_back({ "describe_component_type", "Catalogue entry for a component type WITHOUT adding it: every serialised field with its type, default value and (where known) enum/flag names, range, units and description. Use before add_component / create_entity to know what to set.",
			Obj(json{ {"component", Str("Registered component class name (see hex_list_component_types).")} }, { "component" }) });
		v.push_back({ "list_cvars", "List console variables (render / GI / fog / post / weather / gameplay settings; saved with the scene) with type, value, min, max and description. Filter matches name or description, case-insensitive.",
			Obj(json{ {"filter", Str("Substring of the name or description, e.g. \"fog\" or \"r_gi\".")}, {"offset", Int("Skip this many matches.")},
				{"limit", Int("Max results (<= 2000, default 200).")}, {"details", Bool("Include description/min/max (default true).")} }) });
		v.push_back({ "get_cvars", "Read specific console variables (typed values + range).",
			Obj(json{ {"names", json{ {"type", "array"}, {"items", json{ {"type", "string"} }}, {"minItems", 1}, {"description", "Exact cvar names."} }} }, { "names" }) });
		v.push_back({ "list_console_commands", "List console commands (run them with hex_exec_console).",
			Obj(json{ {"filter", Str("Name substring.")} }) });
		v.push_back({ "list_prefabs", "List prefab assets (.hprefab) in the open project. Paths are project-relative.",
			Obj(json{ {"subdir", Str("Folder under the project's Data folder to search (default: all of Data).")} }) });
		v.push_back({ "list_assets", "List project assets by type (mesh .hmesh, material .hmat, prefab, scene, texture). Each result's ref (e.g. \"GameData.Materials/Rock.hmat\") is what scenes store and what every asset parameter accepts.",
			Obj(json{ {"type", json{ {"type", "string"}, {"enum", json::array({ "all", "mesh", "material", "prefab", "scene", "texture" })}, {"description", "Asset type (default all)."} }},
				{"subdir", Str("Folder under Data to search (default all of Data).")}, {"filter", Str("Case-insensitive substring of the path.")},
				{"limit", Int("Max results (default 500, max 5000).")}, {"includeEngine", Bool("Also list engine assets (EngineData...), e.g. its materials (default false).")} }) });
		v.push_back({ "get_material", "Read a material's properties and texture slots, by asset path or by the entity whose mesh uses it.",
			Obj(json{ {"path", Str("Material asset path/ref.")}, {"entity", EntityRefSchema("Or: read the material this entity's StaticMeshComponent uses.")} }) });
		v.push_back({ "raycast", "Cast a ray into the open scene: exact mesh triangles + physics colliders. Returns hit position, surface normal, distance and the entity hit.",
			Obj(RayOptionProps(json{ {"origin", NumArray(3, "Ray start [x,y,z].")}, {"direction", NumArray(3, "Ray direction (any length).")},
				{"target", NumArray(3, "Or: a point to cast toward (maxDistance defaults to its distance).")} }), { "origin" }) });
		v.push_back({ "ground_height", "Surface height under one or many [x, z] points (rays straight down from fromY): for placing/scattering objects. Use only / ignore / minNormalY to skip canopies, roofs and walls.",
			Obj(RayOptionProps(json{ {"points", json{ {"type", "array"}, {"items", NumArray(2, "[x, z]")}, {"description", "Up to 20000 [x, z] points."} }},
				{"xz", NumArray(2, "Or: a single [x, z].")}, {"fromY", Num("Height the rays start at (default 2000).")} })) });
		v.push_back({ "find_path", "Query a walking path on the scene navmesh (what routine agents use). reaches=false or a large endGap means the target is off the navmesh or unreachable (doors, blockers, gaps). Use it to check that building entry waypoints are reachable from the street.",
			Obj(json{ {"from", NumArray(3, "Start [x,y,z].")}, {"fromEntity", Str("Or: start at this entity's position.")},
				{"to", NumArray(3, "End [x,y,z].")}, {"toEntity", Str("Or: end at this entity's position.")},
				{"tolerance", Num("endGap counted as reaching (default 1.5 m).")},
				{"searchDistance", Num("Horizontal snap distance for the endpoints (default 50).")},
				{"returnPoints", Bool("Include the path points (default true).")} }) });
		v.push_back({ "query_region", "Entities whose world bounds intersect a sphere (center + radius) or box (min + max), nearest first, with their bounds. For 'what is here?' and free-space checks.",
			Obj(json{ {"center", NumArray(3, "Sphere centre.")}, {"radius", Num("Sphere radius (default 10).")},
				{"min", NumArray(3, "Box min corner.")}, {"max", NumArray(3, "Box max corner.")},
				{"component", Str("Only entities with this component type.")}, {"filter", Str("Name substring.")},
				{"rootsOnly", Bool("Only top-level entities.")}, {"limit", Int("Max results (default 200).")} }) });
		v.push_back({ "capture_view", "LOOK at the scene: returns a downscaled image of the editor camera's rendered frame INLINE. Optionally from another viewpoint (position + lookAt / eulerDegrees, or frameEntity to fit an entity in view); the editor camera is moved for the capture and put back afterwards.",
			Obj(json{ {"position", NumArray(3, "Camera position for the shot.")}, {"lookAt", NumArray(3, "Point to look at.")},
				{"eulerDegrees", NumArray(3, "[pitch, yaw, roll]: pitch + = up, yaw 0 faces -Z, yaw + turns toward -X.")},
				{"frameEntity", EntityRefSchema("Fit this entity (and its children) in view; direction from eulerDegrees or a 25-degree look-down at the current heading.")},
				{"distanceScale", Num("frameEntity: distance multiplier (default 1.15).")},
				{"maxSize", Int("Longest image side in pixels (64..2048, default 768).")},
				{"format", json{ {"type", "string"}, {"enum", json::array({ "jpeg", "png" })}, {"description", "Default jpeg."} }},
				{"quality", Int("JPEG quality 10..100 (default 85).")},
				{"waitFrames", Int("Frames to render before grabbing (default 8 after a camera move so temporal effects settle, else 2).")},
				{"restore", Bool("Put the camera back afterwards (default true).")} }) });
		v.push_back({ "list_scripts", "Gameplay scripts (Lua, see docs/Scripting.md) in the project's Data folder: ref (what ScriptComponent.script takes), size, and which entities in the loaded scenes use each one. check: true also compiles each and reports errors.",
			Obj(json{ {"check", Bool("Compile every script and report errors (default false).")} }) });
		v.push_back({ "read_script", "Read a script file. path: a ref (\"GameData.Scripts/Door.lua\") or a Data-relative path (\"Scripts/Door.lua\").",
			Obj(json{ {"path", Str("Script ref or Data-relative path.")} }, { "path" }) });
		v.push_back({ "check_script", "Compile a script without running it: {compiles, error: \"file:line: message\"}.",
			Obj(json{ {"path", Str("Script ref or Data-relative path.")} }, { "path" }) });
		v.push_back({ "list_game_sources", "The project's C++ game code (Code/ folder): source/header files, the .vcxproj/.sln, whether the game DLL is loaded and whether the editor auto-rebuilds on changes.", Obj(json::object()) });
		v.push_back({ "read_game_source", "Read a game source file (path relative to the project's Code folder).",
			Obj(json{ {"path", Str("e.g. \"Wickwood/Wickwood.cpp\".")} }, { "path" }) });
		v.push_back({ "get_build_status", "State of the latest build_game: running / succeeded / failed, duration, compiler errors (file, line, column, code, message), warnings, whether the new DLL was reloaded.",
			Obj(json{ {"waitSeconds", Int("Wait up to this long (0..110) for a running build to finish (default 0).")} }) });
		v.push_back({ "list_scenes", "The project's scenes (.hscene): name, path, whether loaded, which one is active (the one every tool edits), entity count. Includes scene files in Data/ the project hasn't loaded.", Obj(json::object()) });
		v.push_back({ "get_environment", "The active scene's environment in one view: time of day (DayNightCycleComponent), weather preset (WeatherControllerComponent), sun direction, sea level, ocean look and the look-defining cvars (env_*, fog, clouds, sky, exposure, moon/stars, ocean).", Obj(json::object()) });
		v.push_back({ "prefab_info", "Is this entity a prefab instance? Source prefab, instance root, out-of-date state and the list of overridden properties.",
			Obj(json{ {"entity", EntityRefSchema()} }, { "entity" }) });

		// --- write ---
		v.push_back({ "open_project", w("Open a project at the project browser. Async: poll hex_get_editor_status until sceneName is set. Not batchable."),
			Obj(json{ {"path", Str("Project .json path.")}, {"name", Str("Project name (stem from hex_list_projects).")} }) });
		v.push_back({ "exec_console", w("Run an editor console command, e.g. \"ed_gizmoMode 1\". Prefer hex_set_cvars for variables (typed, validated, returns the clamped result)."),
			Obj(json{ {"command", Str("Console command line.")} }, { "command" }) });
		v.push_back({ "set_cvars", w("Set console variables with typed values: bools true/false, numbers, vec3 [x,y,z]. All names and types are validated before anything changes; each var clamps to its [min, max] and the result reports before/value (and requested+clamped when clamped)."),
			Obj(json{ {"values", json{ {"type", "object"}, {"additionalProperties", true}, {"description", "{\"cvar_name\": value, ...}"} }} }, { "values" }) });
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
		v.push_back({ "set_component_field", w("Patch a component's serialised fields (read-modify-write through Serialize/Deserialize). Values must keep the field's type/shape; enum fields accept value names (e.g. \"_bodyType\": \"Dynamic\"), flag fields an array of names. Object fields merge (JSON merge-patch): a null member removes that key, e.g. swap a mesh with {\"mesh\": {\"<old path>\": null, \"<new path>\": {\"materials\": [...]}}}."),
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
		const char* SAVEAS = "Also write the mesh as a project .hmesh (e.g. \"Meshes/Crate.hmesh\") so it saves with the scene, works in prefabs and is undoable. A private material is written beside it (or at materialPath).";
		v.push_back({ "create_primitive", w("Create a mesh entity from a built-in shape, centred on its origin (plane at y=0 facing +Y): box/cube, plane/quad, sphere, capsule, cylinder, cone, torus. Smooth normals, UVs and tangents included. Without saveAs the mesh is in-memory (DoNotSave, not undoable) until hex_save_mesh_asset."),
			Obj(TransformProps(json{
				{"name", Str("Entity name.")},
				{"shape", json{ {"type", "string"}, {"enum", json::array({ "box", "cube", "plane", "quad", "sphere", "capsule", "cylinder", "cone", "torus" })} }},
				{"size", json{ {"description", "box: [x,y,z] or one number; plane: [x,z] or one number (default 1)."}, {"anyOf", json::array({ json{ {"type", "number"} }, json{ {"type", "array"}, {"items", json{ {"type", "number"} }} } })} }},
				{"radius", Num("sphere/capsule/cylinder/cone radius; torus ring radius (default 0.5).")},
				{"height", Num("cylinder/cone height (default 1); capsule TOTAL height incl. caps, >= 2*radius (default 2).")},
				{"tubeRadius", Num("torus tube radius (default 0.15).")},
				{"segments", Int("Around the Y axis (default 32; torus 48).")},
				{"rings", Int("sphere/capsule latitude rings (default 16).")},
				{"sides", Int("torus tube sides (default 24).")},
				{"subdivisions", Int("plane grid subdivisions per side (default 1).")},
				{"caps", Bool("cylinder/cone end caps (default true).")},
				{"uvTiling", NumArray(2, "Multiply UVs by [u, v].")},
				{"parent", EntityRefSchema("Optional parent entity.")},
				{"material", MeshMaterialSchema()},
				{"saveAs", Str(SAVEAS)},
				{"materialPath", Str("Where saveAs writes a private material (default next to the mesh).")},
				{"overwrite", Bool("Allow saveAs to replace existing files.")},
				{"persistent", Bool("Save the entity with the scene even WITHOUT a mesh asset (rarely useful: the mesh reference would be empty).")},
			}), { "name", "shape" }) });
		v.push_back({ "save_mesh_asset", w("Write an entity's bridge-built mesh (create_primitive / create_mesh_entity) as a project .hmesh - plus its private material as a .hmat - so it saves with the scene, can go into prefabs and be reused. Clears DoNotSave."),
			Obj(json{ {"entity", EntityRefSchema()},
				{"path", Str("Output .hmesh (default \"Meshes/<entity name>.hmesh\").")},
				{"materialPath", Str("Where a private material is written (default next to the mesh).")},
				{"overwrite", Bool("Allow replacing existing files (default false).")} }, { "entity" }) });
		v.push_back({ "build_navmesh", w("Bake the open scene's walking navmesh (the editor NavMesh tool's Build Scene NavMesh). Live at once for find_path and routine agents; save_scene writes the sidecar. Rebake after placing buildings. Takes seconds on a big scene."),
			Obj(json{ {"cellSize", Num("Voxel size in metres (default 0.3).")}, {"cellHeight", Num("Voxel height (default 0.3).")},
				{"walkableRadius", Num("Agent radius in voxels (default 1).")}, {"walkableHeight", Num("Agent height in voxels (default 7).")},
				{"walkableClimb", Num("Step height in voxels (default 2).")}, {"walkableSlopeAngle", Num("Max slope degrees (default 45).")},
				{"min", NumArray(3, "Bake region min corner (world). Use with max to bake just the playable area at finer cell sizes.")}, {"max", NumArray(3, "Bake region max corner (world).")} }) });
		v.push_back({ "snap_to_ground", w("Drop an entity (with its children) so its bounds' bottom rests on the surface below it, optionally tilted to the surface normal. Undoable. Use only / minNormalY to choose what counts as ground."),
			Obj(RayOptionProps(json{ {"entity", EntityRefSchema()},
				{"offset", Num("Extra height above the surface (negative sinks it in).")},
				{"alignToNormal", Bool("Tilt the entity's up axis to the surface normal (keeps its heading).")},
				{"castHeight", Num("Start the ray this far above the entity's top (default 2).")} }), { "entity" }) });
		v.push_back({ "create_material", w("Create a project material (.hmat) from a base material plus property/texture overrides. Returns its ref."),
			Obj(MaterialProps(json{
				{"path", Str("Output path, e.g. \"Materials/Rock.hmat\".")},
				{"base", Str("Material to start from (default EngineData.Materials/Default.hmat).")},
				{"overwrite", Bool("Allow replacing an existing file.")},
			}), { "path" }) });
		v.push_back({ "edit_material", w("Change a project material's properties/textures and save it (every entity using it changes; not part of undo). With entity: edits the material that entity uses (an engine material is refused - copy it with create_material first)."),
			Obj(MaterialProps(json{
				{"path", Str("Material asset path/ref.")},
				{"entity", EntityRefSchema("Or: the entity whose material to edit.")},
				{"save", Bool("Write the .hmat (default true).")},
			})) });
		v.push_back({ "retarget_animations", w("Bake a rigged SOURCE mesh's animation clips onto other skinned TARGET meshes with a different skeleton (preset mixamo_to_synty: Mixamo rig -> Synty POLYGON 36-bone rig). Per mapped bone the target turns away from its own bind pose as the source turned from its; the root also follows the source root translation scaled by the root-height ratio. Each target's clips are REPLACED (same order as the source, so clip indices match) and the .hmesh is saved. Targets must be skinned animated meshes - import character packs with importAnimations so their skin is kept. Not batchable."),
			Obj(json{ {"source", Str("Rigged source mesh with clips, e.g. GameData.Meshes/Universe/Characters/casual_male_01/casual_male_01_Cylinder501.hmesh.")},
				{"targets", json{ {"type", "array"}, {"items", json{ {"type", "string"} }}, {"description", "Skinned target .hmesh refs."} }},
				{"preset", Str("Bone map preset (default mixamo_to_synty).")},
				{"clips", json{ {"type", "array"}, {"items", json{ {"type", "integer"} }}, {"description", "Source clip indices (default all)."} }},
				{"sampleRate", Num("Bake rate in samples per second (default 30).")},
				{"save", Bool("Save each target .hmesh (default true). false only edits the LOADED mesh: a target no entity references is unloaded straight after and reloads from disk without the new clips.")} }, { "source", "targets" }) });
		v.push_back({ "recenter_mesh", w("Recenter a STATIC mesh asset so its bounds centre is the model origin (fixes kit meshes whose geometry sits far from the pivot). Updates the live mesh in place and saves the .hmesh; every entity in the open scene using it is moved by rotation*scale*offset (its children counter-shifted) so nothing moves in the world, and its bounds / PVS / triangle-mesh or box collider are refreshed. Prefabs in Data/Prefabs that use the mesh get the same compensation. Other scenes referencing it are listed, not edited. Animated meshes are refused. Save the scene afterwards. Not batchable."),
			Obj(json{ {"mesh", Str("Mesh ref or path, e.g. \"GameData.Meshes/Universe/Buildings/City/X.hmesh\".")},
				{"dryRun", Bool("Only report the offset and the scene entities using the mesh (default false).")},
				{"compensateScene", Bool("Move the open scene's users so they stay put (default true).")},
				{"updatePrefabs", Bool("Apply the same compensation inside Data/Prefabs/*.hprefab (default true).")} }, { "mesh" }) });
		v.push_back({ "assign_material", w("Give an entity's StaticMeshComponent a material asset. Undoable; tracked as a prefab override on instances."),
			Obj(json{ {"entity", EntityRefSchema()}, {"material", Str("Material asset path/ref (see hex_list_assets type=material).")} }, { "entity", "material" }) });
		v.push_back({ "create_mesh_entity", w("Create an entity with a procedural mesh from raw vertices/indices. Front faces are COUNTER-clockwise seen from outside. Normals are computed if omitted; tangents come from the UVs. In-memory until saveAs / hex_save_mesh_asset."),
			Obj(TransformProps(json{
				{"name", Str("Entity name.")},
				{"vertices", json{ {"type", "array"}, {"description", "[[x,y,z],...] or flat [x,y,z,...] (local space)."} }},
				{"indices", json{ {"type", "array"}, {"items", json{ {"type", "integer"} }}, {"description", "Flat triangle list."} }},
				{"normals", json{ {"type", "array"}, {"description", "Optional, same layout/count as vertices."} }},
				{"uvs", json{ {"type", "array"}, {"description", "Optional [[u,v],...] or flat."} }},
				{"parent", EntityRefSchema("Optional parent entity.")},
				{"persistent", Bool("Save the entity with the scene even WITHOUT a mesh asset (rarely useful).")},
				{"material", MeshMaterialSchema()},
				{"saveAs", Str(SAVEAS)},
				{"materialPath", Str("Where saveAs writes a private material (default next to the mesh).")},
				{"overwrite", Bool("Allow saveAs to replace existing files.")},
			}), { "name", "vertices", "indices" }) });
		v.push_back({ "undo", w("Undo the last editor history steps (shared with the editor's Ctrl+Z). Every bridge call or batch that edits entities is ONE step. Not batchable."),
			Obj(json{ {"steps", Int("How many steps (default 1, max 100).")} }) });
		v.push_back({ "redo", w("Redo undone steps. Not batchable."),
			Obj(json{ {"steps", Int("How many steps (default 1, max 100).")} }) });
		v.push_back({ "save_scene", w("Save everything like the editor's Save action: project file, every open scene (cvars included) and the prefab stage if open. DoNotSave entities are skipped. Not batchable."),
			Obj(json::object()) });
		v.push_back({ "instantiate_prefab", w("Spawn a prefab instance (linked to its asset, like drag & drop). The first root is moved to position; rotation/scale apply to the roots."),
			Obj(TransformProps(json{
				{"path", Str("Prefab path from hex_list_prefabs (project-relative, e.g. \"Data/Prefabs/Tree.hprefab\").")},
				{"name", Str("Optional name for the (single) root.")},
				{"parent", EntityRefSchema("Optional parent entity.")},
				{"persistent", Bool("Save with the scene (default true).")},
			}), { "path" }) });
		v.push_back({ "create_prefab", w("Save an entity and its whole hierarchy as a NEW prefab asset. By default the original is replaced by a linked instance of the new prefab (same parent/transform/name). Not batchable."),
			Obj(json{ {"entity", EntityRefSchema()},
				{"path", Str("Output path, project-relative (default \"Data/Prefabs/<entity name>.hprefab\").")},
				{"replaceWithInstance", Bool("Swap the original for a linked instance (default true).")},
				{"overwrite", Bool("Allow replacing an existing prefab file (default false).")} }, { "entity" }) });
		v.push_back({ "apply_prefab_instance", w("Write a prefab instance's changes back into its prefab asset (other instances follow the editor's sync policy). Not batchable."),
			Obj(json{ {"entity", EntityRefSchema("Prefab instance root.")} }, { "entity" }) });
		v.push_back({ "revert_prefab_instance", w("Discard an instance's overrides and rebuild it from its prefab. Returns the rebuilt root. Not batchable."),
			Obj(json{ {"entity", EntityRefSchema("Prefab instance root.")} }, { "entity" }) });
		v.push_back({ "update_prefab_instance", w("Pull the latest prefab asset into an out-of-date instance. Not batchable."),
			Obj(json{ {"entity", EntityRefSchema("Prefab instance root.")} }, { "entity" }) });
		v.push_back({ "open_prefab_stage", w("Open a prefab for EDITING: its own scene becomes the active scene, so every entity/component tool (create, add_component, set_component_field, delete, batch...) now authors the prefab. Finish with save_prefab_stage / close_prefab_stage. Not batchable."),
			Obj(json{ {"path", Str("Prefab path (project-relative).")} }, { "path" }) });
		v.push_back({ "save_prefab_stage", w("Save the open prefab stage to its asset. Not batchable."), Obj(json::object()) });
		v.push_back({ "close_prefab_stage", w("Close the prefab stage and return to the level scene. Not batchable."),
			Obj(json{ {"save", Bool("true = save changes first, false = discard.")} }, { "save" }) });
		v.push_back({ "write_script", w("Write (create or replace) a Lua gameplay script under the project's Data folder and compile it at once: the result has compiles + error (file:line). The engine hot-reloads it within ~0.25 s, also during play (running instances keep their state; a compile error keeps the previous version running). Attach a new script with add_component ScriptComponent {script: <ref>, properties: {...}}. Script format and API: docs/Scripting.md (the script returns a class table with Properties and OnStart/OnUpdate(dt)/OnKeyDown(key)/OnTriggerEnter(other)... callbacks)."),
			Obj(json{ {"path", Str("Data-relative path or ref, e.g. \"Scripts/Door.lua\" (.lua is added if missing).")}, {"content", Str("The whole script text.")},
				{"overwrite", Bool("Replace an existing file (default true).")} }, { "path", "content" }) });
		v.push_back({ "write_game_source", w("Write (create or replace) a C/C++ file in the project's Code folder. New files are added to the game's .vcxproj. Then build_game. While auto-reload is on (set_game_auto_reload) and the game is loaded, the editor rebuilds by itself ~0.4 s after a change."),
			Obj(json{ {"path", Str("Path relative to Code/, e.g. \"Wickwood/Enemy.cpp\".")}, {"content", Str("The whole file text.")},
				{"overwrite", Bool("Replace an existing file (default true).")}, {"addToProject", Bool("Add a NEW file to the owning .vcxproj (default true).")} }, { "path", "content" }) });
		v.push_back({ "set_game_auto_reload", w("Turn the editor's automatic rebuild + reload on Code/ changes on or off. Turn it off while editing several files, then build_game once."),
			Obj(json{ {"enabled", Bool("true = editor rebuilds on file changes (default behaviour).")} }, { "enabled" }) });
		v.push_back({ "build_game", w("Build the project's C++ game (MSBuild) into Build/Game.dll in the background - the editor keeps running - and by default hot-reload it (play mode restarts if it was running). Returns compiler errors with file/line. Waits up to waitSeconds; if still building, poll get_build_status. Not batchable."),
			Obj(json{ {"reload", Bool("Swap in the new DLL after a successful build (default true).")},
				{"waitSeconds", Int("Wait up to this long (0..110, default 100) before returning.")} }) });
		v.push_back({ "reload_game", w("Load the already-built Build/Game.dll into the editor (unload the old one first; play restarts if it was running). Not batchable."), Obj(json::object()) });
		v.push_back({ "new_scene", w("Create a new scene (Data/Scenes/<name>.hscene) with a MainCamera and, by default, a sun; add it to the project, save it and (by default) make it the active scene. Switching scenes clears the undo history. Not batchable."),
			Obj(json{ {"name", Str("Scene name (also the file name).")}, {"withSun", Bool("Add the default sun light (default true).")},
				{"activate", Bool("Switch the editor to it (default true).")} }, { "name" }) });
		v.push_back({ "open_scene", w("Switch the editor to another project scene, loading it if needed (by name, \"Scenes/X.hscene\" or \"Data/Scenes/X.hscene\"). Every tool then acts on it; the undo history is cleared. Not batchable."),
			Obj(json{ {"scene", Str("Scene name or path (see hex_list_scenes).")} }, { "scene" }) });
		v.push_back({ "spawn_mesh", w("Place an existing mesh ASSET (.hmesh, see hex_list_assets type=mesh) in the scene as an entity with a StaticMeshComponent, using the mesh's own material unless one is given. Undoable."),
			Obj(TransformProps(json{ {"mesh", Str("Mesh ref or path, e.g. \"GameData.Models/Tree/Trunk.hmesh\".")},
				{"name", Str("Entity name (default: the mesh's file name).")},
				{"material", Str("Optional material ref to use instead of the mesh's own.")},
				{"parent", EntityRefSchema("Optional parent entity.")} }), { "mesh" }) });
		v.push_back({ "import_model", w("Import a 3D model file (fbx, gltf/glb, obj, dae, 3ds) from anywhere on disk into the project: copies it (and the textures / glTF buffers / .mtl beside it) to Data/<destFolder>, converts it to .hmesh meshes (+ .hmat materials) and lists the new assets. spawn: true also places it. Not batchable."),
			Obj(json{ {"source", Str("Absolute path of the model file (or a project path).")},
				{"destFolder", Str("Folder under Data (default \"Models/<file name>\").")},
				{"createMaterials", Bool("Create .hmat materials from the model (default true).")},
				{"importAnimations", Bool("Import skeletal animations (default true).")},
				{"mergeByMaterial", Bool("Merge child meshes that share a material (default false).")},
				{"mergeSiblingAnimations", Bool("Append the animations of every other .fbx beside the model as named clips (Mixamo rig + one-file-per-clip folders). REQUIRED when re-importing such a rig, or its clips are dropped (default false).")},
				{"scale", Num("Import scale (default 1).")},
				{"copyTextures", Bool("Copy textures/buffers/.mtl next to the source too (default true).")},
				{"spawn", Bool("Place the imported meshes under one root entity (default false).")},
				{"name", Str("spawn: root entity name.")}, {"position", NumArray(3, "spawn: root position.")} }, { "source" }) });
		v.push_back({ "close_scene", w("Unload a project scene that is NOT active and remove it from the project (project file saved); deleteFile also deletes its .hscene. Not batchable."),
			Obj(json{ {"scene", Str("Scene name or path.")}, {"deleteFile", Bool("Also delete the .hscene file (default false).")} }, { "scene" }) });
		v.push_back({ "set_environment", w("Set the active scene's environment in one call: time of day (adds a DayNightCycleComponent to the sun if missing), weather preset (adds a WeatherControllerComponent if missing), sea level, ocean look and any look cvars. Component changes are undoable. Not batchable."),
			Obj(json{
				{"timeOfDay", Num("Hours 0..24.")}, {"freezeTime", Bool("Stop the day/night cycle at timeOfDay.")}, {"dayLengthSeconds", Num("Real seconds per in-game day.")},
				{"weather", json{ {"type", "string"}, {"enum", json::array({ "Clear", "Overcast", "Rain", "HeavyRain", "Storm", "Thunderstorm", "Snow", "Blizzard", "Hot", "Sandstorm", "Custom" })} }},
				{"weatherTransitionSeconds", Num("Blend time to the new weather.")},
				{"seaLevel", json{ {"type", json::array({ "number", "null" })}, {"description", "World height of the sea plane; null = auto-detect from water surfaces."} }},
				{"ocean", json{ {"type", "object"}, {"description", "shallowColour, deepColour ([r,g,b,a] 0..1), fresnelPow, shoreFadeStrength, fadeFactor, reflectionStrength, reflectionNearDistance, reflectionFarDistance."} }},
				{"cvars", json{ {"type", "object"}, {"additionalProperties", true}, {"description", "{cvar: value} e.g. r_fogDensity, r_cloudCoverage, r_exposure (see get_environment / list_cvars)."} }},
			}) });
		v.push_back({ "play", w("Enter play mode (the scene view's Run): loads the project's Build/Game.dll and starts the game. Refuses if the game DLL isn't built. hex_stop restores the scene to its pre-play state. Not batchable."),
			Obj(json{ {"waitFrames", Int("Frames to let the game run before returning (default 5).")} }) });
		v.push_back({ "stop", w("Leave play mode; the scene returns to exactly its pre-play state. Not batchable."), Obj(json::object()) });
		v.push_back({ "set_simulation", w("Play mode only: pause/resume gameplay and set a time scale (slow motion / fast forward). Paused = no physics or fixed ticks and zero gameplay delta time; rendering, UI and input keep running."),
			Obj(json{ {"paused", Bool("Pause (true) or resume (false) the game simulation.")}, {"timeScale", Num("Gameplay speed multiplier 0..100 (1 = real time).")} }) });
		v.push_back({ "step_simulation", w("Play mode only: pause (if not already) and advance the game by exactly N fixed simulation ticks (1/cl_simulationRate s each), then return. For deterministic observe-act loops. Not batchable."),
			Obj(json{ {"steps", Int("Fixed ticks to run (1..6000, default 1).")} }) });
		v.push_back({ "send_input", w("Feed keyboard/mouse input to the RUNNING game (play mode), as a timed sequence. Key binds (e.g. WASD) and input listeners receive it exactly like real input. Keys still held at the end are released. Not batchable."),
			Obj(json{
				{"events", json{ {"type", "array"}, {"minItems", 1}, {"maxItems", 2000}, {"description",
					"Ordered events: {type: keyDown|keyUp, key} | {type: hold, key, frames|ms} (down, wait, up) | {type: mouseMove, dx, dy} (relative look delta as the game receives it) | "
					"{type: mouseMoveTo, x, y} | {type: mouseDown|mouseUp|click, button: left|right|middle, x?, y?} | {type: wheel, delta} | {type: text, text} | {type: wait, frames|ms}. "
					"Keys: \"W\", \"Space\", \"Shift\", \"Ctrl\", \"Alt\", \"Enter\", \"Escape\", \"Tab\", \"Up\"..., \"F1\".., \"Numpad0\".. or a virtual-key code."},
					{"items", json{ {"type", "object"}, {"properties", json{ {"type", json{ {"type", "string"}, {"enum", json::array({ "keyDown", "keyUp", "hold", "mouseMove", "mouseMoveTo", "mouseDown", "mouseUp", "click", "wheel", "text", "wait" })} }} }}, {"required", json::array({ "type" })} }} }},
				{"release", Bool("Release keys/buttons still held at the end (default true).")},
				{"allowInEditMode", Bool("Also send while NOT playing (default false): the editor's own shortcuts react to it.")},
			}, { "events" }) });
		v.push_back({ "capture_frame", w("Save the main camera's rendered frame to an image file. Waits waitFrames (default 2) editor frames first so recent edits are visible. Not batchable."),
			Obj(json{ {"path", Str("Absolute output path (.png / .jpg / .dds).")}, {"waitFrames", Int("Frames to wait before capturing (default 2).")} }, { "path" }) });

		// --- batch ---
		v.push_back({ "batch", "FAST PATH: run many bridge methods (any of the above not marked \"Not batchable\", names WITHOUT the hex_ prefix) in ONE editor frame and ONE undo step. "
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
		// A bridge result carrying "__image" {mimeType, data(base64)} becomes
		// an MCP image block (the model sees the picture) + the rest as text.
		if (!isError && obj.is_object() && obj.contains("__image") && obj["__image"].is_object())
		{
			json rest = obj;
			const json img = rest["__image"];
			rest.erase("__image");
			return json{
				{"content", json::array({
					json{ {"type", "image"}, {"data", img.value("data", std::string())}, {"mimeType", img.value("mimeType", std::string("image/jpeg"))} },
					json{ {"type", "text"}, {"text", rest.dump()} },
				})},
				{"isError", false},
			};
		}
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
				{"serverInfo", { {"name","HexEngine.McpServer"}, {"version","0.6.0"} }},
				{"instructions", kAgentInstructions},
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
