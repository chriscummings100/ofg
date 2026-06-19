# Architecture

## Principles

- Keep the browser client native and lightweight.
- Put deterministic logic in small modules with direct unit tests.
- Let browser glue stay thin: DOM, input forwarding, WebAssembly startup, and UI.
- Prefer explicit data contracts over hidden engine state.
- Move long-lived world, simulation, terrain, render extraction, and WebGPU
  ownership into Rust.
- Preserve 60fps play as an architectural requirement: regular 500ms-class
  frame, terrain-stream, or renderer spikes mean the owning change is unfinished,
  even if the feature is visually correct.

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
  descriptor parsing, terrain variant editor UI, debug hooks, input forwarding,
  and calls into the browser game runtime facade through frame input packets,
  commands, and Rust debug snapshots. The terrain variant editor edits and
  forwards Rust-owned descriptor values; it does not sample terrain, schedule
  nodes, build meshes, classify materials, or decide visibility.

src/engine/input
  DOM input tracking with edge-triggered key events and mouse deltas.

src/engine/browser
  Generic browser substrate helpers. `BrowserWorkerHost` remains as a tested
  generic worker substrate, but the current sine-grass terrain baseline does
  not use browser workers for playable terrain generation. `textureAssetLoader.ts`
  decodes Rust-provided generic texture-array URL requests into RGBA arrays and
  fetches opaque byte asset requests without owning terrain material, model, or
  animation semantics.

src/engine/world
  Terrain descriptor/config types and 3D terrain chunk key helpers for browser
  URL parsing, debug snapshot typing, and small shell tests. Runtime terrain
  generation, meshing, streaming, worker semantics, material packing, terrain
  material manifests, density dependency generation, standalone terrain WASM
  adapters, terrain mesh data/stride contracts, and interpretation of terrain
  variant descriptors are Rust-owned or tested through Rust.

src/engine/math
  Small vector and matrix primitives.

src/engine/render
  Shader contract tests. Actual browser WebGPU resource creation, terrain
  texture manifest interpretation, texture-array validation/upload, terrain mesh
  generation/upload/pruning, active draw-set ownership, and draw submission
  happen in Rust/wgpu through `crates/engine_web`. The old water renderer
  resources are dormant during the sine-grass terrain baseline.

src/engine/web
  Browser-facing WASM loaders for Rust systems that are not pure engine core or
  terrain. `engineWebWasm.ts` loads the wasm-bindgen `RustBrowserGame` facade
  and applies a narrow browser compatibility shim for the pinned `wgpu` limit
  name. `rustBrowserGameRuntime.ts` is the TypeScript shell around debug hooks
  and browser game input types. `terrainWorkerClient.ts` and
  `terrainBuildWorker.ts` remain compatibility scaffolding for possible future
  Rust-issued terrain build requests; the active baseline reports `rust-sync`.
  Terrain scheduling, completion validation, texture manifest ownership, GLTF
  model loading/animation/skinning, visibility, and mesh upload live inside
  `engine_web.wasm`.

src/engine/render/shaders
  Shader source inputs. `uber.wgsl`, `post.wgsl`, and dormant `water.wgsl` are
  compiled into TypeScript artifacts for shader contract tests, and the Rust
  renderer includes the shared WGSL source.

src/generated
  Deterministically generated TypeScript artifacts used by runtime code,
  currently shader source modules, engine-web WASM metadata, and Rust-derived
  terrain preset ID/code metadata.
