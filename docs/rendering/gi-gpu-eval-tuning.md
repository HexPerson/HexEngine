# GI GPU Eval Tuning Guide

This note captures the stable settings for the current GPU material-eval GI path and explains how to tune it safely.

## Recommended Baseline (Smooth + Good Quality)

Use this as the default runtime profile when testing GPU GI material evaluation:

- `r_giGpuVoxelize 1`
- `r_giGpuMaterialEval 1`
- `r_giGpuComputeBaseSun 1`
- `r_giGpuEvalTriangleBudget 7000`
- `r_giGpuEvalMaxVoxelTestsPerTriangle 64`
- `r_giGpuSunShadowMode 0`
- `r_giGpuSunShadowPerVoxel 0`

Why this baseline:

- Keeps triangle workload bounded to avoid heavy eval dispatch cost.
- Uses per-triangle sun visibility by default (much cheaper than per-voxel).
- Preserves per-triangle GPU albedo sampling correctness.

## Performance-First Preset

If a scene still stalls, reduce eval work first:

- `r_giGpuEvalTriangleBudget 4000`
- `r_giGpuEvalMaxVoxelTestsPerTriangle 8`
- `r_giGpuSunShadowMode 0`
- `r_giGpuSunShadowPerVoxel 0`

## Quality-First Preset

If headroom is available and quality is the focus:

- `r_giGpuEvalTriangleBudget 12000`
- `r_giGpuEvalMaxVoxelTestsPerTriangle 96`
- `r_giGpuSunShadowMode 1`
- `r_giGpuSunShadowPerVoxel 0`

Avoid `r_giGpuSunShadowPerVoxel 1` unless explicitly debugging, as it is expensive.

## Telemetry

Enable telemetry for diagnosis:

- `r_giTelemetry 1`

Useful fields in log output:

- `tri` / `cand`: injected triangle counts.
- `uploadBytes`: catches upload churn regressions.
- `evalTriBudget`, `evalMaxTests`, `sunShadowMode`, `sunShadowPerVoxel`: confirms active GI eval configuration.

Default telemetry cadence is intentionally low-noise (`r_giTelemetryLogFrames 300`).

## Known Notes

- `r_giGpuCandidateGen` currently performs broad candidate culling and may show little change in scenes already strongly clip-filtered on CPU.
- If performance drops while CPU GI timings remain low, check `uploadBytes` first; large recurring uploads indicate cache invalidation churn.
