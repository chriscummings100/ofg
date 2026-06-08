# Browser Render Performance Debugging

Archived note: This diagnostic ExecPlan completed on 2026-06-08. The active
follow-up plan for the measured shadow bottleneck is
`docs/SHADOW_CULLING_OPTIMIZATION_PLAN.md`.

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

This document follows `PLANS.md` in this repository. It is the active source of truth for adding performance diagnostic tooling to OFG before making render or terrain performance fixes.

## Purpose / Big Picture

OFG has become slow after terrain changes. The goal of this plan is not to optimize anything yet. The goal is to make the cost of each important frame phase observable, repeatable, and hard to misread.

After this work, a developer can run the browser app, wait for terrain to settle, call `window.__ofgDebug.dumpPerfStats()` in the console, and see latest, minimum, maximum, average, and percentile-style summaries for browser frame-loop CPU time, Rust game/update CPU time, Rust/wgpu render CPU time, optional GPU pass time, draw calls, rendered object counts, terrain LOD draw counts, shadow cascade draw counts, submitted vertices, submitted indices, and terrain upload cost. The same information can be captured by a deterministic script that writes JSON under `artifacts/perf-debug/`.

The finished tooling must also allow controlled experiments by toggling specific render features: terrain LOD visibility, sky rendering, shadow pass rendering, individual shadow cascades, shadow sampling in the main shader, white texture binding, and a basic Lambert material mode. These toggles are diagnostic controls only. They must not mutate terrain scheduling policy, terrain generation, or production render defaults.

The plan is complete when the tools can produce a short factual diagnosis report. Any actual performance fix, such as changing draw distance, terrain LOD density, cascade culling, shader cost, material complexity, or mesh generation, must be proposed as follow-up work tied to measured evidence from this plan.

## Progress

- [x] (2026-06-08 06:29Z) Created this ExecPlan from the performance debugging proposal.
- [x] (2026-06-08) Milestone 1: Defined the perf data model, debug command contract, and API contract documentation updates.
- [x] (2026-06-08) Milestone 2: Added browser and Rust CPU frame timers with fixed-history summaries.
- [x] (2026-06-08) Milestone 3: Added render counters for candidates, visible/cull counts, draw counts, terrain LODs, shadow cascades, vertices, indices, and triangles.
- [x] (2026-06-08) Milestone 4: Added optional WebGPU timestamp timing with an unavailable fallback path.
- [x] (2026-06-08) Milestone 5: Added render debug options for terrain LOD masks, sky, shadow passes, shadow cascades, shadow sampling, white textures, and Lambert material mode.
- [x] (2026-06-08) Milestone 6: Added DevTools perf dump, deterministic browser capture script, tests, and smoke coverage.
- [x] (2026-06-08) Milestone 7: Captured baseline data, recorded conclusions, and stopped before optimization work.

## Surprises & Discoveries

- Observation: The current Rust/wgpu renderer already reports latest-frame `frameDrawCount`, `frameVisibleDrawCount`, `frameShadowDrawCount`, and terrain upload statistics through `debugSnapshot().rendererStatus`.
  Evidence: `crates/engine_web/src/wgpu_renderer.rs` builds `RustBrowserGameStatus` and serializes these fields in `renderer_status_to_js`; `src/engine/web/engineWebWasm.ts` types them as `EngineWebRendererStatus`.

- Observation: Main camera frustum culling appears to exist for the scene pass, but shadow rendering currently appears to draw all prepared render items into every cascade.
  Evidence: In `crates/engine_web/src/wgpu_renderer.rs`, the scene pass checks `frustum_intersects_aabb(camera_frustum, item.world_bounds)` before drawing. The shadow pass loop in `render_shadow_passes` iterates all `render_items` for each cascade before drawing. This is a hypothesis to verify with counters before making any culling change.

