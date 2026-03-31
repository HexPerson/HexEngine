# Build and Dependency Architecture (Migration Guide)

This file is the authoritative guide for HexEngine build/dependency migration status.

## Current Status

- Engine/editor/game compilation remains legacy Visual Studio/MSBuild (`.sln/.vcxproj`) in `Source/HexEngine/`.
- `setup.py` is still the active dependency/bootstrap flow.
- Top-level CMake has been introduced as the canonical orchestration entrypoint for migration tasks.
- Dependency metadata now lives in `build/dependencies.lock.json`.

## Legacy Flow (Still Supported)

1. Run `Setup.bat`.
2. `Setup.bat` installs Python requirements (`gitpython`, `cmake`) and calls `python setup.py --mode legacy`.
3. `setup.py` clones/builds/stages dependencies into:
   - `Libs/x64/<Config>/`
   - `Bin/x64/<Config>/Bin/`
   - `Include/` (legacy header staging for selected header-only dependencies)
4. Build the engine and tools via `Source/HexEngine/HexEngine.sln` in Visual Studio 2022.

## New Recommended Orchestration Flow (Incremental)

Use CMake for orchestration and migration visibility:

1. Configure:
   - `cmake --preset vs2022-x64-debug`
2. Print dependency plan (no mutations):
   - `cmake --build --preset deps-plan-debug`
3. Check ref pin status:
   - `cmake --build --preset deps-check-refs-debug`
4. If you have dependencies cloned locally and want to pin current commits into the manifest:
   - `cmake --build --preset deps-lock-refs-debug`
5. Bootstrap only header dependencies in external-header mode (no native builds):
   - `cmake --build --preset headeronly-bootstrap-debug`
6. If intentional, run legacy setup through canonical entrypoint:
   - `cmake --build --preset legacy-setup-debug`

Equivalent direct command for plan output:
- `python setup.py --print-plan`

## `setup.py` Modes and Flags

- `--mode legacy`
  - Current supported mode; preserves existing bootstrap/build behavior.
- `--print-plan`
  - Prints dependency manifest plan and exits without cloning/building.
- `--frozen`
  - Uses pinned refs from `build/dependencies.lock.json` where provided.
- `--update`
  - Updates existing dependency repos from origin before build.
- `--check-refs`
  - Prints pin status for all dependencies.
- `--check-refs-strict`
  - Same as check, but exits with failure when any ref is missing.
- `--lock-current-refs`
  - Writes local dependency `HEAD` commits into manifest `ref` fields when repos are available.
- `--header-only-bootstrap`
  - Fetches/copies header-only dependencies only and skips native library builds.
- `--header-layout external`
  - Phase 3 starter behavior: `cxxopts` is consumed from `ThirdParty/cxxopts/include` without copying into `Include/`.

Notes:
- Automatic floating update behavior was removed from default path.
- Use `--update` explicitly when you want latest upstream changes.

## Dependency Manifest

`build/dependencies.lock.json` is now the source of truth for dependency metadata:

- `name`
- `path`
- `git_url`
- `ref` (pin target; may be `null` until finalized)
- `build_system`
- `notes`
- `legacy_stage_paths`

This keeps migration incremental while enabling reproducible, reviewable dependency state.

## What Is Still Legacy

- `.vcxproj` post-build copy/staging conventions.
- Hardcoded include/lib paths in project files.
- Editor `GameIntegrator` invoking MSBuild directly for game-code hot reload.
- Header staging into `Include/` for select dependencies in legacy setup path.

## Phase 3 Starter (Now Implemented)

- Introduced target-based dependency scaffold in CMake:
  - `Hex::cxxopts` (`INTERFACE`) in [cmake/HexDependencies.cmake](/C:/HexEngine/cmake/HexDependencies.cmake)
- Extended target-based header-only scaffolding:
  - `Hex::fastnoiselite`
  - `Hex::rapidxml`
  - `Hex::nlohmann_json_headers`
  - `Hex::rectpack2d`
- Added opt-in external-header mode for `cxxopts`:
  - `python setup.py --header-only-bootstrap --header-layout external`
- In external-header mode, these dependencies are no longer copied into `Include/`; they are consumed from `ThirdParty/...`.
- Existing default behavior remains unchanged (`--header-layout legacy`).

## Migration Roadmap Snapshot

- Phase 1 (this pass): stabilization + manifest + docs + CMake orchestration scaffold.
- Phase 2 (started): canonical CMake entrypoint for orchestration targets.
- Phase 3 (started): move dependencies toward target-based CMake linkage and backend integration (vcpkg/FetchContent).
- Phase 4 (next): separate game build service from editor runtime hot-reload logic.
