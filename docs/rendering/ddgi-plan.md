# HexEngine DDGI Plan — replacing voxel diffuse GI

Working branch: `feat/ddgi` (cut from `feat/release-pipeline` @ `11a2ae9e`, carrying the
uncommitted 2026-11-23 GI work). Target hardware: RTX 4090 / i9-12900K (as
`aaa-graphics-plan.md`). Reference look: the user's Unreal Lumen capture (2026-11-23):
low sun through windows makes **blown-out sun patches with bloom**, and those patches
**visibly bounce coloured light onto nearby unlit walls, floor and ceiling**, while the
rest of the room falls off into shade.

## Status (2026-11-23)

Phases 0-6 are implemented on `feat/ddgi` and verified in the editor on the Whereabouts
scene. Everything is behind `r_ddgi` (default 0); the voxel GI is untouched and stays
the default until the side-by-side review.

| Phase | Result |
|---|---|
| 0 | `GpuProfiler` + `r_gpuTimings`. The voxel GI measured **~9 ms GPU**. |
| 1 | `RtScene` + `RtCommon.shader`. `r_rtDebug 1` agrees with raster depth to within 2 cm everywhere. Glass and other blended materials are excluded, so windows pass sunlight. Coherent primary rays run at ~0.5 Grays/s. |
| 2 | DDGI probes, three cascades at 1 / 3 / 9 m. Rays are shaded with shadow-tested sun and local lights, the sky SH, emissive and multi-bounce. The default bias was raised to 0.4 / 0.3 after a leak test. |
| 3 | Apply pass plus compose: full flat-ambient handoff, sky SH off, no voxel GI, no stale voxel AO or fog glow. |
| 4 | Traffic and all movers are traced and refit every frame by EntityId; a full rebuild runs every 2 s (`r_rtRebuildSeconds`). Area lights (tube and rect) are injected. `ExcludeFromGI` is ignored by the ray tracer (`r_rtRespectGiExclude`); city-sim now uses `SetExcludeFromVoxelGI`. A ground plane stands in for the GPU-only voxel terrain (`r_ddgiGroundPlane`). |
| 5 | Street in play mode: **DDGI 2.6-2.8 ms GPU, 61 fps** vs voxel **9.2 ms, 44 fps**. RT scene: 759 BLAS, 695k triangles, 7.2k instances, 69 MB, 1.8-4.8 ms CPU per full rebuild. |
| 6 | The SSR miss fallback samples DDGI irradiance when active (`GiFallbackRadiance`). |
| 7 | Not done: waiting for the user's side-by-side acceptance. |
| 8 | Blocked on the D3D12 backend. |

Correctness fixes made along the way in reused code:
- The sky SH projection is now equal-area (the octahedral grid weighting was biased).
- The albedo bake decodes sRGB only for `_SRGB` textures; the voxel GI still decodes all of them.
- Emissive now requires an emission map, matching DefaultPixel.
- The bake includes `uvScale`.

Known issues:
- Faint probe "spots" on some walls.
- Static temporal noise is ~2.3 vs 1.35 for voxel (`r_ddgiHysteresis` 0.98 brings it to ~2.0).
- Skinned characters are not traced.
- Terrain is a plane, not its real shape.
- Garbage BLAS ranges after mesh rebuilds are only reclaimed on `Destroy`.

## Why replace the voxel GI

These findings come from the 2026-11-23 tuning sessions (see the `DiffuseGI.cpp` /
`DiffuseGITrace.shader` history on this branch). Each is structural, not a tuning gap:

1. **Walls are thinner than voxels.** Clip-0 voxels are 0.33 m, and house walls and the
   floor-to-outside-ground gap are smaller. One voxel holds both the sunlit exterior and
   the dark interior. Every leak fixed this week came from that, each with a heuristic:
   - the sunlit wall bleeding through, fixed with `DirectionalSurfaceFactor`;
   - the outside ground glowing along interior wall bases, fixed with the cone near-fade;
   - the ceiling-line strip, fixed by arming the snap on large jumps.

   Separating the two sides needs voxels of about 0.1 m, which is ~30x the memory.