- Observation: Browser WebGPU timestamp query availability may vary by adapter, browser, and feature configuration.
  Evidence: The current renderer requests `wgpu::Features::empty()` in `BrowserWgpuRenderer::new`. GPU timing must be feature-gated and report unavailable cleanly rather than failing boot.

- Observation: GPU timestamp queries were available on the validation machine.
  Evidence: `artifacts/perf-debug/2026-06-08T07-32-04-418Z/summary.json` reports `gpuTimerStatus.available: true`, `timestampPeriodNs: 1`, and six pending readbacks during the rockyHighland capture.

- Observation: Main-camera culling is working for the scene pass in the captured frame, while shadow cascades are not culled.
  Evidence: The baseline capture reports `frameDrawCount: 336`, `frameVisibleDrawCount: 78`, and `frameCulledDrawCount: 258`. Each of the four shadow cascades reports `candidateCount: 336`, `visibleCount: 336`, and `culledCount: 0`.

- Observation: Shadow-map rendering is the dominant measured GPU cost in the captured baseline.
  Evidence: Baseline measured GPU time averaged 22.495 ms. Disabling shadow-map passes averaged 7.408 ms, a 15.087 ms reduction, and reduced shadow draws from 1344 to 0. Rendering only one shadow cascade averaged 11.144 to 12.357 ms, a roughly 10 to 11 ms reduction.

- Observation: Main-pass material/texture cost is real but smaller than shadow-map generation in the captured baseline.
  Evidence: Shadow sampling off reduced measured GPU time by 0.410 ms, diagnostic white textures by 3.144 ms, and Lambert mode by 1.859 ms compared with the same 22.495 ms baseline.

- Observation: Terrain vertex volume remains high enough to matter, especially because shadow passes multiply it.
  Evidence: Baseline visible scene submission averaged 78 draws and 550,510 submitted vertices/550,512 indices. Baseline shadow counters report 336 draws and 1,926,694 vertices per cascade, for 1344 shadow draws across four cascades.

## Decision Log

- Decision: Keep this plan diagnostic-only and explicitly defer performance fixes.
  Rationale: The user requested rock solid conclusions before optimization. Fixes made before measurement risk hiding the real cost.
  Date/Author: 2026-06-08 / Codex

- Decision: Rust owns Rust game, terrain, renderer, and GPU-pass metrics; TypeScript owns only browser shell frame-loop timings and console/UI aggregation.
  Rationale: This preserves the current architecture. TypeScript may measure its own browser work, but it must not compute terrain scheduling, render visibility, shader state, or renderer semantics.
  Date/Author: 2026-06-08 / Codex

- Decision: Add debug controls through `GameCommand` and report new state through `debugSnapshot()` / `rendererStatus` instead of adding raw wasm methods.
  Rationale: `docs/API_CONTRACTS.md` says new debug control belongs in `GameCommand`, and new HUD/smoke state belongs in `debugSnapshot()`.
  Date/Author: 2026-06-08 / Codex

- Decision: GPU timers are optional and must degrade to CPU-only metrics when unavailable.
  Rationale: Timestamp queries are valuable, but browser support is not guaranteed. The diagnostic tool must still work on machines without query support.
  Date/Author: 2026-06-08 / Codex

- Decision: Render debug terrain LOD controls filter only the render packet/draw submission.
  Rationale: This preserves Rust terrain stream scheduling, worker requests, mesh generation, and cache state while still allowing isolated GPU/render experiments.
  Date/Author: 2026-06-08 / Codex

- Decision: Move `perf.rs` unit tests to `perf_tests.rs`.
  Rationale: Local milestone review flagged the new implementation file crossing 1000 lines. Splitting tests kept `perf.rs` focused at 771 lines while preserving coverage.
  Date/Author: 2026-06-08 / Codex

- Decision: Do local milestone-review passes instead of spawning sub-agents.
  Rationale: The milestone-review skill asks for sub-agents when available, but the sub-agent tool contract in this session permits spawning only when the user explicitly asks for delegation/sub-agents. Contract, code-quality, legacy, correctness, and validation reviews were done locally.
  Date/Author: 2026-06-08 / Codex

