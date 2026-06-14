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
  descriptor parsing, terrain variant editor UI, debug hooks, input forwarding,
  and calls into the browser game runtime facade through frame input packets,
  commands, and Rust debug snapshots. The terrain variant editor edits and
  forwards Rust-owned descriptor values; it does not sample terrain, schedule
  nodes, build meshes, classify materials, or decide visibility.

src/engine/input
  DOM input tracking with edge-triggered key events and mouse deltas.

src/engine/browser
  Generic browser substrate helpers. `BrowserWorkerHost` remains as a tested
  generic worker substrate; the terrain worker client uses it only to route
  Rust-issued opaque build requests and completions. `textureAssetLoader.ts`
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
  generation/upload/pruning, water bathymetry/composite resources, active
  draw-set ownership, and draw submission happen in Rust/wgpu through
  `crates/engine_web`.

src/engine/web
  Browser-facing WASM loaders for Rust systems that are not pure engine core or
  terrain. `engineWebWasm.ts` loads the wasm-bindgen `RustBrowserGame` facade
  and applies a narrow browser compatibility shim for the pinned `wgpu` limit
  name. `rustBrowserGameRuntime.ts` is the TypeScript shell around debug hooks
  and browser game input types. `terrainWorkerClient.ts` and
  `terrainBuildWorker.ts` host browser worker execution for Rust-issued terrain
  build requests, but terrain scheduling, completion validation, texture
  manifest ownership, GLTF model loading/animation/skinning, visibility, and
  mesh upload live inside `engine_web.wasm`.

src/engine/render/shaders
  Shader source inputs. `uber.wgsl`, `post.wgsl`, and `water.wgsl` are compiled
  into TypeScript artifacts for shader contract tests, and the Rust renderer
  includes the shared WGSL source.

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
  Rust/wgpu draw submission. It also owns the sea-level water renderer: water
  settings, water status, terrain-job bathymetry packet upload, opaque scene
  targets, optional planar reflection targets, and water compositing before
  post-process.
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
  validation, flat descriptor layout, height/density sampling, generated chunk
  mesh emission, exact polygonized surface queries, mesh-backed placement sample
  generation, terrain-job sea-depth packet generation, descriptor probe
  summaries, stream scheduling, density storage, worker-pool request-state
  tests, and the tested legacy terrain mesh packet store. The playable browser
  path now reaches it through `engine_web` as a Rust library for scheduling,
  sea-depth sampling, placement counting, and through a dedicated browser worker
  `terrain_core.wasm` instance for build execution. The standalone
  `terrain_core.wasm` artifact remains a narrow export-contract and
  worker-build artifact, not a TypeScript terrain ownership boundary; native
  Rust tests and `npm run bench:terrain:rust` cover terrain behavior and
  benchmarking. The Rust terrain benchmark includes a profiled terrain-node
  population sampled from streaming-style LOD bands, movement centers, multiple
  presets, derived seeds, and explicit air/solid/surface probes so generation
  cost is reported as distributions instead of single-chunk anecdotes.
- `engine_web` owns the Rust/wgpu browser renderer and current GLTF model path:
  WebGPU canvas surface, adapter/device/queue, surface configuration, depth
  texture, HDR scene color, linear-depth, and half-resolution bloom
  post-process targets, shader modules, terrain, static-model, sky, bloom
  extraction, depth-of-field CoC/blur sampling, water opaque color/depth
  targets, water bathymetry texture, water reflection targets, water composite
  pipeline, and fullscreen post-process pipelines,
  GLB parsing, model image/texture/sampler/material import, embedded PNG/JPEG decode,
  static model resource registration, non-skinned node animation sampling,
  skin joint/inverse bind import, CPU skinning for all active player-character
  primitives, male/female player-character descriptor selection, buffers,
  texture arrays, samplers, bind groups, render-pass submission, frame/resource
  counts, post-process tone-map/bloom/DoF settings, debug view selection, and
  GPU resource pruning.
