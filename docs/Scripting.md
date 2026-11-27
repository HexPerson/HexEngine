# Gameplay scripting (Lua)

Game logic can be written in **Lua 5.5** and changed while the editor runs: no C++ build, no relaunch.
Scripts are attached to entities with a `ScriptComponent`. They run in play mode. Saving the file
hot-reloads it. Errors are written to the engine log with file and line.

- Runtime: `Source/HexEngine/Plugins/HexEngine.ScriptingPlugin`, which provides `IScriptEngine` through Lua via [sol2](https://github.com/ThePhD/sol2).
- Core side: `Scripting/IScriptEngine.hpp` (plugin ABI) and `Entity/Component/ScriptComponent.*`.
- Samples: `Plugins/HexEngine.ScriptingPlugin/Samples/` (`Rotator.lua`, `Bobber.lua`).

C++ game DLLs (`IGameExtension`) still work and can live alongside scripts. Use scripts for per-entity
behaviour that you want to iterate on quickly.

## Setup

Lua and sol2 come from the repo-root `vcpkg.json` (`lua[cpp]`, `sol2`). After pulling, install them once:

```powershell
ThirdParty\vcpkg\vcpkg.exe install --x-manifest-root=. --x-install-root=vcpkg_installed --triplet x64-windows
```

Build the solution. The plugin's post-build copies `lua-c++.dll` into the nested
`Bin/<plat>/<config>/Bin/` folder, where plugin vendor DLLs live. The engine logs
`[Script] Lua 5.5 scripting ready` at startup. Without the plugin, ScriptComponents load and save
normally but do nothing.

Lua is compiled **as C++** (`lua-c++`), so a script error unwinds C++ frames with destructors
instead of `longjmp`-ing over them.

## Attaching a script

1. Put the script in the project's `Data` folder, for example `Data/Scripts/Rotator.lua`.
2. Add a **ScriptComponent** to an entity and set **Script** to `GameData.Scripts/Rotator.lua`. You can also drag the file onto the field.
3. Press Play.

Serialised fields of the ScriptComponent, which are also what the MCP bridge sees:

| field | meaning |
|---|---|
| `script` | resource path, e.g. `"GameData.Scripts/Rotator.lua"` (a data-relative or absolute path inside a mounted file system is canonicalised to this form) |
| `properties` | object of the script's exposed variables. **Reads** list every declared property (the script default unless overridden). **Writes** keep only values that differ from the default, so changing a default in the script reaches every instance that didn't override it. |

Through the bridge, an agent does:

```json
add_component        {"entity": "Cube", "component": "ScriptComponent",
                      "fields": {"script": "GameData.Scripts/Rotator.lua", "properties": {"speed": 180}}}
set_component_field  {"entity": "Cube", "component": "ScriptComponent", "fields": {"properties": {"speed": 45}}}
```

`properties` merges (JSON merge-patch), so you only send the keys that change. A newly attached
script compiles on the next editor frame, and only then do its declared defaults appear in
`inspect_component`. Overrides you set before that are kept.

## Anatomy of a script

```lua
local Rotator = {
    Properties = {              -- exposed + serialised per entity
        speed = 90,             -- number
        axis = Vec3(0, 1, 0),   -- Vec3 (stored as [x, y, z])
        reverseKey = "R",       -- string; booleans work too
    },
}

function Rotator:OnStart()           self.direction = 1 end
function Rotator:OnKeyDown(key)      if key == self.reverseKey then self.direction = -self.direction end end
function Rotator:OnUpdate(dt)        self.entity:Rotate(self.axis, self.speed * self.direction * dt) end

return Rotator
```

- The script returns a **class table**. Each ScriptComponent gets its own `self`, whose metatable indexes that table.
- `self` starts with the effective properties, plus `self.entity` (an `Entity` handle) and `self.script` (the path). Anything else you store on `self` is that instance's state.
- Global-style scripts also work. If the file returns nothing, its globals table is the class (`function OnUpdate(self, dt) ... end`).
- Every script has its own globals table, so scripts can't clobber each other. Share code with `require("GameData.Scripts/Util.lua")`, which runs once and is cached.

### Callbacks

All callbacks are optional and are called with `self`.

| callback | when |
|---|---|
| `OnStart()` | play begins, or the entity is spawned during play |
| `OnUpdate(dt)` | every frame; `dt` is **gameplay** time (scaled, and skipped while paused unless stepping) |
| `OnFixedUpdate(dt)` | every fixed physics tick (`cl_simulationRate`); none while paused |
| `OnLateUpdate(dt)` | after every entity's update |
| `OnStop()` | play ends, or the entity is destroyed during play |
| `OnKeyDown(key)` / `OnKeyUp(key)` | key or mouse-button edges this frame (`"W"`, `"Space"`, `"Mouse1"`, ...); auto-repeat is filtered |
| `OnTriggerEnter(other)` / `OnTriggerExit(other)` | overlap with a trigger RigidBody. Both sides get it: the body entering, and the trigger entity itself |
| `OnCollision(other, point)` | contact between rigid bodies |
| `OnReload()` | after a hot reload swapped the code under a running instance |

Scripts run only while the game is running: editor play mode, or a standalone game. In edit mode
the script compiles, so the inspector and bridge can show its properties, but no callback runs.
Entities in editor utility scenes (prefab stage, icon previews) never run scripts. The editor's
pause, step and time-scale controls (and the bridge's `set_simulation` / `step_simulation`) apply
to scripts.

## API

### Math

- `Vec3(x, y, z)` / `Vec3(s)` / `Vec3()`. Fields `.x .y .z`; operators `+ - * /` (scalar or component-wise) and unary `-`; methods `:Length()`, `:LengthSquared()`, `:Normalized()`, `:Dot(b)`, `:Cross(b)`. Statics `Vec3.Distance(a, b)`, `Vec3.Lerp(a, b, t)`, `Vec3.Zero()`, `Vec3.One()`, `Vec3.Up()`, `Vec3.Right()`, `Vec3.Forward()` (forward is -Z).
- `Quat()` is the identity. Also `Quat.Euler(pitch, yaw, roll)` (degrees about X/Y/Z), `Quat.AxisAngle(axis, degrees)`, `Quat.LookRotation(forward, up?)` and `Quat.Slerp(a, b, t)`. `a * b` rotates by `a` then `b`, and `q * v` rotates a vector. Methods: `:Rotate(v)`, `:Inverse()`, `:ToEuler()` (degrees).

### Entity

Entity handles are safe to keep: a destroyed entity's handle returns `false` from `:IsValid()`.
Any other call on it raises a script error that names the line.

- **Identity**: `:IsValid()`, `:GetName()`, `:GetId()`.
- **Transform**: `:GetPosition()` / `:SetPosition(v | x, y, z)` (local), `:GetWorldPosition()`, `:GetRotation()` / `:SetRotation(q)`, `:GetScale()` / `:SetScale(v | s)`, `:Translate(delta)`, `:Rotate(axis, degrees)`, `:LookAt(target, up?)`, `:GetForward()`, `:GetRight()`, `:GetUp()`.
- **Hierarchy**: `:GetParent()`, `:GetChildren()`, `:FindChild(name)` (recursive), `:Destroy()`, `:SetVisible(bool)`, `:IsVisible()`.
- **Components**: `:HasComponent(name)`, `:GetLight()`, `:GetRigidBody()`, and `:GetScript()` (the other entity's running `self`, so `door:GetScript():Open()` works).
- **Any component field**: `:GetField(component, field)` and `:SetField(component, field, value)`. These use the same serialised fields as the editor bridge, e.g. `e:SetField("PointLight", "_strength", 4)`. They round-trip through the component's own Serialize/Deserialize, so avoid calling them every frame on heavy components such as RigidBody.

**Light** (any light type): `:GetStrength()` / `:SetStrength(v)`, `:GetMultiplier()` / `:SetMultiplier(v)`, `:GetRadius()` / `:SetRadius(v)`, `:GetColour()` returns r, g, b, a, `:SetColour(r, g, b, a?)`.

**RigidBody**: `:AddForce(v)`, `:AddTorque(v)`, `:GetVelocity()`, `:GetMass()` / `:SetMass(m)`, `:SetGravityEnabled(b)`, `:WakeUp()`, `:Teleport(pos)`. For character controllers: `:Move(displacement, dt)` and `:IsOnGround()`.

### Scene (the script's own scene)

- `Scene.Find(name)` returns an entity, or nil.
- `Scene.CreateEntity(name, pos?)`.
- `Scene.Spawn("GameData.Prefabs/Crate.hprefab", pos?, rot?)` returns the prefab's root entity, or nil. Entities spawned during editor play are removed when play stops.
- `Scene.Raycast(origin, direction, maxDistance?, options?)` returns `{entity, position, normal, distance}`, or nil. It uses `PhysUtils::RayCast`, which finds the nearest hit across meshes and the physics scene. Options: `ignore` (an entity or a list of entities), `ignoreSelf` (default `true`), `layerMask`, `meshes`, `physics`, `minNormalY`.

### Input, Time, Log

- `Input.IsKeyDown(key)`, `Input.WasKeyPressed(key)`, `Input.WasKeyReleased(key)`. A key is a name (`"A"`–`"Z"`, `"0"`–`"9"`, `"Space"`, `"Enter"`, `"Escape"`, `"Tab"`, `"Shift"`, `"Ctrl"`, `"Alt"`, arrow keys `"Up"` etc., `"F1"`–`"F12"`, `"Mouse1"`–`"Mouse5"`) or a virtual-key code.
- Also: `Input.IsMouseButtonDown(0 | 1 | 2)`, `Input.GetMouseDelta()` (raw delta this frame) and `Input.GetMousePosition()`. Input injected by the bridge's `send_input` counts too.
- `Time.GetDeltaTime()`, `Time.GetFixedDeltaTime()`, `Time.GetTime()` (gameplay seconds since play started), `Time.GetRealTime()`, `Time.GetTimeScale()`, `Time.IsPaused()`.
- `Log.Info(...)`, `Log.Warn(...)`, `Log.Error(...)` (logged as a warning with an `error:` prefix). `print(...)` logs at Info level. Lines are tagged `[Script] <file> on '<entity>'`.

## Hot reload and errors

- The engine checks loaded scripts and `require`d modules for changes every 0.25 s. A changed script is recompiled. **Running instances keep their state** (`self`): the class under them is swapped, new properties get their values, and `OnReload()` is called. A changed module reloads every script.
- A script that failed to load when play started begins running (`OnStart`) as soon as a save fixes it. A script attached by path before the file exists loads once the file appears.
- **Compile errors** keep the previous working version running and log `[Script] compile error: GameData.Scripts/Door.lua:12: ...` as a warning.
- Script problems (compile, runtime, `Log.Error`) are logged as **warnings**, never as critical errors: the engine shows a blocking dialog for critical errors, and a typo must not freeze the editor or an agent driving it.
- **Runtime errors** log the callback, file:line and a short traceback, e.g. `[Script] GameData.Scripts/Door.lua on 'Door': OnUpdate failed: GameData.Scripts/Door.lua:12: attempt to index a nil value (local 'x')`. That instance's callbacks then pause until the file is saved again, so the error isn't repeated every frame.
- Errors from engine calls (an invalid entity, an unknown key name) carry the calling script line too.
- Agents read all of this with the bridge's `get_recent_engine_logs` (filter on `[Script]`). `IScriptEngine::CheckScript(path, error)` compiles without running, for tools.

## Sandbox

Scripts get only `base`, `math`, `string`, `table`, `utf8` and `coroutine`, plus `os.clock`, `os.time`, `os.date` and `os.difftime`.

They do not get `io`, `os.execute`/`exit`/`getenv`/`remove`, `package`, `debug`, `load`, `loadfile`, `dofile`, `string.dump` (bytecode) or `collectgarbage`. `require` resolves only through the engine's file systems.

Each top-level call has an instruction budget of about 200 million VM instructions, so an accidental
`while true do end` raises an error instead of freezing the editor.

## Notes and limits

- Property types are number, boolean, string and Vec3. Other types in `Properties` are skipped with a warning.
- In the inspector, each property is a text field (`x, y, z` for a Vec3). After attaching a new script, reselect the entity to see its properties.
- A script on an entity that is destroyed during play gets `OnStop` (as an OnDestroy would).
- The plugin interface version is `IScriptEngine002`. `IScriptEngine` / `ScriptComponent` are a Core ABI change, so after pulling this, do a full rebuild of Core and every plugin/game DLL.
