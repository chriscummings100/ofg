# Architecture

## Principles

- Keep the browser client native and lightweight.
- Put deterministic logic in small modules with direct unit tests.
- Let browser glue stay thin: DOM, input forwarding, WebAssembly startup, and UI.
- Prefer explicit data contracts over hidden engine state.
- Move long-lived world, simulation, terrain, render extraction, and WebGPU
  ownership into Rust.

The detailed migration path is tracked in
[RUST_ENGINE_PLAN.md](RUST_ENGINE_PLAN.md). The TypeScript scene/render systems
are now transitional prototype infrastructure, not the long-term engine center.

## Current Layers

```text
src/app
  Browser lifecycle, canvas setup, frame loop, HUD state, and current scene
  bootstrapping. Long term, this becomes the TypeScript shell around the Rust
  engine.

src/engine/input
  DOM input tracking with edge-triggered key events and mouse deltas.

src/engine/world
  Deterministic terrain fields, 3D simplex noise, 3D density chunks, terrain
  edits, and mesh generation.

src/engine/math
  Small vector and matrix primitives.

src/engine/render
  CPU-side render resources, scene render components, RenderWorld extraction, and
  WebGPU resource setup/draw submission. Runtime terrain chunks now enter this
  path through a Rust-backed terrain render-packet adapter instead of a scene
  terrain component. This renderer is current runtime infrastructure, but
  Rust/wgpu is the target renderer.

src/engine/render/shaders
  Shader source inputs. The current `uber.wgsl` is the single shader contract for
  render items.

src/generated
  Deterministically generated TypeScript artifacts used by runtime code.

src/engine/scene
  Global active Scene, Entity tree, Component lifecycle, Transform hierarchy, and
  CPU-side ResourceStore. This model is useful for the current playable seed, but
  should not receive new high-volume world ownership.

src/game/components
  Game-specific compatibility/browser bridge components, currently
  RustPlayerController and TerrainCoreWorkerStreamer.
```

## Scene Model

The current TypeScript prototype uses one global active `Scene`. The scene owns a tree of `Entity`
objects, each entity has a `Transform`, and behavior/renderability is attached with
`Component` objects. This is intentionally a small scene graph and component model,
not a general-purpose ECS.

The current playable is partly backed by this model:

- A terrain entity owns `TerrainCoreWorkerStreamer`, a browser bridge that
  executes Worker jobs selected by `terrain_core.wasm`. Visible terrain chunks
  are stored in `terrain_core.wasm` through `TerrainCoreRenderPacketStore`
  outside the scene component render path.
- A player entity owns `RustPlayerController`, which forwards input into
  `engine_core.wasm` and mirrors the Rust player transform back into the
  TypeScript scene for terrain streaming and the debug marker.
- A child marker entity owns `MeshRenderer` and is visible in debug fly mode.
- The runtime camera and main light come from a Rust render packet snapshot.
- `SceneRenderExtractor` still gathers TypeScript scene render items for
  `WebGpuRenderer`, can consume a Rust packet camera/light instead of
  `scene.activeCamera`, and can append external Rust terrain packet render items.

The detailed API and next rollout steps are tracked in
[SCENE_MODEL_PLAN.md](SCENE_MODEL_PLAN.md).

Future large-scale world state should move into Rust rather than expanding this
TypeScript scene graph. The Rust engine plan replaces the scene graph as the
authoritative home for entities, transforms, streaming, factory simulation,
render extraction, and eventually WebGPU resource ownership.

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

`TerrainCoreWorkerStreamer` keeps the playable browser terrain bridge thin:
`terrain_core.wasm` owns desired density/LOD0 sets, dependency coordinates,
in-flight work, stale generation rejection, ready/empty state, density storage,
mesh packet storage, packet pruning, and the worker-pool/request model. That
worker model includes slot assignment, request IDs, reset generations, and
completion validation. TypeScript still constructs browser Workers, but only
through a generic transport utility; the dev/smoke runtime is cross-origin
isolated and the playable bridge uses `SharedArrayBuffer`-backed density
dependency payloads when available. Workers still copy those payloads into their
local `terrain_core.wasm` density stores before contouring; Rust-managed wasm
threads are still future work. Loaded density chunk keys remain fully 3D.
Runtime terrain meshes carry position, color, normal, uv, material layer indices,
and material weights. A small mesh post-pass expands indexed triangles so each
triangle has a coherent local four-material palette for interpolation.
In the playable browser runtime, those chunk mesh payloads are written into and
pruned from a Rust-owned mesh packet store in `terrain_core.wasm`, then appended
to the `RenderWorld` by `TerrainCoreRenderPacketStore`. The old compiled
TypeScript `TerrainChunkStreamer`, `TerrainRenderer`, `TerrainRenderPacketStore`,
highest-surface mesher, and heightfield mesh path have been retired rather than
kept as parallel terrain owners.

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

The first implementation used TypeScript for iteration speed. That phase is now
over for scaling-sensitive systems: terrain, world state, render extraction, and
WebGPU ownership should migrate toward the Rust-first plan.

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
renders a blue gradient plus a sun disk in the direction of `RenderWorld.mainLight`.
The browser runtime now sources that light from the Rust render packet bridge.

## Testing Direction

- Unit tests cover deterministic math, Rust player/camera behavior, world, and
  mesh generation code.
- Shader tests verify generated shader metadata and the renderer vertex layout
  contract.
- Browser smoke tests cover canvas rendering, input toggles, resize behavior, and
  basic chunk streaming after moving the player across chunk columns.
- Golden fixture tests cover terrain meshing once voxel chunks exist.
- Performance tests should be explicit scripts with stable scene seeds, not hidden
  assertions inside regular unit tests.
