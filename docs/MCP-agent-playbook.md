# HexEngine MCP — agent playbook

How to build a game in HexEngine through the MCP editor bridge, fast and
without surprises. Tool reference: [MCP.md](MCP.md). This page is the
*how to use them well* part. A condensed copy is sent to every MCP client as
the server's `instructions`.

## 0. Setup

1. Launch the editor with `HEXENGINE_EDITOR_BRIDGE=1` (inspection) and
   `HEXENGINE_EDITOR_BRIDGE_WRITE=1` (authoring).
2. `hex_get_editor_status`. If `sceneName` is empty the editor is at the project
   browser: `hex_list_projects`, then `hex_open_project {name}` and poll
   `hex_get_editor_status` until `sceneName` is set.
3. Orient yourself: `hex_list_scenes`, `hex_get_environment`,
   `hex_capture_view {}` (look at it), `hex_validate_current_scene`.

## 1. The loop: observe → act → verify

- **Observe** with cheap reads: `get_editor_status`, `list_entities` (filter /
  component), `query_region`, `inspect_entity`, `get_environment`, and LOOK with
  `capture_view` (returns an image; `frameEntity` to frame what you just built,
  or `position` + `lookAt`).
- **Act** in batches (below).
- **Verify**: `capture_view` the result, `validate_current_scene` (use
  `groundCheck: true` after placing things), and `get_recent_engine_logs
  {sinceSeq}` for errors. Fix, repeat.

## 2. Speed

- **Every call costs at least one editor frame** (~0.1–0.3 s in Debug). Use
  `hex_batch` for anything more than one or two edits: hundreds of ops run in ONE
  frame and are ONE undo step.
- Chain ops inside a batch with `"as": "label"` and `{"$ref": "label.id"}` (e.g.
  create a parent, then children with `parent: {"$ref": "house.id"}`).
- `ground_height` takes up to 20000 points per call: compute a whole scatter in
  one request, then place it in one batch.
- Not batchable (one per call): play/stop/step/input, captures, builds, scene
  switching, prefab stage, undo/redo, save.

## 3. Conventions

- World: right-handed, **Y up**, metres. A camera with yaw 0 looks along **-Z**;
  `eulerDegrees` are `[pitch, yaw, roll]`, pitch + = up, yaw + turns toward -X.
- Front faces are counter-clockwise seen from outside (`create_mesh_entity`).
- Entity names are unique per scene: a duplicate gets a suffix. Use the `id`
  (`{index, generation}`) returned by the call that made it.
- Assets are referenced as scenes store them: `GameData.<path under Data>`
  (`GameData.Materials/Rock.hmat`), engine ones `EngineData.…`. `hex_list_assets`
  returns these `ref`s. Writes only go inside the project's `Data/` folder.

## 4. Building a level

- **New level:** `hex_new_scene {name}` (MainCamera + sun, saved, active).
  `hex_open_scene` switches between scenes; switching clears the undo history.
- **Environment:** `hex_set_environment {timeOfDay, freezeTime, weather,
  seaLevel, ocean, cvars}`.
