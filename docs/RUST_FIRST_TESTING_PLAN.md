# Move OFG to a Rust-first testing system

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

If `PLANS.md` is present in the repo, maintain this document in accordance with it. This plan follows `PLANS.md` in this repository.

## Purpose / Big Picture

OFG should test engine behavior where the engine lives: in Rust. The browser should be tested only as a browser integration boundary. After this plan is complete, TypeScript will no longer act as a terrain client in runtime code or tests. Terrain density sampling, chunk generation, stream scheduling, mesh generation, seam behavior, material classification, renderer bookkeeping, and image-producing smoke coverage will run in Rust. Browser smoke tests will remain narrow and will prove that the browser shell can load `engine_web.wasm`, initialize WebGPU on a canvas, forward browser input, load browser-only assets, and render a nonblank frame.

The user-visible outcome is a clearer and safer testing system: AI agents and humans can run one command for Rust logic tests, one command for Rust offscreen image smoke tests, one command for browser integration smoke, and one combined command. Both browser and Rust smoke tests write PNG artifacts and JSON reports under `artifacts/` so AI tools can read, inspect, and show images from either test family.

## Progress

- [x] (2026-06-07) Created this ExecPlan from the user decision that TypeScript should never be a terrain client, even in tests.
- [x] (2026-06-07) Baseline audit commands passed before implementation: `cargo test --workspace` and the pre-migration `npm test`.
- [x] (2026-06-07) Audited the TypeScript terrain test adapters. The former TypeScript terrain-client coverage was concentrated in `src/engine/world/terrainCoreWasm.test.ts`; replacement coverage now lives in `crates/terrain_core/src/tests.rs`.
- [x] (2026-06-07) Split the TypeScript app and test compilation lanes with `tsconfig.app.json` and `tsconfig.test.json`. `npm run build` now emits app/runtime output only, while `npm run test:ts` emits test output to `dist-test`.
- [x] (2026-06-07) Added `tests/ts/runtimeImportGraph.test.ts` to prove the browser runtime import graph from `src/main.ts` cannot reach standalone TypeScript terrain clients.
- [x] (2026-06-07) Validation after the build split passed: `npm run build`, `npm run test:rust`, and `npm run test:ts`. The new TypeScript lane reports 61 passing tests, including the runtime import-graph guard.
- [x] (2026-06-07) Ran the Milestone 1 review locally. Sub-agent review was not used because the available sub-agent tool requires an explicit user request for delegated agent work. Required code-quality cleanup was applied to `tests/ts/runtimeImportGraph.test.ts`; `npm run test:ts`, `npm test`, and scoped `git diff --check` passed after the cleanup.
- [x] (2026-06-07) Added Rust `terrain_core` coverage for all-preset finite height/density samples, deterministic density chunk fill, terrain facade buffer shape, and material weight sums in generated mesh vertices.
- [x] (2026-06-07) Deleted the standalone TypeScript terrain client harness: `src/engine/world/terrainCoreWasm.test.ts`, `terrainCoreWasm.ts`, `terrainCoreChunkMesh.ts`, `terrainCoreDensityChunk.ts`, `terrainCoreDensityChunkStore.ts`, `terrainCoreStreamScheduler.ts`, `terrainMesh.ts`, and `src/generated/terrain/terrainCoreWasm.ts`.
- [x] (2026-06-07) Updated `tools/build-terrain-wasm.mjs` so it validates expected standalone WASM exports directly and no longer generates TypeScript terrain metadata.
- [x] (2026-06-07) Validation after deleting the TypeScript terrain clients passed: `cargo test -p terrain_core`, `npm run build`, `npm run check:wasm`, `npm run test:ts`, and `npm test`.
- [x] (2026-06-07) Ran the Milestone 2 review locally. Required cleanup was applied to `tests/ts/runtimeImportGraph.test.ts` so the guard catches static imports, runtime re-exports, and dynamic imports before the milestone was marked complete.
- [x] (2026-06-07) Added `crates/ofg_test_harness` with the `ofg-render-smoke` native wgpu binary. `npm run smoke:rust` renders six Rust terrain/sky PNGs and writes `artifacts/rust-smoke/<run-id>/report.json`.
- [x] (2026-06-07) Refactored the Rust smoke harness into `render_smoke` modules after review found the first binary file exceeded the repository split-pressure limit.
- [x] (2026-06-07) Ran the Milestone 3 review locally. Required cleanup was applied to document the safety of GPU upload byte casts in `crates/ofg_test_harness/src/render_smoke/renderer.rs`; `cargo check -p ofg_test_harness` and scoped `git diff --check` passed after the cleanup.
- [x] (2026-06-07) Moved terrain preset and seam smoke command ownership to Rust. `npm run smoke:terrain-presets` now renders all four preset PNGs through `ofg-render-smoke --scenario presets`, and `npm run smoke:terrain-seams` renders x-seam, z-seam, and chunk-corner PNGs through `--scenario seams`.
- [x] (2026-06-07) Deleted the browser-specific terrain smoke scripts `tools/terrain-seam-smoke.mjs` and `tools/terrain-preset-smoke.mjs`.
- [x] (2026-06-07) Narrowed `tools/browser-smoke.mjs` to browser integration only: headers/isolation, WebGPU canvas, wasm loading, Rust runtime sentinels, one `C` keyboard toggle, reload, screenshots, and pixel checks.
- [x] (2026-06-07) Ran the Milestone 4/5 review locally. No required findings remained after expanding Rust seam smoke to x-seam, z-seam, and chunk-corner scenarios with Rust-side coverage checks.
- [x] (2026-06-07) Removed legacy standalone TypeScript `terrain_core.wasm` adapters and generated metadata.
- [x] (2026-06-07) Verified no TypeScript terrain clients remain in source or build outputs except intentional guard/denylist strings. The standalone `terrain_core.wasm` artifact remained only for export-contract checks and benchmark tooling at the time; the later API coverage plan moved benchmarking to Rust.
- [x] (2026-06-07) Updated contracts, architecture docs, terrain plan validation guidance, README commands, and AGENTS command guidance to describe the Rust-first test system.
- [x] (2026-06-07) Final validation passed: `npm run clean`, `npm run build`, `npm run test:rust`, `npm run test:ts`, `npm run check:shaders`, `npm run check:wasm`, `npm test`, and `npm run smoke`.
- [x] (2026-06-07) Ran the final Milestone 6/7 review locally. No required findings remained.

