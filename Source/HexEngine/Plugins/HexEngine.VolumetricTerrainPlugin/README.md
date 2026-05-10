# HexEngine Volumetric Terrain Plugin

This plugin adds a chunked 3D SDF terrain system that is separate from the legacy heightmap terrain path.

## Density Convention

- `density < 0`: solid terrain
- `density >= 0`: empty space

The marching mesher extracts an isosurface at `density == 0`.

## Chunk Border Handling

Each chunk stores voxel points on its full border (`resolution + 1` points per axis) and uses deterministic world-space generation.
When a brush edits a chunk, neighbouring chunks are also marked dirty so border triangles are regenerated consistently.
This avoids visible seams/cracks at chunk boundaries.

## Save Format (Versioned)

Serialized under `volumetricTerrain` with `version = 1`:

- `generation`: deterministic generation parameters (seed, chunk size/resolution, noise/cave settings)
- `editedChunks`: sparse list of chunk blobs containing edited density/material arrays only

This avoids writing a full generated world volume to disk.

## Notes

- CPU SDF generation + CPU marching meshing are the reliable baseline path.
- Real-time brush sculpting is implemented through `VolumetricTerrainComponent`.
- Optional GPU edit path has an API scaffold and placeholder compute shader for future extension.
