# GPU Visibility Culling (Frustum + Occlusion)

## Overview
- `GpuVisibilityCulling` is integrated into the existing scene renderer without replacing the current draw submission path.
- The existing CPU PVS/snapshot system remains the coarse gather stage.
- Opaque main-camera rendering can optionally apply GPU frustum and HZB occlusion filtering before CPU draw submission.

## Data Flow
1. `Scene::RenderEntities` builds/uses `RenderableSnapshot` from PVS as before.
2. For normal opaque passes, `GpuVisibilityCulling::CullOpaqueRenderables`:
   - gathers eligible snapshot entries,
   - uploads world-space bounds to a structured buffer,
   - runs `GpuFrustumCull` compute,
   - runs `GpuOcclusionCull` compute (if HZB history is valid),
   - applies visibility flags back to snapshots.
3. The existing draw loop skips `RenderableSnapshot` entries where `gpuVisible == false`.
4. After opaque rendering, `SceneRenderer` updates the depth pyramid (`BuildDepthPyramid`) for use by the next frame.

## HZB / Hybrid Source
- Primary path: previous-frame HZB.
- Fallback path: optional startup depth-prepass bootstrap (`r_gpuCullDepthPrepassFallback`) to seed HZB when history is missing.
- Culling is conservative when HZB is unavailable or camera movement is too fast.

## Compatibility Notes
- Shadow rendering path is unchanged.
- Transparency rendering path is unchanged.
- Debug/editor rendering remains active and can visualize candidate/rejected bounds.
- Default behavior is preserved with `r_gpuCullEnable = 0`.

## Runtime Controls
- `r_gpuCullEnable`
- `r_gpuCullFrustum`
- `r_gpuCullOcclusion`
- `r_gpuCullDepthPrepassFallback`
- `r_gpuCullFreeze`
- `r_gpuCullDebugBounds`
- `r_gpuCullDebugFrustumRejected`
- `r_gpuCullDebugOcclusionRejected`
- Stability controls: `r_gpuCullGraceFrames`, `r_gpuCullFastCameraDistance`, `r_gpuCullNearBypassDistance`, `r_gpuCullLargeSphereBypass`

## Current Limitations
- Opaque pass only (main path); transparent and shadow passes still use legacy filtering.
- Visibility application still uses CPU submission, so this is an incremental step toward fully GPU-driven indirect rendering.
- Readback is currently synchronous for correctness; future work should migrate to non-blocking staged readback + delayed application or GPU-driven indirect draws.

