# AGENTS

This file is the first stop for AI agents working in this repository. It gives the
project shape, the commands to trust, and the deeper docs to read before changing a
system.

## Project Goal

OFG is a browser-native online factory game prototype. The long-term direction is a
lightweight custom engine with voxel terrain, Dual Contouring, WebGPU rendering, and
a Rust/TypeScript toolchain that stays friendly to automated AI development.

The current playable seed is still simple:

- Chunk-streamed generated terrain from 3D density chunks.
- Runtime terrain meshed as per-chunk neighbor-aware Dual Contouring chunks.
- Poly Haven terrain materials rendered from global WebGPU texture arrays.
- Rust/WASM terrain core artifact that owns terrain height/density sampling,
  chunk meshing, stream scheduling, density storage, and mesh packet storage.
- Rust-owned first-person camera/player movement through `engine_core.wasm`.
- First Rust/WASM WebGPU renderer bridge through `engine_web.wasm`, currently
  tracking renderer resource lifetimes while TypeScript still submits draws.
- Debug fly camera toggled with `C` or `F1`.
- A yellow player marker visible in debug fly mode.
- WebGPU renderer using generated WGSL shader artifacts.

The current terrain uses same-LOD per-chunk Dual Contouring. LOD transitions and
far-field terrain are still future terrain architecture work.

## Read These When Needed

- [README.md](README.md): setup, commands, and high-level project shape.
- [docs/ROADMAP.md](docs/ROADMAP.md): milestone direction.
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): current architecture overview.
- [docs/RUST_ENGINE_PLAN.md](docs/RUST_ENGINE_PLAN.md): Rust-first engine
  migration, including Rust-owned WebGPU through `wgpu`.
- [docs/SCENE_MODEL_PLAN.md](docs/SCENE_MODEL_PLAN.md): scene/entity/component model
  and its intended test coverage. This is now historical/transitional guidance,
  not the target architecture for high-volume world systems.
- [docs/BROWSER_VERIFICATION.md](docs/BROWSER_VERIFICATION.md): screenshot and
  browser interaction verification.
- [docs/AI_WORKFLOW.md](docs/AI_WORKFLOW.md): expected agent loop and testing habits.

If context is compacted or you are unsure about engine ownership, reread
`docs/RUST_ENGINE_PLAN.md` and `docs/terrainplan.md` before continuing.

## Commands

```powershell
npm run clean
npm run build
npm run build:shaders
npm run check:shaders
npm run build:wasm
npm run check:wasm
npm run bench:terrain:wasm
npm test
npm run smoke:browser
npm run dev
```

Use `npm test` for logic changes. Use `npm run smoke:browser` whenever rendering,
input, camera behavior, HUD behavior, or browser integration changes.

`npm run smoke:browser` launches installed Chrome/Edge through Playwright Core,
saves screenshots in `artifacts/browser-smoke/`, samples pixels, and verifies the
`FIRST -> FLY` camera toggle.

## Current Architecture

```text
src/app
  Browser setup, game loop, HUD, input forwarding, debug hooks, and direct
  assembly of the temporary TypeScript `RenderWorld`. Long term this becomes a
  thinner TypeScript shell around the Rust engine.

src/engine/math
  Vec3, Vec4, Quat, Mat4 primitives.

src/engine/input
  DOM input tracker for keys, edge-triggered presses, pointer-lock mouse deltas.

src/engine/world
  Browser-side terrain descriptor/config types, 3D density chunk data contracts,
  Rust/WASM terrain adapters, generic browser worker transport, density transfer
  helpers, terrain material metadata, terrain mesh vertex layout helpers, and
  primitive box mesh. Compiled TypeScript no longer owns terrain generation,
  noise, Dual Contouring, or a terrain manager.

src/engine/render
  WebGPU renderer plus packet render data types. Runtime rendering flows through
  `RenderWorld` assembled by the app, `TerrainCoreRenderPacketStore` for streamed
  terrain chunks, Rust engine render packets for camera/light/player marker, and
  the temporary TypeScript `WebGpuRenderer`. That renderer now mirrors canvas,
  mesh, texture, object, frame, draw, and pruning lifetimes into
  `engine_web.wasm`; actual WebGPU calls remain TypeScript-owned until Rust/wgpu
  lands.
  Materials currently support albedo factor, albedo/normal/material texture
  resources, specular, and specular factor; the shader uses Lambert plus
  Blinn-Phong lighting. Terrain rendering uses global 16-layer albedo, normal, and
  roughness texture arrays; normal maps are loaded but not yet applied in shading.
  `RenderWorld.mainLight` also drives the procedural sky sun disk. Rust/wgpu is
  the target renderer once Rust render packets exist.

src/engine/render/shaders
  Shader source inputs. `uber.wgsl` is compiled into a TypeScript artifact before
  `tsc` runs.

src/generated
  Deterministic generated TypeScript artifacts, currently shader source modules
  and Rust/WASM terrain, engine, and engine-web artifact metadata.

crates/engine_core
  Rust engine core built to wasm32-unknown-unknown. It owns player/camera state,
  a small world/entity ID model, transforms, and the first render packet snapshot
  bridge for camera/light/player-marker data.

crates/terrain_core
  Rust terrain core built to wasm32-unknown-unknown. It owns macro base
  elevation, density, compatibility height sampling, density chunk filling,
  browser runtime chunk mesh generation, stream scheduling, density storage,
  worker-pool state, and terrain mesh packet storage. It is now the browser
  terrain source of truth.

crates/engine_web
  Browser-facing Rust renderer bridge built to wasm32-unknown-unknown. It owns
  the first tested WebGPU resource ledger and is the staging crate for the future
  Rust/wgpu renderer.

src/game/components
  Game-level browser bridge classes such as `RustPlayerController` and
  `TerrainCoreWorkerStreamer`. These are plain TypeScript wrappers around Rust
  engine/terrain state, not scene components.

tools
  Local scripts, including shader generation, Poly Haven terrain texture import,
  the static dev server, and browser smoke tests.
```

