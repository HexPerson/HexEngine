# HexEngine AAA Graphics Plan

Target: match the image quality of Cyberpunk 2077 (raster tiers first, RT Overdrive as the
long-horizon goal). Reference material: six in-game screenshots supplied 2026-07-25 —
daytime waterfront, backlit vegetation with sun flare, dense daytime street, **wet neon
street at night**, neon sign wall, daytime food-stand street.

Working branch: `feat/aaa-graphics`. Target hardware: RTX 4090 / i9-12900K.

## Non-negotiables

1. **No existing functionality is lost.** Every change ships behind an `r_*` HVar whose
   default preserves today's behaviour until the new path is validated, matching the
   convention already used by `r_giGpu*`, `r_gpuCull*`, `r_useGIAO`. Old code paths are
   retired only after a side-by-side capture comparison.
2. **D3D11 remains the reference backend.** Everything through Phase 5 is D3D11-first.
   Ray tracing (Phase 6) is the only work that requires the D3D12 backend, which is still
   blocked on its own parity work.
3. **Each phase is independently shippable and verifiable** via editor-bridge frame
   captures of a fixed camera pose, before/after.

## What the engine already has

Unusually strong for a solo engine, and none of it needs replacing:

- Hillaire/Bruneton precomputed atmosphere (transmittance, multi-scattering, sky-view,
  aerial-perspective froxels)
- Froxel volumetric fog 128×72×64 with temporal reprojection, sun + local light
  contribution, per-froxel shadow sampling, emissive injection
- Voxel radiance clipmap diffuse GI (4 levels, 128³) with temporal resolve
- Screen-space reflections with NRD RELAX denoising
- TAA with velocity buffer; DLSS via Streamline (DLSS-G and Reflex also vendored)
- PCSS soft shadows, 4 cascades; screen-space contact shadows (implemented, off)
- Deferred decals writing albedo + roughness/metallic
- Physical bloom, ACES-fitted tonemap, auto-exposure histogram, Bokeh DoF, colour grade,
  vignette, chromatic aberration, scRGB HDR output
- Clearcoat / anisotropic / sheen / screen-space SSS material models
- GPU frustum + occlusion culling with a Hi-Z pyramid, GPU particles, volumetric clouds
- Vendored and ready: NRD, OIDN, HBAO+, Streamline

## The three structural deficits

Feature *breadth* is not the problem. Three foundational gaps cap the achievable image
quality regardless of what else is added.

### D1 — No image-based lighting whatsoever

`getIBLContribution` in `PBRutils.shader:11-24` is commented out, along with all three of
its call sites (`:311`, `:425`, `:494`). Its inputs — an irradiance cubemap, a prefiltered
specular cubemap, a BRDF LUT — do not exist anywhere in the codebase. There are no
reflection probes, no environment cubemaps, no SH irradiance, no split-sum DFG LUT.

Ambient is a single flat term: `albedo * g_atmosphere.ambientLight` (`PBRutils.shader:306`),
diffuse only. Indirect specular comes exclusively from SSR, and SSR's only fallback on a
miss is a 12-step cone trace through the GI voxels (`SSR.shader:177-224`).

**Consequence, concretely.** Reference screenshot 4 — the wet neon street — is currently
unreachable. Traced step by step:

1. A wet road pixel gets smoothness ≈0.96, so SSR runs.
2. Its near-mirror specular ray points up and forward, and exits the top of the screen
   within a few of the 28 march steps.
3. Off-screen exit deliberately takes no screen-space fallback (`SSR.shader:316-320`) —
   the `lastInScreenTex` fallback was disabled because it caused vertical stripe artifacts.
4. It falls through to the voxel cone trace, which returns near-black, because: the finest
   voxel is 0.875 m and directionless (a sign reflects as a ~1 m colour blob, not a legible
   sign); moving objects are excluded from the voxel world entirely; and **emissive
   textures never reach GI at all** (see D3).
5. There is no sky fallback. `SSR.shader` includes `Atmosphere` (line 14) but calls no
   atmosphere function, and sky pixels can never register as SSR hits by construction.

Net result today: **a wet road reflects the sky as black**, and an off-screen neon sign as
nothing. Every glossy surface in the reference shots — chrome arm, car paint, glass,
puddles — depends on coherent environment response that the engine cannot currently
produce.

This is the highest-value fix in the entire plan.

### D2 — Hard ceiling of ~16 local lights, and no light culling

