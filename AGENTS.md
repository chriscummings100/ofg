# AGENTS

This file is the first stop for AI agents working in this repository. It gives the
project shape, the commands to trust, and the deeper docs to read before changing a
system.

## Project Goal

OFG is a browser-native online factory game prototype. The long-term direction is a
lightweight custom engine with voxel terrain, Dual Contouring, WebGPU rendering, and
a Rust/TypeScript toolchain that stays friendly to automated AI development.

The current playable seed is still simple:

- Chunk-streamed generated terrain from a sine-wave heightfield baseline.
- Runtime terrain meshed as whole generated LOD nodes, grass material only.
- Poly Haven terrain materials rendered from global WebGPU texture arrays.
- Rust terrain core library/artifact that owns terrain height sampling, node
  meshing, stream scheduling, worker build packets, and minimal triangle-backed
  height queries for player grounding.
- Rust-owned first-person camera/player movement through `engine_web.wasm`,
  backed by `engine_core`.
- Rust/wgpu WebGPU renderer through `engine_web.wasm`; Rust owns browser draw
  submission, terrain mesh handles, texture handles, and renderer pruning.
- Rust-owned browser terrain stream inside `engine_web.wasm`, backed by
  `terrain_core` as a Rust library.
- Debug fly camera toggled with `C` or `F1`.
- A yellow player marker visible in debug fly mode.
- WebGPU renderer using generated WGSL shader artifacts.

The previous density/Dual Contouring terrain implementation is preserved only as
reference under `docs/reference/terrain_legacy_2026_06_15/`. The active terrain
rebuild starts from sine grass, no separate collision mesh, no aprons, no
placement, and no water so the multi-LOD streaming and transition model can
stay lean. First-person/third-person player grounding uses the generated
visible terrain triangles when available.

## Read These When Needed

- [README.md](README.md): setup, commands, and high-level project shape.
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): current architecture overview.
- [docs/API_CONTRACTS.md](docs/API_CONTRACTS.md): living contracts between
  browser TypeScript, Rust/WASM, terrain, renderer, asset loading, debug hooks,
  and fixture-only adapters.
- [docs/TERRAIN_PLAN.md](docs/TERRAIN_PLAN.md): living terrain realism plan.
- [docs/TERRAIN_GEN_RESEARCH.md](docs/TERRAIN_GEN_RESEARCH.md): terrain
  generation research reference.
- [PLANS.md](PLANS.md): OpenAI/Codex ExecPlan standard for substantial
  multi-step work.

If context is compacted or you are unsure about engine ownership, reread
`docs/API_CONTRACTS.md` and `docs/ARCHITECTURE.md`. If terrain realism or
terrain generation is involved, also reread `docs/TERRAIN_PLAN.md`.

## Archived Docs

`docs/archived/` contains retired plans and reference snapshots. Documents in
that folder are not active instructions. Use them only for historical context
when explicitly needed.

When an active plan finishes or is replaced, move it to `docs/archived/` and add
a short note explaining where the active source of truth moved.

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

After each ExecPlan milestone, run the repo-local `milestone-review` skill before
marking the milestone complete. Act on required findings or record a rejection
with rationale in the plan's Decision Log.

## Commands

```powershell
npm run clean
npm run build
npm run build:shaders
npm run check:shaders
npm run build:wasm
npm run check:wasm
npm run bench:terrain:rust
npm run coverage:rust
npm run test:rust
npm run test:ts
npm test
npm run smoke:rust
npm run smoke:browser
npm run smoke
npm run smoke:terrain-seams
npm run smoke:terrain-presets
npm run dev
```

