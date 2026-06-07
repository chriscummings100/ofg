# Architecture

## Principles

- Keep the browser client native and lightweight.
- Put deterministic logic in small modules with direct unit tests.
- Let browser glue stay thin: DOM, input forwarding, WebAssembly startup, and UI.
- Prefer explicit data contracts over hidden engine state.
- Move long-lived world, simulation, terrain, render extraction, and WebGPU
  ownership into Rust.

The Rust conversion is complete; its historical plan is archived at
`docs/archived/RUST_CONVERSION_PLAN.md`. The TypeScript scene/component model has
been retired from the compiled source tree, and Rust/wgpu is now the browser
WebGPU renderer. Use [API_CONTRACTS.md](API_CONTRACTS.md) before adding,
deleting, or moving TypeScript around terrain, rendering, or engine ownership.
The remaining TypeScript render-adjacent code is a generic browser image decoder
plus debug shell around a Rust-owned browser game/render facade.

## Current Layers

```text
src/app
  Browser lifecycle, canvas setup, frame loop, HUD state, URL terrain
  descriptor parsing, debug hooks, input forwarding, and calls into the browser
  game runtime facade through frame input packets, commands, and Rust debug
  snapshots.

src/engine/input
  DOM input tracking with edge-triggered key events and mouse deltas.

src/engine/browser
  Generic browser substrate helpers. `BrowserWorkerHost` remains as tested
  generic worker substrate, but the playable terrain path no longer uses a
  TypeScript terrain worker bridge. `textureAssetLoader.ts` decodes
  Rust-provided generic texture-array URL requests into RGBA arrays and fetches
  opaque byte asset requests without owning terrain material, model, or
  animation semantics.

src/engine/world
  Terrain descriptor/config types, 3D terrain chunk key helpers, Rust/WASM
  terrain artifact test adapters, and the terrain mesh data/stride contract.
  Runtime terrain generation, meshing, streaming, worker semantics, material
  packing, terrain material manifests, terrain edits, and density dependency
  generation are Rust-owned.

src/engine/math
  Small vector and matrix primitives.

src/engine/render
  Shader contract tests. Actual browser WebGPU resource creation, terrain
  texture manifest interpretation, texture-array validation/upload, terrain mesh
  generation/upload/pruning, active draw-set ownership, and draw submission
  happen in Rust/wgpu through `crates/engine_web`.

src/engine/web
  Browser-facing WASM loaders for Rust systems that are not pure engine core or
  terrain. `engineWebWasm.ts` loads the wasm-bindgen `RustBrowserGame` facade
  and applies a narrow browser compatibility shim for the pinned `wgpu` limit
  name. `rustBrowserGameRuntime.ts` is the TypeScript shell around debug hooks
  and browser game input types; terrain streaming, texture manifest ownership,
  GLTF model loading/animation/skinning, and mesh upload live inside
  `engine_web.wasm`.

src/engine/render/shaders
  Shader source inputs. `uber.wgsl` is compiled into a TypeScript artifact for
  shader contract tests, and the Rust renderer includes the shared WGSL source.

src/generated
  Deterministically generated TypeScript artifacts used by runtime code.
```

## Runtime Ownership

The playable browser runtime is now Rust-owned for player/camera state, terrain
state, terrain texture semantics, and WebGPU rendering, with TypeScript acting
as a browser shell plus generic browser image decoder.

- `engine_web` composes `engine_core` and `terrain_core` as Rust libraries for
  the active browser game facade. It owns player/camera movement, terrain-height
  grounding, first-person/third-person/debug-fly camera mode switching, scene
  mesh item resolution for the debug player marker and imported model items,
  frame packet construction, terrain stream advancement, terrain mesh
  upload/pruning, and Rust/wgpu draw submission.
- `engine_core` remains the browser-free Rust logic crate for engine/player/world
  behavior and native tests. It owns the Rust scene/component model: one
  scene tree of entities addressed by stable generational `EntityId` handles,
  local/world transforms on every entity, typed components for player, camera,
  terrain, and mesh rendering, and scene-level convenience handles for terrain,
  player, and active camera. It also extracts visible mesh renderer items with
  logical mesh/material IDs and world matrices for `engine_web` to resolve. It
  is linked into `engine_web`; no standalone `engine_core.wasm` browser artifact
  is built for the playable app.
