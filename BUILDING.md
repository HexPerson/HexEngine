# Build and Dependency Architecture (Migration Guide)

This file is the authoritative guide for HexEngine build/dependency migration status.

## Current Status

- Engine/editor/game compilation remains legacy Visual Studio/MSBuild (`.sln/.vcxproj`) in `Source/HexEngine/`.
- `setup.py` is still the active dependency/bootstrap flow.
- Top-level CMake has been introduced as the canonical orchestration entrypoint for migration tasks.
- Dependency metadata now lives in `build/dependencies.lock.json`.

## Legacy Flow (Still Supported)

1. Run `Setup.bat`.
2. `Setup.bat` installs Python requirements (`gitpython`, `cmake`) and calls `python setup.py --mode legacy --frozen`.
3. `setup.py` clones/builds/stages dependencies into:
   - `Libs/x64/<Config>/`
   - `Bin/x64/<Config>/Bin/`
   - `Include/` (legacy header staging for selected header-only dependencies)
   - Native library bootstrap includes `physx` and `shaderconductor` as managed dependencies in the default legacy flow.
4. Build the engine and tools via `Source/HexEngine/HexEngine.sln` in Visual Studio 2022.

## New Recommended Orchestration Flow (Incremental)

Use CMake for orchestration and migration visibility:

1. Configure:
   - `cmake --preset vs2022-x64-debug`
2. Print dependency plan (no mutations):
   - `cmake --build --preset deps-plan-debug`
3. Check ref pin status:
   - `cmake --build --preset deps-check-refs-debug`
4. Enforce strict ref pin status (CI-friendly):
   - `cmake --build --preset deps-check-refs-strict-debug`
5. If you have dependencies cloned locally and want to pin current commits into the manifest:
   - `cmake --build --preset deps-lock-refs-debug`
6. Bootstrap only header dependencies in external-header mode (no native builds):
   - `cmake --build --preset headeronly-bootstrap-debug`
7. Bootstrap required runtime modules only (streamline + physx + shaderconductor):
   - `cmake --build --preset required-modules-bootstrap-debug`
   - prerequisite: `git-lfs` installed and initialized (`git lfs install`) for Streamline SDK binaries
8. If intentional, run full legacy setup through canonical entrypoint (pinned refs):
   - `cmake --build --preset legacy-setup-debug`
9. Build the opt-in dependency probe executable:
   - `cmake --preset vs2022-x64-debug-dep-probe`
   - `cmake --build --preset dep-probe-debug`
10. Build the opt-in legacy imported-target assimp probe:
   - `cmake --preset vs2022-x64-debug-assimp-probe`
   - `cmake --build --preset assimp-probe-debug`
11. Build the opt-in legacy imported-target brotli probe:
   - `cmake --preset vs2022-x64-debug-brotli-probe`
   - `cmake --build --preset brotli-probe-debug`
12. Build the opt-in Streamline imported-target probe:
   - `cmake --preset vs2022-x64-debug-streamline-probe`
   - `cmake --build --preset streamline-probe-debug`
13. Build the opt-in ShaderConductor imported-target probe:
   - `cmake --preset vs2022-x64-debug-shaderconductor-probe`
   - `cmake --build --preset shaderconductor-probe-debug`
14. Build the opt-in PhysX imported-target probe:
   - `cmake --preset vs2022-x64-debug-physx-probe`
   - `cmake --build --preset physx-probe-debug`
15. Build the engine solution via canonical CMake orchestration targets:
   - Debug: `cmake --build --preset sln-build-debug`
   - Release: `cmake --build --preset sln-build-release`
16. Run Streamline preflight only (fast check):
   - `cmake --build --preset streamline-artifacts-check-debug`

Note:
- In fresh clones where legacy staged libs are not present yet, assimp/brotli probe targets now report a skipped message instead of failing configuration/build.

Equivalent direct command for plan output:
- `python setup.py --print-plan`

## `setup.py` Modes and Flags

- `--mode legacy`
  - Current supported mode; preserves existing bootstrap/build behavior.
- `--print-plan`
  - Prints dependency manifest plan and exits without cloning/building.
- `--frozen`
  - Uses pinned refs from `build/dependencies.lock.json` where provided.
  - Recommended/default for wrapper entrypoints (`Setup.bat`, `hex-legacy-setup`) to avoid floating upstream layout breaks.
  - If checkout fails due dirty dependency worktrees, setup attempts fetch + hard reset + clean inside that dependency repo before retrying checkout.
  - If recovery still fails, setup re-clones the dependency and retries checkout.
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
  - `Streamline` is required and validated (including `git-lfs` artifacts); bootstrap fails if SDK artifacts are missing.
- `--header-layout external`
  - Phase 3 starter behavior: `cxxopts` is consumed from `ThirdParty/cxxopts/include` without copying into `Include/`.
- `--required-modules-only`
  - Bootstraps required runtime modules only (`streamline`, `physx`, `shaderconductor`) for Debug and Release.