Use `npm test` for logic changes; it runs Rust workspace tests and the separated
TypeScript test lane. Use `npm run test:rust` for Rust-only logic changes and
`npm run test:ts` for browser shell or TypeScript utility changes. Use
`npm run smoke:rust` for Rust-owned terrain/render image smoke. Use
`npm run smoke:browser` for browser integration changes: browser boot, WebGPU
canvas setup, wasm-bindgen loading, browser asset fetch/decode, HUD, page
reload, and DOM input forwarding. Use `npm run smoke` to run both Rust image
smoke and browser integration smoke. Use `npm run smoke:terrain-seams` for
terrain seam, mesh, material, or Dual Contouring changes; it now runs Rust
offscreen image smoke. Use `npm run smoke:terrain-presets` for preset,
descriptor, biome/material classification, or terrain visual changes; it now
runs Rust offscreen image smoke.
Use `npm run bench:terrain:rust` for performance-sensitive terrain density,
mesh, store, or streaming changes. It runs a Rust benchmark and writes JSON
under `artifacts/terrain-bench/`; TypeScript must not benchmark terrain WASM
directly. Use `npm run coverage:rust` when extending Rust API tests or auditing API
coverage. If `cargo-llvm-cov` is missing, the command prints setup guidance
without installing tools or mutating build output. By default, the console and
`artifacts/coverage/rust/summary.json` / `summary.pretty.json` show only
implementation files below the documented 90% line-coverage attention threshold,
excluding tests, the smoke/benchmark harness, and Rust export glue such as
`lib.rs` and `facade.rs`; use `npm run coverage:rust -- --full` for the full
cargo summary.

`npm run smoke:browser` launches installed Chrome/Edge through Playwright Core,
saves screenshots in `artifacts/browser-smoke/`, samples pixels, verifies
COOP/COEP browser isolation, validates Rust runtime sentinel strings, reloads,
and verifies one `C` camera-toggle input path.

## Agent Workflow

1. Read the nearest module and test before changing behavior.
2. Make the smallest coherent change.
3. Run `npm test` for logic changes.
4. Run `npm run build` when build output or generated artifacts may be affected.
5. Run `npm run smoke:rust` for terrain mesh, material, preset, terrain visual,
   or Rust-owned render image changes.
6. Run `npm run smoke:browser` for browser, input, HUD, wasm loading, browser
   asset loading, reload, or WebGPU canvas integration changes.
7. Summarize what changed, what was verified, and any remaining risk.

Prefer behavior-focused test names such as `grounds first-person player on sampled
terrain` or `rejects stale worker completions after reset`.

## Browser Verification

`npm run smoke:browser`:

- Builds the TypeScript app.
- Starts a temporary local dev server.
- Launches installed Chrome/Edge through Playwright Core.
- Saves screenshots under `artifacts/browser-smoke/`.
- Reads HUD state and samples screenshot pixels to catch blank or solid frames.
- Reloads the page and fails on black or blank refresh frames.
- Presses `C` and verifies the camera mode changes from `FIRST` to `THIRD`.
- Verifies browser-only integration signals: COOP/COEP headers,
  `crossOriginIsolated`, `SharedArrayBuffer`, Rust runtime sentinel strings, and
  Rust/wgpu renderer status.

`npm run smoke:rust`:

- Runs `ofg-render-smoke` from `crates/ofg_test_harness`.
- Creates native `wgpu` offscreen textures without a browser.
- Ticks the Rust terrain stream, renders terrain/sky images, writes PNGs under
  `artifacts/rust-smoke/`, writes `report.json`, and samples pixels to catch
  blank or solid frames.
- Owns terrain preset and seam/corner image smoke through
  `npm run smoke:terrain-presets` and `npm run smoke:terrain-seams`.

Useful environment variables:

- `OFG_SMOKE_PORT`: preferred local port. Defaults to `5174`.
- `OFG_BROWSER_PATH`: explicit Chromium-based browser executable path.
- `OFG_SMOKE_HEADED=1`: launch a visible browser for debugging.

This is a smoke test, not a full visual diff. Extend it as interactions become
important.

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
  debug hooks, and calls into the `RustBrowserGameRuntime` facade through frame
  input packets, commands, and debug snapshots.

src/engine/math
  Vec3, Vec4, Quat, Mat4 primitives.

src/engine/input
  DOM input tracker for keys, edge-triggered presses, pointer-lock mouse deltas.

src/engine/browser
  Generic browser substrate helpers. `BrowserWorkerHost` remains as tested
  generic worker substrate, but the playable terrain path no longer uses a
  TypeScript terrain worker bridge. `textureAssetLoader.ts` is a generic browser
  image decoder that accepts Rust-provided URL lists and returns RGBA texture
  arrays without interpreting terrain manifests.

src/engine/world
  Browser-side terrain descriptor/config types and 3D chunk coordinate/key
  helpers. Compiled TypeScript no longer owns terrain generation, noise, Dual
  Contouring, terrain streaming policy, terrain workers, terrain edits, material
  manifests, density transfer between WASM instances, terrain mesh data/stride
  contracts, standalone terrain WASM adapters, or a terrain manager.

