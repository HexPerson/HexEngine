# HexEngine MCP Integration

HexEngine exposes live editor inspection **and authoring** to AI clients
(Claude, Codex, ChatGPT, etc.) through the **Model Context Protocol (MCP)**. The MCP
protocol lives in a **standalone external process** — it is never compiled into
the engine runtime or the shipped game launcher.

```
Claude / Codex / ChatGPT
        │  (MCP JSON-RPC over stdio)
        ▼
HexEngine.McpServer.exe          ← standalone tool, no engine link
        │  (local Windows named pipe)
        ▼
HexEngine.EditorBridgePlugin     ← editor-only plugin, off by default
        │
        ▼
Running HexEngine Editor / live scene / ECS / ResourceSystem
```

Inspection is always available once the bridge is enabled. **Mutation** (entity
/ component / transform edits, console commands, frame capture, opening a
project) additionally requires `HEXENGINE_EDITOR_BRIDGE_WRITE=1` in the editor's
environment. There is still no asset import, scene save or shell execution.
Anything not yet implementable returns a clear `NotImplemented` /
`NotAvailable` error rather than faking data.

---

## Components

### 1. `HexEngine.EditorBridgePlugin` (editor-only)

A normal HexEngine plugin (`IPlugin` + `IEditorToolPlugin`) that starts a local
**named-pipe** server exposing live inspection methods.

- **Editor-only + opt-in.** The bridge only starts from the editor's
  `OnCreateUI` hook (the shipped game never calls it) **and** only when opted in
  via `--enable-editor-bridge` on the editor command line or
  `HEXENGINE_EDITOR_BRIDGE=1` in the environment. Off by default — fail closed.
- **Pipe name:** `\\.\pipe\HexEngine.EditorBridge.<pid>`.
- **Discovery:** on start it writes a session file to
  `%TEMP%\HexEngine\EditorBridge\session-<pid>.json` (pid, pipe name, project,
  start time) and removes it on shutdown.
- **Connections:** persistent and concurrent. A client may send any number of
  newline-delimited requests over one connection (responses come back in
  order); up to 8 clients can be connected at once, each on its own owned
  thread (joined on unload — never detached). One-shot clients (connect, one
  line, disconnect) still work.
- **Threading:** engine work is **marshalled to the editor main thread** and
  drained once per editor frame (`OnEditorFrameTick`). A request the main
  thread hasn't *started* within 10 s is cancelled and is guaranteed **not** to
  run (`NotAvailable`, "safe to retry"); once started it always completes.
- **Coalesced PVS rebuild:** mutations mark the potentially-visible-set dirty and
  it is rebuilt once at the end of the frame, not once per edit.

### 2. `HexEngine.McpServer.exe` (standalone)

Speaks MCP JSON-RPC 2.0 over **stdio** (newline-delimited). It runs with or
without a live editor:

- **Static tools** inspect files under a project/repo root and always work.
- **Live tools** proxy to the editor bridge over **one persistent pipe
  connection** (no per-call discovery/connect; ~50 µs transport overhead per
  call). If the editor restarts, the next call re-discovers and reconnects. A
  request whose write succeeded is never resent, so a timeout can't duplicate a
  mutation. With no editor running they return `EditorNotConnected`.
- Results are returned as **compact** JSON (the bridge `result` object, or
  `{"error": {...}}`) to keep agent token cost down. Every tool has a typed
  input schema.

Run it pointed at your repo/project root:

```
HexEngine.McpServer.exe --root C:\HexEngine
```

(Root can also be set with `HEXENGINE_MCP_ROOT`.) An MCP client is configured to
launch this command as an stdio MCP server. Example client config:

```json
{
  "mcpServers": {
    "hexengine": {
      "command": "C:\\HexEngine\\Bin\\x64\\Debug\\HexEngine.McpServer.exe",
      "args": ["--root", "C:\\HexEngine"]
    }
  }
}
```

The repo root ships a `.mcp.json` doing exactly this for Claude Code (override
the checkout location with `HEXENGINE_ROOT`).

---

## Tools

### Static (no editor required)