## Outcomes & Retrospective

Delivered tooling:

- Rust-owned perf data model and frame history in `crates/engine_web/src/perf.rs`, with unit tests in `crates/engine_web/src/perf_tests.rs`.
- Browser and Rust CPU frame timings, render counters, terrain LOD counters, shadow cascade counters, submitted vertex/index/triangle counters, and optional GPU timestamp pass timings surfaced through `debugSnapshot().rustPerfStats`, `rendererStatus`, and `window.__ofgDebug.getPerfStats()`.
- Render debug commands and reset path: `setRenderDebugOptions`, `resetRenderDebugOptions`, and `resetPerfStats`.
- DevTools dump path: `window.__ofgDebug.dumpPerfStats()`.
- Deterministic capture path: `node tools/browser-perf-debug-capture.mjs`, also aliased by `npm run perf:debug:capture`.
- Browser smoke coverage for perf hooks and render debug option set/reset.
- API contract updates in `docs/API_CONTRACTS.md`.

First captured diagnosis:

- Capture artifact: `artifacts/perf-debug/2026-06-08T07-32-04-418Z/summary.json`.
- Scenario: Chrome headless, `terrainSeed=24681357`, `terrainPreset=rockyHighland`, 120 sampled frames per experiment.
- Baseline: browser CPU averaged 11.298 ms, Rust CPU averaged 8.042 ms, measured GPU averaged 22.495 ms. GPU timing was available, so the current frame is GPU-bound on this machine.
- Main camera culling: 336 candidates became 78 visible scene draws, with 258 culled. This is working and observable.
- Shadow cascades: all four cascades draw all 336 candidates with zero cascade culling, producing 1344 shadow draws. This is now fact, not a hunch.
- Shadow-map generation: disabling shadow-map passes reduced measured GPU time from 22.495 ms to 7.408 ms, a 15.087 ms reduction. Rendering only one cascade kept 336 shadow draws and reduced GPU time to roughly 11 to 12 ms.
- Shadow sampling: disabling main-pass shadow sampling reduced GPU time by only 0.410 ms, so sampling is not the primary shadow cost in this capture.
- Material/texture diagnostics: white textures reduced GPU time by 3.144 ms and Lambert mode by 1.859 ms. Material cost exists, but it is smaller than shadow-map generation here.
- Terrain LOD isolation: LOD-only captures reduced measured GPU time to 7.109 ms for LOD0, 10.683 ms for LOD1, 10.937 ms for LOD2, and 10.042 ms for LOD3+. These results reinforce that terrain geometry volume matters, but shadow multiplication is the first-order measured issue.

Recommended follow-up plan:

1. Create a focused optimization ExecPlan for shadow cascade culling and shadow draw reduction. Acceptance should require counters showing cascade `culledCount > 0`, fewer shadow draws/vertices, and a repeat capture showing lower measured GPU time.
2. After shadow culling is measured, use the same tools to decide whether far terrain LOD density/cell-size changes are justified. The current evidence supports investigating it, but not before the shadow multiplier is addressed.
3. Before adding more renderer diagnostics or features, split `wgpu_renderer.rs` further, especially GPU timer helpers and JS serialization/debug-contract helpers. `tools/browser-smoke.mjs` also crossed 1000 lines and should shed reusable debug-contract helpers before further smoke expansion.

Milestone review:

- Scope: all seven diagnostic milestones implemented together as one measured slice.
- Reviewers: local contract, code quality, legacy, correctness, and validation passes. Sub-agent spawning was not used because explicit user permission for delegation was absent.
- Required findings fixed: moved `perf.rs` tests into `perf_tests.rs` after line-count review; reran focused tests, `npm test`, and coverage.
- Follow-ups recorded: split `wgpu_renderer.rs` and `tools/browser-smoke.mjs` before further growth; create a measured shadow cascade culling optimization plan.
- Rejected findings: none.
- Remaining risk: GPU timestamp availability is browser/adapter dependent, but unavailable mode is represented and tested by fallback status.