## Runtime Ownership Rules

The compiled TypeScript scene/component model has been retired. Do not recreate a
new TypeScript scene graph, ECS, terrain generator, or terrain manager as the next
step.

- Rust owns player/camera state through `engine_core.wasm`.
- Rust owns generated terrain sampling, Dual Contouring mesh emission, stream
  scheduling, density stores, worker-pool state, and terrain mesh packet stores
  through `terrain_core.wasm`.
- TypeScript currently owns browser startup, DOM input collection, URL parameter
  parsing, debug hooks, browser Worker transport, WebGPU resource upload/cache
  adaptation, and actual draw submission through the temporary `WebGpuRenderer`.
  Rust already tracks the renderer resource ledger through `engine_web.wasm`.
- New high-volume world, terrain streaming, simulation, render extraction, and
  WebGPU ownership should follow `docs/RUST_ENGINE_PLAN.md`.

## Testing Expectations

This project should be test-heavy because it is intended to be heavily AI-built.

Current test areas include:

- Math: vectors, quaternions, and matrices.
- Render data: mesh/material/texture metadata and Rust-backed terrain render
  packet store.
- Shader boundary: generated shader source artifact metadata and vertex layout
  contract.
- World terrain: 3D density chunks, terrain edits, primitive meshes, terrain
  material packing, Rust-owned chunk streaming, and Rust/WASM terrain core
  sampling/mesh/stream fixtures.
- Gameplay/input: Rust player controller adapter and input tracker.
- Browser smoke: actual Chrome/Edge WebGPU render, screenshots, pixel checks, HUD
  camera toggle verification, and a basic player-position chunk streaming check.

When adding behavior, add tests near the behavior first or in the same change.
Prefer behavior names such as `rejects stale worker completions after reset`.

## Browser Verification Workflow

For visual or interactive work:

1. Run `npm test`.
2. Run `npm run smoke:browser`.
3. Inspect screenshots in `artifacts/browser-smoke/<run-id>/` when behavior or
   framing matters.
4. Check `report.json` for HUD state, WebGPU availability, pixel stats, and console
   messages.

The smoke test is designed to catch blank frames, solid-color regressions, broken
WebGPU startup, and camera toggle failures. Extend it as new interactions become
important.

## Design Bias

- Keep the engine lightweight and browser-native.
- Prefer deterministic pure logic in small modules.
- Add abstractions only when they match the architecture plan or remove real
  duplication.
- Keep WebGPU details behind render-facing boundaries.
- Do not introduce a full ECS.
- Use Rust as the target home for world, simulation, terrain streaming, render
  extraction, and eventually WebGPU rendering. Migrate by tested vertical slices
  that remove TypeScript ownership rather than by adding parallel systems.
- Keep shader work in plain WGSL behind `tools/build-shaders.mjs`. Do not introduce
  alternate shader languages unless the project direction changes again.

## Git Notes

There may be Windows ownership warnings from Git. Use the repository-safe-directory
flag if needed:

```powershell
git -c safe.directory=C:/dev/ofg status
```

Do not commit generated `dist/`, `node_modules/`, or `artifacts/` output.