## Surprises & Discoveries

- Observation: The former browser screenshot smoke scripts did not import the TypeScript terrain WASM adapters. They drove the browser through Playwright, read `window.__ofgDebug`, and analyzed PNG screenshots.
  Evidence: at audit time, `tools/browser-smoke.mjs`, `tools/terrain-seam-smoke.mjs`, and `tools/terrain-preset-smoke.mjs` imported Playwright and `pngjs`, but did not import `src/engine/world/terrainCoreWasm.ts` or the TypeScript terrain scheduler/mesh/density helpers. The two terrain-specific browser scripts have since been deleted.
- Observation: The substantial TypeScript terrain client surface exists primarily because `src/engine/world/terrainCoreWasm.test.ts` tests raw standalone `terrain_core.wasm` exports through TypeScript helper modules.
  Evidence: `src/engine/world/terrainCoreWasm.test.ts` imports `terrainCoreWasm.ts`, `terrainCoreChunkMesh.ts`, `terrainCoreDensityChunk.ts`, `terrainCoreDensityChunkStore.ts`, `terrainCoreStreamScheduler.ts`, and `terrainMesh.ts`.
- Observation: `crates/terrain_core/src/tests.rs` already covers many behaviors currently duplicated by `src/engine/world/terrainCoreWasm.test.ts`, including deterministic height/density behavior, density chunk fill, mesh generation, density-window reuse, stream scheduler buffers, density store reuse/pruning, and worker-pool facade behavior.
  Evidence: `cargo test --workspace` passed with 25 `terrain_core` tests including `height_sampling_is_deterministic`, `fills_density_chunk_buffer_in_terrain_chunk_order`, `builds_renderable_chunk_mesh_buffers`, `prepares_density_window_for_mesh_reuse`, and `stream_scheduler_facade_ticks_and_completes_jobs_through_buffers`.
- Observation: The app build and TypeScript test lane no longer need standalone TypeScript terrain client modules.
  Evidence: `npm run build` and `npm run test:ts` passed after deleting the terrain client files, and `rg --files dist dist-test | rg "(terrainCoreWasm|terrainCoreChunkMesh|terrainCoreDensityChunk|terrainCoreDensityChunkStore|terrainCoreStreamScheduler|terrainMesh|generated/terrain)"` returned no matches.
- Observation: After the Rust coverage port, the TypeScript terrain-client harness can be deleted without reducing the public TS test lane below browser shell and utility coverage.
  Evidence: `npm run test:ts` passed with 54 tests after deleting the `terrainCoreWasm` suite; the remaining `runtime import graph` tests assert the deleted terrain client files stay absent.
- Observation: The native Rust smoke harness caught a shader portability issue that browser smoke had not exposed. Native `wgpu`/Naga rejected the procedural sky vertex shader's runtime array index.
  Evidence: the first `npm run smoke:rust` failed in `Device::create_shader_module` because `positions[vertexIndex]` in `src/engine/render/shaders/uber.wgsl` was not constant-indexed. The shader now uses a `switch` for the full-screen triangle, and `npm run check:shaders` plus `npm run smoke:rust` pass.

## Decision Log

- Decision: TypeScript must not be a terrain client, including in tests.
  Rationale: Test-only TypeScript terrain clients preserve an architectural surface that the playable runtime has intentionally moved to Rust. Keeping that surface makes future AI work more likely to reintroduce TypeScript terrain ownership.
  Date/Author: 2026-06-07 / Codex, from user direction.
- Decision: Keep one narrow browser smoke family, not terrain-specific browser smoke families.
  Rationale: Browser tests should validate browser integration risks: wasm-bindgen loading, canvas/WebGPU initialization, headers, browser asset decoding, DOM input, resize/reload, and the debug contract. Terrain behavior belongs in Rust tests and Rust offscreen render smoke.
  Date/Author: 2026-06-07 / Codex.
- Decision: Rust smoke tests should write PNG artifacts and `report.json`, matching the browser smoke artifact style.
  Rationale: AI tools need a uniform way to read and show images from both browser and Rust tests. A report with absolute image paths, pixel stats, scenario metadata, and debug snapshots keeps verification observable.
  Date/Author: 2026-06-07 / Codex.
- Decision: Move TypeScript tests out of app source and add separate app/test TypeScript configs.
  Rationale: Anything under `src` currently looks like app/runtime code and is compiled by `tsconfig.json`. Separating test code makes architecture leaks easier to spot.
  Date/Author: 2026-06-07 / Codex.
- Decision: During Milestone 1, keep existing TypeScript tests in place but compile them only through `tsconfig.test.json` to `dist-test`.
  Rationale: This creates an immediate app/runtime quarantine while preserving legacy coverage until equivalent Rust tests are audited and the TypeScript terrain client tests can be safely removed.
  Date/Author: 2026-06-07 / Codex.