Validation:

- `cargo check -p engine_web --target wasm32-unknown-unknown` passed.
- `cargo test -p engine_web` passed.
- `npm run check:shaders` passed.
- `npm run build:engine-web-wasm` passed and regenerated `assets/wasm/engine_web/*` plus `src/generated/web/engineWebWasm.ts`.
- `npm run test:ts` passed.
- `npm run smoke:browser` passed; report artifact `artifacts/browser-smoke/2026-06-08T07-25-32-792Z/report.json`.
- `node tools/browser-perf-debug-capture.mjs` passed; capture artifact `artifacts/perf-debug/2026-06-08T07-32-04-418Z/summary.json`.
- `npm test` passed after the `perf_tests.rs` split.
- `npm run coverage:rust` passed with no files below the default filtered 90% line-coverage threshold.
- `git -c safe.directory=C:/dev/ofg diff --check` passed with only line-ending conversion warnings.

## Contract and Quality Baseline

This plan preserves `OFG-API-001: Browser Shell To Rust Browser Game` from `docs/API_CONTRACTS.md`. New controls must be added to the TypeScript `GameCommand` schema in `src/engine/web/browserGameTypes.ts`, routed through `RustBrowserGameRuntime.command(...)`, and parsed by `RustBrowserGame.command(...)` in `crates/engine_web/src/wgpu_renderer.rs`. Do not add scalar wasm-bindgen frame methods or raw public renderer methods.

This plan intentionally extends `OFG-API-003: Debug And Smoke-Test Hooks`. New debug fields must be Rust-assembled for Rust-owned data, exposed through `debugSnapshot()` and typed in TypeScript. Browser shell timing may be exposed by `window.__ofgDebug` because TypeScript owns the browser frame loop, but it must remain diagnostic. Browser code must not compute terrain desired sets, culling policy, render packets, material interpretation, sky state, shadow state, or LOD selection.

This plan preserves `OFG-API-004: Terrain Vertex And Material Layout`. Counting vertices and indices must use the existing Rust vertex stride constants and GPU mesh metadata. Do not change terrain vertex layout, material packing, shader inputs, or generated shader contracts except for explicit debug-mode uniforms needed by the render debug options.

This plan preserves `OFG-API-009: Forbidden TypeScript Ownership`. Do not recreate a TypeScript terrain manager, renderer, scene graph, terrain visibility selector, or WebGPU owner. TypeScript may display and dump Rust-provided debug data only.

Quality gates:

- Add focused Rust tests for the perf ring buffer, summary calculations, render debug option parsing, and counter aggregation.
- Add TypeScript tests for debug hook validation, command routing, and console dump shape.
- Update `docs/API_CONTRACTS.md` when the implemented debug commands and debug snapshot fields are known.
- After each milestone, run the repo-local `milestone-review` skill before marking that milestone complete. Apply required findings or record a rejected finding with rationale in this plan's Decision Log.
- For implementation work, run `npm run coverage:rust` before completion and confirm changed implementation files do not appear in the default filtered coverage output, or record a specific exception with rationale.

## Context and Orientation

The browser app starts in `src/main.ts` and runs the frame loop in `src/app/game.ts`. That loop collects DOM input, builds a `BrowserFrameInput`, calls `game.tick(frameInput)`, refreshes the latest Rust debug snapshot, updates HUD text, and schedules the next `requestAnimationFrame`.

The TypeScript browser game runtime lives in `src/engine/web/rustBrowserGameRuntime.ts`, `src/engine/web/rustBrowserGameAdapter.ts`, `src/engine/web/engineWebWasm.ts`, and `src/engine/web/browserGameTypes.ts`. These files type the browser-facing command and debug snapshot contracts. TypeScript is allowed to validate command names, forward commands, display debug data, and measure TypeScript browser shell work.

