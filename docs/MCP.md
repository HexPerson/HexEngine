# HexEngine MCP Integration

HexEngine exposes read-only editor/project inspection to AI clients (Claude,
Codex, ChatGPT, etc.) through the **Model Context Protocol (MCP)**. The MCP
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

**Phase 1 is read-only.** No mutation, no asset import/reimport, no scene
editing, no file writes (other than the bridge's own discovery session file), no
shell/process execution. Anything not yet safely implementable returns a clear
`NotImplemented` / `NotAvailable` error rather than faking data or touching
engine state unsafely.

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
- **Threading:** the pipe server runs on one owned background thread (RAII,
  joined on unload — never detached). ECS/scene reads are **marshalled to the
  editor main thread** (drained during editor tool-message dispatch); if the
  editor is idle the request times out cleanly instead of touching engine state
  from the pipe thread.

### 2. `HexEngine.McpServer.exe` (standalone)

Speaks MCP JSON-RPC 2.0 over **stdio** (newline-delimited). It runs with or
without a live editor:

- **Static tools** inspect files under a project/repo root and always work.
- **Live tools** proxy to the editor bridge over the named pipe. With no editor
  running they return a clear `EditorNotConnected` error.

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

| Tool → bridge method | Status |
| --- | --- |
| `hex_get_editor_status` | Implemented (running / scene / mode). |
| `hex_get_open_scene` | Implemented (scene name + entity count). |
| `hex_list_entities` | Implemented (id/name/components, paginated). |
| `hex_inspect_entity` | Implemented (by `name` or `index`). |
| `hex_list_components` | Implemented (component types in the scene). |
| `hex_validate_current_scene` | Implemented (minimal Phase-1 diagnostics). |
| `hex_get_open_project` | `NotImplemented` (project metadata not exposed to plugins yet). |
| `hex_get_selected_entity` | `NotImplemented` (editor selection not exposed yet). |
| `hex_inspect_component` | `NotImplemented` (safe per-field serialization needs reflection). |
| `hex_list_loaded_resources` | `NotImplemented` (no safe ResourceSystem enumeration yet). |
| `hex_find_missing_references` | `NotImplemented` (follow-up). |
| `hex_get_recent_engine_logs` | `NotImplemented` (log ring buffer not exposed yet). |

`NotImplemented` methods return a structured error with a TODO — they never fake
data.

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
- **Read-only (Phase 1).** No scene/component/asset mutation, no asset
  import/reimport, no file writes (except the bridge session file), no file
  deletes, no shell/process execution.
- **Fail closed.** The bridge is off unless explicitly opted in, and only in the
  editor. Unknown methods are rejected; all params are validated; responses are
  size-capped (1 MiB); oversized requests are rejected.
- **No unsafe threading.** Engine state is read on the editor main thread via a
  marshalling queue, never from the pipe thread.
- **Logging.** Bridge start/stop is logged; method calls can be logged at
  debug/info level.

---

## Intentionally NOT supported yet

- Any mutation: creating/editing/deleting entities, components, or assets.
- Asset import/reimport, packaging, or build execution.
- Arbitrary shell/command execution.
- File writes or deletes.
- An in-editor AI assistant UI (this PR is infrastructure only).

These are deliberately out of scope for Phase 1 and will be designed with their
own authorization model before being added.