Notes:
- Automatic floating update behavior was removed from default path.
- Use `--update` explicitly when you want latest upstream changes.
- Git operations in setup run with `GIT_LFS_SKIP_SMUDGE=1` to reduce bootstrap failures on machines without `git-lfs`.
- Legacy setup treats recastnavigation/oidn configure as optional scaffolding and continues with warnings if those configure-only steps fail in constrained environments.
- Legacy setup currently skips OIDN bootstrap by default (optional dependency; known submodule path-length instability in some Windows environments).
- Streamline is treated as required by default; setup validates `ThirdParty/Streamline/lib/x64/sl.interposer.lib` is materialized (not a git-lfs pointer).
- `HexEngine.StreamlinePlugin` now fails early with an explicit pre-build error if `sl.interposer.lib` is missing or still an unresolved git-lfs pointer.
- `hex-sln-build-debug` / `hex-sln-build-release` now depend on `hex-streamline-artifacts-check`, so canonical CMake orchestration fails fast before lengthy solution compilation when Streamline artifacts are unavailable.
- Legacy setup configures NRD with `NRD_EMBEDS_SPIRV_SHADERS=OFF` for Windows-first builds to avoid SPIR-V codegen requirements in environments that only ship DXIL-capable DXC.

## Dependency Manifest

`build/dependencies.lock.json` is now the source of truth for dependency metadata:

- `name`
- `path`
- `git_url`
- `ref` (pin target; may be `null` until finalized)
- `build_system`
- `required_runtime_module` (optional; marks modules included in `--required-modules-only` bootstrap)
- `notes`
- `legacy_stage_paths`

This keeps migration incremental while enabling reproducible, reviewable dependency state.

## What Is Still Legacy

- `.vcxproj` post-build copy/staging conventions.
- Hardcoded include/lib paths in project files.
- Game code builds still execute through MSBuild (now behind an editor build-service abstraction).
- Header staging into `Include/` for select dependencies in legacy setup path.
- Final compilation still goes through `HexEngine.sln`/`.vcxproj`; CMake is now the canonical orchestration entrypoint for invoking those builds.

## Phase 4 Implementation Notes

- `GameIntegrator::BuildGame` now delegates execution details to an internal `MSBuildGameBuildService` in [GameIntegrator.cpp](/C:/HexEngine/Source/HexEngine/HexEngine.Editor2/GameIntegrator.cpp).
- Hot-reload behavior, MSBuild command line, log output handling, and generated props workflow are preserved.
- This is a first separation step toward a dedicated project/game build service.

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
- Added optional dependency probe executable:
  - target: `hex-dep-probe`
  - source: [dep_probe.cpp](/C:/HexEngine/cmake/examples/dep_probe.cpp)
  - purpose: compile-time validation that target-based include propagation works.
- Added optional legacy imported-target probe executable:
  - target: `hex-assimp-probe`
  - source: [assimp_probe.cpp](/C:/HexEngine/cmake/examples/assimp_probe.cpp)
  - purpose: validate non-header dependency consumption through `Hex::assimp_legacy`.
- Added optional legacy imported-target probe executable:
  - target: `hex-brotli-probe`
  - source: [brotli_probe.cpp](/C:/HexEngine/cmake/examples/brotli_probe.cpp)
  - purpose: validate non-header dependency consumption through `Hex::brotli_legacy`.
- Added optional Streamline imported-target probe executable:
  - target: `hex-streamline-probe`
  - source: [streamline_probe.cpp](/C:/HexEngine/cmake/examples/streamline_probe.cpp)
  - purpose: validate Streamline SDK include/link consumption through `Hex::streamline_vendor`.
- Added optional ShaderConductor imported-target probe executable:
  - target: `hex-shaderconductor-probe`
  - source: [shaderconductor_probe.cpp](/C:/HexEngine/cmake/examples/shaderconductor_probe.cpp)
  - purpose: validate ShaderConductor include/link consumption through `Hex::shaderconductor_vendor`, directly from `ThirdParty/shaderconductor/Build` (no `Libs/` or `Include/` copy).
- Added optional PhysX imported-target probe executable:
  - target: `hex-physx-probe`
  - source: [physx_probe.cpp](/C:/HexEngine/cmake/examples/physx_probe.cpp)
  - purpose: validate PhysX include/link consumption through `Hex::physx_vendor`, directly from `ThirdParty/physx/.../bin/win.x86_64.vc143.md`.
- Existing default behavior remains unchanged (`--header-layout legacy`).

## Migration Roadmap Snapshot

- Phase 1 (completed): stabilization + manifest + docs + reproducible ref workflow.
- Phase 2 (completed for this tranche): canonical CMake orchestration entrypoint and presets.
- Phase 3 (completed for this tranche): header-only target migration plus opt-in non-header imported-target probes (assimp, brotli).
- Phase 4 (completed for this tranche): game build execution extracted behind an MSBuild service abstraction while preserving hot-reload behavior.