- Decision: Keep `assets/wasm/terrain_core.wasm` and `tools/build-terrain-wasm.mjs` for now, but stop generating TypeScript metadata for that artifact.
  Rationale: The standalone artifact is still used by the current benchmark script and useful for export-contract checks, but TypeScript source and tests no longer need or own terrain WASM metadata.
  Date/Author: 2026-06-07 / Codex.
- Decision: Put native image smoke in a new `crates/ofg_test_harness` workspace crate.
  Rationale: This keeps test-only native GPU setup out of the browser-facing `engine_web` crate while still reusing Rust terrain stream, shared renderer packet/uniform helpers, WGSL shader source, and terrain vertex layout constants.
  Date/Author: 2026-06-07 / Codex.
- Decision: Use synthetic 16-layer terrain texture arrays in the first Rust smoke harness.
  Rationale: The goal of this milestone is Rust-owned terrain-to-wgpu-to-PNG coverage without browser asset decoding. Synthetic arrays exercise material indices, weights, triplanar sampling, roughness sampling, and pixel variation while keeping browser-only image decoding out of Rust smoke.
  Date/Author: 2026-06-07 / Codex.

## Outcomes & Retrospective

Implementation is complete. The TypeScript app/test build has been split, the standalone TypeScript terrain client harness has been removed, and Rust `terrain_core` tests now cover the former TypeScript terrain WASM adapter behaviors.

The Rust offscreen image smoke path now exists and is validated on a native Vulkan adapter. It renders boot, all preset, x-seam, z-seam, and chunk-corner terrain scenarios using synthetic terrain texture arrays. The old browser terrain smoke scripts have been deleted, and browser smoke is now limited to browser integration.

The final command shape is:

    npm run test:rust
    npm run test:ts
    npm test
    npm run smoke:rust
    npm run smoke:terrain-seams
    npm run smoke:terrain-presets
    npm run smoke:browser
    npm run smoke

`npm run smoke` now proves both image paths: Rust offscreen terrain PNGs and browser integration screenshots. The remaining accepted risk is that Rust smoke uses synthetic terrain texture arrays; browser smoke still validates that browser assets load and decode, but the Rust smoke does not visually compare against Poly Haven texture contents.

## Contract and Quality Baseline

This plan preserves and sharpens these API contracts from `docs/API_CONTRACTS.md`:

`OFG-API-001: Browser Shell To Rust Browser Game` stays active. Browser TypeScript may load `RustBrowserGame`, call `resize`, `tick`, `command`, and `debugSnapshot`, and validate/copy packet values. It must not add terrain mesh, density, stream scheduler, renderer, or scalar terrain APIs.

`OFG-API-002: Rust Game To Browser Asset Loader` stays active. TypeScript may decode Rust-requested texture and byte assets. TypeScript must not parse terrain manifests, assign terrain material layers, or interpret model bytes.

`OFG-API-003: Debug And Smoke-Test Hooks` stays active but narrows in purpose. Browser debug hooks may expose Rust-owned snapshots and command affordances for integration testing. They must not compute terrain state. Browser smoke scripts should remain black-box tests of the browser boundary.

`OFG-API-004: Terrain Vertex And Material Layout` must move toward Rust as the single test owner. If a TypeScript terrain vertex stride constant remains temporarily, it is only a browser wrapper contract risk and must be removed or generated from Rust during this plan.

`OFG-API-005: Terrain Presets And World Descriptor Codes` must be reduced to the browser initialization handshake. Preset validation and terrain preset behavior should be tested in Rust. TypeScript may keep URL parsing only while the browser shell owns URL query parameters.

`OFG-API-006: Standalone Terrain WASM Artifact` has changed during this plan. The TypeScript adapters are removed; the standalone artifact remains only for benchmark and export-contract tooling while the Rust offscreen smoke harness is built. Runtime TypeScript and test TypeScript must not load `terrain_core.wasm`, and TypeScript must not call terrain WASM buffers or exports.

`OFG-API-009: Forbidden TypeScript Ownership` becomes stricter. TypeScript must not own or test terrain generation, density sampling, Dual Contouring, terrain stream scheduling, density stores, terrain worker protocols, terrain material classification, or mesh generation.

Quality requirements:

All files added or changed must keep comments useful and concise. Rust tests should have behavior-focused names. Browser smoke must continue to produce screenshots and pixel-stat assertions for the one browser integration path. Rust smoke must produce PNGs and pixel-stat assertions for terrain/render scenarios. After each milestone, run the repo-local `milestone-review` skill before marking that milestone complete.

## Context and Orientation

The current playable browser path starts at `index.html`, loads `/dist/main.js`, calls `src/main.ts`, then `src/app/game.ts`, then `src/engine/web/rustBrowserGameRuntime.ts`, then `src/engine/web/rustBrowserGameAdapter.ts`, then `src/engine/web/engineWebWasm.ts`. The browser runtime loads the wasm-bindgen `RustBrowserGame` class from `assets/wasm/engine_web/engine_web.js`. Rust code in `crates/engine_web` owns the game facade, terrain stream, terrain texture handling, renderer resources, WebGPU draw submission, and debug snapshots. `crates/terrain_core` owns terrain density, height sampling, chunk filling, stream scheduling, density stores, mesh generation, presets, and material classification.

The confusing TypeScript terrain surface is under `src/engine/world`. The key files are:

`src/engine/world/terrainDescriptor.ts`: browser-side world seed and preset descriptor. This is currently used by runtime URL parsing and by TypeScript tests.

`src/engine/world/terrainChunk.ts`: browser-side terrain chunk coordinate and key helpers. Runtime TypeScript only needs chunk keys as opaque debug strings; coordinate math is mostly test helper behavior.

`src/engine/world/terrainMesh.ts`: deleted by Milestone 2. Terrain mesh data shape and stride checks now live in Rust tests and renderer/shader contract tests.