- **Water** needs the engine's water shader: `create_material {path, base:
  "EngineData.Materials/Water.hmat"}` on a large `plane` (an `isWater` flag on
  a Default-based material does NOT render as water). Check the basin first:
  `ground_height` around it must be BELOW the water height, or the plane hides
  under the terrain.
- **Geometry:** `create_primitive` (box, plane, sphere, capsule, cylinder, cone,
  torus) or `create_mesh_entity` for custom meshes. They are **in-memory** until
  saved: pass `saveAs: "Meshes/Name.hmesh"` (or `save_mesh_asset` later) for
  anything that should survive a scene save, go into a prefab or be undoable.
  `validate_current_scene` flags unsaved ones (`meshUnsaved`).
- **Materials:** `create_material` once, then share it with `assign_material` /
  `material: "GameData.Materials/X.hmat"`. Per-object property overrides make
  private unsaved copies — avoid them for anything you keep.
- **Placement:** `ground_height` / `raycast` (pass `only: [terrain]` or
  `minNormalY: 0.7` to ignore canopies, roofs and walls; `fromY` below overhangs),
  then `snap_to_ground` to rest objects on the surface (`alignToNormal` for
  slopes). `query_region` before placing to avoid overlaps.
- **Reuse:** build one, `create_prefab`, then `instantiate_prefab` copies.
  Edit every copy at once with `open_prefab_stage` → edit → `close_prefab_stage
  {save: true}`. Instance edits show up as overrides (`prefab_info`).
- **Components:** `describe_component_type` BEFORE adding one: it lists every
  field, its type, default, enum names and ranges. Then `create_entity {components:
  [{component, fields}]}` or `add_component` / `set_component_field` (enum fields
  take names; objects merge).
- **Look:** cvars (`list_cvars`, `set_cvars`) are saved with the scene.
- **Glowing things:** emissive materials also light the world through voxel GI
  (`emissiveAffectsGI`, on by default). A few bright emitters can flood the area
  with their colour after a few seconds; keep emission modest or set
  `emissiveAffectsGI: false` for decorative glow.
- **Terrain:** a heightfield via `create_mesh_entity` (grid vertices + UVs,
  `saveAs`) works well up to ~100k vertices. Verify the winding with a downward
  `raycast {only: [terrain]}`: the hit must not report `backFace`.

## 5. Game logic (C++)

The game is a C++ DLL (`IGameExtension`: `OnCreateGame`, `OnUpdate`,
`OnFixedUpdate`, `OnStopGame`) in the project's `Code/` folder.

1. `set_game_auto_reload {enabled: false}`: otherwise the editor rebuilds by
   itself ~0.4 s after each file write.
2. `list_game_sources`, `read_game_source`, then `write_game_source` (whole
   files; new files are added to the `.vcxproj`).
3. `build_game`: compiler errors come back as `{file, line, column, code,
   message}`. Fix and rebuild. Success hot-reloads the game.
4. `play` and check behaviour (logs, captures, input).

Scripted logic (no C++ build) is coming with the scripting plugin.

## 6. Play-testing

- `play` needs a built `Build/Game.dll`. `stop` restores the scene to exactly
  its pre-play state — **edits made while playing are discarded**.
- Deterministic testing: `set_simulation {paused: true}`, then `step_simulation
  {steps: N}` (exact fixed ticks), `capture_view` between steps.
  `timeScale` slows or speeds gameplay.
- `send_input {events: [...]}`: keys go through the engine's key binds exactly
  like real ones (`hold`, `keyDown`/`keyUp`, `mouseMove` look deltas, `click`,
  `text`, `wait`). Held keys are released at the end.
  Note: `FirstPersonCameraController` only *moves* through a RigidBody character
  controller on the camera entity — without one, W/A/S/D do nothing even for a
  human.
- Read `get_recent_engine_logs {sinceSeq}` after each step for errors.

## 7. Safety and recovery

- Every call or batch is one undo step: `undo` reverts your last action.
  Material/asset file edits and scene switches are not part of undo.
- While a prefab stage is open, every tool edits the PREFAB, not the level.
- `save_scene` writes the project and every open scene to disk — only when you
  mean to keep the work.
- `validate_current_scene` before finishing: no `error` issues, no
  `meshUnsaved`/`materialUnsaved` on things you keep, nothing `fellOutOfWorld`.

## 8. Known quirks

- `save_scene` saves EVERY open scene; there is no single-scene save yet.
- `validate_current_scene` reports deliberate `snap_to_ground` offsets as
  `floating` (info) - ignore those.
- No bridge tool writes Lua scripts yet (the scripting plugin exists; tools to
  create/attach scripts are next).
- Engine render helpers (`LightSphere`, `ReflectionProbeRig`) appear on their
  own; they are not scene content.
- `capture_view` takes ~1–2 s (it waits frames so temporal effects settle).
- A huge batch stalls that editor frame; split batches beyond a few thousand ops.