2. **Gather reach.** The per-clip evaluation reads ~0.5 m around the receiver. The cone
   gather added this week reaches 8 m, but only through 0.67–2.7 m voxels at distance,
   so ceilings still read 30–50% "open sky".
3. **Injection approximations.** Several shortcuts make lighting depend on settings
   rather than physics:
   - sparse AABB-budget voxelisation of large triangles;
   - sun-visibility test points lifted off the surface;
   - occupancy dilated into air by propagation;
   - bleed boosts and saturation trims.
4. **Accumulated heuristics.** Snap windows, settle limiters, movement presets (preset 2
   caused a 46% brightness dip on every move), hold radii and EMA chains. Every change
   now has knock-on effects.

DDGI-style probes avoid 1 and 2 by design. Rays hit real triangles, and per-probe
depth moments stop light passing through walls. Infinite bounces come from the probes
sampling themselves, which also removes most of 3 and 4.

## Non-negotiables

1. **Nothing lost until validated.** The new system ships behind `r_ddgi` (default 0).
   The voxel GI stays the default and untouched until a side-by-side capture review
   accepts DDGI. Only then is the voxel path retired (Phase 7).
2. **D3D11 first, ray tracing in compute.** No DXR dependency. The tracer is isolated
   behind one interface so a D3D12 DXR backend can replace it later (Phase 8) without
   touching the probe code.
3. **Measured, not judged by eye alone.** Every phase ends with:
   - fixed-pose back-buffer captures, before and after;
   - GPU timings from the timestamp profiler built in Phase 0;
   - the motion-lag metric from this week (in-motion vs settled frame difference).
4. **Lessons kept.**
   - No absolute per-frame change limiters.
   - History is clamped to the current neighbourhood.
   - Feedback loops need a gain below 1 by construction.
   - Camera-relative structures must be world-locked and cleared on teleport.

## What we reuse

| Need | Existing piece | Where |
|---|---|---|
| CPU triangles per mesh | `Mesh::_vertices` / `_indices`, `GetGeometryRevision()` | `Scene/Mesh.hpp:242-252, 301, 317` |
| BVH builder | `RayMath::TriangleBVH`: binned SAH, 4 tris/leaf | `Physics/RayMath.hpp:196` |
| Per-mesh BVH cache pattern | `GetMeshBvh` keyed by `Mesh*` | `Physics/PhysUtils.cpp:34-95` |
| GI-eligible instances | `Scene::GatherStaticMeshesInBounds`, `IsGiContributor`, `IsGiStaticLayer` | `Scene/Scene.cpp:530-608, 3665` |
| Change signals | `_giGeometryRevision`, `_giLightRevision`, `_giMaterialRevision`, mover debounce | `Scene/Scene.cpp:445-600` |
| Instance proxies | `GiMeshInstanceProxy`, `ExtractGiSceneProxies` | `Scene/DiffuseGI.cpp:3749-3867` |
| Hit albedo / emissive | `GpuGiMaterial` + packed texel buffer, `SampleMaterialAlbedo/Emission` | `DiffuseGI.hpp:349-361`, `DiffuseGIVoxelizeEval.shader:161-196` |
| Sun shadow at a hit | `BuildSunShadowCasterBuffer` + cascade SRVs, `ProjectToShadowCascade` | `DiffuseGI.cpp:249-301, 6180-6214`, `VoxelizeEval.shader:261-306` |
| Local lights (incl. tube/rect) | `ClusteredLighting::GpuLight` buffer (1024 max) | `Graphics/ClusteredLighting.hpp:43, 93-95, 120-135` |
| Sky radiance on a miss | Sky-view / transmittance LUTs, `SkyViewLutParamsToUv` | `Graphics/AtmosphereLUTs.hpp:139-154`, `AtmosphereCommon.shader:204` |
| Compose hook | `RenderDiffuseGI` after `RenderLights`, `g_giComposeParams` handoff in Deferred | `SceneRenderer.cpp:3573-3583, 4965-4972, 5815-5833`, `Deferred.shader:374-443` |
| Compute plumbing | `IGraphicsDevice::CreateStructuredBuffer / Dispatch / DispatchIndirect` | `Graphics/IGraphicsDevice.hpp:381-459` |