`r_maxPointLights` = 16, `r_maxSpotLights` = 16 (max 256), selected closest-N by camera
distance (`SceneRenderer.cpp:53-54`). Shadow-casting lights of *all* types including the
sun are capped at `MaxShadowCasters = 4` (`SceneRenderer.cpp:24`).

There is no tiled or clustered lighting. Local lights are drawn as **one
`DrawIndexedInstanced` of a sphere volume per light**, with `DepthBufferState::DepthNone`
(`SceneRenderer.cpp:3383`), so every pixel in a light's screen footprint runs the full
pixel shader — including an inline 8–20 step volumetric ray march. Two overlapping 30 m
lights near camera each shade most of the screen. There is also no per-light frustum cull:
a light directly behind the camera still pays a full draw.

Reference screenshots 4 and 5 are streets containing *hundreds* of emissive light sources.
16 is not close.

### D3 — Energy and correctness defects that flatten every surface

Individually small, collectively responsible for the "not quite right" look:

| Defect | Location | Effect |
|---|---|---|
| Diffuse BRDF missing its `1/π` — `return diffuseColor;// / PI` | `PBRutils.shader:26-29` | Diffuse ~3.14× hot relative to specular, everywhere. Kills specular separation. |
| No multi-scatter / Kulla-Conty energy compensation | `PBRutils.shader` | Rough metals lose energy, look dull |
| F0 hardcoded `float3(0.04)`, no specular/IOR/reflectance control | `PBRutils.shader:7` | No authored specular response |
| AO applied to *pre-lighting albedo*, before `RenderLights()` | `SceneRenderer.cpp:2742-2767` | Direct light is added un-occluded on top; AO darkens albedo instead of occluding indirect |
| HBAO+ invoked with `normals = nullptr` | `SceneRenderer.cpp:2747` | Runs depth-only, no normal reconstruction — much weaker AO |
| No specular occlusion at all | — | Glossy surfaces don't darken in cavities |
| `ApplyNormalMap` never normalizes, despite its own comment | `Utils.shader:28-31` | Static meshes write **unnormalized** normals to the GBuffer |
| Green-channel flip inconsistent: static passes `flipY=true`, animated omits it | `DefaultPixel.shader:241` vs `DefaultAnimated.shader:310` | The same normal map renders inverted on skinned vs static meshes |
| No previous-frame bone transforms | `Global.shader:272-275` (one bone array) | **Skinned meshes emit zero motion vectors** → characters ghost under TAA and DLSS |
| `MaxShadowCasters` sorted farthest-first | `SceneRenderer.cpp:1845-1851` | With >4 casters the *nearest* lights are dropped — potentially the sun |
| Cascade blend can index unbound cascade 4 | `ShadowUtils.shader:198` | Guard is `i < MAX_SHADOW_CASCADES-1` (5) but only 4 cascades exist |
| SSS gate never compares against `MATERIAL_MODEL_SSS` | `SubsurfaceScattering.shader:76` | Clearcoat/aniso/sheen surfaces also get skin-blurred and warm-tinted |
| **TAA reprojects with an un-negated Y velocity.** `CalcVelocity` returns clip-space Y-up (`Utils.shader:77,82` — the flip is commented out) and TAA adds it straight to a UV-Y-down texcoord | `TAAResolve.shader:63` | **Vertical reprojection is inverted.** Both other consumers of the same buffer *do* negate Y (`Streamline.cpp:350` `mvecScale={1,-1}`, `NRDInterface.cpp:885`), which confirms the convention |
| TAA variance clip uses `gamma = 0.5` while its own comment says 1.25 is standard | `TAAResolve.shader:104,123` | Over-tight clip eats legitimate detail |
| `TAA::ResetHistory()` implemented, never called anywhere | `TAA.cpp:53-56` | Camera cuts and scene loads carry stale history |
| **Aerial perspective depth axis mismatched.** LUT integrates to `MAX_DIST_M = 32000`, apply pass looks up `w = depth/100000` | `AtmosphereAerialPerspectiveLUT.shader:112` vs `...Apply.shader:46` | Every pixel samples a slice integrated to ~⅓ its true distance → **atmosphere systematically ~3× under-hazed**, partly masked by an artistic sky lerp |
| `PostFog` ends with `saturate(foggedAlbedo)` | `PostFog.shader:215` | **Clamps HDR to 1.0 before bloom, auto-exposure and tonemap** — destroys highlight headroom on every fogged pixel |
| Cloud shadows re-march the cloud volume with the *old, inverted* erosion curve | `Deferred.shader:104` (`lerp(1.22,0.78,…)`) vs the fixed `VolumetricClouds.shader:131` (`lerp(0.55,1.45,…)`) | World cloud shadows are computed from a **different density function** than the visible clouds |
| `r_colourFilter` is uploaded but the shader multiplies by a hardcoded `static float3` instead | `ColourGrade.shader:38` vs `SceneRenderer.cpp:1917` | The cvar is inert; grading colour filter cannot be changed |
| Colour-grade contrast pivots on `ACEScc_MIDGRAY` while operating on linear HDR | `ColourGrade.shader:80` | Pivot is in the wrong space |
| Post shaders declare `PointSampler : register(s3)`, but s3 is bound to **anisotropic + mirror** | `Tonemap/ColourGrade/Vignette/TAAResolve` vs `GraphicsDeviceD3D11.cpp:714-720, 2636` | Every post pass that thinks it is point-sampling is not. Point is s2 |
| DLSS is evaluated *after* TAA and *after* bloom, and `r_taa` is not gated on DLSS | `SceneRenderer.cpp:2824` (TAA), `:2839` (bloom), `:2927` (DLSS) | DLSS receives temporally-resolved, bloom-composited colour instead of raw jittered render-res colour, and **two temporal accumulators run in the same frame** |
| FXAA runs pre-tonemap on unbounded linear HDR at 3.9-era preset 6 | `SceneRenderer.cpp:3026-3034` | Fixed relative thresholds behave inconsistently across the exposure range |
| Emissive textures computed on CPU, never read by the shipping voxelize shader | `DiffuseGIVoxelize.shader` declares `emissiveUvRect` but never reads it; only `DiffuseGIVoxelizeEval.shader` does, gated on `r_giGpuMaterialEval` (default **false**) | **Neon signs contribute nothing to GI** unless their emission is a material scalar above 0.05 |
| `SSRResolve.shader` is a passthrough — entire temporal resolve is `#if 0` | `SSRResolve.shader:47-157` | `_ssrHistory` is written every frame and never sampled; all temporal burden on NRD |
| TAA and FXAA both on by default | `r_taa`=true, `r_fxaa`=1 | Double AA, unnecessary softening |