The Rust browser game facade and renderer live mostly in `crates/engine_web/src/wgpu_renderer.rs`. `RustBrowserGame::tick` advances Rust-owned game and terrain state, updates terrain mesh uploads, and calls `render_frame`. `BrowserWgpuRenderer::render` prepares GPU objects, performs main-camera culling for the scene pass, renders shadow maps, renders sky and scene geometry into post-process targets, runs post processing, submits the command buffer, and presents the frame.

The current shadow resources and pipelines live in `crates/engine_web/src/shadow_renderer.rs`; shadow cascade math lives in `crates/engine_web/src/shadows.rs`; camera, frustum, AABB, and matrix helpers live in `crates/engine_web/src/render_math.rs`; uniform packing lives in `crates/engine_web/src/render_uniforms.rs`; post-process resources live in `crates/engine_web/src/post_process.rs`.

The current browser movement performance smoke lives in `tools/browser-smoke-movement-performance.mjs`. It samples frame deltas, terrain worker counters, renderer resource counts, draw counts, and terrain upload counters, but it does not provide pass-level CPU/GPU timings, frame history, debug render toggles, or a general dump command.

Definitions used in this plan:

- Browser shell CPU time: time spent in TypeScript frame-loop work outside Rust, such as input snapshot consumption, frame input construction, calling `game.tick`, reading debug snapshots, and HUD updates.
- Rust update CPU time: time spent in Rust-owned player/camera, animation, terrain streaming, mesh upload/prune, render packet construction, and render command encoding.
- GPU pass time: elapsed GPU execution time for a render pass or group of passes, measured by WebGPU timestamp queries when available.
- Candidate item: a render item prepared for a pass before pass-specific culling.
- Visible item: a render item that passes culling for a pass and is submitted for drawing.
- Draw call: one `draw` or `draw_indexed` command submitted to a render pass.
- Submitted vertices and indices: counts associated with meshes actually drawn in a pass, not merely uploaded or resident.
- CSM: cascaded shadow maps. OFG currently uses four shadow cascades, configured by `SHADOW_CASCADE_COUNT`.

## Plan of Work

Milestone 1 defines the contracts and data model. Add a new Rust module, likely `crates/engine_web/src/perf.rs`, with small, tested types for frame samples, summaries, pass counters, LOD counters, shadow cascade counters, GPU timer availability, and render debug options. Add or update TypeScript types in `src/engine/web/browserGameTypes.ts` and `src/engine/web/engineWebWasm.ts` for the new command and debug status shapes. Update `docs/API_CONTRACTS.md` to describe the new debug command and snapshot fields once the exact names are implemented.

The preferred data shape is a fixed-size ring buffer, initially 300 or 600 frames. It should store recent frame samples and produce summaries with at least latest, min, max, average, and p95 for numeric timing fields. It should avoid heap churn during normal frames where practical. Tests should cover empty buffers, single-sample buffers, wraparound, non-finite value rejection, and summary math.

Milestone 2 adds CPU timing. In TypeScript, add a small browser frame-loop perf tracker, likely `src/app/perfDebug.ts`, that measures browser shell spans using `performance.now()`. At minimum, measure total frame callback time, input/frame-input construction, `game.tick`, `game.debugSnapshot`, and HUD/debug UI update cost. In Rust, add timing spans around player/game update, terrain stream update, terrain mesh upload/prune, render packet build, model animation/skinning if it has a clear boundary, renderer CPU preparation, render command encoding, queue submit, and present handoff where observable. Reuse the existing target-specific time helper pattern already used for `terrain_update_now_ms`.