Missing pieces:
- a GPU acceleration structure;
- GPU timestamps (only CPU `PROFILE()` exists);
- DXR (D3D12 is pre-draw parity, with zero ray-tracing code).

## Architecture

```
            CPU (worker thread)                         GPU (per frame)
 ┌───────────────────────────────┐        ┌──────────────────────────────────────────┐
 │ BLAS cache: Mesh* + geomRev → │        │ 1. Probe scheduler: pick N probes        │
 │   flattened BVH2 + tri data   │  upload│    (priority: near camera, in view,      │
 │ TLAS: GI instances in radius  │ ─────▶ │    lighting changed, never-updated)      │
 │   (world→object, blasRoot,    │        │ 2. Trace: R rays/probe (rotated Fibonacci)│
 │   materialBase), SAH rebuild  │        │    compute BVH traversal → hit           │
 │   on geom/instance revision   │        │ 3. Shade hits: albedo × (sun via cascades│
 └───────────────────────────────┘        │    + local lights + emissive + previous  │
                                          │    probe irradiance) ; miss → sky LUT    │
                                          │ 4. Blend into probe atlases (irradiance  │
                                          │    8×8 oct, depth moments 16×16 oct)     │
                                          │ 5. Classify / relocate probes            │
                                          │ 6. Apply: per pixel sample 8 probes      │
                                          │    (trilinear × backface × Chebyshev)    │
                                          │    → indirect diffuse into beauty        │
                                          └──────────────────────────────────────────┘
```

**Probe volume.**
- Camera-centred cascades, world-locked, with toroidal (scrolling) addressing so a camera
  move only re-initialises the slab entering the volume.
- No content moves with the camera; this is the lesson of this week's lag.
- Starting point:
  - cascade 0: 1.0 m spacing, 32×12×32 probes (32 × 12 × 32 m);
  - cascade 1: 3 m spacing;
  - cascade 2: 9 m spacing.
- Interiors are the cascade-0 case. A 7 × 17 m room gets about 7 × 2 × 17 probes.

## Phases

Each phase lists its deliverable and an **acceptance check**. Effort is relative: S/M/L.

### Phase 0 — Test bed and instruments (S)
- **GPU timestamp profiler.**
  - D3D11 `TIMESTAMP` / `TIMESTAMP_DISJOINT` queries in a 3-frame ring.
  - `GPU_SCOPE("name")` macro and `r_gpuTimings` (log and overlay).
  - Exposed through the editor bridge, so captures record ms per pass.
- **Test scene `GITest`.**
  - A white box room (albedo 0.8) with one window and one coloured wall (red), plus a
    copy with a 0.1 m thin wall next to a sunlit exterior.
  - Fixed camera poses and a sun at about 25° elevation, matching the Lumen reference.
- **Check out the capture harness.** The Python scripts used this week (`interior.py`,
  `coneperf.py`, `movedip.py`) move into `tools/gi/` so every phase uses the same
  measurements.
- **Acceptance:**
  - Per-pass GPU ms are logged for the current voxel GI.
  - Baseline captures of `GITest` and the Whereabouts house are recorded, at
    10:00 / 16:30 / 21:30.

### Phase 1 — Compute ray tracer (L)
- **BLAS.**
  - Built from `RayMath::TriangleBVH` (reuse the SAH builder).
  - Flattened into GPU nodes (BVH2, 32 B: two child AABBs quantised or full float, plus
    child/leaf index).
  - Triangle records: positions, two UV sets for material lookup, materialIndex.
  - Cached by `Mesh*` + `GetGeometryRevision()`, using the lowest LOD where LODs exist.
  - Built on a worker thread; a disk cache is optional later.