- TypeScript collects DOM input, parses URL seed/preset values, starts WASM,
  exposes debug hooks, displays terrain variant editor and water debug controls,
  decodes Rust-provided generic texture-array URL requests into RGBA arrays,
  and fetches Rust-provided opaque byte asset requests for model loading.
  `src/app` no
  longer constructs the terrain scheduler, density store, render packet store,
  mirrored terrain sink, texture upload path, or terrain height sampler
  directly. The terrain worker client exists only to route Rust-issued opaque
  build requests and completions. Rust owns the terrain renderer vertex stride,
  terrain texture layer requests, terrain variant descriptor interpretation,
  surface-placement sampling, stream status/debug snapshot, and active frame
  construction at that facade.
  TypeScript no longer creates WebGPU devices, pipelines, buffers, textures,
  render passes, shader uniform buffers, renderer resource handles, shader
  material packets, camera frames, light packets, player-marker mesh/material
  data, scene mesh world matrices, normal matrices, water bathymetry data,
  water visibility, terrain placement decisions, optical path length, or
  reflection cameras.

The retired TypeScript scene model is archived under `docs/archived/`. Future
large-scale world state should move into Rust rather than recreating that graph.
The active direction is a small Rust-owned scene/component layer in
`engine_core`, not a TypeScript scene graph or a general-purpose ECS framework.

## Terrain Direction

The visible seed terrain now defaults to a Rust-owned multi-LOD terrain view.
Near terrain still uses the current highest-detail LOD0 Dual Contouring chunks,
while farther bands render coarser LOD1 through LOD4 nodes with larger
world-space cell sizes. The default horizon band renders a measured settled
span above 4 km in X and Z. The runtime streamer schedules generated nodes as
the active unit of work: a node build produces either a renderable mesh or an
empty node, with density sampling kept as an internal meshing detail. It builds
neighbor-aware meshes with deterministic same-LOD seam ownership, keeps
generated mesh data cached, and selects a hole-free visible cover by keeping
parent nodes rendered until their desired child group is generated or proven
empty. Terrain stream scheduling, browser stream updates, renderer mesh IDs,
and debug snapshots are node-keyed for a rootless multi-resolution LOD grid.
The stream also builds Rust-owned placement sample packets from accepted
polygonized meshes and reports only aggregate placement candidate/sample/reject
counts through debug status. For mixed-LOD boundaries, the stream derives
separate Rust-owned transition edge meshes from cached child and parent
polygonized meshes, uploads them as optional terrain drawables with keys
distinct from canonical terrain nodes, and reports aggregate transition
face/mesh/buffer counts. No foliage instances are rendered yet.

The terrain data model is 3D from the start. A terrain density chunk has 32 cells
per axis and 33 samples per axis, so adjacent chunks share boundary samples
cleanly. The compiled TypeScript terrain generator/noise reference has been
deleted; Rust is now the browser terrain source of truth for height, density,
material classification, and mesh emission. `heightAt(x, z)` remains a Rust
compatibility query for player grounding until movement is density/mesh aware,
and it uses the active Rust terrain variant descriptor.

Terrain variants are Rust-owned shape descriptors for the current generator.
They tune broad landform geometry such as base elevation, relief scale,
large-feature noise, ridge strength, domain warp, cellular breakup, and detail
noise. The browser terrain variant editor can duplicate catalog presets, edit
numeric descriptor fields, apply them through the Rust command lane, preview the
active draft at the world origin, import/export JSON, and display Rust probe
summaries. It does not own the generator. Shape presets are also not biomes:
future climate, biome, hydrology, rivers, lakes, water-body generation,
vegetation, prop placement, material palette, and local feature systems should
compose with these shape descriptors as separate Rust-owned layers rather than
turning every terrain shape preset into an all-in-one world type. The current
sea-level water is a renderer feature over the existing terrain height surface,
not a terrain generator layer. Current built-in preset scales, terrain-band
constraints, and post-band-fix target numbers are recorded in
`docs/TERRAIN_PRESET_SCALE.md`.