## Reclaimable budget

This plan is largely self-funding — the new features are paid for by waste already present,
not by a bigger frame budget.

**Shadow maps: ~2.1 GB → ~270 MB.** *(Done in P0-B, @a1f7ce4.)* `ShadowMap::Create`
unconditionally allocated *both* an `R32_TYPELESS` depth map **and** a paired `R32_FLOAT`
colour render target (`ShadowMap.cpp:33-59`). At the hardcoded 8192²
(`DirectionalLight.cpp:13`) that is ~268 MB each, ~536 MB per cascade, **~2.1 GB for the sun
alone**.

Correction to an earlier reading of this: the colour target is **not** vestigial. Point
lights genuinely need it — the volumetric scattering path copies their six faces into a
`TextureCubeArray` and needs a plain non-typeless source (`SceneRenderer.cpp:1701-1710`).
What *was* dead is its use on directional cascades and spot maps, which are only ever
sampled through the depth SRV. It is now opt-in per shadow map (point lights only), and the
cascade resolution is `r_shadowMapResolution` (default 4096). Combined: 2147 MB → 268 MB,
**~1.88 GB reclaimed**, with shadow quality visually unchanged.

Related fix in the same area: `r_shadowFilterMaxSize`/`r_penumbraFilterMaxSize` are in
texels and PCSS multiplies them by `texelSize`, so holding a constant world-space penumbra
means scaling them *proportionally* to resolution. The code did `8192 / size` — inverted —
and then doubled it again above 1.0, so it was only ever correct at exactly 8192. Any
attempt to lower the resolution before this would have produced an 8× wider penumbra.

**GBuffer: 72 → ~24 bytes/pixel.** Three of six targets are full `R32G32B32A32_FLOAT`
(`GBuffer.cpp:50,65,83`): material, normal+depth, and world position. World position is
entirely redundant — reconstructible from depth and the inverse view-projection. Normals
are stored unencoded and unnormalized in FP32 where octahedral R16G16 is standard. At 4K
this is ~596 MB of MRT traffic re-written every frame. Target layout: albedo+AO (RGBA8),
octahedral normal + roughness + metallic (RGBA16), features (RGBA8), velocity (RG16F),
depth. *Caveat: many shaders read `GBUFFER_POSITION.xyz` and `GBUFFER_NORMAL.w`, so this
is a real refactor touching ~15 shaders — split into an easy format-narrowing step first,
then position removal.*