`src/engine/world/terrainCoreWasm.ts`: deleted by Milestone 2. TypeScript no longer adapts raw standalone `terrain_core.wasm` exports or memory buffers.

`src/engine/world/terrainCoreDensityChunk.ts`: deleted by Milestone 2. Rust tests now call the Rust terrain facade directly.

`src/engine/world/terrainCoreChunkMesh.ts`: deleted by Milestone 2. Rust tests now call the Rust terrain facade directly.

`src/engine/world/terrainCoreDensityChunkStore.ts`: deleted by Milestone 2.

`src/engine/world/terrainCoreStreamScheduler.ts`: deleted by Milestone 2.

`src/engine/world/terrainCoreWasm.test.ts`: deleted by Milestone 2. Replacement coverage is in `crates/terrain_core/src/tests.rs`.

`src/generated/terrain/terrainCoreWasm.ts`: deleted by Milestone 2. `tools/build-terrain-wasm.mjs` now validates the standalone WASM export list without generating TypeScript.

The current smoke scripts are:

`tools/browser-smoke.mjs`: browser integration smoke. It checks browser headers/isolation, WebGPU canvas setup, wasm loading, Rust runtime sentinel strings, one camera keyboard toggle, reload, screenshots, and pixel stats.

`tools/terrain-seam-smoke.mjs`: deleted. `npm run smoke:terrain-seams` now calls the Rust offscreen harness with `--scenario seams`.

`tools/terrain-preset-smoke.mjs`: deleted. `npm run smoke:terrain-presets` now calls the Rust offscreen harness with `--scenario presets`.

The target is to keep only browser integration assertions in browser smoke and move seam/preset/render image coverage into Rust offscreen image smoke.

## Target Browser Test List

Browser tests should exist only for the browser boundary:

`browser boot smoke`: Load `index.html` from the local dev server, initialize `engine_web.wasm`, create a WebGPU canvas, load required browser assets, render one nonblank frame, save a PNG, and write a browser smoke report.

`browser input smoke`: Press `C` in the browser and assert the camera mode changes through the DOM/HUD and Rust debug snapshot. This proves DOM keyboard input reaches Rust through the TypeScript shell.

`browser shell smoke`: Verify canvas resize, page reload, required COOP/COEP response headers, wasm-bindgen module path, and asset request paths. This catches browser-only issues that native Rust cannot catch.

`browser debug contract smoke`: Verify `window.__ofgDebug` exists, reports Rust-owned system sentinel strings such as `"rust"` and `"rust-wgpu"`, and exposes only black-box debug observation/control needed for browser integration. It must not expose density buffers, mesh buffers, terrain generators, terrain stream schedulers, or raw terrain WASM access.

These browser tests should not validate terrain density, height determinism, terrain presets, seam ownership, material classification, mesh index validity, stream scheduling correctness, or renderer internal resource bookkeeping.

## Target Rust Test List

Rust tests should own the vast majority of coverage:

`terrain_core` unit and integration tests: height and density determinism for all presets, density chunk fill shape and values, chunk coordinate/key behavior if keys remain meaningful in Rust, material classification and material weights, Dual Contouring mesh generation, vertex stride, index bounds, normal/material finite values, same-LOD seam ownership, density store retain/reuse/eviction, stream scheduler desired sets, dependency coordinates, stale completion rejection, reset/invalidate behavior, and worker-pool request-state fixtures.

`engine_core` tests: first-person, third-person, and debug-fly camera/player behavior; movement and grounding; render snapshot extraction; scene resource/model item extraction where relevant.

`engine_web` Rust tests: browser game facade command handling, terrain stream ticking, mesh upload/prune decisions, renderer resource bookkeeping, terrain texture manifest parsing, debug snapshot field assembly, player character/model asset state, model animation/skinning state, and negative checks that raw terrain-client methods are not supported as public browser APIs.

`Rust offscreen render smoke`: native Rust scenarios that create a `wgpu` device without a browser canvas, tick the game/terrain systems, render to an offscreen texture, copy pixels to CPU memory, write PNG images, analyze pixel stats, and write `report.json`.

## Plan of Work

Milestone 1 audits current coverage and separates TypeScript app build from TypeScript test build. Add `tsconfig.app.json` for runtime app source and `tsconfig.test.json` for TypeScript tests. Update `package.json` scripts so `npm run build` uses the app config and `npm run test:ts` uses the test config. Move existing TypeScript tests into a top-level `tests/ts` tree or keep them temporarily while proving app builds exclude `*.test.ts`; the target is that app `dist` does not include TypeScript test files. Add a simple test that walks the runtime import graph from `src/main.ts` and fails if it reaches any standalone terrain WASM adapter file.

Milestone 2 ports standalone terrain WASM adapter coverage to Rust. For every assertion in `src/engine/world/terrainCoreWasm.test.ts`, add or identify equivalent Rust tests in `crates/terrain_core/src/tests.rs` or `crates/terrain_core/tests/*.rs`. Cover export-metadata checks by replacing them with Rust API tests or build-script checks where still needed. Cover density sampling, density chunk fill, mesh buffers, density store behavior, and stream scheduling in Rust. Run the Rust tests before removing TypeScript coverage.

Milestone 3 introduces a Rust offscreen render smoke harness. Add a binary or test-support crate. A reasonable first target is `crates/ofg_test_harness` as a new workspace member, or a `crates/engine_web/src/bin/ofg_render_smoke.rs` binary if sharing private renderer code is easier. The harness must create a native `wgpu::Instance`, request an adapter/device, create an offscreen `Texture` with `RENDER_ATTACHMENT | COPY_SRC`, initialize the same Rust game/terrain/render systems used by `engine_web`, tick until terrain is ready, render into the texture, copy the texture to a CPU buffer, encode PNGs with the Rust `image` crate, compute pixel stats, and write `artifacts/rust-smoke/<run-id>/report.json`.

