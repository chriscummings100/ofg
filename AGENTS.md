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
- Rust-owned first-person camera/player movement through `engine_web.wasm`,
  backed by `engine_core`.
- Rust/wgpu WebGPU renderer through `engine_web.wasm`; Rust owns browser draw
  submission, terrain mesh handles, texture handles, and renderer pruning.
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
- [docs/BROWSER_RUST_API.md](docs/BROWSER_RUST_API.md): target
  TypeScript-to-Rust browser API and Rust-to-browser interaction contract.
- [docs/TYPESCRIPT_REDUCTION_AUDIT.md](docs/TYPESCRIPT_REDUCTION_AUDIT.md):
  current TypeScript ownership audit, redundancy map, and deletion paths.
- [docs/SCENE_MODEL_PLAN.md](docs/SCENE_MODEL_PLAN.md): scene/entity/component model
  and its intended test coverage. This is now historical/transitional guidance,
  not the target architecture for high-volume world systems.
- [docs/BROWSER_VERIFICATION.md](docs/BROWSER_VERIFICATION.md): screenshot and
  browser interaction verification.
- [docs/AI_WORKFLOW.md](docs/AI_WORKFLOW.md): expected agent loop and testing habits.
- [PLANS.md](PLANS.md): OpenAI/Codex ExecPlan standard for substantial
  multi-step work.

If context is compacted or you are unsure about engine ownership, reread
`docs/RUST_ENGINE_PLAN.md` and `docs/terrainplan.md` before continuing.

## ExecPlans

Use an ExecPlan for multi-step or multi-file work, new features, refactors, or
tasks expected to take more than about an hour. Follow [PLANS.md](PLANS.md):
read it before drafting, keep the plan self-contained, and update its living
sections as work proceeds.

When implementing from an ExecPlan, continue to the next milestone without
asking for next steps unless blocked. Keep Progress, Surprises & Discoveries,
Decision Log, and Outcomes & Retrospective current at every stopping point, and
make acceptance criteria observable with exact commands or screenshots where
relevant.

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

## Code style

- Code should be designed to be human readable
- Avoid huge files, or tiny files. Over 1000 lines is concerning.
- Files should have comments at the top saying what they do
- Comments at the top of a file can act as a living document - notes on decisions made
  for it can be stored and re-read.
- Well commented functions are important.
   - All functions should always have a description of what they do
   - Long functions should have internal comments to explain their steps
- Avoid overengineering, such as:
   - Tiny wrapper functions that're only called once, and could just be used directly
   - Complex data structures in anticipation of features we don't need
   - Backwards compatibility - we will never need this
- Cleanup functions that are no longer needed as you go - it is better to rewrite something than
  keep dead code around just in case its needed later.
- Commit and push regularly to git

## Current Architecture