**Shadow filtering.** PCSS runs on *all four* cascades including the most distant, at
`r_shadowSamples`=32 → roughly 128 taps/pixel (blocker search + filter), plus a fixed
4-tap box blend at 40% on top (`ShadowUtils.shader:41-51`). Distant cascades do not need
PCSS; the per-cascade deterministic PCF alternative already exists but is `#if 0`
(`ShadowUtils.shader:54-73`).

**Dead work every frame.** The Hi-Z depth pyramid is built unconditionally
(`SceneRenderer.cpp:1435`) but its only consumer is GPU occlusion culling, which is gated
on `r_gpuCullEnable` — **default false**. In the shipping configuration the HZB is built
every frame and read by nobody. `DepthPrePass.shader` exists and is never loaded.
`BuildShadowMask.shader` is dead. `BasicDenoise.shader` is loaded and never used.
`_ssrHistory` is copied every frame and never sampled.

**Per-directional-light full-screen copy.** `RenderDirectionalLights` does
`_lightAccumulationBuffer->CopyTo(_beautyRT)` *inside* the per-light loop
(`SceneRenderer.cpp:3257`), so N suns cost N full-screen copies.

**~7+ redundant full-resolution RT copies per frame.** `GetDiffuse()→_beautyRT`,
`→_atmosphereRT`, `_beautyRT→_waterRT`, `_waterRT→_beautyRT`, `_fogBuffer→_beautyRT`,
`_subsurfaceIntermediateRT→_beautyRT` (twice), plus 4–5 more in the overlay chain where
each pass does a full `CopyTo(beauty)` round trip (`SceneRenderer.cpp:3005, 3012, 3021,
3031`) instead of a ping-pong pair.

**The sky is rendered twice per frame at full resolution** (`SceneRenderer.cpp:2329-2356`),
with a full-res copy between the two passes, purely so the fog pass has a sky texture.

**Terrain costs ~72 texture fetches per pixel** — 6 map types × 4 material layers × 3
triplanar taps (`VolumetricTerrainSurface.shader:206-247`) — plus 25 `SetTexture2D` calls
per chunk per LOD per frame, with all 3 LODs kept resident simultaneously.

**Cloud shadows re-march the entire cloud volume per-pixel inside the deferred sun pass**
(`Deferred.shader:112-149`), on top of the half-res cloud march itself. A cached cloud
shadow map would remove one of the two.

## Phased plan

### Phase 0 — Correctness and reclamation

Cheap, low-risk, and improves the *existing* renderer before any new feature lands.

- Restore `1/π` on the diffuse lobe; re-balance light intensities and exposure defaults so
  existing content doesn't get darker. Behind `r_pbrEnergyFix`.
- Add multi-scatter GGX energy compensation and a DFG LUT (the LUT is also Phase 1's
  split-sum input, so build it here).
- Fix `ApplyNormalMap` to normalize; unify `flipY` across static/animated/graph paths.
- Add `g_boneTransformsPrev` and a previous-bone-matrix upload; emit real skinned motion
  vectors. Removes character ghosting under TAA/DLSS.
- Move AO to modulate indirect only, after lighting; pass GBuffer normals to HBAO+.
- Fix the `MaxShadowCasters` sort to nearest-first; fix the cascade-blend bound; fix the
  SSS model-id gate.
- Make contact shadows shippable, then enable by default. They are **not** merely switched
  off — they were disabled because the interleaved-gradient-noise jitter flickers on
  volumetric-terrain slopes under TAA (`SceneRenderer.cpp:158-170`). Fix is a world-stable
  or blue-noise sample source (the blue-noise texture is already loaded and used by SSR,
  clouds and volumetrics), then flip the default.
- Fix the TAA velocity Y sign, raise the variance-clip gamma toward 1.25, add motion-vector
  dilation and a Catmull-Rom history filter, and call `ResetHistory()` on camera cuts and
  scene loads.
- Re-sequence DLSS to consume raw jittered render-resolution colour *before* TAA and bloom,
  and gate `r_taa` off when DLSS is active so only one temporal resolver runs.