| Tool | Description |
| --- | --- |
| `hex_validate_nlohmann_staging` | Verify the full `Include/nlohmann` tree is staged. |
| `hex_parse_msbuild_log` | Parse an MSBuild/cl.exe log into error/warning diagnostics. |
| `hex_read_build_log` | Read a build/text log (capped). |
| `hex_validate_dependency_layout` | Validate `build/dependencies.lock.json` (refs + cloned dirs). |
| `hex_verify_package_manifest` | Verify a `.pkg` against its `.hashmanifest` (SHA-256). |
| `hex_list_project_files` | List files under a subdir (capped, optional ext filter). |
| `hex_list_asset_files` | List HexEngine asset files under a subdir. |
| `hex_list_editor_sessions` | List discovered running editor bridge sessions. |

### Live (require a running editor with the bridge enabled)

Read (always available):

| Tool | Notes |
| --- | --- |
| `hex_get_editor_status` | Scene, edit/play mode, project, protocol version, `writeEnabled`, frame counter. |
| `hex_get_open_project` / `hex_get_open_scene` | Project paths; scene name + entity count. |
| `hex_get_selected_entity` | Selected entity with full detail. |
| `hex_list_entities` | Paginated; filter by name substring and/or `component`. |
| `hex_inspect_entity` | id, name, parent, children, components, AABB. |
| `hex_list_components` | Component types present in the scene. |
| `hex_list_component_types` | Every registered component class (what `add_component` accepts). |
| `hex_inspect_component` | The component's `Serialize()` fields = the exact names `set_component_field` accepts, plus `fieldInfo` (enum names, ranges, units) where known. |
| `hex_describe_component_type` | Catalogue entry for a type **without adding it**: every field with type, default, and enum/flag names, range, units, description. Defaults come from a throwaway instance built in a private unregistered scene (never the user scene) and are cached. |
| `hex_list_cvars` / `hex_get_cvars` | Console variables (render, GI, fog, post, weather, gameplay; saved with the scene) with typed value, min, max and description. Filter matches name or description. |
| `hex_list_console_commands` | Console command names (run them with `exec_console`). |
| `hex_list_prefabs` | Prefab assets in the open project (project-relative paths). |
| `hex_prefab_info` | Instance link, root, out-of-date state and overridden properties of an entity. |
| `hex_list_assets` | Project assets by `type` (mesh, material, prefab, scene, texture), `filter`, `subdir`; `includeEngine` adds EngineData. Returns each asset's `ref`. |
| `hex_get_material` | A material's properties and texture slots, by `path` or by the `entity` using it. |
| `hex_capture_view` | **Vision.** The camera's rendered frame, downscaled (`maxSize`, default 768) and returned **inline** as an MCP image. Optional viewpoint: `position` + `lookAt` / `eulerDegrees`, or `frameEntity` to fit an entity in view. The editor camera is moved for the shot and restored; `waitFrames` (default 8 after a move) lets temporal effects settle. Readback is one main-thread hop; downscale + JPEG/PNG encode run off the main thread. |
| `hex_raycast` | The engine raycast (`PhysUtils::RayCast` on the edited scene): exact nearest triangle via a cached per-mesh BVH (visible surfaces, not RigidBody colliders) plus physics-scene colliders (terrain chunks without a mesh). Position, front-face normal, distance, entity, `source`. Filters: `ignore`, `only`, `minNormalY`, `includeHidden`. |
| `hex_ground_height` | Surface height under up to 20000 `[x, z]` points (rays down from `fromY`), same filters — for scattering and placement. |
| `hex_query_region` | Entities whose world bounds touch a sphere or box, nearest first, with their bounds. |
| `hex_get_recent_engine_logs` | Lines carry a `seq`; pass the previous `lastSeq` as `sinceSeq` for only new lines; `minLevel` filter. |
| `hex_validate_current_scene` | Problems to fix, sorted error > warning > info with a `code` and the entity: NaN / zero-scale transforms, entities below `killY`, missing or unsaved (in-memory) mesh/material assets on saved entities, double-spawned duplicates (same mesh, position, rotation), out-of-date prefab instances, no camera / lights / PlayerStart, and with `groundCheck` floating top-level meshes. |
| `hex_find_missing_references` | The asset-reference subset of the above. |
| `hex_list_game_sources` / `hex_read_game_source` | The project's C++ game code under `Code/` (sources, headers, `.vcxproj`/`.sln`), whether the game DLL is loaded and whether auto-reload is on. |
| `hex_get_build_status` | Latest `build_game`: running / succeeded / failed, duration, compiler errors (file relative to `Code/`, line, column, code, message), warnings, reload result. `waitSeconds` blocks until it finishes. |
| `hex_list_loaded_resources`, `hex_list_projects` | |