- **TLAS.**
  - Instances from the GI spatial grid within about 300 m.
  - Each instance holds world→object 3×4, BLAS root, material base and flags (static or
    dynamic).
  - SAH rebuild on `_giGeometryRevision`. Movers refit per frame, with a cap.
- **Traversal.**
  - `RayTrace.shader` include providing a stack-based closest-hit and any-hit (for shadow
    rays) with instance transform.
  - Returns hit t, instance, primitive and barycentrics.
- **Debug.**
  - `r_rtDebug` views: primary-ray depth / normal / instance id from the camera.
  - The bridge's `raycast` gains a `gpu` source, to cross-check against
    `RayCastMeshTriangles`.
- **Acceptance:**
  - The GPU primary-ray image matches raster depth to within 1 cm on `GITest` and the
    house.
  - Measured rays/s logged for coherent and incoherent rays.
  - A Whereabouts BLAS + TLAS build-time report.
  - **Go/no-go gate:** incoherent throughput must support Phase 2's budget (target
    ≥ 150 Mrays/s on the 4090). If not, do the mitigations first (see Risks).

### Phase 2 — Probe update (L)
- **Probe atlases.**
  - Irradiance: 8×8 octahedral plus a 1-texel border, RGBA16F (R11G11B10 later).
  - Depth moments: 16×16, RG16F.
  - Per-probe state: offset (relocation), class (active / inactive / sleeping), last
    update frame.
- **Scheduler.**
  - A fixed ray budget per frame, starting at 64 rays × N probes with N chosen from the
    measured Mrays/s for ≤ 2 ms.
  - Priority: never-updated first, then near camera, then in frustum, then lighting
    changed (sun moved, light revision). Round-robin for the rest.
- **Hit shading.**
  - Albedo × [sun (cascade lookup; outside the cascades, a shadow ray) + local lights
    (stochastic: one light sampled per hit, weighted by contribution, with a shadow ray)
    + previous-frame probe irradiance at the hit (infinite bounce, gain < 1 because
    albedo < 1)] + emissive.
  - Miss: sky-view LUT radiance, which includes weather/overcast through
    `GetSkyRenderCBuffer()`.
- **Blend.**
  - Hysteresis about 0.97, dropped toward 0.5 for texels whose new value jumps by more
    than 2x (fast response to lights switching, no smearing).
  - Depth moments use the standard DDGI sharpness exponent.
- **Classification and relocation** come from the same rays: a high back-face ratio
  means the probe is inside geometry, so it is deactivated or moved to the nearest
  front-facing side.
- **Scrolling.** A camera move re-initialises only the slab entering the volume.
  Teleports re-initialise everything with high priority.
- **Acceptance:**
  - The `GITest` room reaches converged bounce within 1 s of a lighting change.
  - The thin-wall variant shows no exterior light on the interior face, and the sunlit
    side is unchanged.
  - The probe-update GPU time is within budget.

### Phase 3 — Apply and compose (M)
- **Apply pass.**
  - Half-res, then bilateral upsample. Reuse the resolve's bilateral upsample but not its
    history: probes are already temporally stable.
  - Per pixel: 8 probes of the best cascade (blend at cascade edges), weighted by
    trilinear × back-face (wrap) × Chebyshev visibility from depth moments, with
    normal/view bias.
  - Output: irradiance × albedo / π into the beauty target.
- **Compose.** While `r_ddgi` is on:
  - flat ambient fully handed off (`g_giComposeParams.x = 1`);
  - sky SH diffuse off, because the probes contain the sky through misses;
  - the voxel GI pass skipped.
  - AO input: the GI AO texture is replaced by HBAO+ (vendored) or the existing SSAO, a
    decision for Phase 3.
- **Near-field detail.** Keep the existing SSGI (`r_giSSGI`) on top for contact-scale
  bounce that probe spacing cannot resolve.
