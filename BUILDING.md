# Build and Dependency Architecture (Migration Guide)

This file is the authoritative guide for HexEngine build/dependency migration status.

## Current Status

- Engine/editor/game compilation remains legacy Visual Studio/MSBuild (`.sln/.vcxproj`) in `Source/HexEngine/`.
- `tools/deps/bootstrap.py` is now the canonical dependency/bootstrap executor.
- Top-level CMake has been introduced as the canonical orchestration entrypoint for migration tasks.
- Dependency metadata now lives in `build/dependencies.lock.json`.

## Legacy Flow (Still Supported)

1. Run `Setup.bat`.
2. `Setup.bat` delegates to canonical CMake orchestration (`vs2022-x64-debug` + `minimal-bootstrap-debug` + `sln-build-debug-modern`).
3. The bootstrap tool clones/builds/stages dependencies into:
   - `Libs/x64/<Config>/`
   - `Bin/x64/<Config>/Bin/`
   - No third-party header copying into `Include/` in the modern path.
4. Build the engine and tools via `Source/HexEngine/HexEngine.sln` in Visual Studio 2022.

## New Recommended Orchestration Flow (Incremental)

Use CMake for orchestration and migration visibility:

1. Configure:
   - `cmake --preset vs2022-x64-debug`
2. Team default bootstrap + build (Debug modern lane):
   - `cmake --build --preset minimal-bootstrap-debug`
   - `cmake --build --preset sln-build-debug-modern`
3. Team default Release lane:
   - `cmake --build --preset sln-build-release-modern`
4. Print dependency plan (no mutations):
   - `cmake --build --preset deps-plan-debug`
5. Check ref pin status:
   - `cmake --build --preset deps-check-refs-debug`
6. Enforce strict ref pin status (CI-friendly):
   - `cmake --build --preset deps-check-refs-strict-debug`
7. Print dependency backend status:
   - `cmake --build --preset deps-backend-info-debug`
8. Print vcpkg wiring status:
   - `cmake --build --preset vcpkg-status-debug`
9. If you have dependencies cloned locally and want to pin current commits into the manifest:
   - `cmake --build --preset deps-lock-refs-debug`
10. Bootstrap core header dependencies only (no native builds):
   - `cmake --build --preset headeronly-bootstrap-debug`
11. Bootstrap required dependencies only:
   - `cmake --build --preset required-modules-bootstrap-debug`
   - prerequisite: `git-lfs` installed and initialized (`git lfs install`) for Streamline SDK binaries
12. Bootstrap the minimal non-legacy dependency set (recommended migration path):
   - `cmake --build --preset minimal-bootstrap-debug`
   - release variant: `cmake --build --preset minimal-bootstrap-release`
   - includes:
     - core header bootstrap (`cxxopts`, `fastnoiselite`, `rapidxml`)
     - required dependency bootstrap (manifest `required=true`, plus `required_runtime_module=true`)
     - current required dependency set:
       - `directxtk`
       - `freetype`
       - `directxtex`
       - `brotli`
       - `rapidjson`
       - `retpack2d`
       - `physx`
       - `shaderconductor`
       - `streamline`
   - prerequisite: `git-lfs` installed and initialized (`git lfs install`) for Streamline SDK binaries
13. Build the opt-in dependency probe executable:
   - `cmake --preset vs2022-x64-debug-dep-probe`
   - `cmake --build --preset dep-probe-debug`
14. Build the opt-in legacy imported-target assimp probe:
   - `cmake --preset vs2022-x64-debug-assimp-probe`
   - `cmake --build --preset assimp-probe-debug`
15. Build the opt-in legacy imported-target brotli probe:
   - `cmake --preset vs2022-x64-debug-brotli-probe`
   - `cmake --build --preset brotli-probe-debug`
16. Build the opt-in Streamline imported-target probe:
   - `cmake --preset vs2022-x64-debug-streamline-probe`
   - `cmake --build --preset streamline-probe-debug`
17. Build the opt-in ShaderConductor imported-target probe:
   - `cmake --preset vs2022-x64-debug-shaderconductor-probe`
   - `cmake --build --preset shaderconductor-probe-debug`
18. Build the opt-in PhysX imported-target probe:
   - `cmake --preset vs2022-x64-debug-physx-probe`
   - `cmake --build --preset physx-probe-debug`
19. Build the engine solution via canonical CMake orchestration targets:
   - Debug: `cmake --build --preset sln-build-debug`
   - Release: `cmake --build --preset sln-build-release`
20. Build the engine solution with minimal non-legacy bootstrap:
   - Debug: `cmake --build --preset sln-build-debug-modern`
   - Release: `cmake --build --preset sln-build-release-modern`
21. Run Streamline preflight only (fast check):
   - `cmake --build --preset streamline-artifacts-check-debug`