src/engine/render
  Shader metadata tests. The playable browser path no longer has terrain texture
  manifest helpers, a TypeScript WebGPU renderer, `RenderWorld`, terrain render
  sink, or terrain mesh upload bridge; Rust owns texture manifest semantics,
  mesh generation, GPU handles, active draw sets, and draw submission.
  Terrain rendering uses global 16-layer albedo, normal, and roughness texture
  arrays. Normal maps are loaded but not yet applied in shading.

src/engine/render/shaders
  Shader source inputs. `uber.wgsl` is compiled into a TypeScript artifact for
  shader contract tests, and the Rust renderer includes the shared WGSL source.

src/generated
  Deterministic generated TypeScript artifacts, currently shader source modules
  and engine-web artifact metadata.

crates/engine_core
  Browser-free Rust engine core. It owns player/camera logic, a small
  world/entity ID model, transforms, and render snapshot logic. It is tested as
  a native Rust crate and reached by the playable browser app through
  `engine_web`; no standalone `engine_core.wasm` browser artifact is built.

crates/terrain_core
  Rust terrain core built as both an rlib and a wasm32-unknown-unknown test/dev
  artifact. It owns the sine baseline terrain variant catalog, compatibility
  height sampling, generated node mesh emission, stream scheduling, and the
  narrow worker-build facade. The playable browser app reaches it through
  `engine_web` as a Rust library and through the dedicated browser terrain build
  worker; TypeScript does not own terrain scheduling or generation.

crates/engine_web
  Browser-facing Rust game/render bridge built to wasm32-unknown-unknown. It
  owns the active browser player/camera tick state, Rust-owned terrain stream,
  terrain mesh generation/upload/pruning, terrain texture manifest parsing,
  texture-array validation/upload, Rust/wgpu renderer, WebGPU resource handles,
  terrain texture handles, terrain mesh handles, live terrain draw set, and
  frame draw submission.

src/engine/web
  Browser-facing TypeScript shell around Rust/WASM systems. It loads
  `RustBrowserGame`, forwards input/debug commands, passes a generic browser
  texture-array decoder into Rust at creation time, reads Rust debug snapshots,
  and keeps browser-only compatibility shims. It should keep shrinking toward a
  generic browser shell with no terrain semantics.

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
- Rust owns generated terrain sampling, mesh emission, stream scheduling, and
  terrain mesh packet ownership through `terrain_core` and `engine_web`.
- Rust owns browser WebGPU resource creation and draw submission through
  `engine_web.wasm` and `wgpu`.
- TypeScript currently owns browser startup, DOM input collection, URL parameter
  parsing, debug hooks, generic browser image decoding for Rust-provided texture
  requests, and the browser runtime facade.
- New high-volume world, terrain streaming, simulation, render extraction, and
  WebGPU ownership should follow `docs/API_CONTRACTS.md` and
  `docs/ARCHITECTURE.md`.
- Use `docs/API_CONTRACTS.md` before deleting or adding TypeScript around
  terrain, rendering, or engine ownership.

## Testing Expectations

This project should be test-heavy because it is intended to be heavily AI-built.

Current test areas include:

- Math: vectors, quaternions, and matrices.
- Render data: mesh/material/texture metadata, Rust renderer resource contracts,
  and fixture-only terrain render packet stores.
- Shader boundary: generated shader source artifact metadata and vertex layout
  contract.
- World terrain: 3D terrain chunk keys, Rust-owned chunk streaming, and Rust
  terrain core sampling/mesh/store/stream fixtures.
- Gameplay/input: Rust browser game/player facade and input tracker.
- Browser smoke: actual Chrome/Edge WebGPU render, screenshots, pixel checks,
  browser isolation, reload, Rust runtime sentinels, and one HUD camera-toggle
  input path.
- Rust terrain smoke: native `wgpu` offscreen PNG captures for terrain
  boot/preset/seam image regressions, with reports under `artifacts/rust-smoke/`.

When adding behavior, add tests near the behavior first or in the same change.
Prefer behavior names such as `rejects stale worker completions after reset`.

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
- Prefer deleting or demoting whole forbidden TypeScript ownership categories
  named in `docs/API_CONTRACTS.md` rather than repeatedly shrinking wrappers
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