```

## Runtime Ownership

The playable browser runtime is now Rust-owned for player/camera state, terrain
state, terrain texture semantics, and WebGPU rendering, with TypeScript acting
as a browser shell plus generic browser image decoder.

- `engine_web` composes `engine_core` and `terrain_core` as Rust libraries for
  the active browser game facade. It owns player/camera movement, terrain-height
  grounding, active terrain variant descriptor and revision, terrain resets,
  first-person/third-person/debug-fly camera mode switching, scene mesh item
  resolution for the debug player marker and imported model items, frame packet
  construction, terrain stream advancement, terrain mesh upload/pruning, and
  Rust/wgpu draw submission. The sea-level water command/status surface remains
  for compatibility, but the sine-grass baseline starts with water disabled and
  bypasses water compositing.
- `engine_core` remains the browser-free Rust logic crate for engine/player/world
  behavior and native tests. It owns the Rust scene/component model: one
  scene tree of entities addressed by stable generational `EntityId` handles,
  local/world transforms on every entity, typed components for player, camera,
  terrain, and mesh rendering, and scene-level convenience handles for terrain,
  player, and active camera. It also extracts visible mesh renderer items with
  logical mesh/material IDs and world matrices for `engine_web` to resolve. It
  is linked into `engine_web`; no standalone `engine_core.wasm` browser artifact
  is built for the playable app.
- `terrain_core` owns terrain preset metadata, terrain variant descriptor
  validation, flat descriptor layout, sine height sampling, generated node mesh
  emission, generated-triangle height queries, descriptor probe summaries,
  stream scheduling, exact visible-cover selection, and the narrow fixture
  facade. The playable browser path reaches it through `engine_web` as a Rust
  library for scheduling, whole-node mesh generation, and main-thread height
  queries. Density storage, Dual Contouring, placement sampling,
  transition-edge meshes, and water generation were moved back to
  reference/future status during the reset.
- `engine_web` owns the Rust/wgpu browser renderer and current GLTF model path:
  WebGPU canvas surface, adapter/device/queue, surface configuration, depth
  texture, HDR scene color, linear-depth, and half-resolution bloom
  post-process targets, shader modules, terrain, static-model, sky, bloom
  extraction, depth-of-field CoC/blur sampling, distance fog, and fullscreen
  post-process pipelines,
  GLB parsing, model image/texture/sampler/material import, embedded PNG/JPEG decode,
  static model resource registration, non-skinned node animation sampling,
  skin joint/inverse bind import, CPU skinning for all active player-character
  primitives, male/female player-character descriptor selection, buffers,
  texture arrays, samplers, bind groups, render-pass submission, frame/resource
  counts, post-process tone-map/bloom/DoF/fog settings, debug view selection,
  and GPU resource pruning.
- TypeScript collects DOM input, parses URL seed/preset values, starts WASM,
  exposes debug hooks, displays terrain variant editor and compatibility water
  debug controls,
  decodes Rust-provided generic texture-array URL requests into RGBA arrays,
  and fetches Rust-provided opaque byte asset requests for model loading.
  `src/app` no
  longer constructs the terrain scheduler, density store, render packet store,
  mirrored terrain sink, texture upload path, or terrain height sampler
  directly. The terrain worker client exists only as compatibility scaffolding
  while the baseline stream runs synchronously in Rust. Rust owns the terrain
  renderer vertex stride, terrain texture layer requests, terrain variant
  descriptor interpretation, stream status/debug snapshot, and active frame
  construction at that facade.
  TypeScript no longer creates WebGPU devices, pipelines, buffers, textures,
  render passes, shader uniform buffers, renderer resource handles, shader
  material packets, camera frames, light packets, player-marker mesh/material
  data, scene mesh world matrices, normal matrices, terrain placement decisions,
  water bathymetry data, water visibility, optical path length, or reflection
  cameras.

The retired TypeScript scene model is archived under `docs/archived/`. Future
large-scale world state should move into Rust rather than recreating that graph.
The active direction is a small Rust-owned scene/component layer in
`engine_core`, not a TypeScript scene graph or a general-purpose ECS framework.

## Terrain Direction

The visible seed terrain is being rebuilt from a small Rust-owned sine-grass
baseline. The active generator emits grass-only heightfield meshes, no separate
collision mesh, no aprons, no placement samples, no transition-edge meshes, and
no water or bathymetry packets. A minimal main-thread height query samples the
generated visible terrain triangles so first-person/third-person player
grounding follows the streamed mesh once available. Rich density fields, Dual
Contouring, material classification, placement, aprons, and water are future
milestones again; the previous implementation is preserved as reference under
`docs/reference/terrain_legacy_2026_06_15/`.

The multi-LOD model remains the core architectural shape. `lod0` is the highest
detail level, and larger LOD numbers are coarser. The current coarsest playable
grid is `lod5`, which is rootless and infinite rather than one global tree.
LOD0 nodes span 32x32x32 meters, contain 32 cells per axis, and use 33 shared
edge samples. Coarser LODs keep 32 cells per axis and double world cell size per
level. A parent node at `lod + 1` covers a 2x2x2 group of children at `lod`.

The runtime streamer generates one whole terrain node per job. A generated node
produces either a renderable mesh or an empty flag. The visible cover starts
from the LOD5 3x3x3 root grid around the player and recursively descends
through desired child octets only when all eight children are generated or
proven empty. A visible node hides all ancestors and descendants, so the settled
cover has no duplicate parent/child terrain and no gaps under the LOD5 roots.
Desired child sets are derived from a 3x3x3 grid of parent nodes around the
player, with bounded vertical policies so the model does not assume terrain
lives in one fixed Y band. Dissolve transitions are the next active streaming
milestone; the current baseline keeps the state model small and observable
before adding shader transition masks.

Terrain variants are Rust-owned descriptors for the sine baseline. The current
flat descriptor contains version, preset code, base height, sine height scale,
wavelength, secondary wave scale, grass bias, and a cache key. The browser
terrain variant editor can edit and forward those numbers, preview the active
draft at the world origin, import/export JSON, and display Rust probe summaries.
It does not own sampling, material classification, desired sets, or visibility.
`sineGrass` is the only active preset.

`engine_web` now keeps the playable browser terrain stream inside Rust. Its
`BrowserTerrainStream` delegates stream policy to `terrain_core`: desired
nodes, generated/empty state, mesh-created events, mesh-destroyed events,
visible cover, and height queries all come from the core scheduler. The current
browser path generates those nodes synchronously in Rust and reports
`rust-sync`; the browser worker methods remain compatibility no-ops until
worker-backed whole-node execution is worth reintroducing. The rendered terrain
mesh set also owns the runtime terrain height query used by the Rust player
tick; if no rendered visible mesh covers the next player X/Z yet, the game
state treats the terrain sample as missing for that frame rather than sampling
the analytic sine field. The `terrain_core` scheduler and renderer-facing
stream updates address work as `TerrainNodeKey { lod, coord }`, with LOD0 chunk
compatibility adapters and legacy density-named status fields retained for
current HUD/smoke fields and the fixture-only facade. The wasm-bindgen facade
has no public terrain mesh upload, destroy, retain, clear, or render-frame
method; `tick(frame)` advances player/camera state, advances terrain streaming,
mirrors core mesh events into GPU resources, and submits the frame. Loaded
chunk keys and terrain node keys are exposed only in the Rust-assembled debug
snapshot. TypeScript displays/debug-captures reported timings and status only.
Runtime terrain meshes carry position, color, normal, uv, material layer indices,
and material weights from Rust `terrain_core`. Rust/wgpu owns the actual GPU
mesh handles, node-keyed object handles, and active draw set. The old compiled
TypeScript
`TerrainChunkStreamer`, `TerrainRenderer`, `TerrainRenderPacketStore`,
`TerrainCoreWorkerStreamer`, `terrainChunkWorkerClient`, and `RenderWorld` have
been retired rather than kept as parallel terrain owners.

The first implementation used TypeScript for iteration speed. That phase is now
over for scaling-sensitive systems: terrain, world state, render extraction, and
WebGPU ownership should stay on the Rust-first plan.

## Shader Direction

Shader source sits behind `tools/build-shaders.mjs`. Current inputs include
`src/engine/render/shaders/uber.wgsl` for opaque scene, sky, shadow, and model
rendering, dormant `src/engine/render/shaders/water.wgsl`, and
`src/engine/render/shaders/post.wgsl` for fullscreen post-process presentation.
Generated runtime artifacts live under
`src/generated/render/`.

The Rust renderer includes the shared WGSL shader source, while TypeScript shader
tests still validate the generated metadata and vertex-layout contract. WGSL is
the intended shader language for this project because it is browser-native,
direct, and familiar enough for AI-driven changes.

The browser scene pass now writes an HDR scene color target and an `R32Float`
linear-depth/distance target owned by the post-process resources. The sine-grass
baseline has no active water pass, so the old water composite targets are not in
the main frame graph. `water.wgsl` and `water_renderer.rs` remain only dormant
compatibility until a later water milestone. Scene shaders output scene-linear
color; the post shader owns exposure and filmic tone mapping, with the selected
sRGB surface doing final display encoding. The post-process frame graph also
writes a half-resolution `Rgba16Float` bloom target from bright HDR scene color
and composites it before tone mapping. Depth of field is default-off and uses
the linear-depth target to calculate a per-pixel circle of confusion before
sampling a small HDR scene blur in the final pass. Distance fog is default-on
and uses linear depth to fade opaque scene pixels toward the same procedural sky
color used by the sky pass, with RGB fog controls acting as a sky tint before
tone mapping while leaving sky pixels untouched. Debug hooks may select final
output, scene color, linear depth, post-tone-map color, bloom contribution, DoF
CoC, DoF blurred scene color, or fog factor, but TypeScript only sends commands
and reads Rust-reported status.

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
texture handles. Browser TypeScript returns only mip-0 RGBA bytes; Rust/wgpu
derives deterministic RGBA8 mip chains and uses mip-filtered texture sampling.
Rust/wgpu builds compact frame packets from its Rust-owned browser game state,
owns shader material packets and material-to-texture
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

The sky is also shader-driven. Rust/wgpu draws a depth-tested sky pass after
terrain and scene mesh geometry, reconstructs world rays from the inverse
view-projection matrix, and renders a Hosek/Wilkie-inspired analytic sky with
sun glow, moon glow, procedural stars, and a moving procedural cloud layer.
Rust owns the time-of-day cycle in `engine_core`, derives sun direction, sun
color, intensity, ambient, cloud parameters, star intensity, and night blend,
and packs those values into the shared camera uniform consumed by the WGSL sky
pass. Browser TypeScript may expose debug/smoke sky values from
`debugSnapshot()`, but it does not compute sky, lighting, cloud, or time state.

## Testing Direction

- Unit tests cover deterministic math, Rust player/camera behavior, render data,
  terrain data contracts, Rust terrain facade behavior, and Rust-owned browser
  terrain stream behavior.
- Shader tests verify generated shader metadata and the renderer vertex layout
  contract.
- Browser smoke tests cover browser integration only: WebGPU canvas rendering,
  wasm-bindgen loading, browser asset fetch/decode, HUD state, reload behavior,
  browser isolation headers, DOM input forwarding, Rust runtime sentinel strings,
  Rust/wgpu renderer status, terrain worker transport counters, movement-delta
  frame/worker/upload telemetry, post-process debug view selection, and
  Rust-owned water debug/status controls.
- Rust terrain tests cover sine height determinism, mesh buffers, stream
  scheduling, and the worker-build facade. The
  removed TypeScript terrain ownership adapters must not be recreated for test
  coverage; the dedicated browser build worker is the only TypeScript path that
  loads `terrain_core.wasm`.
- Rust offscreen image smoke in `crates/ofg_test_harness` creates native `wgpu`
  render targets, ticks Rust terrain streaming, renders terrain/sky PNGs, and
  writes `artifacts/rust-smoke/<run-id>/report.json`. Water image smoke is a
  future water-milestone gate.
- Performance tests should be explicit scripts with stable scene seeds, not hidden
  assertions inside regular unit tests. Terrain generation performance is
  measured by `npm run bench:terrain:rust`, including aggregate, per-LOD,
  per-class, and phase timing distributions for realistic node populations.