```text
src/app
  Browser setup, game loop, HUD, URL seed/preset parsing, input forwarding,
  debug hooks, and calls into the coarse `RustBrowserGameRuntime` facade for
  `tick` and `renderFrame`.

src/engine/math
  Vec3, Vec4, Quat, Mat4 primitives.

src/engine/input
  DOM input tracker for keys, edge-triggered presses, pointer-lock mouse deltas.

src/engine/world
  Browser-side terrain descriptor/config types, 3D density chunk data contracts,
  Rust/WASM terrain adapters, generic browser worker transport, density transfer
  helpers, terrain material metadata, terrain mesh vertex layout helpers, and
  primitive box mesh. Compiled TypeScript no longer owns terrain generation,
  noise, Dual Contouring, terrain streaming policy, or a terrain manager.

src/engine/render
  Browser-side texture loading helpers, shader metadata tests, and the legacy
  `TerrainCoreRenderPacketStore` adapter/test surface. The playable browser path
  no longer has a TypeScript WebGPU renderer or `RenderWorld`; runtime worker
  mesh results are handed to `RustBrowserGame` by chunk key, and Rust/wgpu owns
  actual WebGPU resources and draw submission.
  Terrain rendering uses global 16-layer albedo, normal, and roughness texture
  arrays. Normal maps are loaded but not yet applied in shading.

src/engine/render/shaders
  Shader source inputs. `uber.wgsl` is compiled into a TypeScript artifact for
  shader contract tests, and the Rust renderer includes the shared WGSL source.

src/generated
  Deterministic generated TypeScript artifacts, currently shader source modules
  and Rust/WASM terrain, engine, and engine-web artifact metadata.

crates/engine_core
  Browser-free Rust engine core. It owns player/camera logic, a small
  world/entity ID model, transforms, and render snapshot logic. Its standalone
  WASM artifact remains tested, but the playable browser app now reaches this
  logic through `engine_web`.

crates/terrain_core
  Rust terrain core built to wasm32-unknown-unknown. It owns macro base
  elevation, density, compatibility height sampling, density chunk filling,
  browser runtime chunk mesh generation, stream scheduling, density storage,
  worker-pool state, and the tested legacy terrain mesh packet store. It is now
  the browser terrain source of truth; the playable mesh handoff currently goes
  from worker results straight into `engine_web`/Rust-wgpu terrain mesh handles.

crates/engine_web
  Browser-facing Rust game/render bridge built to wasm32-unknown-unknown. It
  owns the active browser player/camera tick state, Rust/wgpu renderer, WebGPU
  resource handles, terrain texture handles, terrain mesh handles, live terrain
  draw set, and frame draw submission.

src/engine/web
  Browser-facing TypeScript shell around Rust/WASM systems. It loads
  `RustBrowserGame`, hosts the temporary terrain Worker transport, forwards
  input/debug commands, uploads decoded terrain texture arrays and worker mesh
  bytes to Rust, and keeps browser-only compatibility shims. It should keep
  shrinking toward a generic browser shell with no terrain semantics.

tools
  Local scripts, including shader generation, Poly Haven terrain texture import,
  the static dev server, and browser smoke tests.
```

## Runtime Ownership Rules

The compiled TypeScript scene/component model has been retired. Do not recreate a
new TypeScript scene graph, ECS, terrain generator, or terrain manager as the next
step.

- Rust owns active browser player/camera state through `engine_web.wasm`, using
  `engine_core` as a Rust library.
- Rust owns generated terrain sampling, Dual Contouring mesh emission, stream
  scheduling, density stores, worker-pool state, and terrain mesh packet stores
  through `terrain_core.wasm`.
- Rust owns browser WebGPU resource creation and draw submission through
  `engine_web.wasm` and `wgpu`.
- TypeScript currently owns browser startup, DOM input collection, URL parameter
  parsing, debug hooks, browser Worker transport, terrain mesh/texture byte
  upload adaptation, and terrain status/debug mirrors below the browser runtime
  facade.
- New high-volume world, terrain streaming, simulation, render extraction, and
  WebGPU ownership should follow `docs/RUST_ENGINE_PLAN.md`.
- Use `docs/TYPESCRIPT_REDUCTION_AUDIT.md` before deleting or adding TypeScript
  around terrain, rendering, or engine ownership.
- Use `docs/BROWSER_RUST_API.md` to judge whether a TypeScript/Rust boundary
  change moves toward or away from the exact target API.

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
- Gameplay/input: Rust browser game/player facade and input tracker.
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
- Prefer deleting or demoting whole TypeScript categories named in
  `docs/TYPESCRIPT_REDUCTION_AUDIT.md` rather than repeatedly shrinking wrappers
  while preserving terrain-aware TypeScript.
- Keep shader work in plain WGSL behind `tools/build-shaders.mjs`. Do not introduce
  alternate shader languages unless the project direction changes again.

## Git Notes

There may be Windows ownership warnings from Git. Use the repository-safe-directory
flag if needed:

```powershell
git -c safe.directory=C:/dev/ofg status
```

Do not commit generated `dist/`, `node_modules/`, or `artifacts/` output.