- **Exposure.** Retune the day AE curve (`r_autoExposureEvCompBreakDay` / `SlopeDay`)
  for physically dark interiors so sun patches go past white and bloom
  (`r_bloomLuminanceThreshold`).
- **Acceptance:**
  - Side-by-side with the Lumen reference on `GITest`, judged by the user.
  - House captures at 10:00 / 16:30 / 21:30.
  - Motion-lag metric ≤ today's (rotation ≤ 1.0, walk ≤ 3.0).
  - No light through walls in the house.

### Phase 4 — Dynamic content and lights (M)
- Vehicles and NPCs (Layer `DynamicGeometry`, characters) as TLAS instances, using refit
  and low-LOD BLAS. They occlude and bounce light but are not probe receivers.
- Area lights (tube / rect), which voxel GI never injected.
- Emissive signage at night from `GpuGiMaterial` emissive, without the voxel
  "emissive REPLACE" special cases.
- Night street check: street lamps through probes vs today's look.

### Phase 5 — City scale and performance (M)
- Cascade sizes, ray budget and update rates tuned on the full Whereabouts city.
- CPU work: TLAS rebuild time with 17k instances; incremental rebuild by spatial cells.
- Memory report: BLAS/TLAS, atlases and texel buffers.
- **Acceptance:** total DDGI GPU time ≤ 3 ms at 1440p on the 4090 (update + apply), and
  no hitches above 2 ms on a full-speed bike ride.

### Phase 6 — Specular fallback (S)
SSR currently falls back to a 12-step cone through the GI voxels (`SSR.shader:177-224`).
Phase 6 replaces that with a BVH ray (rough reflections) or a probe irradiance fallback,
so voxels can be deleted.

### Phase 7 — Retire voxel GI (S)
- After the user accepts DDGI, `r_ddgi` defaults to 1.
- `DiffuseGI.cpp` (~6.7k lines) and the voxel shaders are deleted: Voxelize,
  VoxelizeEval, InjectResolve, Propagate, Shift, LitCacheMerge, ScreenFeedback,
  BuildCandidates, Trace, TraceBlur, Resolve, AOBlur.
- Their `r_gi*` cvars are removed. Scene files keep loading, because unknown cvars are
  ignored.
- `EngineAssets.pkg` is rebuilt.

### Phase 8 — D3D12 DXR tracer (L, blocked)
Once the D3D12 backend reaches draw parity, implement the Phase 1 tracer interface with
DXR (BLAS/TLAS and inline `RayQuery` in the probe-trace pass). Probe code is unchanged.
Not scheduled until D3D12 Phase B5 is unblocked.

## Risks and mitigations

| Risk | Mitigation |
|---|---|
| Compute traversal throughput on SM5 (no wave ops; the stack lives in indexable temp) | Phase 1 go/no-go gate. If short: BVH4/compressed nodes, rays sorted by direction, fewer rays/probe with stronger temporal accumulation, update cascade 2 less often |
| CPU BLAS build time for many unique meshes | Worker thread, build only what is in the TLAS radius, optional disk cache keyed by mesh asset + revision |
| Probes inside thin walls / floors | DDGI classification + relocation; 1 m cascade-0 spacing keeps several probes per room |
| Sun-patch bounce too soft at 1 m spacing | That bounce is low frequency by nature, and SSGI covers contact range. If needed, a cascade-0 refinement at 0.5 m around the camera inside buildings |
| Light leaking from local lights through walls | Shadow ray per sampled light (Phase 2), no unshadowed injection |
| Lag on fast lighting change (sun, lamps) | Adaptive hysteresis per texel, change-priority scheduling, measured with the lag metric |
| Exposure/bloom look differs from the voxel baseline | Phase 3 retune with the user, captures at fixed times |

## Decisions for the user

1. **GPU budget.** Proposed: ≤ 3 ms total at 1440p on the 4090 (update ≤ 2 ms, apply
   ≤ 1 ms).