Write (need `HEXENGINE_EDITOR_BRIDGE_WRITE=1`):

| Tool | Notes |
| --- | --- |
| `hex_create_entity` | Name, transform, parent, `persistent`, and inline `components: [{component, fields}]` — a fully configured entity in one call. |
| `hex_add_component` / `hex_remove_component` | By registered class name; `add_component` takes optional initial `fields`. One component per type per entity. |
| `hex_set_component_field` | Read-modify-write through Serialize/Deserialize. Unknown keys, wrong types/shapes (vec3 stays length 3, ints stay integral) and unknown enum names are rejected before anything applies. Enum fields accept value names (`"_bodyType": "Dynamic"`), flag fields arrays of names. Object values merge recursively. |
| `hex_set_cvars` | `{values: {name: value}}` with typed values; every name/type validated before anything changes; reports before/value and `clamped` + `requested` when the var's range clamped it. |
| `hex_set_entity_transform` | position / rotation quat / eulerDegrees / scale. Cameras are driven through their own angles. |
| `hex_rename_entity`, `hex_set_parent`, `hex_set_entity_flags`, `hex_delete_entity`, `hex_clone_entity`, `hex_select_entity` | |
| `hex_create_primitive` | box/cube, plane/quad, sphere, capsule, cylinder, cone, torus with size/radius/height/segments, normals, UVs (`uvTiling`) and UV-derived tangents. Same `material` / `saveAs` options as below. |
| `hex_create_mesh_entity` | Mesh from raw vertices/indices (normals computed if omitted, tangents from UVs). |
| `hex_save_mesh_asset` | Write a bridge-built mesh as a project `.hmesh` (and its private material as a `.hmat`); the entity becomes an ordinary asset-backed mesh that saves with the scene and works in prefabs. Clears DoNotSave. |
| `hex_create_material` | New project `.hmat` from a `base` plus property/texture overrides. |
| `hex_edit_material` | Change a project material (every user updates) and save it; by `path` or the `entity` using it. Engine materials are refused. Not part of undo. |
| `hex_snap_to_ground` | Drop an entity hierarchy so its bounds rest on the surface below (optionally tilted to the normal, `offset`); undoable. |
| `hex_assign_material` | Set an entity's mesh material to an asset; undoable and tracked as a prefab override. |
| `hex_exec_console` | Console command / cvar set. |
| `hex_capture_frame` | Waits `waitFrames` (default 2) so recent edits are rendered, then saves the main camera image. |
| `hex_open_project` | Async; poll `hex_get_editor_status`. |
| `hex_undo` / `hex_redo` | The editor's own history. Every bridge call or batch that edits entities is **one** step. |
| `hex_save_scene` | The editor's Save action: project file, every open scene, the prefab stage if open. |
| `hex_instantiate_prefab` | Spawn a linked instance (like drag & drop) with position/rotation/scale/parent/name. |
| `hex_create_prefab` | Save an entity hierarchy as a new `.hprefab`; by default the original is replaced by a linked instance. |
| `hex_apply_prefab_instance` / `hex_revert_prefab_instance` / `hex_update_prefab_instance` | Instance ↔ asset sync, same as the inspector buttons. |
| `hex_open_prefab_stage` / `hex_save_prefab_stage` / `hex_close_prefab_stage` | Prefab authoring: while the stage is open, every entity/component tool edits the prefab. |
| `hex_write_game_source` | Create/replace a C/C++ file under `Code/` (never build-output folders); new files are added to the owning `.vcxproj`. |
| `hex_set_game_auto_reload` | The editor's own watcher rebuilds + reloads ~0.4 s after any `Code/` change (blocking the editor while MSBuild runs). Agents editing several files should turn it off and call `build_game` once. |
| `hex_build_game` | MSBuild of the game project on a background thread (the editor keeps rendering), exclusive with the watcher's reloads; then, by default, swaps in the new DLL on the main thread (play restarts if it was running). Returns parsed compiler errors; polls via `get_build_status` past `waitSeconds`. |
| `hex_reload_game` | Load the already-built `Build/Game.dll`. |
| `hex_play` / `hex_stop` | The scene view's Run / Stop. Play needs the project's built `Build/Game.dll` (it refuses rather than starting a build); stop restores the scene to its pre-play state. |
| `hex_set_simulation` | Play mode: `paused`, `timeScale` (0..100). Paused = no fixed ticks / physics and zero gameplay delta time; rendering, UI and input keep running. Resets when play stops, so edit mode is never frozen. |
| `hex_step_simulation` | Play mode: pause and advance exactly N fixed ticks (1/`cl_simulationRate` s each), then return. |
| `hex_send_input` | Timed keyboard/mouse sequence for the running game (`keyDown`/`keyUp`/`hold`/`mouseMove`/`mouseMoveTo`/`mouseDown`/`mouseUp`/`click`/`wheel`/`text`/`wait`). Goes through `InputSystem`, so key binds (WASD) and listeners see it like real input; held keys are released at the end. Refused outside play mode unless `allowInEditMode`. |
| `hex_batch` | See below. |