- Fix the aerial-perspective `MAX_DIST_M` mismatch, and remove the `saturate()` at the end
  of `PostFog` that clamps HDR before bloom/exposure/tonemap.
- Fix the inverted cloud-shadow erosion curve so world shadows match the visible clouds.
- Fix the `r_colourFilter` inert-cvar bug and the post-chain `s3` sampler mismatch.
- Drop the redundant shadow colour RT; move cascades to 2048²–4096² with logarithmic
  splits; add normal-offset bias alongside the existing slope-scaled depth bias; restrict
  PCSS to near cascades.
- Default `r_fxaa` off when `r_taa` is on.
- Narrow GBuffer formats (FP32→FP16/packed) without yet removing the position target.

**Acceptance:** identical-or-better reference captures, >1.8 GB VRAM freed, measurable ms
saved, no visual regression at a fixed camera pose.

### Phase 1 — Image-based lighting and the reflection chain

The single largest visual delta. Fixes D1.

- `ReflectionProbeComponent`: cubemap capture (baked, with optional time-sliced realtime
  refresh), GGX-importance-sampled prefiltered specular mip chain, SH-9 irradiance.
- Probe blending: per-pixel selection from a small clustered probe list with box/sphere
  influence volumes and parallax-corrected reflection vectors.
- Sky/atmosphere as the infinite fallback probe. The atmosphere sky-view LUT already
  exists and `SSR.shader` already includes it — wiring it as the SSR miss fallback is a
  small change with a large payoff.
- Full reflection chain with proper weighting: SSR hit → probe → sky, roughness-aware,
  with screen-edge fade and horizon occlusion.
- Replace the flat ambient constant with SH sky irradiance.
- Add specular occlusion derived from AO + roughness.
- Re-enable `SSRResolve`'s temporal accumulation, or delete it and formally delegate to
  NRD; either way stop the dead history copy.
- Add per-material specular/F0/IOR and an alpha-cutoff parameter.

**Acceptance:** a wet road reflects a legible sky gradient and off-screen signage; a rough
metal sphere in an empty scene is lit plausibly; reference screenshot 4's character of
reflection is reproducible.

### Phase 2 — Many lights

Fixes D2, enables the neon-street look.

- Clustered light culling in compute. Reuse the froxel grid layout the volumetric system
  already establishes so fog and surface lighting share cluster assignment.
- Convert deferred local lighting from per-light volume draws to a single full-screen or
  compute pass consuming cluster light lists. Target: hundreds of lights.
- Remove the inline per-light volumetric march from the surface shaders — the froxel
  system already does this correctly and better.
- Shadow atlas with LRU caching and static-light caching, so dozens of shadowed local
  lights are affordable instead of four.
- Per-light frustum and screen-area culling.
- Physical light units (lumens/candela) with exposure coupling, so authored intensities
  stop being arbitrary.
- Extend the forward/transparent path to the same cluster lists (it is currently a fixed
  16+16 array) and give transparents real shadows — today `depthValue` is hardcoded `1.0f`
  so **transparent surfaces receive no shadows at all** (`DefaultPixel.shader:367-409`).

**Acceptance:** 200+ local lights at target framerate; a neon-lit street where the signage
actually lights the ground.

### Phase 3 — Surface realism

- **Wetness/porosity system.** `WeatherSurfaceParams` already carries `wetness`,
  `puddleAmount`, `dirtAmount`, `snowMelt` — but `dirtAmount`, `snowMelt`,
  `temperatureBias` and `precipitationIntensity` have **no shader consumer at all**, and
  `wetness` only gates rain drips. Needs: albedo darkening (currently absent entirely,
  which is why wet surfaces look plastic-wrapped), roughness reduction, porosity per
  material, puddle normals and rain-impact ripples (`AutoPuddles` writes no normals, so
  puddles are flat mirrors on the dry surface normal), shelter occlusion, and accumulation
  /drying state. Snow already does albedo blending correctly and is the model to follow.
- **Parallax occlusion mapping + parallax interior mapping.** Today there is only
  single-tap offset parallax with a hardcoded scale (`Utils.shader:63-70`, 0.018 static vs
  0.03 animated — inconsistent). Interior mapping for building windows is the cheapest
  large win available for city density.
