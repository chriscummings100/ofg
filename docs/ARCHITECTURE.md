# Architecture

## Principles

- Keep the browser client native and lightweight.
- Put deterministic logic in small modules with direct unit tests.
- Let browser glue stay thin: DOM, input, WebGPU setup, and the game loop.
- Prefer explicit data contracts over hidden engine state.
- Add Rust/WASM when it removes real cost from terrain meshing or simulation.

## Current Layers

```text
src/app
  Browser lifecycle, canvas setup, frame loop, HUD state, and scene bootstrapping.

src/engine/input
  DOM input tracking with edge-triggered key events and mouse deltas.

src/engine/world
  Deterministic terrain fields, 3D simplex noise, 3D density chunks, terrain
  edits, and mesh generation.

src/engine/math
  Small vector and matrix primitives.

src/engine/render
  CPU-side render resources, scene render components, RenderWorld extraction, and
  WebGPU resource setup/draw submission.

src/engine/render/shaders
  Shader source inputs. The current `uber.wgsl` is the single shader contract for
  render items.

src/generated
  Deterministically generated TypeScript artifacts used by runtime code.

src/engine/scene
  Global active Scene, Entity tree, Component lifecycle, Transform hierarchy, and
  CPU-side ResourceStore.

src/game/components
  Game-specific behavior components, currently PlayerController and
  TerrainChunkStreamer.
```

## Scene Model

The engine uses one global active `Scene`. The scene owns a tree of `Entity`
objects, each entity has a `Transform`, and behavior/renderability is attached with
`Component` objects. This is intentionally a small scene graph and component model,
not a general-purpose ECS.

The current playable is backed by this model:

- A terrain entity owns `TerrainRenderer`.
- A player entity owns `PlayerController`.
- A child marker entity owns `MeshRenderer` and is visible in debug fly mode.
- A camera entity is assigned to `scene.activeCamera`.
- `scene.mainLight` defines the sun direction, color, intensity, and ambient term.
- `SceneRenderExtractor` builds plain `RenderWorld` data for `WebGpuRenderer`.

The detailed API and next rollout steps are tracked in
[SCENE_MODEL_PLAN.md](SCENE_MODEL_PLAN.md).

## Terrain Direction

The visible seed terrain now uses a same-LOD per-chunk Dual Contouring path. The
runtime streamer builds neighbor-aware chunk meshes with deterministic seam
ownership, so adjacent chunks do not both emit the same border quads. LOD
transitions and far-field terrain are still future work.

The terrain data model is 3D from the start. A terrain density chunk has 32 cells
per axis and 33 samples per axis, so adjacent chunks share boundary samples cleanly.
Baseline generation samples any `TerrainDensitySource`, and edits are applied on
top. The first edit operation is subtracting a sphere, which turns solid density
into air inside the sphere and sets up cave/mining-style operations.

The seed terrain generator is an implicit density field. Low-frequency x/z noise
sets a broad preferred surface height, then octave 3D simplex noise perturbs the
density near that surface:

```text
density(p) = p.y - largeFeatureHeight(p.x, p.z) - detail3D(p) * amplitude
```

The simplex module exposes analytic gradients, and the terrain generator exposes
`sampleAt(position)` so terrain systems can get signed density, gradient, biome
weights, and material weights at any world-space position. `heightAt(x, z)` is now
a compatibility query that scans a density column for the highest zero crossing so
player grounding can keep working until movement is density/mesh aware.

`TerrainChunkStreamer` keeps a square x/z neighborhood of density chunks around a
target entity, centers its vertical chunk-offset stack on the target chunk y
coordinate, and rebuilds the visible per-chunk Dual Contouring meshes as the
player crosses chunk boundaries. Loaded density chunk keys remain fully 3D.
Runtime terrain meshes carry position, color, normal, uv, material layer indices,
and material weights. A small mesh post-pass expands indexed triangles so each
triangle has a coherent local four-material palette for interpolation.

The Dual Contouring implementation lives in
`src/engine/world/dualContouring.ts`. It extracts Hermite edge intersections for
one cell, places one vertex per active cell with centroid or QEF placement, can
mesh a chunk-local surface, can mesh multiple chunks into one stitched debug mesh,
and can mesh one chunk with positive-neighbor apron data for runtime rendering. It
is tested against flat planes, diagonal planes, sphere-like fields, scaled/offset
chunks, winding reversal, underconstrained and out-of-cell QEFs, procedural-field
Hermite plane sanity, seam ownership, and invalid index invariants. QEF placement
rejects solves outside the owning cell and falls back to the Hermite centroid.
Runtime terrain currently uses centroid placement while QEF conditioning is still
being improved for noisy terrain.

The intended Dual Contouring boundary is:

- Density field interface: sample signed density and gradients at world positions,
  with materials added once the surface representation is stable.
- Chunk sampler: evaluate density at deterministic 33x33x33 chunk lattice points.
- Mesher: produce compact per-chunk vertex/index/material buffers with smooth
  normals and neighbor-aware boundary quads.
- Renderer: upload chunk meshes without knowing how they were generated.

The first implementation can be TypeScript for iteration speed. Rust/WASM becomes
worthwhile once the mesher contract and test fixtures are stable.

## Shader Direction

Shader source sits behind `tools/build-shaders.mjs`. The current input is
`src/engine/render/shaders/uber.wgsl`, and the generated runtime artifact is
`src/generated/render/uberShader.ts`.

The renderer imports WGSL source and entry-point metadata from the generated
artifact rather than embedding shader text. WGSL is the intended shader language
for this project because it is browser-native, direct, and familiar enough for
AI-driven changes.

The first material model is intentionally pre-PBR: mesh vertex color multiplied by
an albedo factor and optional albedo texture sample, plus specular color and
specular factor. The shader uses a simple Lambert diffuse plus Blinn-Phong specular
model. `Texture` stores CPU-side rgba8 data that the WebGPU renderer uploads into
GPU-owned texture resources.

Terrain uses checked-in Poly Haven CC0 materials imported into 16-layer global
texture arrays. The runtime currently loads albedo, normal, and roughness arrays;
the WGSL terrain path triplanar-blends albedo and roughness from up to four
material weights per vertex. Normal maps are loaded as renderer resources but are
not yet applied to lighting.

The sky is also shader-driven. `WebGpuRenderer` draws a full-screen sky pass before
scene geometry, reconstructs world rays from the inverse view-projection matrix, and
renders a blue gradient plus a sun disk in the direction of `scene.mainLight`.

## Testing Direction

- Unit tests cover deterministic math, player/camera behavior, world, and mesh
  generation code.
- Shader tests verify generated shader metadata and the renderer vertex layout
  contract.
- Browser smoke tests cover canvas rendering, input toggles, resize behavior, and
  basic chunk streaming after moving the player across chunk columns.
- Golden fixture tests cover terrain meshing once voxel chunks exist.
- Performance tests should be explicit scripts with stable scene seeds, not hidden
  assertions inside regular unit tests.