Milestone 4 moves terrain seam and preset smoke scenarios from browser scripts to Rust offscreen smoke. The Rust harness recreates the old preset list and the old x-seam, z-seam, and chunk-corner seam intent. Each scenario saves a PNG and includes scenario metadata, seed, preset, camera pose, rendered chunk keys, mesh counts, and pixel stats in the report. Once Rust coverage exists and passes, remove or demote the old browser terrain scripts.

Milestone 5 narrows `tools/browser-smoke.mjs` to browser integration. Keep one browser smoke command that starts the dev server, opens Chrome/Edge through Playwright, loads the game, waits for a nonblank rendered frame, validates one camera/input toggle, verifies reload/resizing/header basics, checks `window.__ofgDebug` sentinel strings, saves one or a small number of screenshots, and writes `artifacts/browser-smoke/<run-id>/report.json`. Remove terrain preset/seam scenario loops from browser smoke.

Milestone 6 removes or quarantines any remaining TypeScript terrain clients. The original TypeScript terrain WASM adapter files were deleted during Milestone 2 after replacement Rust coverage landed. The remaining Milestone 6 work is to verify no TypeScript terrain clients remain anywhere in source, decide whether the standalone `terrain_core.wasm` benchmark path should stay or move to Rust, and remove any obsolete script/config/doc references that would invite TypeScript terrain clients back.

Milestone 7 updates docs and command contracts. Update `docs/API_CONTRACTS.md`, `docs/ARCHITECTURE.md`, `AGENTS.md`, and `README.md` so the active test commands and ownership rules match the new system. Remove `OFG-API-006` or change it to Archived/Removed. Update `OFG-API-003` to clarify that browser debug hooks exist only for browser integration. Update `OFG-API-004` and `OFG-API-005` if TypeScript constants or mappings are removed or generated.

## Concrete Steps

Run all commands from `C:\dev\ofg`.

Initial audit:

    git -c safe.directory=C:/dev/ofg status --short
    rg -n "terrainCore|generateTerrain|createTerrainCore|instantiateTerrainCore|terrainMesh" src tools docs package.json
    cargo test --workspace
    npm test

Expected result: current tests either pass or any pre-existing failure is recorded in Surprises & Discoveries before changes.

After TypeScript build split:

    npm run build
    npm run test:ts

Expected result: `npm run build` emits app/runtime files only, and `npm run test:ts` runs TypeScript tests from the explicit test config. No app build output should include `*.test.js` files.

After Rust coverage port:

    cargo test -p terrain_core
    cargo test --workspace

Expected result: Rust terrain tests cover every behavior formerly covered by `src/engine/world/terrainCoreWasm.test.ts`.

After Rust image smoke harness:

    npm run smoke:rust

Expected result: command prints a path like `Artifacts: C:\dev\ofg\artifacts\rust-smoke\<run-id>`, writes PNG files, writes `report.json`, and exits successfully after pixel-stat assertions pass.

After browser smoke narrowing:

    npm run smoke:browser

Expected result: command prints a path like `Artifacts: C:\dev\ofg\artifacts\browser-smoke\<run-id>`, writes one or a small number of PNG files, writes `report.json`, and exits successfully after browser integration assertions pass.

Final validation:

    npm run clean
    npm run build
    npm run test:rust
    npm run test:ts
    npm run smoke:rust
    npm run smoke:browser
    npm run smoke

Expected result: all commands pass. The combined `npm run smoke` runs Rust offscreen image smoke and browser integration smoke.

## Proposed Command Shape

Update `package.json` toward these scripts:

    "build:shaders": "node tools/build-shaders.mjs",
    "check:shaders": "node tools/build-shaders.mjs --check",
    "build:engine-web-wasm": "node tools/build-engine-web-wasm.mjs",
    "check:engine-web-wasm": "node tools/build-engine-web-wasm.mjs --check",
    "build": "npm run clean && npm run build:shaders && npm run build:engine-web-wasm && tsc -p tsconfig.app.json",
    "test:rust": "cargo test --workspace",
    "test:ts": "npm run build && tsc -p tsconfig.test.json && mocha \"dist-test/**/*.test.js\"",
    "test": "npm run test:rust && npm run test:ts",
    "smoke:rust": "cargo run -p ofg_test_harness --bin ofg-render-smoke -- --out artifacts/rust-smoke",
    "smoke:browser": "npm run build && node tools/browser-smoke.mjs",
    "smoke": "npm run smoke:rust && npm run smoke:browser"

The exact package name and binary path may change during implementation if `engine_web` is the better home for the harness. Record the final decision in the Decision Log.

## Rust Offscreen Image Smoke Design

The Rust image smoke harness should expose a command-line interface:

    cargo run -p ofg_test_harness --bin ofg-render-smoke -- --out artifacts/rust-smoke --scenario all

It should create a run directory using an ISO timestamp, for example:

    C:\dev\ofg\artifacts\rust-smoke\2026-06-07T12-00-00-000Z\

It should write:

    report.json
    boot-frame.png
    preset-rollingHills.png
    preset-mountainValley.png
    seam-x-grazing.png
    seam-z-grazing.png
    seam-corner-oblique.png