22. Run all required-runtime dependency probes in one shot:
   - `cmake --preset vs2022-x64-debug-required-runtime-probes`
   - `cmake --build --preset required-runtime-probes-debug`
23. Build ShaderCompiler via canonical orchestration:
   - Legacy linkage: `cmake --build --preset shadercompiler-build-debug`
   - Opt-in vendor linkage (`ThirdParty/shaderconductor/Build`): `cmake --build --preset shadercompiler-build-debug-vendor`

Note:
- In fresh clones where legacy staged libs are not present yet, assimp/brotli probe targets now report a skipped message instead of failing configuration/build.

Equivalent direct command for plan output:
- `python tools/deps/bootstrap.py plan`

## Bootstrap Tool Commands

- `plan`
  - Prints dependency manifest plan and exits without cloning/building.
- `check-refs`
  - Prints pin status for all dependencies.
- `check-refs-strict`
  - Same as check, but exits with failure when any ref is missing.
- `lock-current-refs`
  - Writes local dependency `HEAD` commits into manifest `ref` fields when repos are available.
- `bootstrap-headeronly --frozen`
  - Bootstraps core header-only dependencies required by modern compilation.
- `bootstrap-required --frozen --configs Debug,Release`
  - Bootstraps required dependencies declared in the manifest (`required=true` and/or `required_runtime_module=true`).
- `bootstrap-minimal --frozen --configs Debug,Release`
  - Bootstraps required dependencies plus core header-only dependencies.
- `--frozen`
  - Uses pinned refs from `build/dependencies.lock.json` where provided.
- `--update`
  - Updates existing dependency repos from origin before build.

Notes:
- `setup.py` has been removed from the repository.
- `Setup.bat` remains as a thin convenience wrapper around canonical CMake debug presets.
- Streamline remains required by default; bootstrap validates `ThirdParty/Streamline/lib/x64/sl.interposer.lib` materialization (not a git-lfs pointer).
- Streamline plugin pre-build now validates required library existence via an absolute path computed in MSBuild; git-lfs pointer validation remains centralized in canonical CMake preflight (`hex-streamline-artifacts-check*`).
- `HexEngine.StreamlinePlugin` now fails early with an explicit pre-build error if `sl.interposer.lib` is missing or still an unresolved git-lfs pointer.
- `hex-sln-build-debug` / `hex-sln-build-release` now depend on `hex-streamline-artifacts-check`, so canonical CMake orchestration fails fast before lengthy solution compilation when Streamline artifacts are unavailable.

## Dependency Manifest

`build/dependencies.lock.json` is now the source of truth for dependency metadata:

- `name`
- `path`
- `git_url`
- `ref` (pin target; may be `null` until finalized)
- `build_system`
- `required` (marks required dependencies for modern bootstrap)
- `required_runtime_module` (optional; marks modules included in `--required-modules-only` bootstrap)
- `notes`
- `legacy_stage_paths`

This keeps migration incremental while enabling reproducible, reviewable dependency state.

## What Is Still Legacy

- `.vcxproj` post-build copy/staging conventions.
- Hardcoded include/lib paths in project files.
- Game code builds still execute through MSBuild (now behind an editor build-service abstraction).
- Final compilation still goes through `HexEngine.sln`/`.vcxproj`; CMake is now the canonical orchestration entrypoint for invoking those builds.
- `HexEngine.ShaderCompiler` now supports opt-in vendor linkage through MSBuild property `HexUseVendorShaderConductor=true` (used by `shadercompiler-build-debug-vendor` preset) while preserving legacy defaults.

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
- Core header dependencies are now consumed directly from `ThirdParty/...` in canonical bootstrap paths (no third-party `Include/` copy).
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
- `HexEngine.Core` now includes `ThirdParty/rapidjson/include` and `ThirdParty/retpack2d` directly in project include paths, reducing dependence on copied `Include/` headers for these dependencies.

## Dependency Backend Scaffold

- Added `cmake/deps/HexDependencyBackend.cmake` to centralize backend selection.
- Current default:
  - `HEXENGINE_DEPENDENCY_BACKEND=vcpkg_manifest`
- Available options:
  - `fetchcontent`
  - `legacy_manifest`
  - `vcpkg_manifest`
- `vcpkg_manifest` backend is active for CMake-managed dependencies (`cxxopts`, `nlohmann-json`) when configured with vcpkg toolchain presets.
- Native engine solution builds still rely on staged artifacts for required runtime modules (PhysX, ShaderConductor, Streamline) in this tranche.

## Migration Roadmap Snapshot

- Phase 1 (completed): stabilization + manifest + docs + reproducible ref workflow.
- Phase 2 (completed for this tranche): canonical CMake orchestration entrypoint and presets.
- Phase 3 (completed for this tranche): header-only target migration plus opt-in non-header imported-target probes (assimp, brotli).
- Phase 4 (completed for this tranche): game build execution extracted behind an MSBuild service abstraction while preserving hot-reload behavior.