### Undo and prefab overrides

Mutating bridge methods record into the editor's undo history through
`IEditorContext` (the same `Record*` path the inspector and gizmos use), so a
user can Ctrl+Z an agent's work and — because recording also drives prefab
override tracking — edits to prefab instances show up as overrides and survive
instance sync. Each request (single call or whole `batch`) is grouped into one
undo step. A mesh built by `create_primitive` / `create_mesh_entity` is only
recorded when it is backed by a `.hmesh` (`saveAs`): an in-memory mesh can't be
rebuilt by redo. Material asset edits (`edit_material`) are not part of undo.

### Meshes, materials and asset references

Assets are referenced the way scenes store them: `<FileSystem>.<path under its
Data folder>`, e.g. `GameData.Materials/Rock.hmat` or
`EngineData.Materials/Default.hmat` (`hex_list_assets` returns these as `ref`).
Every asset parameter also accepts a project path (absolute, `Data/...`, or
relative to `Data`). Writes are only allowed inside the open project's `Data`
folder.

Mesh tools take `material` as an asset path (the entity uses that shared asset)
or an object `{path, saveAs, overwrite, ...properties}`: property overrides make a
private copy, which `saveAs` writes as a new `.hmat`. Without `saveAs`, the mesh
lives in memory (DoNotSave) until `hex_save_mesh_asset` writes it, together with
a private material. The material vocabulary is the same everywhere
(`create_material`, `edit_material`, `get_material`, mesh `material`):
`diffuseColour`, `emissiveColour`, `metallic`, `roughness`, `smoothness`,
`transparent`, `isWater`, `affectsGI`, `emissiveAffectsGI`, `receivesSnow`,
`cullMode` (`back`/`front`/`none`), `rainDripIntensity`, `materialModel`,
`modelParams`, `windSwayParams`, `cullDistance`, and `textures` (`albedo`,
`normal`, `roughness`, `metallic`, `height`, `emission`, `opacity`,
`ambientOcclusion`: asset path or null). Graph materials compute their surface
in the graph, so these properties mostly don't show on them.

Generated geometry follows the engine's measured convention: front faces wind
counter-clockwise seen from outside (`(b-a)x(c-a)` points along the normal).

### Entity references

Every method that targets an existing entity takes `entity` as a **name**
(string), a **live index** (integer) or an **exact id** `{index, generation}`
(as returned by every create/clone/list call — stale ids are rejected instead of
hitting a reused slot). Legacy `name` / `index` / `id` params still work.

### `batch` — the fast path

`batch` runs many methods in **one main-thread task (one editor frame, one PVS
rebuild)**. Later ops can consume earlier results with `{"$ref": "<op>.<path>"}`
where `<op>` is an op index or an `as` label:

```json
{"ops": [
  {"method": "create_entity", "as": "lamp", "params": {"name": "Lamp", "position": [0, 30, 0],
     "components": [{"component": "PointLight", "fields": {"_strength": 4.0}}]}},
  {"method": "clone_entity", "params": {"entity": {"$ref": "lamp.id"}, "newName": "Lamp2", "position": [5, 0, 0]}},
  {"method": "set_parent", "params": {"entity": {"$ref": "1.id"}, "parent": {"$ref": "lamp.id"}}}
], "stopOnError": true}
```

