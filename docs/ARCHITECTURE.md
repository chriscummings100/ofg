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

src/engine/camera
  Legacy tested camera rig helpers. Runtime camera state now comes from scene
  entities and PlayerController.

src/engine/world
  Deterministic terrain field and mesh generation.

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
  Game-specific behavior components, currently PlayerController.
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

The seed terrain is a heightfield, not voxel Dual Contouring. It exists to prove the
rendering, controls, and test workflow before adding the harder terrain system.

The intended Dual Contouring boundary is:

- Density field interface: sample signed density and material at world positions.
- Chunk sampler: evaluate density at deterministic chunk lattice points.
- Mesher: produce compact vertex/index/material buffers.
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
an albedo factor, optional CPU-side albedo texture id, specular color, and specular
factor. The shader uses a simple Lambert diffuse plus Blinn-Phong specular model.
Texture sampling is a later renderer slice because `Texture` currently stores
metadata only.

The sky is also shader-driven. `WebGpuRenderer` draws a full-screen sky pass before
scene geometry, reconstructs world rays from the inverse view-projection matrix, and
renders a blue gradient plus a sun disk in the direction of `scene.mainLight`.

## Testing Direction

- Unit tests cover deterministic math, camera, world, and mesh generation code.
- Shader tests verify generated shader metadata and the renderer vertex layout
  contract.
- Browser smoke tests cover canvas rendering, input toggles, and resize behavior.
- Golden fixture tests cover terrain meshing once voxel chunks exist.
- Performance tests should be explicit scripts with stable scene seeds, not hidden
  assertions inside regular unit tests.