The report should be machine-readable and AI-friendly:

    {
      "kind": "rust-offscreen-render",
      "artifactDir": "C:/dev/ofg/artifacts/rust-smoke/...",
      "images": [
        {
          "name": "preset-rollingHills",
          "path": "C:/dev/ofg/artifacts/rust-smoke/.../preset-rollingHills.png",
          "width": 1280,
          "height": 720,
          "pixelStats": {
            "sampledPixels": 57600,
            "opaquePixels": 57600,
            "uniqueColorBuckets": 120,
            "dominantColorRatio": 0.12,
            "meanColor": { "r": 83.0, "g": 111.0, "b": 91.0 }
          },
          "debug": {
            "terrainPreset": "rollingHills",
            "terrainSeed": 246,
            "renderedChunkCount": 9
          }
        }
      ]
    }

The pixel-stat thresholds should mirror the current smoke intent: mostly opaque, enough color variation, and not a solid fill. Exact thresholds may differ between browser and native GPU backends; record selected values and evidence in the Decision Log.

The harness should avoid browser-only code. It should not use Playwright, DOM, wasm-bindgen JS glue, `window`, `HtmlCanvasElement`, or browser asset decoding. If the current renderer is too tied to `web-sys::HtmlCanvasElement`, first extract a Rust renderer creation path that accepts a `wgpu::Device`, `wgpu::Queue`, texture format, width, and height. That extraction is part of Milestone 3.

## Milestone Review

After each milestone:

1. Update any changed API contracts or active docs.
2. Run the repo-local `milestone-review` skill against the milestone diff and this ExecPlan.
3. Apply required findings before marking the milestone complete, or record a rejected finding with rationale in the Decision Log.
4. Re-run relevant validation commands.
5. Record the review summary, commands, artifacts, and remaining risks in Progress or Outcomes & Retrospective.

Do not mark a milestone complete until the review is done and required findings are addressed.

## Validation and Acceptance

The migration is accepted when all of these are true:

TypeScript no longer imports, defines, or tests standalone terrain clients for density sampling, chunk filling, mesh generation, stream scheduling, density stores, or raw `terrain_core.wasm` buffers.

The app/runtime TypeScript build excludes TypeScript tests and test helpers.

The only TypeScript terrain-adjacent runtime code is browser shell configuration and opaque debug typing, such as URL seed/preset parsing and `TerrainChunkKey` as a string if still needed.

Every behavior formerly covered by `src/engine/world/terrainCoreWasm.test.ts` has equivalent Rust coverage in `crates/terrain_core`, `crates/engine_core`, `crates/engine_web`, or a Rust test harness.

`npm run smoke:rust` writes PNG images and a JSON report under `artifacts/rust-smoke/`, and the images are suitable for AI inspection by absolute path.

`npm run smoke:browser` writes PNG screenshots and a JSON report under `artifacts/browser-smoke/`, and validates only browser integration behavior.

`npm run smoke` runs both Rust and browser smoke tests.

`npm run test` runs the Rust tests and remaining TypeScript tests.

`docs/API_CONTRACTS.md`, `docs/ARCHITECTURE.md`, `README.md`, and `AGENTS.md` describe the new test ownership accurately.

Final commands pass:

    npm run build
    npm run test
    npm run smoke

## Idempotence and Recovery

The migration should be incremental. Do not delete TypeScript terrain tests until replacement Rust tests pass. If a Rust test port fails, keep the old TypeScript test temporarily, label it legacy, and record the blocker in Surprises & Discoveries.

Artifact directories under `artifacts/` are generated output and may be deleted and regenerated. Do not commit generated `artifacts/`, `dist/`, `dist-test/`, `node_modules/`, or smoke PNG output.

If native `wgpu` offscreen rendering is unavailable on a CI or developer machine, the Rust smoke harness should print adapter/device diagnostics and fail with a clear message. Browser smoke remains the fallback for browser-specific WebGPU failures, but not for terrain correctness.

If offscreen native rendering produces visual differences from browser rendering, compare debug snapshots and pixel stats first. Differences that come from backend-specific shading or precision should be recorded in the Decision Log with adjusted thresholds only after review.

## Artifacts and Notes

Expected final artifact structure:

    artifacts/
      rust-smoke/
        <run-id>/
          report.json
          *.png
      browser-smoke/
        <run-id>/
          report.json
          *.png

AI tools can inspect either report, open the absolute PNG paths, and show them in chat. The report schema should stay stable enough that future agents can locate images without reading command output.

Milestone 1 validation transcript summary:

    cargo test --workspace
    Result: passed. Rust workspace reported 37 engine_core tests, 51 engine_web tests, and 25 terrain_core tests passing.

    npm test
    Result before migration: passed. Existing TypeScript lane reported 60 passing tests.

    npm run build
    Result after migration: passed. App build uses `tsconfig.app.json`.

    npm run test:rust
    Result after migration: passed.

    npm run test:ts
    Result after migration: passed. TypeScript test lane reported 61 passing tests, including `runtime import graph`.

    npm test
    Result after migration: passed. The public test command now runs Rust workspace tests and the separated TypeScript test lane.

    rg --files dist | rg "(\.test\.js|terrainCoreWasm|terrainCoreChunkMesh|terrainCoreDensityChunk|terrainCoreDensityChunkStore|terrainCoreStreamScheduler|terrainMesh|generated/terrain)"
    Result after migration: no matches. App output contains no test files and no standalone TypeScript terrain client modules.

Milestone 1 review:

    Scope: TypeScript app/test build split, package script update, clean script update, runtime import-graph guard, and ExecPlan living-section updates.
    Reviewers: contract, code quality, legacy, correctness, and validation passes were done locally. Sub-agent tooling was available but not used because its contract requires explicit user authorization for delegated/sub-agent work.
    Required findings fixed: added a top-of-file purpose comment to `tests/ts/runtimeImportGraph.test.ts` and removed an unused helper from that file.
    Follow-ups recorded: legacy TypeScript terrain client tests still exist in `dist-test`; removing or quarantining them remains Milestones 2 and 6. Active docs and command lists still describe the old smoke/test split; updating them remains Milestone 7.
    Rejected findings: none.
    Validation rerun: `npm run test:ts`, `npm test`, scoped `git diff --check`, and the `dist` output grep.
    Remaining risk: `npm run build` still builds the standalone `terrain_core.wasm` artifact because the legacy TypeScript tests still depend on it until the Rust coverage port is complete.