2. **Dynamic objects.** Cars and NPCs in the BVH from Phase 4, or static only for the
   first playable version?
3. **AO source** once voxel AO goes: HBAO+ (vendored) or the existing SSAO?
4. **Test scene.** A dedicated `GITest` scene in the engine's sample data, or a room
   inside the Whereabouts project?

### SSGI + probe tuning (2026-11-23, later)
- Near-field SSGI moved to a half-res pass (`DDGISSGI.shader`, RGBA16F, a = eye distance) with a depth-aware 4-tap upsample in `DDGIApply`. Full-res 16 taps cost ~5.7 ms at 1440p (scattered G-buffer reads); half-res is ~1.15 ms. Taps now read position first and skip albedo.
- New defaults: `r_ddgiSSGIIntensity 2`, `r_ddgiSSGIRadius 2.5`. At intensity 4 grain shows. (These final-image observations were made under the gamma-crushed composite fixed below; re-judge them.) Under live auto-exposure the visible difference is small: it reads as a warm lift around lamp-lit walls, while the small sun pools barely light the room.
- Rays stay at 128 (static noise ~7x lower than 64). Probe period 4/8/16 and hysteresis 0.96 kept:
  - period 6 saves ~0.4 ms of trace and period 8 ~0.7 ms, but both slow the response;
  - hysteresis 0.90 adds noise for only 8 s -> 5 s on a sun-bounce step.
- Cost at the interior test view: DDGI 5.0 ms total = Trace 2.3, Blend 0.47, SSGI 1.15, Apply 1.1.
- Open: a sun-bounce step takes ~8 s to reach 90%. The adaptive hysteresis only triggers on changes over 50%, so moderate steps creep (a continuous adaptive term would help). The interiors also read strongly blue from sky-ray bounce at 10:00.

### Blue interiors + response (2026-11-23, late)
The 10:00 blue cast had three causes, all fixed:
1. **Composite gamma-crush (regression from the SSGI change).** Apply rendered into an intermediate target and composited with `ITexture2D::BlendTo_Additive(…, nullptr)`. That draws with the GUI basic shader: `UIBasic` saturates to 1; `UIBasicHDR` (HDR back buffer) applies `pow(saturate(x), 2.2) * 1.9`. As a result 1x indirect added ~1/10th of what the debug view showed, and warm bounce vanished. Apply now draws straight into beauty (additive; opaque in debug). Its shader no longer reads beauty, because SSGI has its own pass.
   - **Same pre-existing trap elsewhere:** the voxel GI composite (`DiffuseGI.cpp` `_giResolved->BlendTo_Additive`), the volumetric lighting composite (`SceneRenderer.cpp`) and the reflection-probe face blend all use `BlendTo_Additive(…, nullptr)`.
2. **Lights inside their fixture.** A floor lamp's PointLight child sits inside the shade mesh, so every probe shadow ray hit the shade and the lamp never lit the probes. RT instances now carry an owner key (entity index + 1 in `GpuInstance::pad`). A light skips its direct parent's mesh when that parent is fixture-sized (<= 2.5 m; `DDGI.cpp FixtureKey`, `RtOccludedSkip`).
3. **Unoccluded sky specular.** The env-spec fallback was only occluded by the voxel AO, which is off under DDGI, so matte interior floors reflected the full sky. Probes now store sky visibility in the irradiance alpha: the cosine-weighted fraction of rays that missed. `DdgiOccludeEnvSpecular` (SSRResolve + Deferred) keeps env spec by that visibility and fills the rest with probe radiance along R.
   - Sampling along a direction (R, SSR miss rays) must bias and weight with the SURFACE normal/view and only look up along the ray (`DdgiSampleDirectional4`). Passing the ray as the "normal" pushed lookups through walls and made bright seams at every room edge; SSR's `GiFallbackRadiance` had the same flaw.

Response: a probe-level change detector (mean luminance over the tile, near noise-free with 128 rays) cuts hysteresis by up to 60% for 10-40% changes. A sun-bounce step now takes 3.5 s to reach 90%, down from 8 s, with no measurable extra noise.