- `terrain_core` owns terrain height/density sampling, generated chunk mesh
  emission, stream scheduling, density storage, worker-pool request-state tests,
  and the tested legacy terrain mesh packet store. The playable browser path now
  reaches it through `engine_web` as a Rust library; the standalone
  `terrain_core.wasm` artifact remains for tests, benchmarks, and compatibility
  fixtures, not runtime TypeScript terrain ownership.
- `engine_web` owns the Rust/wgpu browser renderer and current GLTF model path:
  WebGPU canvas surface, adapter/device/queue, surface configuration, depth
  texture, shader modules, terrain and static-model pipelines, GLB parsing,
  model image/texture/sampler/material import, embedded PNG/JPEG decode,
  static model resource registration, non-skinned node animation sampling,
  skin joint/inverse bind import, CPU skinning for all active player-character
  primitives, male/female player-character descriptor selection, buffers,
  texture arrays, samplers, bind groups, render-pass submission, frame/resource
  counts, and GPU resource pruning.
- TypeScript collects DOM input, parses URL seed/preset values, starts WASM,
  exposes debug hooks, decodes Rust-provided generic texture-array URL requests
  into RGBA arrays, and fetches Rust-provided opaque byte asset requests for
  model loading. `src/app` no longer constructs the terrain
  scheduler, density store, render packet store, worker client, mirrored terrain
  sink, texture upload path, or terrain height sampler directly. Rust owns the
  terrain renderer vertex stride, terrain texture layer requests, stream
  status/debug snapshot, and active frame construction at that facade.
  TypeScript no longer creates WebGPU devices, pipelines, buffers, textures,
  render passes, shader uniform buffers, renderer resource handles, shader
material packets, camera frames, light packets, player-marker mesh/material
data, scene mesh world matrices, or normal matrices.

The retired TypeScript scene model is archived under `docs/archived/`. Future
large-scale world state should move into Rust rather than recreating that graph.
The active direction is a small Rust-owned scene/component layer in
`engine_core`, not a TypeScript scene graph or a general-purpose ECS framework.

## Terrain Direction

The visible seed terrain now uses a Rust-owned same-LOD per-chunk Dual Contouring
path. The runtime streamer builds neighbor-aware chunk meshes with deterministic
seam ownership, so adjacent chunks do not both emit the same border quads. LOD
transitions and far-field terrain are still future work.

The terrain data model is 3D from the start. A terrain density chunk has 32 cells
per axis and 33 samples per axis, so adjacent chunks share boundary samples
cleanly. The compiled TypeScript terrain generator/noise reference has been
deleted; Rust is now the browser terrain source of truth for height, density,
material classification, and mesh emission. `heightAt(x, z)` remains a Rust
compatibility query for player grounding until movement is density/mesh aware.

`engine_web` now keeps the playable browser terrain stream inside Rust. Its
`BrowserTerrainStream` uses `terrain_core` as a Rust library for desired
density/LOD0 sets, dependency coordinates, ready/empty state, and chunk mesh
generation. The wasm-bindgen facade has no public terrain mesh upload, destroy,
retain, clear, or render-frame method; `tick(frame)` advances player/camera
state, advances terrain streaming, uploads/prunes terrain meshes, and submits
the frame. Loaded density chunk keys remain fully 3D and are exposed only in the
Rust-assembled debug snapshot.
Runtime terrain meshes carry position, color, normal, uv, material layer indices,
and material weights from Rust `terrain_core`. Rust/wgpu owns the actual GPU
mesh handles, chunk-keyed object handles, and active draw set. The old compiled
TypeScript
`TerrainChunkStreamer`, `TerrainRenderer`, `TerrainRenderPacketStore`,
`TerrainCoreWorkerStreamer`, `terrainChunkWorkerClient`, `RenderWorld`,
highest-surface mesher, and heightfield mesh path have been retired rather than
kept as parallel terrain owners.

The Dual Contouring implementation now lives in `crates/terrain_core`. The
intended boundary is:

- Density field interface: sample signed density and gradients at world positions,
  with materials added once the surface representation is stable.
- Chunk sampler: evaluate density at deterministic 33x33x33 chunk lattice points.
- Mesher: produce compact per-chunk vertex/index/material buffers with smooth
  normals and neighbor-aware boundary quads.