Milestone 2 validation transcript summary:

    cargo test -p terrain_core
    Result: passed. `terrain_core` now reports 27 tests, including `height_and_density_samples_are_finite_for_every_preset`, `fills_density_chunks_deterministically_with_finite_samples`, and the strengthened mesh/material weight checks.

    npm run build
    Result: passed after deleting the TypeScript terrain client files and generated terrain metadata.

    npm run check:wasm
    Result: passed. `tools/build-terrain-wasm.mjs` validates standalone terrain WASM exports directly and no longer writes TypeScript metadata.

    npm run test:ts
    Result: passed with 54 tests. The old `terrain core WASM` TypeScript suite is gone; `runtime import graph` now verifies the deleted client files stay absent.

    npm test
    Result: passed. The public command runs Rust workspace tests and the separated TypeScript test lane.

    rg -n "TERRAIN_CORE_WASM_METADATA|terrainCoreWasm|terrainCoreChunkMesh|terrainCoreDensityChunk|terrainCoreDensityChunkStore|terrainCoreStreamScheduler|terrainMesh" src tests tools package.json tsconfig.app.json tsconfig.test.json
    Result: only the forbidden-file list inside `tests/ts/runtimeImportGraph.test.ts` remains.

    rg --files dist dist-test | rg "(terrainCoreWasm|terrainCoreChunkMesh|terrainCoreDensityChunk|terrainCoreDensityChunkStore|terrainCoreStreamScheduler|terrainMesh|generated/terrain)"
    Result: no matches.

Milestone 2 review:

    Scope: Rust terrain coverage replacing the standalone TypeScript `terrain_core.wasm` adapter suite, deletion of TypeScript terrain client files, direct WASM export validation in `tools/build-terrain-wasm.mjs`, active docs updates, and runtime import-graph enforcement.
    Reviewers: contract, code quality, legacy, correctness, and validation passes were done locally. Sub-agent tooling was available but not used because its contract requires explicit user authorization for delegated/sub-agent work.
    Required findings fixed: strengthened `tests/ts/runtimeImportGraph.test.ts` so runtime reachability checks include static imports, runtime re-exports, and dynamic imports, and so source-root checks use path-relative containment rather than a string-prefix check.
    Follow-ups recorded: the standalone `terrain_core.wasm` benchmark/export-contract artifact still exists while the Rust smoke harness is built; the later API coverage plan moves the benchmark path to Rust.
    Rejected findings: none.
    Validation rerun: `npm run test:ts`, scoped source/output greps for deleted TypeScript terrain clients, and scoped `git diff --check`.
    Remaining risk: resolved by Milestones 4-5; browser screenshot smoke no longer carries terrain seam/preset coverage.

Milestone 3 validation transcript summary:

    npm run smoke:rust
    Result: passed. The command rendered six PNGs and wrote `artifacts/rust-smoke/run-1780824999-209/report.json`.
    Images: `boot-frame.png`, `preset-seed.png`, `preset-rollingHills.png`, `preset-mountainValley.png`, `preset-rockyHighland.png`, and `seam-corner-oblique.png`.
    Renderer: native `wgpu` on Vulkan / NVIDIA GeForce RTX 3050 Ti Laptop GPU.
    Report shape: `kind: "rust-offscreen-render"`, absolute `artifactDir`, `renderer`, and `images[]` entries with absolute PNG paths, dimensions, pixel stats, terrain seed/preset, camera pose, rendered chunk keys, and mesh counts.

    cargo test --workspace
    Result: passed after adding `crates/ofg_test_harness` as a workspace member. The workspace now includes zero-test lib/bin checks for the harness plus existing `engine_core`, `engine_web`, and `terrain_core` tests.

    npm run check:shaders
    Result: passed after changing `skyVertexMain` from runtime array indexing to a `switch` and regenerating `src/generated/render/uberShader.ts`.

    Visual inspection
    Result: inspected `C:/dev/ofg/artifacts/rust-smoke/run-1780824999-209/boot-frame.png`. It shows a nonblank terrain render with procedural sky; pixel stats reported 31 color buckets and a 0.3827 dominant bucket ratio.

Milestone 3 review:

    Scope: new native Rust image smoke harness crate, `smoke:rust` and combined `smoke` package scripts, WGSL sky shader portability fix, generated shader artifact update, and ExecPlan updates.
    Reviewers: contract, code quality, legacy, correctness, and validation passes were done locally. Sub-agent tooling was available but not used because its contract requires explicit user authorization for delegated/sub-agent work.
    Required findings fixed: split the initial 1330-line harness binary into modules so the largest new file is 773 lines, and added explicit safety comments for the GPU upload byte-cast helpers in `renderer.rs`.
    Follow-ups recorded: Rust smoke initially used synthetic terrain texture arrays and one seam/corner scenario; Milestone 4 expanded it to the full old terrain seam/preset smoke intent and deleted browser terrain smoke scripts.
    Rejected findings: none.
    Validation rerun: `npm run smoke:rust`, `cargo test --workspace`, `npm run check:shaders`, `cargo check -p ofg_test_harness`, scoped legacy/browser grep for the harness, file-size scan, and scoped `git diff --check`.
    Remaining risk: resolved by Milestone 5; browser smoke now contains browser integration assertions only.

