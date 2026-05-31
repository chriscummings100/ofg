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

The visible seed terrain is not Dual Contouring yet. It exists to prove rendering,
controls, chunk boundaries, and the test workflow before adding the harder terrain
system.

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

The simplex module exposes analytic gradients, and the seed field exposes
`sampleAt(position)` so terrain systems can get signed density and gradient at any
world-space position. `heightAt(x, z)` is now a compatibility query that scans a
density column for the highest zero crossing so player grounding can keep working
until movement is density/mesh aware.

The current runtime mesher is deliberately simple: it scans each x/z column in a
vertical stack of density chunks, finds the highest solid-to-air crossing, and emits
a shared-vertex surface mesh with smooth normals. `TerrainChunkStreamer` keeps a
square x/z neighborhood of density chunks around a target entity, centers its
vertical chunk-offset stack on the target chunk y coordinate, and replaces the
visible render chunks as the player crosses chunk boundaries. Render chunks are
keyed by their x/z column at y=0, while loaded density chunk keys remain fully 3D.

The first Dual Contouring foundation lives in `src/engine/world/dualContouring.ts`.
It can extract Hermite edge intersections for one cell, place one vertex per active
cell with centroid or QEF placement, and build an initial chunk mesh by connecting
cell vertices around sign-changing grid edges. It is tested against flat planes,
diagonal planes, and sphere-like fields, but the runtime streamer still uses the
highest-surface mesher until cross-chunk stitching and edit-driven rebuild behavior
are ready.

The intended Dual Contouring boundary is:

- Density field interface: sample signed density and gradients at world positions,
  with materials added once the surface representation is stable.
- Chunk sampler: evaluate density at deterministic 33x33x33 chunk lattice points.
- Mesher: produce compact vertex/index/material buffers with smooth normals.
- Renderer: upload chunk meshes without knowing how they were generated.

The first implementation can be TypeScript for iteration speed. Rust/WASM becomes
worthwhile once the mesher contract and test fixtures are stable.

## Shader Direction

Shader source sits behind `tools/build-shaders.mjs`. The current input is
`src/engine/render/shaders/uber.wgsl`, and the generated runtime artifact is
`src/generated/render/uberShader.ts`.

The renderer imports shader source and entry-point metadata from the generated
artifact rather than embedding shader text. This keeps the current WGSL path simple
while leaving one clear build boundary for Slang-generated WGSL or SPIR-V outputs
later.

The first material model is intentionally pre-PBR: mesh vertex color multiplied by
an albedo factor and optional albedo texture sample, plus specular color and
specular factor. The shader uses a simple Lambert diffuse plus Blinn-Phong specular
model. `Texture` stores CPU-side rgba8 data that the WebGPU renderer uploads into
GPU-owned texture resources.

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