Milestone 3 adds render counters. Extend `GpuMesh` metadata in `crates/engine_web/src/wgpu_renderer.rs` if needed to expose vertex count in addition to `index_count` and `vertex_float_count`. Count per-frame candidates, visible draws, culled objects, terrain draws by LOD, model draws, sky draws, shadow draws per cascade, post-process draws, submitted vertices, submitted indices, and submitted triangles. Track main-camera candidate versus visible counts. For shadow cascades, track candidate counts and actual submitted counts per cascade. If a reliable cascade frustum can be built from each cascade light-view-projection matrix, also track "would be visible after cascade culling" without changing draw behavior yet.

Milestone 4 adds optional GPU timestamp timing. During renderer initialization, inspect adapter features for timestamp query support using the exact `wgpu 0.20.1` feature names and APIs. If supported, request the feature, create a timestamp `QuerySet`, resolve query values to a readback buffer, and report GPU pass timings after the required asynchronous delay. If unsupported, keep all fields present but mark `gpuTimerAvailable: false` and avoid creating query resources. Add timestamp writes for shadow cascades, scene pass, bloom pass, final post-process pass, and optionally total frame GPU time. Because `PostProcessResources::render` owns bloom and final passes, it may need to accept an optional timing writer or return pass labels and query indices to the caller. Keep the implementation simple and explicitly tested where native `wgpu` supports it; unit tests should cover unavailable mode without needing a GPU.

Milestone 5 adds debug render options. Add `RenderDebugOptions` with production-safe defaults:

    terrainLodMask: number, default all supported LOD bits enabled
    skyEnabled: boolean, default true
    shadowPassEnabled: boolean, default true
    shadowCascadeMask: number, default all cascades enabled
    shadowSamplingEnabled: boolean, default true
    whiteTexturesEnabled: boolean, default false
    materialMode: "full" | "lambert", default "full"

Route the options through a `setRenderDebugOptions` command and a `resetRenderDebugOptions` command. The command may accept partial options from TypeScript but Rust should validate and clamp or reject invalid values. Terrain LOD filtering must filter only the render packet or draw submission; it must not change `BrowserTerrainStream` desired sets, worker requests, mesh generation, or cached terrain visibility. Sky disabling should skip the sky draw. Shadow pass disabling should skip shadow-map render passes and set shadow uniforms so the main pass behaves as shadows disabled. Shadow cascade masking should render only selected cascades and report skipped cascades in counters. Shadow sampling disabling should keep shadow map passes independent so the sampling cost can be isolated from shadow-map generation cost. White textures should bind diagnostic fallback textures. Lambert mode should use a shader/debug uniform path that removes expensive material logic while keeping geometry, camera, light, and optional shadow behavior comparable.

Milestone 6 adds the developer-facing dump and capture workflow. Extend `window.__ofgDebug` in `src/app/game.ts` with:

    getPerfStats(): object
    dumpPerfStats(): object
    resetPerfStats(): void
    getRenderDebugOptions(): object
    setRenderDebugOptions(options): void
    resetRenderDebugOptions(): void

`dumpPerfStats()` should log compact tables for browser CPU, Rust CPU, GPU pass timing, draw counts, LOD counts, cascade counts, and renderer resource counts, then return the same structured JSON object. It should be useful from DevTools without any UI. If a button is added, keep it small and debug-oriented; it should call the same dump function and not clutter the main game UI.

Add `tools/browser-perf-debug-capture.mjs` or a similarly named script. It should build or use the dev server in the same style as existing browser smoke tooling, open a deterministic seed and preset, wait for terrain to settle, collect a baseline sample window, then repeat sample windows with one debug option changed at a time: sky off, shadow pass off, shadow sampling off, each shadow cascade mask, white textures, Lambert mode, and selected terrain LOD masks. It should write `samples.json`, `summary.json`, and a short text summary under `artifacts/perf-debug/<run-id>/`.

Milestone 7 captures and records conclusions. Run the capture script on the current terrain-heavy build, inspect the summary, and update this plan's Outcomes & Retrospective with factual conclusions. Example conclusions should use language like "shadow pass GPU time accounts for X percent of measured GPU frame time with Y draws and Z indices per frame" or "main camera culling rejects X of Y candidates but shadow cascades submit all candidates." Do not make performance fixes in this milestone. If the data points clearly to a fix, create a new ExecPlan or a new milestone explicitly scoped to that fix.