Ops run in order. With `stopOnError` (default) the remaining ops are reported
`skipped` after a failure. **Not transactional** — completed ops stay applied.
`open_project` and `capture_frame` are not batchable (a batch runs inside one
frame, so a capture could never see its own edits). Up to 5000 ops / 16 MiB per
request; responses are capped at 8 MiB.

---

## Building

Both projects are part of `Source/HexEngine/HexEngine.sln`, so the normal modern
build produces them:

```
cmake --preset vs2022-x64-debug
cmake --build --preset sln-build-debug-modern
```

Or build them individually:

```
msbuild Source\HexEngine\HexEngine.sln -t:HexEngine_EditorBridgePlugin;HexEngine_McpServer /p:Configuration=Debug /p:Platform=x64
```

Outputs: `HexEngine.McpServer.exe` (and the plugin DLL under `Bin\...\Plugins\`).
Neither is required by the shipped game build.

Unit tests (protocol parser + static tools) run in the dependency-light test
target:

```
cmake --preset vs2022-x64-debug-tests
cmake --build --preset tests-debug
ctest --preset tests-debug
```

---

## Running end-to-end

1. Build (above).
2. Launch the editor with the bridge enabled:
   `HexEngine.Editor2.exe --enable-editor-bridge` (or set `HEXENGINE_EDITOR_BRIDGE=1`).
3. Point your MCP client at `HexEngine.McpServer.exe --root <repo>`.
4. Static tools work immediately; live tools connect to the newest editor
   session. If multiple editors are running, the newest session (by start time)
   is selected; use `hex_list_editor_sessions` to see them all.

---

## Security model

- **Local only.** Named pipe only — no network sockets, no external ports.
- **Per-session auth token.** On start the bridge generates a random token, stores
  it in its session file, and requires every request to echo it (constant-time
  compared, `Unauthorized` otherwise). Only a process that can read that file (the
  same OS user) learns the token — a different local user cannot call the bridge.
  Stale session files (editor exited/crashed) are pruned on start and skipped by
  the client's discovery. The plugin must never appear in a **production plugin
  allowlist**; it is already gated to editor + opt-in and cannot run in a shipped
  game.
- **Two-level opt-in.** Inspection needs the bridge opt-in; every mutating
  method (and any `batch` containing one) additionally needs
  `HEXENGINE_EDITOR_BRIDGE_WRITE=1`, checked per method before anything runs.
  No asset import/reimport, no scene save, no shell/process execution. File
  writes are limited to the session file and `capture_frame`'s output image.
- **Fail closed.** The bridge is off unless explicitly opted in, and only in the
  editor. Unknown methods are rejected; all params are validated; responses are
  size-capped (8 MiB); oversized requests (> 16 MiB) are rejected.
- **No unsafe threading.** Engine state is read on the editor main thread via a
  marshalling queue, never from the pipe thread.
- **Logging.** Bridge start/stop is logged; method calls can be logged at
  debug/info level.

---

### Field metadata (Core)

`HexEngine.Core/Environment/ComponentFieldMeta.hpp` holds optional per-field
metadata (description, units, range, enum / flag value names) keyed by component
class name + serialised field key. Core components are described in
`ComponentFieldMeta::RegisterBuiltIns()`; plugins can call
`ComponentFieldMeta::Register(...)` for their own components after `REG_CLASS`.
Serialize/Deserialize stay the source of truth — metadata is purely additive.

## Not supported yet

- New / open / switch scenes (only saving is supported).
- Model import (FBX/glTF), material graphs, texture creation.
- Script-based game logic (Phase E2: scripting plugin, in progress separately).
- Arbitrary shell/command execution.

### Play mode notes

- Gameplay time is separate from real time (`TimeManager::GetGameFrameTime()`):
  scene / game-extension updates, fixed ticks, animation blends, light pulses and
  particles use it; UI, input and render temporal effects keep real time.
- While a game with a locked mouse runs, the engine recentres the cursor every
  frame, which also captures the user's real mouse while the editor has focus.