### Default on, contact AO, probe pulsing (2026-11-24)
- `r_ddgi` now defaults to 1; `r_ddgi 0` brings back the voxel GI. Reports of "GI moves with the camera" and bright rings around props came from the voxel GI. In indirect-only view, returning the camera to the same pose changes DDGI by 0.02 levels vs 2.4-2.7 for voxel.
- **Contact AO:** HBAO+ (`_ssaoProvider`) renders into a white-cleared `_aoRT`. Its blend mode is MULTIPLY_RGB, so the target ends up holding the AO term, with no plugin-interface change. Apply multiplies the probe term by it (`r_ddgiAO`, 0-1; `g_ddgiGround.z`, t7). The voxel-backed fallback provider is never used. Cost ~0.47 ms.
- **"Disco" probes indoors:** single-update change detectors fired on ray noise and dropped the history, so probes that see a small bright window popped every update. That covers both the per-texel ">50%" rule (original) and the probe-level 10-40% rule (2026-11-23). Measured flicker in the probe view: SD 8.3 with those detectors. It is now a dual-rate, noise-aware detector (`r_ddgiAdaptive` 1): a per-probe fast mean (`stats` buffer, u1) must sit 3.5-7 standard errors from the history, with the error estimated from the update's ray-luminance spread. Flicker drops to 0.63, equal to fixed hysteresis (0.71); `r_ddgiAdaptive 2` restores the old detectors for A/B.
- Response to a lamp switching on in a closed room is dominated by multi-bounce through the probes (each bounce lags several updates). Even hysteresis 0.8 reaches only ~50% in 2 s, so no detector makes it instant. Measure with weather random cycling off: it drifts the sky during tests.

### Probe "lumps" / poster-paint look (2026-11-24)
- Plain trilinear blending between 1 m probes turns real variation (probes nearer an opening see more sky and sunlit ground) into soft-edged boxes on flat walls; the cinema entrance is the reference view. Visibility sharpness, relocation and 0.5 m spacing did not fix it (finer spacing gives smaller, more numerous blotches); lower bias helps a little but brings back the edge streaks.
- Fix: **stochastic probe interpolation** in DDGIApply (`r_ddgiSampleJitter`, default 0.5 probe spacings). The lookup point is jittered within the surface plane (IGN, rotated per frame, resolved by TAA), which convolves the trilinear blend with a disc.
- Two guards against leaks:
  - the jitter fades out where HBAO marks a corner or crease (`smoothstep(0.80, 0.97, ao)`);
  - the jittered result is clamped to 0.6-1.6x an unjittered lookup, which catches door frames and window reveals that AO does not mark.
- Without the guards, sky light leaked indoors. With them, interior flicker equals jitter-off. Cost: one extra probe lookup, about +1 ms in Apply at 1440p.

### Weather fog indoors (2026-11-24)
- The froxel fog's indoor occlusion was the voxel GI's AO (gated by `g_giComposeParams.z`), which is off under DDGI, so rooms filled with weather haze (sandstorm: orange fog and a big lamp fog cone).
- Now `VolumetricScatterDensity` includes DDGICommon and scales the fog EXTINCTION by DDGI sky visibility looking up (`DdgiSampleIrradiance4(worldPos, up, -rayDir).a`, smoothstep 0.05-0.6). It runs within `r_ddgiFogRange` (64 m); `r_ddgiFogIndoor` (default 0) sets the fraction left in enclosed air.
- The ambient haze scales with extinction. Sun shafts use the constant `MIE_COEFF`, independent of fog density, so they keep their strength; lamp glow keeps its `r_volumetricLocalHaze` floor.
- `DDGI::BindForCS` / `UnbindCS` bind t56-64 + b8 around the dispatch. Cost +0.18 ms (Froxel.Scatter 0.21 → 0.39 ms).
- Trap: the shader compiler treats comment lines inside a `ComputeShaderIncludes` block as include names.