- Renderer: upload chunk meshes without knowing how they were generated.

The first implementation used TypeScript for iteration speed. That phase is now
over for scaling-sensitive systems: terrain, world state, render extraction, and
WebGPU ownership should stay on the Rust-first plan.

## Shader Direction

Shader source sits behind `tools/build-shaders.mjs`. The current input is
`src/engine/render/shaders/uber.wgsl`, and the generated runtime artifact is
`src/generated/render/uberShader.ts`.

The Rust renderer includes the shared WGSL shader source, while TypeScript shader
tests still validate the generated metadata and vertex-layout contract. WGSL is
the intended shader language for this project because it is browser-native,
direct, and familiar enough for AI-driven changes.

The model material path supports glTF 2.0 core metallic-roughness and the
archived `KHR_materials_pbrSpecularGlossiness` extension. Rust imports material
workflow records, decodes embedded model images into one-layer RGBA texture
arrays, chooses fallback handles for missing maps, and binds model albedo,
normal, and material textures through the same object bind group shape as
terrain. The WGSL model path uses a small direct-light PBR approximation with
glTF metallic-roughness channel semantics: roughness from green and metallic
from blue. The specular-glossiness path uses diffuse RGB, specular RGB, and
glossiness alpha. Terrain keeps its separate triplanar material workflow and is
not reinterpreted as glTF metallic-roughness.

Rust interprets the checked-in terrain texture manifest, requests generic
browser RGBA texture arrays, validates the returned arrays, and installs the GPU
texture handles. Rust/wgpu builds compact frame packets from its Rust-owned
browser game state, owns shader material packets and material-to-texture
selection, keeps the low-level debug player marker in `engine_core` rather than
the browser renderer path, validates per-chunk terrain draw transforms inside
the browser game facade, consumes scene mesh world matrices extracted by
`engine_core`, computes object normal matrices, and packs the WGSL camera/object
uniform buffers. Static and CPU-skinned model meshes use a separate 12-float
vertex layout and `modelVertexMain` pipeline entry point instead of pretending
to be terrain vertices. The current player-character prototype loads a shared
Quaternius UAL1 animation GLB and male/female Quaternius base-character body
GLBs, samples `Idle_Loop`, `Walk_Loop`, and `Sprint_Loop` in Rust, blends
walk-to-sprint from Rust player speed, CPU-skins every renderable skinned body
primitive each frame, updates all active model vertex buffers before drawing,
and attaches one Rust-owned scene mesh item per character primitive to a shared
player-following root. The checked-in male/female bodies are Superhero
placeholders until Regular GLBs are available. The browser path no longer draws
the old yellow marker as the normal debug-fly player representation.

Terrain uses checked-in Poly Haven CC0 materials imported into 16-layer global
texture arrays. The runtime currently loads albedo, normal, and roughness arrays;
the WGSL terrain path triplanar-blends albedo and roughness from up to four
material weights per vertex. Normal maps are loaded as renderer resources but are
not yet applied to lighting. Imported glTF normal textures are likewise recorded
and uploaded when present, but tangent-space normal-map lighting is deferred
until tangents are imported or generated.

The sky is also shader-driven. Rust/wgpu draws a full-screen sky pass before
terrain and scene mesh geometry, reconstructs world rays from the inverse
view-projection matrix, and renders a blue gradient plus a sun disk in the
direction of the Rust-owned main light.

## Testing Direction

- Unit tests cover deterministic math, Rust player/camera behavior, render data,
  terrain data contracts, Rust/WASM terrain adapters, and Rust-owned browser
  terrain stream behavior.
- Shader tests verify generated shader metadata and the renderer vertex layout
  contract.
- Browser smoke tests cover canvas rendering, input toggles, resize behavior, and
  basic chunk streaming after moving the player across chunk columns. The smoke
  path also verifies the Rust-owned GLTF player character scene item, hidden
  marker state, animation clock, CPU skinning state, HUD male/female character
  toggle, movement-driven walk selection, Shift-driven sprint blend, and
  release-driven idle transition.
- Rust/WASM terrain tests cover height/density determinism, density chunk fill,
  mesh buffers, retained stores, stream scheduling, and worker-pool fixtures.
- Performance tests should be explicit scripts with stable scene seeds, not hidden
  assertions inside regular unit tests.