Milestone 4/5 validation transcript summary:

    npm run smoke:terrain-seams
    Result: passed. The command now runs `ofg-render-smoke --scenario seams` and wrote x-seam, z-seam, and chunk-corner PNGs under `artifacts/rust-smoke/run-1780825485-040/`.

    npm run smoke:terrain-presets
    Result: passed. The command now runs `ofg-render-smoke --scenario presets` and wrote `seed`, `rollingHills`, `mountainValley`, and `rockyHighland` PNGs under `artifacts/rust-smoke/run-1780825351-010/`.

    npm run smoke:browser
    Result: passed after narrowing browser smoke. The command wrote three integration screenshots under `artifacts/browser-smoke/2026-06-07T09-46-26-064Z/`: first-person boot, camera-toggle, and reloaded frame.

    npm run smoke
    Result: passed. The combined command first ran Rust offscreen image smoke and wrote eight PNGs under `artifacts/rust-smoke/run-1780825923-072/`, then ran browser integration smoke and wrote three screenshots under `artifacts/browser-smoke/2026-06-07T09-52-39-122Z/`.

Milestone 4/5 review:

    Scope: Rust seam/preset scenario migration, removal of browser terrain smoke scripts, package command rerouting, narrowed browser smoke, active docs updates, and ExecPlan updates.
    Reviewers: contract, code quality, legacy, correctness, and validation passes were done locally. Sub-agent tooling was available but not used because its contract requires explicit user authorization for delegated/sub-agent work.
    Required findings fixed: expanded Rust seam smoke from one corner image to the old x-seam, z-seam, and chunk-corner intent, and added Rust-side chunk coverage assertions.
    Follow-ups recorded: none for this milestone.
    Rejected findings: browser smoke still imports Playwright and uses DOM/window APIs by design because it is the browser integration boundary; the Rust harness grep confirmed no browser or TypeScript terrain-client references in `crates/ofg_test_harness`.
    Validation rerun: `node --check tools/browser-smoke.mjs`, `npm run smoke:terrain-seams`, `npm run smoke:terrain-presets`, `npm run smoke:browser`, `npm run smoke`, scoped legacy/browser greps, file-size scan, and scoped `git diff --check`.
    Remaining risk: synthetic terrain texture arrays in Rust smoke are sufficient for material-index/weight shader coverage, but they do not compare against the browser-decoded Poly Haven texture assets. Browser boot smoke still exercises browser asset fetch/decode at integration level.

Final validation transcript summary:

    npm run clean
    Result: passed. Removed `dist` and `dist-test`.

    npm run build
    Result: passed. Shader artifacts, standalone terrain WASM export checks, engine-web WASM artifacts, and app TypeScript build completed.

    npm run test:rust
    Result: passed. Rust workspace tests passed for `engine_core`, `engine_web`, `terrain_core`, and `ofg_test_harness`.

    npm run test:ts
    Result: passed with 54 TypeScript tests. The runtime import graph guard still asserts the deleted TypeScript terrain client files stay absent.

    npm run check:shaders
    Result: passed.

    npm run check:wasm
    Result: passed.

    npm test
    Result: passed. Public test command ran Rust workspace tests and the separated TypeScript lane.

    npm run smoke
    Result: passed. Rust smoke wrote eight PNGs under `artifacts/rust-smoke/run-1780825923-072/`, then browser smoke wrote three screenshots under `artifacts/browser-smoke/2026-06-07T09-52-39-122Z/`.

    rg --files dist dist-test | rg "(terrainCoreWasm|terrainCoreChunkMesh|terrainCoreDensityChunk|terrainCoreDensityChunkStore|terrainCoreStreamScheduler|terrainMesh|generated/terrain)"
    Result: no matches.

    Test-Path tools/terrain-seam-smoke.mjs; Test-Path tools/terrain-preset-smoke.mjs
    Result: both `False`.

Final Milestone 6/7 review:

    Scope: no remaining TypeScript terrain clients, standalone terrain WASM benchmark/export-contract decision, final command/docs/contracts, generated artifacts, and final validation evidence.
    Reviewers: contract, code quality, legacy, correctness, and validation passes were done locally. Sub-agent tooling was available but not used because its contract requires explicit user authorization for delegated/sub-agent work.
    Required findings fixed: none.
    Follow-ups recorded: none.
    Rejected findings: `terrainCoreWasm` appears only in the TypeScript runtime import-graph guard and browser debug-name denylist, which are intentional protections against reintroduction rather than terrain clients.
    Validation rerun: final command set above, source/output terrain-client greps, deleted-script checks, file-size scan, and `git diff --check`.
    Remaining risk: the standalone `assets/wasm/terrain_core.wasm` artifact still exists for export-contract tooling, by explicit decision. It is not loaded by runtime TypeScript, TypeScript tests, or benchmarks.

## Interfaces and Dependencies

The final system should include these stable interfaces:

`npm run test:rust`: runs `cargo test --workspace`.

`npm run test:ts`: builds and runs TypeScript tests using `tsconfig.test.json`.

`npm run smoke:rust`: runs the Rust offscreen image smoke harness and writes PNG/report artifacts.

`npm run smoke:browser`: runs the browser integration smoke through Playwright and writes PNG/report artifacts.

`npm run smoke`: runs Rust image smoke followed by browser integration smoke.

`tsconfig.app.json`: TypeScript app build config that excludes tests and test support.

`tsconfig.test.json`: TypeScript test build config that includes tests and test support.

Rust smoke harness interface:

    ofg-render-smoke --out <artifact-root> [--scenario all|boot|presets|seams]

Report schema:

    kind: "rust-offscreen-render" | "browser-integration-smoke"
    artifactDir: absolute path string
    images: array of { name, path, width, height, pixelStats, debug? }

Any deviation from these names or paths must be recorded in the Decision Log and updated in this section.