## Concrete Steps

All commands run from `C:\dev\ofg`.

1. Read current contracts and relevant renderer files before editing:

    Get-Content docs/API_CONTRACTS.md
    Get-Content docs/ARCHITECTURE.md
    Get-Content crates/engine_web/src/wgpu_renderer.rs
    Get-Content src/app/game.ts
    Get-Content src/engine/web/browserGameTypes.ts

2. Implement Milestone 1 data structures and contract typing:

    Add crates/engine_web/src/perf.rs
    Update crates/engine_web/src/lib.rs or module declarations as needed
    Update src/engine/web/browserGameTypes.ts
    Update src/engine/web/engineWebWasm.ts
    Update docs/API_CONTRACTS.md

3. Validate Milestone 1:

    npm run test:rust
    npm run test:ts

4. Run milestone review before marking Milestone 1 complete:

    Use the repo-local milestone-review skill against the milestone diff and this plan.

5. Implement Milestones 2 through 6 in order. After each milestone, update Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective as appropriate, then run the relevant tests and milestone review before marking the milestone complete.

6. Run full validation after the tooling lands:

    npm test
    npm run smoke:browser
    npm run coverage:rust

7. Run the new capture script:

    node tools/browser-perf-debug-capture.mjs

Expected capture output should include a new directory like:

    artifacts/perf-debug/<run-id>/samples.json
    artifacts/perf-debug/<run-id>/summary.json
    artifacts/perf-debug/<run-id>/summary.txt

8. Update this ExecPlan with the captured diagnosis and any recommended follow-up optimization plan.

## Milestone Review

After each milestone:

1. Update changed API contracts or active docs.
2. Run the repo-local `milestone-review` skill against the milestone diff and this ExecPlan.
3. Apply required findings before marking the milestone complete, or record a rejected finding with rationale in the Decision Log.
4. Re-run relevant validation commands.
5. Record the review summary, commands, artifacts, and remaining risks in Progress or Outcomes & Retrospective.

The milestone is not complete until the review is done and this plan's living sections are updated.

## Validation and Acceptance

Behavioral acceptance:

- In the browser app, `window.__ofgDebug.dumpPerfStats()` prints and returns latest/min/max/average/p95 summaries for browser CPU spans, Rust CPU spans, GPU pass spans when available, draw counts, visible/candidate/cull counts, submitted vertices, submitted indices, terrain LOD draw counts, and shadow cascade draw counts.
- `window.__ofgDebug.resetPerfStats()` clears the frame history without restarting the app.
- `window.__ofgDebug.setRenderDebugOptions(...)` can independently disable sky rendering, shadow-map rendering, shadow sampling, individual shadow cascades, texture sampling through white textures, full material mode through Lambert mode, and terrain LOD rendering through a LOD mask.
- `window.__ofgDebug.resetRenderDebugOptions()` restores production defaults.
- The debug snapshot reports whether GPU timers are available. If unavailable, the app still runs and CPU/counter metrics still work.
- The capture script writes JSON artifacts with baseline and toggled measurements.
- The capture script can show whether main-camera culling and shadow-cascade culling are working by comparing candidate, visible, culled, draw, index, and vertex counts.
- The plan's final outcome records a factual diagnosis and does not include unmeasured performance fixes.

Test and command acceptance:

- `npm run test:rust` passes.
- `npm run test:ts` passes.
- `npm test` passes after all implementation milestones.
- `npm run smoke:browser` passes and verifies the new debug hooks enough to catch missing command/snapshot wiring.
- `npm run coverage:rust` runs. Changed implementation files do not appear in the default filtered coverage output, or this plan records a specific exception with rationale.
- `node tools/browser-perf-debug-capture.mjs` completes and writes `artifacts/perf-debug/<run-id>/summary.json`.

