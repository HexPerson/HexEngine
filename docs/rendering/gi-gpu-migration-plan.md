# HexEngine GI GPU Migration Plan

This document tracks the engineering migration of diffuse GI from CPU-driven triangle generation toward a GPU-driven worklist architecture.

## Current Implementation Status

Implemented in branch `codex/gi-gpu-migration-plan`:

- Added GI migration feature flags:
  - `r_giGpuCandidateGen`
  - `r_giGpuMaterialEval`
  - `r_giGpuMaterialEvalMaxLights`
  - `r_giGpuCompareMode`
  - `r_giTelemetry`
  - `r_giTelemetryLogFrames`
- Added stage telemetry counters in `DiffuseGI`:
  - CPU triangle-build time
  - CPU upload time and upload bytes
  - GPU candidate-pass submission time
  - GPU dispatch submission time
  - source and candidate triangle counts
- Introduced bridge data contracts inside `DiffuseGI`:
  - `GiClipmapParams`
  - `GiMeshInstanceProxy`
  - `GiMaterialProxy`
  - `GiLocalLightProxy`
- Added extraction boundary method:
  - `ExtractGiSceneProxies(...)`
- Added append-buffer candidate infrastructure:
  - candidate structured buffer (SRV/UAV append)
  - candidate counter buffers/readback (debug/telemetry only)
  - candidate indirect-dispatch args buffer
  - `EnsureGpuVoxelCandidateBuffer(...)`
  - `BuildGpuVoxelCandidateList(...)`
- Added compute shader source:
  - `Source/HexEngine/HexEngine.Shaders/DiffuseGIBuildCandidates.shader`
  - Performs clipmap-bound triangle candidate filtering into append buffer.
- Integrated optional candidate generation into `RunGpuVoxelization`:
  - falls back to existing `_voxelTriangleSrv` path when disabled or unavailable.
- Added optional GPU local-light evaluation path:
  - `Source/HexEngine/HexEngine.Shaders/DiffuseGIVoxelizeEval.shader`
  - uploads packed GI light proxies to GPU structured buffer.
  - extracts clipmap-local lights each update, sorts by relevance, and caps upload count for stable cost.
  - skips CPU local-light contribution when `r_giGpuMaterialEval=1`.

## Intentional Safety Rules

- Existing CPU fallback GI path is untouched.
- Existing GPU voxelization path remains default and stable.
- Candidate generation is opt-in (`r_giGpuCandidateGen=1`) and degrades to previous behavior if shader/resources are unavailable.
- GPU material evaluation is opt-in (`r_giGpuMaterialEval=1`) and falls back to standard voxelize when unavailable.

## Next Implementation Steps

1. Promote GI proxy contracts to a reusable render-bridge module (outside `DiffuseGI`).
2. Replace CPU triangle payload generation with GPU candidate generation from mesh/material/light proxy buffers.
3. Add dedicated GPU material proxy texture indirection for albedo/emissive sampling.
4. Move local light and sun evaluation fully into compute (remove CPU per-triangle lighting work).
5. Add explicit CPU-vs-GPU diff visualization mode in GI debug views.

## Notes

- Current telemetry uses CPU-side timing for GPU submission windows; true GPU execution timings should be added with D3D11 timestamp/disjoint queries in a follow-up.
- `r_giGpuMaterialEval` now enables active GPU local-light evaluation; sun/base/albedo triangle injection is still CPU-authored and remains in migration scope.