- Decal normals. Currently impossible because `GBUFFER_NORMAL.a` carries packed depth and
  D3D11 has no per-RT blend mask — the Phase 0/1 GBuffer repack removes that blocker.
  Also add decal sorting (there is none) and mesh decals.
- Detail maps and triplanar for regular meshes (both exist, terrain-only today).
- Vegetation: wind vertex animation (**no vertex displacement exists anywhere** — the wind
  vector is uploaded and read by nothing) and two-sided translucent leaf shading (no
  `SV_IsFrontFace` anywhere in the tree).
- Upgrade SSS to pre-integrated + transmission/thickness.
- Expand the material graph: it has 15 nodes and only `Add`/`Multiply`/`Lerp`/`OneMinus`
  for math, no vertex output pin, no time/world-position/screen inputs, no UV transform,
  and no custom-HLSL node — which is why wind cannot be authored.
- Water: it is 4 hardcoded Gerstner waves that ignore wind, `WaterMask` uses *different*
  wave constants than the shaded surface, foam is commented out, caustics return a red
  debug value and are never called, there are no shadows on water
  (`float depthValue = 1.0f; // CalculateShadows(...)`, `Water.shader:673`), and the result
  is `saturate()`d to LDR before tonemapping. It also writes no G-buffer, so it receives no
  aerial perspective or volumetric fog at all.
- Weather surface response: wetness is an authored per-preset constant, not simulated —
  there is no accumulation/drying and no shelter/rain-occlusion map, so surfaces under a
  roof get identical wetness. There are also no screen-space raindrops on the lens.

### Phase 4 — Cinematic post

The reference shots are photographic; the post chain is currently the weakest link
relative to how good the lighting could be.

- **Motion blur — does not exist at all.** No camera and no per-object blur; `BlurEffect`
  asserts `"Radial blur not yet implemented"` (`BlurEffect.cpp:19`). The velocity buffer
  already exists, so a tile-max + gather reconstruction filter is straightforward. Depends
  on Phase 0's skinned motion vectors to work on characters.
- **Bloom is one mip.** A single quarter-res RT with one horizontal + one vertical 9-tap
  Gaussian (`Bloom.cpp:15-26`, `GaussianBlur*.shader`) — effective radius ~32 native
  pixels with a hard cut. Replace with the Jimenez/COD progressive downsample + tent
  upsample chain so glow actually spreads across the frame the way neon does in
  screenshots 4 and 5. Also fix the point-sampled prefilter (1-in-16 undersampling of
  full-res HDR → highlight flicker) and add energy conservation.
- **Lens flare, lens dirt, anamorphic streaks — none exist.** Screenshot 2 is largely a
  lens-flare shot. Add physically-plausible ghosts + a dirt/streak layer driven by the
  bloom chain.
- **Colour grading has no 3D LUT** and no lift/gamma/gain, white balance, curves, or
  post-process volumes. Add ACEScc-space grading with `.cube` LUT support and per-volume
  blending — this is how the reference achieves its distinct day/night colour identity.
- **Tonemap is the Narkowicz ACES approximation**, not RRT+ODT. Add AgX and/or a proper
  ACES fit; add HDR10/PQ output alongside the existing scRGB.
- **Auto exposure is a single average-log-luminance mean**, metered on the *post-bloom*
  image and applied late inside colour grading. Move to real histogram metering with
  percentile rejection, a metering mask, separate light/dark adaptation rates, EV bias, and
  physical EV100 calibration coupled to Phase 2's physical light units.
- **No film grain, no output dithering, no sharpening.** The SDR backbuffer is
  `R8G8B8A8_UNORM` with no dither, so gradients band. Add triangular/blue-noise dither
  (the blue-noise texture is already loaded), film grain, and CAS/RCAS.
- Make DoF physically derived (focal length / aperture / sensor) with autofocus; it is
  currently a unitless blur scale and defaults off.
- Give the hardcoded vignette cvars (`Vignette.shader:33-38` is all `#define`s and runs
  unconditionally every frame), and expose the whole post chain in the settings UI — today
  it has no entries for TAA, FXAA, DLSS, bloom, DoF, vignette or chromatic aberration.
- Collapse the 4–5 full-res copies in the overlay chain into a ping-pong pair.

### Phase 5 — Scale and density

Screenshot 3 shows kilometres of dense city. The current caps and streaming model do not
support that.

