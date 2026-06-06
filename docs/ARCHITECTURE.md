# Architecture

## Principles

- Keep the browser client native and lightweight.
- Put deterministic logic in small modules with direct unit tests.
- Let browser glue stay thin: DOM, input forwarding, WebAssembly startup, and UI.
- Prefer explicit data contracts over hidden engine state.
- Move long-lived world, simulation, terrain, render extraction, and WebGPU
  ownership into Rust.

The detailed Rust conversion path is tracked in
[RUST_CONVERSION_PLAN.md](RUST_CONVERSION_PLAN.md). The TypeScript
scene/component model has been retired from the compiled source tree, and
Rust/wgpu is now the browser WebGPU renderer. Use the Rust conversion plan before
adding, deleting, or moving TypeScript around terrain, rendering, or engine
ownership. The remaining TypeScript render-adjacent code is a temporary
byte-upload, worker, asset, and debug adapter around a Rust-owned browser
game/render facade.

## Current Layers

```text
src/app
  Browser lifecycle, canvas setup, frame loop, HUD state, URL terrain
  descriptor parsing, debug hooks, input forwarding, and calls into the browser
  game runtime facade through frame input packets, commands, debug snapshots, and
  the transitional `renderFrame` call.

src/engine/input
  DOM input tracking with edge-triggered key events and mouse deltas.

src/engine/browser
  Generic browser substrate helpers. `BrowserWorkerHost` owns Worker lifecycle,
  request-id envelopes, resets, disposal, and completion forwarding without
  understanding terrain job payloads.

src/engine/world
  Terrain descriptor/config types, 3D terrain chunk key helpers, Rust/WASM
  terrain adapters, worker density result contracts, worker transport, and the
  terrain mesh data/stride contract. Runtime terrain generation, meshing,
  material packing, terrain material manifests, terrain edits, and worker
  density dependency generation are Rust-owned.

src/engine/math
  Small vector and matrix primitives.

src/engine/render
  CPU-side terrain texture decoding helpers that read the checked-in Poly Haven
  manifest, shader contract tests, and the temporary terrain render chunk sink
  contract used by the browser Worker bridge. Runtime terrain worker results are
  handed directly to `RustBrowserGame` by chunk key. Actual browser
  WebGPU resource creation and draw submission happen in Rust/wgpu through
  `crates/engine_web`; TypeScript only uploads terrain mesh bytes by chunk key
  and uploads the terrain texture arrays for the Rust browser game facade. Rust
  now constructs the terrain draw transforms internally.

src/engine/web
  Browser-facing WASM loaders for Rust systems that are not pure engine core or
  terrain. `engineWebWasm.ts` loads the wasm-bindgen `RustBrowserGame` facade
  and applies a narrow browser compatibility shim for the pinned `wgpu` limit
  name. `rustBrowserGameRuntime.ts` is the temporary TypeScript shell around
  remaining browser-only terrain Worker transport, texture asset decoding, stream
  status, debug hooks, and browser game input types. `terrainCoreWorkerStreamer`
  is the browser-only Worker bridge over the Rust-owned terrain scheduler.

src/engine/render/shaders
  Shader source inputs. `uber.wgsl` is compiled into a TypeScript artifact for
  shader contract tests, and the Rust renderer includes the shared WGSL source.

src/generated
  Deterministically generated TypeScript artifacts used by runtime code.
```

## Runtime Ownership

The playable browser runtime is now Rust-owned for player/camera state, terrain
state, and WebGPU rendering, with TypeScript acting as a browser shell and
temporary terrain worker/asset transport.

- `engine_web` composes `engine_core` and `terrain_core` as Rust libraries for
  the active browser game facade. It owns player/camera movement, terrain-height
  grounding, camera mode switching, debug player marker state, frame packet
  construction, and Rust/wgpu draw submission.
- `engine_core` remains the browser-free Rust logic crate for engine/player/world
  behavior and native tests. It is linked into `engine_web`; no standalone
  `engine_core.wasm` browser artifact is built for the playable app.
- `terrain_core.wasm` owns terrain height/density sampling, generated chunk mesh
  emission, stream scheduling, density storage, worker-pool request state, and
  the tested legacy terrain mesh packet store. The playable browser path now
  sends completed worker mesh results straight into `RustBrowserGame` terrain
  mesh handles instead of mirroring through that store.
- `engine_web` owns the Rust/wgpu browser renderer: WebGPU canvas surface,
  adapter/device/queue, surface configuration, depth texture, shader modules,
  pipelines, buffers, texture arrays, samplers, bind groups, render-pass
  submission, frame/resource counts, and GPU resource pruning.
