# Material Graph v1

## Summary
Material Graph v1 adds graph-authored PBR materials inside the existing `.hmat` workflow.  
Legacy non-graph materials still load and render using the existing runtime path.
Legacy shininess/shininessStrength shader semantics are intentionally not supported in this workflow.

## Architecture
- Graph data model: `MaterialGraph`, `MaterialGraphNode`, `MaterialGraphPin`, `MaterialGraphConnection`, `MaterialGraphParameter`.
- Validation: `MaterialGraphValidator`.
- Compilation/apply: `MaterialGraphCompiler`.
- Editor UI: `MaterialGraphDialog` with node canvas + property panel.
- Loader integration: `MaterialLoader` reads/writes `graph` and `instance` blocks in `.hmat`.

Runtime remains authoritative on existing material fields:
- `textures`
- `properties`
- `renderer`
- `shaders`

The graph compiler bakes graph results into those runtime fields and reuses `EngineData.Shaders/Default.hcs` + `EngineData.Shaders/ShadowMapGeometry.hcs` for v1 safety.

## `.hmat` Format Additions
Optional top-level sections:

```json
{
  "graph": {
    "version": 1,
    "nodes": [],
    "connections": [],
    "outputs": [],
    "parameters": []
  },
  "instance": {
    "parentMaterialPath": "GameData.Materials/MyGraph.hmat",
    "overrides": []
  }
}
```

- `graph` defines structure/defaults.
- `instance` defines parent graph material + parameter overrides.

## Supported v1 Nodes
- Scalar constant
- Vector/color constant
- Texture sample
- TexCoord / UV
- Add
- Multiply
- Lerp
- OneMinus
- NormalMap
- Scalar parameter
- Vector parameter
- Texture parameter

Outputs:
- BaseColor
- Normal
- Roughness
- Metallic
- Emissive
- Opacity

## Compile / Apply Flow
1. Validate graph (cycles, outputs, type checks, broken refs, unsupported combos).
2. Evaluate graph defaults or instance overrides.
3. Bake into material runtime fields (`textures`, `properties`).
4. Save `.hmat`.

Instance overrides do not require shader recompilation in v1.

## Current Limitations
- v1 uses a safe master-material compile path (default engine shader) instead of full custom per-graph shader generation.
- Texture arithmetic is limited: texture multiply with scalar/vector tint/factor is supported, but add/lerp/one-minus with textures is rejected in v1.
- UI uses a minimal node canvas (functional, not advanced layout/subgraph tooling).
- Material instances currently compile/apply override data through loader/runtime paths; graph-structure authoring is done in graph parent materials.

## Extension Points
- Replace master-material bake with generated shader code per graph.
- Add richer type coercion and texture-channel math.
- Add subgraphs/functions and parameter grouping.
- Add richer preview controls and diagnostics.