Expected `summary.json` shape should include at least:

    browserCpu
    rustCpu
    gpu
    rendererCounters
    terrainLodCounters
    shadowCascadeCounters
    renderDebugOptions
    terrainStreamStatus
    rendererStatus

## Idempotence and Recovery

All debug controls must be resettable through `resetRenderDebugOptions` and by reloading the page. They must default to production rendering behavior.

Perf history must be resettable through `resetPerfStats`. Resetting history must not reset terrain streaming, renderer resources, player position, or camera state.

The capture script writes only under `artifacts/perf-debug/`, which is generated output and should not be committed. Rerunning the script should create a new run directory or safely overwrite only an explicitly requested output directory.

If GPU timestamp queries are unavailable or fail during initialization, the renderer must continue with CPU and counter metrics. The debug status should make the lack of GPU timing explicit.

If a debug option causes invalid render state, Rust should reject the command with a clear error and preserve the previous options. Partial option updates should be applied atomically after validation.

No destructive git or filesystem commands are required by this plan.

## Artifacts and Notes

The main persistent artifact from implementation is the diagnostic tooling itself. Runtime capture output belongs under:

    C:\dev\ofg\artifacts\perf-debug\

The plan should end with a concise diagnosis note in Outcomes & Retrospective, for example:

    Baseline frame summary on 2026-06-08 showed GPU timing available on the test machine.
    Shadow pass GPU time averaged N ms and submitted M cascades, D draws, V vertices, and I indices per frame.
    Main scene pass averaged N ms after culling C of K candidates.
    Disabling shadow sampling changed final pass cost by N ms, while disabling shadow-map rendering changed frame GPU cost by N ms.
    Recommended follow-up: create a focused plan for the measured bottleneck.

Do not check in generated screenshots, browser smoke output, `dist/`, `node_modules/`, or `artifacts/`.

## Interfaces and Dependencies

New or extended Rust interfaces:

- `crates/engine_web/src/perf.rs`
  - `FramePerfRing`
  - `PerfSummary`
  - `FramePerfSample`
  - `RenderCounterSample`
  - `TerrainLodCounter`
  - `ShadowCascadeCounter`
  - `GpuTimerStatus`
  - `RenderDebugOptions`

- `crates/engine_web/src/wgpu_renderer.rs`
  - Store `RenderDebugOptions` in `BrowserWgpuRenderer` or `RustBrowserGame`.
  - Store Rust perf history in `RustBrowserGame` and/or `BrowserWgpuRenderer`.
  - Parse `setRenderDebugOptions`, `resetRenderDebugOptions`, and `resetPerfStats`.
  - Serialize perf and debug option state through `renderer_status_to_js` or a clearly named debug snapshot field.

- `crates/engine_web/src/post_process.rs`
  - Accept optional GPU timing instrumentation for bloom and final post-process passes, or expose a minimal pass-timing hook owned by the renderer.

New or extended TypeScript interfaces:

- `src/engine/web/browserGameTypes.ts`
  - `RenderDebugOptions`
  - `RenderMaterialDebugMode`
  - `GameCommand` variants for render debug options and perf reset.
  - Debug snapshot or renderer status types for perf and render debug state.

- `src/engine/web/engineWebWasm.ts`
  - Extend `EngineWebRendererStatus` with Rust perf and render debug fields.

- `src/app/perfDebug.ts`
  - Browser shell frame-loop perf tracker and dump formatting helpers.

- `src/app/game.ts`
  - Measure browser shell spans.
  - Add `window.__ofgDebug` methods for perf stats, perf dump, perf reset, render debug option get/set/reset.

New or extended tools:

- `tools/browser-perf-debug-capture.mjs`
  - Deterministic browser capture for baseline and isolated render-debug toggles.

No new runtime framework should be introduced. Use existing Rust, TypeScript, `wgpu`, wasm-bindgen, and Playwright Core patterns already present in the repository.