`engine_web` now keeps the playable browser terrain stream inside Rust. Its
`BrowserTerrainStream` uses `terrain_core` as a Rust library for stream desired
sets, generated/empty state, request ids, retry state, and completion
validation. On the browser path, Rust emits opaque terrain build requests,
TypeScript routes them through a browser worker pool, and each worker calls the
raw `terrain_core.wasm` mesh-build export with the Rust-authored flat terrain
variant descriptor and variant revision before returning typed-array mesh
buffers to Rust. Rust rejects stale completions whose generation, node key, or
variant revision no longer matches. The `terrain_core` scheduler and
renderer-facing stream
updates address work as
`TerrainNodeKey { lod, coord }`, with LOD0 chunk compatibility adapters and
legacy density-named status fields retained for current HUD/smoke fields and
the fixture-only facade. The wasm-bindgen facade has no public terrain mesh
upload, destroy, retain, clear, or render-frame method; `tick(frame)` advances
player/camera state, advances terrain streaming, uploads/prunes terrain meshes,
and submits the frame. Loaded chunk keys and terrain node keys are exposed only
in the Rust-assembled debug snapshot.
The browser worker bridge drains a bounded number of completions per frame.
Inside Rust, terrain mesh upload/registration and mesh destruction are also
budgeted, and `BrowserTerrainStream` caches the desired node set and recomputes
visible cover only when the stream center or generated/empty node state changes.
These smoothing mechanisms are Rust-owned runtime policy; TypeScript only
routes opaque worker packets and displays/debug-captures reported timings and
budget status.
Runtime terrain meshes carry position, color, normal, uv, material layer indices,
and material weights from Rust `terrain_core`. Rust/wgpu owns the actual GPU
mesh handles, node-keyed object handles, and active draw set. The old compiled
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

Shader source sits behind `tools/build-shaders.mjs`. Current inputs include
`src/engine/render/shaders/uber.wgsl` for opaque scene, sky, shadow, and model
rendering, `src/engine/render/shaders/water.wgsl` for sea-level water
compositing, and `src/engine/render/shaders/post.wgsl` for fullscreen
post-process presentation. Generated runtime artifacts live under
`src/generated/render/`.

The Rust renderer includes the shared WGSL shader source, while TypeScript shader
tests still validate the generated metadata and vertex-layout contract. WGSL is
the intended shader language for this project because it is browser-native,
direct, and familiar enough for AI-driven changes.

The browser scene pass now writes an HDR opaque color target and an `R32Float`
opaque linear-depth/distance target. The water pass can then composite a fixed
sea-level plane into the final HDR scene color and final linear-depth targets
before a fullscreen Rust/wgpu post pass presents the selected output to the
canvas. Terrain generation jobs emit optional node-local bathymetry packets for
sea-level nodes; the renderer uploads visible packets into a bathymetry atlas and
draws matching water-plane instances. The water shader samples those packets for
vertical bottom depth and the opaque linear-depth target for eye-ray optical path
length, then applies denser shallow-water tinting, small animated ripple normals,
and procedural shoreline foam. Planar reflections are default-off because the
current experimental reflection path has screen-edge artifacts; when explicitly
enabled for diagnosis, a mirrored camera renders a half-resolution reflection
color target for Fresnel-weighted sampling. Scene and water shaders output
scene-linear color; the post shader owns exposure and filmic tone mapping, with
the selected sRGB surface doing final display encoding. The
post-process frame graph also writes a half-resolution `Rgba16Float` bloom
target from bright HDR scene color and
composites it before tone mapping. Depth of field is default-off and uses the
linear-depth target to calculate a per-pixel circle of confusion before sampling
a small HDR scene blur in the final pass. Debug hooks may select final output,
scene color, linear depth, post-tone-map color, bloom contribution, DoF CoC, or
DoF blurred scene color, but TypeScript only sends commands and reads
Rust-reported status.

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
- Rust terrain tests cover height/density determinism, density chunk fill, mesh
  buffers, retained stores, stream scheduling, and worker-pool fixtures. The
  removed TypeScript terrain ownership adapters must not be recreated for test
  coverage; the dedicated browser build worker is the only TypeScript path that
  loads `terrain_core.wasm`.
- Rust offscreen image smoke in `crates/ofg_test_harness` creates native `wgpu`
  render targets, ticks Rust terrain streaming, renders terrain/sky/water PNGs,
  writes `artifacts/rust-smoke/<run-id>/report.json`, reports water runtime and
  bathymetry coverage, and owns terrain preset and seam/corner image smoke.
- Performance tests should be explicit scripts with stable scene seeds, not hidden
  assertions inside regular unit tests. Terrain generation performance is
  measured by `npm run bench:terrain:rust`, including aggregate, per-LOD,
  per-class, and phase timing distributions for realistic node populations.