- TypeScript collects DOM input, parses URL seed/preset values, starts WASM,
  hosts browser Workers below `RustBrowserGameRuntime`, wraps shared density
  buffers, exposes debug hooks, receives terrain mesh bytes from worker results,
  fetches texture assets, and passes terrain mesh bytes by chunk key plus texture
  arrays into Rust-owned renderer facades. `src/app` no longer constructs the
  terrain scheduler, density store, render packet store, worker client, mirrored
  terrain sink, texture upload path, or terrain height sampler directly. Rust
  owns the terrain renderer vertex stride and active frame construction at that
  facade.
  TypeScript no longer creates WebGPU devices, pipelines, buffers, textures,
  render passes, shader uniform buffers, renderer resource handles, shader
  material packets, camera frames, light packets, player-marker mesh/material
  data, player-marker world matrices, or normal matrices.

The retired TypeScript scene model is archived under `docs/archived/`. Future
large-scale world state should move into Rust rather than recreating that graph.

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

`RustBrowserGameRuntime` keeps the playable browser terrain bridge away from the
app layer. Beneath it, `TerrainCoreWorkerStreamer` still coordinates the
browser-only Worker transport while `terrain_core.wasm` owns desired
density/LOD0 sets, dependency coordinates, in-flight work, stale generation
rejection, ready/empty state, density storage, mesh packet storage, packet
pruning, and the worker-pool/request model. That worker model includes slot
assignment, request IDs, reset generations, and completion validation. TypeScript
still constructs browser Workers, but only below the runtime facade and through
the generic `BrowserWorkerHost` request envelope. Worker density jobs return
WASM-filled density arrays that the main bridge stores in the Rust density store
for scheduler bookkeeping; LOD0 jobs build meshes inside the worker
`terrain_core.wasm` instance, which generates or reuses its own neighbor density
apron. The TypeScript terrain client still names density and chunk payloads.
Rust-managed wasm threads or a Rust-owned opaque worker job protocol are still
future work.
Loaded density chunk keys remain fully 3D.
Runtime terrain meshes carry position, color, normal, uv, material layer indices,
and material weights from Rust `terrain_core`. In the playable browser runtime,
those chunk mesh payloads are uploaded directly into `RustBrowserGame`
chunk-keyed terrain mesh handles. The adapter tracks live chunk keys for
debug/smoke, while Rust/wgpu owns the actual GPU mesh handles and active draw
set. The old compiled TypeScript
`TerrainChunkStreamer`, `TerrainRenderer`, `TerrainRenderPacketStore`,
`RenderWorld`, highest-surface mesher, and heightfield mesh path have been
retired rather than kept as parallel terrain owners.

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

The first material model is intentionally pre-PBR: mesh vertex color multiplied by
an albedo factor and optional albedo texture sample, plus specular color and
specular factor. The shader uses a simple Lambert diffuse plus Blinn-Phong specular
model. Terrain texture helpers decode checked-in image assets into CPU-side
rgba8 arrays that the TypeScript shell passes through one terrain-specific Rust
facade call. Rust/wgpu builds compact frame packets from its Rust-owned browser
game state, owns shader material packets and material-to-texture selection, owns
the debug player-marker mesh/material and world matrix, validates per-chunk
terrain draw transforms inside the browser game facade, computes object normal
matrices, and packs the WGSL camera/object uniform buffers.

Terrain uses checked-in Poly Haven CC0 materials imported into 16-layer global
texture arrays. The runtime currently loads albedo, normal, and roughness arrays;
the WGSL terrain path triplanar-blends albedo and roughness from up to four
material weights per vertex. Normal maps are loaded as renderer resources but are
not yet applied to lighting.

The sky is also shader-driven. Rust/wgpu draws a full-screen sky pass before
terrain and marker geometry, reconstructs world rays from the inverse
view-projection matrix, and renders a blue gradient plus a sun disk in the
direction of the Rust-owned main light.

## Testing Direction

- Unit tests cover deterministic math, Rust player/camera behavior, render data,
  terrain data contracts, Rust/WASM terrain adapters, and worker bridge behavior.
- Shader tests verify generated shader metadata and the renderer vertex layout
  contract.
- Browser smoke tests cover canvas rendering, input toggles, resize behavior, and
  basic chunk streaming after moving the player across chunk columns.
- Rust/WASM terrain tests cover height/density determinism, density chunk fill,
  mesh buffers, retained stores, stream scheduling, and worker-pool behavior.
- Performance tests should be explicit scripts with stable scene seeds, not hidden
  assertions inside regular unit tests.