- **Turn GPU-driven culling on and make it real.** `r_gpuCullEnable` defaults **false**;
  when on, results round-trip through a CPU staging readback with 2-frame latency and a
  large pile of stability heuristics to hide it (`GpuVisibilityCulling.cpp:539-592`). The
  indirect path is cosmetic — `r_gpuCullUseIndirectDraw` defaults false and fills the args
  buffer *from the CPU* via `UpdateSubresource` (`Scene.cpp:1843-1874`). Target: GPU-written
  counts, instance compaction, no readback.
- **Revive the depth pre-pass.** `DepthPrePass.shader` exists and is never loaded; the Hi-Z
  pyramid is built every frame and, in the default config, read by nobody.
- **LOD quality.** Selection is a crude distance band (`r_lodPartition` 250 m) with no
  screen-space error metric and no dithered crossfade, so LOD pops. HLOD exists and works
  (name-convention clusters, 400 m switch, with mesh streaming). **Imposters do not exist**
  — they are the standard answer for distant city blocks and dense foliage.
- **Texture streaming does not exist.** `TextureLoader` is a stub returning `nullptr`;
  materials hold fully-resident textures with no mip budget or residency manager. Package
  streaming is file-level and synchronous on the requesting thread. For a dense city this
  is "load everything up front" and will be the wall long before the GPU is.
- **Terrain.** Three LODs are extracted and kept resident simultaneously with no cross-LOD
  stitching (seams), 72 texture fetches per pixel, and only 4 material layers globally.
  Load stutter of 30 s+ is recorded in comments, with GPU upload named as the bottleneck.
- **Volumetric fog reaches only 128 m** (`kFarDepthM`; several comments still claim 256 m).
  Beyond that it falls back to an analytic continuation. Extend the range or add a second
  coarse cascade for the long draw distances the reference shots show.
- **Clouds terminate 600 m out** — the volume is a 1.2 km × 100 m slab
  (`SceneRenderer.cpp:90-91`), so there is no horizon cloud deck. They also have no temporal
  reprojection, only per-pixel jitter, which is the classic recipe for boiling under motion.
- No mesh shaders, no bindless, no virtual texturing. Binding uses an *implicit slot
  counter* that must be manually saved and restored (see the "CRITICAL" comment at
  `SceneRenderer.cpp:3717-3721`) — a recurring bug source that bindless would eliminate,
  though that realistically arrives with D3D12.

### Phase 6 — Ray tracing (long horizon)

Gated, in order:

1. Finish D3D12 parity. Currently blocked — the terrain `ExecuteIndirect` TDR, and GI is
   entirely non-functional on D3D12 (`DiffuseGI.cpp:1014-1019`: *"D3D12-native GI lands as
   part of Phase B5"*).
2. DXR plumbing: `lib_6_3+` targets in the shader compiler (it emits `*_6_0` today, no
   library profile), acceleration-structure build, bindless descriptors, shader tables.
3. RT shadows → RT reflections → ReSTIR GI → path tracing. NRD is already vendored for
   denoising. DLSS Ray Reconstruction would need a newer Streamline (`sl.dlss_d` is absent
   from the vendored SDK).

Nothing in Phases 0–5 depends on this, and Phases 0–5 are what close most of the gap.

## Recommended starting point

**Phase 0, then Phase 1.** Phase 0 is a couple of days of small, independently verifiable
correctness fixes that make the *existing* renderer visibly better and free over 1.8 GB of
VRAM plus real frame time — and several of its items (the DFG LUT, the GBuffer narrowing,
the energy fix) are prerequisites the later phases need anyway. Phase 1 then delivers the
single largest visual delta toward the reference material.

Phase 2 is the other half of the Cyberpunk look and is the natural follow-on, but it is a
substantially bigger piece of engineering than 0 or 1 and benefits from Phase 0's GBuffer
work landing first.

Ordering note: Phases 0–5 are all D3D11 and none of them depend on Phase 6, so the D3D12
parity work can proceed independently whenever it is picked back up.

## Deliberately out of scope

- Rewriting the voxel GI. It works, it is tuned, and its failure modes are documented.
  Phase 1's IBL covers the specular hole it was never designed to fill; a probe/surfel
  irradiance cache is a Phase 6-era consideration.
- MSAA. Hardcoded off (`GraphicsDeviceD3D11.cpp:33`), all the MS paths are dead, and the
  deferred pipeline would need `Texture2DMS` throughout. TAA/DLSS is the correct answer.
- Replacing the material graph with a full node editor.
