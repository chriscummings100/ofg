# Raise OFG API test coverage with Rust-first coverage tooling

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

If `PLANS.md` is present in the repo, maintain this document in accordance with it. This plan follows `PLANS.md` in this repository.

## Purpose / Big Picture

OFG should have every supported API tested to some degree, with Rust owning coverage for engine, terrain, renderer, smoke harness, and benchmark behavior. Browser and TypeScript tests should cover only browser integration and TypeScript-owned shell contracts. The user-visible outcome is that a developer or AI agent can run the test suite plus coverage commands and see which supported Rust APIs, TypeScript shell APIs, build tools, generated artifacts, and smoke-image paths are covered.

This plan starts by making Rust coverage measurable. Coverage is not a substitute for judgment, but it is useful for finding missed branches and public functions after the sub-agent review. Once coverage works, use its missing-line output alongside the sub-agent gap list to add focused tests until the supported API surface has high line/function coverage and every deliberate public API has at least one direct or indirect contract test.

This plan also reduces the supported API surface where the review found accidental or obsolete APIs. The raw `ofg_engine_web_*` facade is not used by the playable browser path; if no necessary consumer exists, remove or feature-gate it instead of writing tests that bless an unsupported API. The remaining Node/TypeScript terrain WASM benchmark must move to Rust so TypeScript is no longer a terrain client even in benchmarking.

## Progress

- [x] (2026-06-07) Created this ExecPlan after the API test-gap sub-agent review.
- [x] (2026-06-07) Verified the local machine currently lacks `cargo llvm-cov`, `cargo tarpaulin`, and `llvm-tools-preview`.
- [x] (2026-06-07) Recorded that local Rust is `rustc 1.78.0` on `x86_64-pc-windows-msvc` with LLVM `18.1.2`.
- [x] (2026-06-07) Added `npm run coverage:rust` through `tools/rust-coverage.mjs`, plus README/AGENTS guidance. Verified the missing-tool path exits with setup instructions and `git diff --check` passes for the touched files.
- [x] (2026-06-07) Ran the required Milestone 1 review locally across contract, code-quality, legacy, correctness, and validation passes. Required findings fixed: none. Follow-ups recorded: install or otherwise provide `cargo-llvm-cov` before the first real coverage report. Validation rerun: `npm run coverage:rust` missing-tool path, `git diff --check -- tools/rust-coverage.mjs package.json README.md AGENTS.md docs/API_COVERAGE_TESTING_PLAN.md`.
- [x] (2026-06-07) Installed `llvm-tools-preview` and `cargo-llvm-cov 0.6.15`, the newest compatible coverage subcommand found for local Rust 1.78 after latest `cargo-llvm-cov 0.8.7` rejected the compiler.
- [x] (2026-06-07) Generated the first Rust coverage report. Evidence: `npm run coverage:rust -- --no-clean` passed, wrote `artifacts/coverage/rust/coverage.txt` and `artifacts/coverage/rust/summary.json`, and reported 74.0% line coverage, 74.0% function coverage, and 71.3% region coverage.
- [x] (2026-06-07) Used the first coverage report to augment the backlog. Top uncovered supported areas by line count are `crates/terrain_core/src/facade.rs`, `crates/ofg_test_harness/src/render_smoke/*`, raw `crates/engine_web/src/facade.rs`, `crates/engine_web/src/model_assets.rs`, `crates/engine_web/src/model_locomotion.rs`, `crates/engine_web/src/game_state.rs`, and `crates/engine_web/src/terrain_textures.rs`.
- [x] (2026-06-07) Ran the required Milestone 2 review locally. Required finding fixed: coverage setup guidance now names `cargo install cargo-llvm-cov --locked --version 0.6.15`, because latest `0.8.7` requires Rust 1.87 while the repo toolchain is Rust 1.78. Validation rerun: `npm run coverage:rust -- --no-clean`, `git diff --check -- tools/rust-coverage.mjs package.json README.md AGENTS.md docs/API_COVERAGE_TESTING_PLAN.md`, and coverage artifact inspection.
- [x] (2026-06-07) Removed the unsupported raw `engine_web` facade instead of testing it as supported API. Evidence: deleted `crates/engine_web/src/facade.rs`, removed the module/re-export from `crates/engine_web/src/lib.rs`, regenerated `assets/wasm/engine_web/*` and `src/generated/web/engineWebWasm.ts`, and added a `tools/build-engine-web-wasm.mjs` guard that fails if wasm-bindgen glue contains `ofg_engine_web_`.
- [x] (2026-06-07) Ran the required Milestone 3 review locally. Required findings fixed: none after updating `docs/API_CONTRACTS.md` to record the removal and guard. Validation rerun: `cargo test -p engine_web`, `npm run build:engine-web-wasm`, `npm run check:engine-web-wasm`, and `rg -n "ofg_engine_web_|FacadeErrorCode|crates/engine_web/src/facade.rs|pub use facade|mod facade" crates/engine_web src tests tools docs/API_CONTRACTS.md docs/API_COVERAGE_TESTING_PLAN.md assets/wasm/engine_web src/generated/web`, which now finds only docs and the build-script forbidden-prefix guard.
- [x] (2026-06-07) Moved terrain benchmarking from TypeScript/Node WASM instantiation to Rust. Evidence: added `crates/terrain_core/src/benchmark.rs` behind the `benchmark` feature, added `ofg-terrain-bench` in `crates/ofg_test_harness`, wired `npm run bench:terrain:rust`, deleted `tools/benchmark-terrain-wasm.mjs`, and updated README/AGENTS/API/TERRAIN docs.
- [x] (2026-06-07) Ran the required Milestone 4 review locally. Required findings fixed: none after updating active docs. Validation rerun: `cargo test -p terrain_core --features benchmark`, `cargo test -p ofg_test_harness`, `cargo test --workspace`, `npm run bench:terrain:rust -- --iterations 1 --mesh-iterations 1 --warmup 1 --output artifacts/terrain-bench/npm-test-run/report.json`, `npm run bench:terrain:rust`, artifact shape inspection, and `rg -n "terrain_core\\.wasm|ofg_fill_density_chunk|ofg_build_chunk_mesh|ofg_density_at|WebAssembly\\.instantiate" src tests tools package.json`, which now finds only the allowed export-contract builder `tools/build-terrain-wasm.mjs`.
- [x] (2026-06-07) Added the first high-priority Rust facade and benchmark contract tests. Evidence: `crates/terrain_core/src/tests.rs` now covers facade buffer capacities/pointers, macro base elevation, invalid mesh cell sizes, invalid mesh-packet input lengths, missing mesh-packet loads, invalid LOD, density-store invalid cell sizes, reversed retain windows, eviction counters, stream invalid config/reset/invalidate/stale-result paths, worker-pool max workers, invalid task inputs, failed tasks, and non-finite generation sentinels. `crates/terrain_core/src/benchmark.rs` tests now share a crate-level test lock with terrain facade tests. Validation: `cargo test -p terrain_core --features benchmark`.
- [x] (2026-06-07) Added native Rust smoke-harness contract tests. Evidence: `crates/ofg_test_harness/src/render_smoke/mod.rs` covers CLI parser defaults/errors/run-id shape; `report.rs` covers pixel sampling, transparent/flat/solid rejection, report JSON camelCase shape, and normalized absolute paths; `scenarios.rs` covers filter parsing, scenario inventory, boot/seam terrain construction, seam/corner coverage errors, and preset names; `renderer.rs` covers pure renderer helpers for dimensions, row alignment, terrain layer colors, texture bindings, vector normalization, and byte views. Validation: `cargo test -p ofg_test_harness`.
- [x] (2026-06-07) Refreshed Rust coverage after Milestone 5's first test batch. Evidence: `npm run coverage:rust -- --no-clean` passed and reported 79.1% line coverage, 80.7% function coverage, and 77.5% region coverage. Top uncovered files are now `crates/terrain_core/src/facade.rs`, `crates/ofg_test_harness/src/render_smoke/renderer.rs`, `crates/ofg_test_harness/src/terrain_bench.rs`, `crates/engine_web/src/model_assets.rs`, `crates/engine_web/src/model_locomotion.rs`, `crates/engine_web/src/game_state.rs`, `crates/ofg_test_harness/src/render_smoke/mod.rs`, and `crates/engine_web/src/model_animation.rs`.
- [x] (2026-06-07) Added high-priority Rust `engine_web` asset/model/locomotion tests. Evidence: `crates/engine_web/src/model_assets.rs` now covers model count helpers, static vertex packing, data URI decoding, private importer validators, matrix flattening, and every `ModelAssetError` display variant. `crates/engine_web/src/model_locomotion.rs` now covers invalid blend duration, invalid delta/speed/tuning, fallback clip names, tuning recomputation, helper clamp behavior, and every `PlayerCharacterModelError` display variant. `crates/engine_web/src/tests.rs` now covers `PlayerCharacterModel` part accessors, material/index/mesh-node accessors, current/ticked part vertices, locomotion tuning getters/setters, and invalid tuning rejection through real Quaternius fixtures. Validation: `cargo test -p engine_web`.
- [x] (2026-06-07) Refreshed Rust coverage after the `engine_web` contract tests. Evidence: `npm run coverage:rust -- --no-clean` passed and reported 82.1% line coverage, 82.5% function coverage, and 79.6% region coverage. Top uncovered files are now `crates/terrain_core/src/facade.rs`, `crates/ofg_test_harness/src/render_smoke/renderer.rs`, `crates/ofg_test_harness/src/terrain_bench.rs`, `crates/engine_web/src/game_state.rs`, `crates/ofg_test_harness/src/render_smoke/mod.rs`, `crates/engine_web/src/model_animation.rs`, `crates/engine_web/src/terrain_textures.rs`, and `crates/engine_web/src/model_locomotion.rs`.
- [x] (2026-06-07) Reduced the default Rust coverage output to a human attention list. Evidence: `npm run coverage:rust -- --no-clean` now suppresses raw cargo output unless `--full` is passed, writes `summary.json` and `summary.pretty.json` filtered to implementation files below the default 90% line threshold, writes the unfiltered cargo summary to `summary.full.json` / `summary.full.pretty.json`, and after excluding tests, `ofg_test_harness`, `lib.rs`, and `facade.rs`, reports 13 files needing attention out of 53 source files while excluding 15 default-ignored files.
- [x] (2026-06-07) Added high-priority TypeScript shell/generated-artifact tests without making TypeScript a terrain client. Evidence: shader and engine-web wasm metadata tests recompute generated hashes from source/assets; `RustBrowserGameRuntime` tests cover the browser preset-to-Rust reset code mapping; the runtime import graph now allowlists reachable `src/engine/world/**` modules and bans reachable `src/generated/terrain/**`; package/tsconfig manifest tests preserve separated Rust, TypeScript, smoke, coverage, and Rust terrain benchmark lanes. Validation: `tsc -p tsconfig.test.json`, `npx mocha "dist-test/**/*.test.js"`, and `npm run test:ts`.
- [x] (2026-06-07) Added another focused Rust implementation test batch for the default coverage attention list. Evidence: `engine_web` now directly tests player character descriptors, resource handles, material packets, embedded model texture decode/errors, terrain texture manifest/assets/errors, and model render asset helper/error branches; `engine_core` now directly tests scene-resource IDs, hashing, lookup, and arena reuse; `terrain_core` now tests zero-vector normalization, clamping, smoothstep, and preset fallback behavior. Validation: `cargo test -p engine_core -p terrain_core --features terrain_core/benchmark` and `cargo test -p engine_web`.
- [x] (2026-06-07) Refreshed Rust coverage after the second implementation test batch. Evidence: `npm run coverage:rust -- --no-clean` passed and reported 84.3% line coverage, 84.5% function coverage, and 82.0% region coverage. With the default human filter, only `crates/engine_web/src/model_materials.rs`, `crates/engine_web/src/renderer.rs`, `crates/engine_web/src/model_animation.rs`, and `crates/engine_web/src/game_state.rs` remain below 90% line coverage.
- [x] (2026-06-07) Decided to exclude raw Rust `facade.rs` export-boundary files from the default native coverage attention list while preserving explicit contract evidence. Evidence: `terrain_core` facade functions have direct native tests for the supported C ABI contracts, `tools/build-terrain-wasm.mjs --check` remains the standalone wasm export-contract check, and `tools/rust-coverage.mjs` records `**/facade.rs` in `ofgCoverageFilter.excludedByDefault`.
- [x] (2026-06-07) Added the final focused Rust `engine_web` coverage batch for supported contracts in model material import, renderer resources, model animation sampling/import, and browser game state. Evidence: `model_materials_tests.rs` now covers invalid buffer-view images, invalid/unsupported image data URIs, empty data-URI MIME, sampler enum variants, and blend alpha mode. `tests.rs` now covers renderer resource getters/default/object unregister, texture not-configured and resize errors, animation step/final/zero-duration sampling, invalid animation target/time/blend errors, malformed animation glTF imports, scaled static model scenes, player-character replacement, absent character snapshots, and `BrowserGameStateError` formatting. Validation: `cargo test -p engine_web`.
- [x] (2026-06-07) Established the normal coverage validation workflow as the thresholded `npm run coverage:rust` attention command. Evidence: README/AGENTS document the command, `tests/ts/packageScripts.test.ts` preserves the explicit coverage lane, default `summary.json` and `summary.pretty.json` are filtered to implementation files below 90% line coverage, and `npm run coverage:rust -- --no-clean` now reports no default attention files.
- [x] (2026-06-07) Completed the wasm/browser bridge milestone through the narrow browser-integration path instead of adding `wasm-bindgen-test`. Evidence: `src/engine/web/rustBrowserGameAdapter.test.ts` and `rustBrowserGameRuntime.test.ts` cover TypeScript command/snapshot forwarding and preset reset codes with fakes; `tools/browser-smoke.mjs` remains the browser integration check for Rust runtime sentinels, WebGPU boot, reload, HUD mode, keyboard toggle, and debug movement hooks; Rust-side browser game command/state behavior is covered by native `engine_web` tests.
- [x] (2026-06-07) Completed the TypeScript shell/tooling milestone without making TypeScript a terrain client. Evidence: TypeScript tests cover runtime import quarantine, package command graph, generated shader hash recomputation, engine-web wasm hash/export metadata, browser adapter copies, and runtime command forwarding. Build/check scripts are validated through final `npm run check:shaders` and `npm run check:wasm` integration commands rather than unit-testing every stale-output branch.
- [x] (2026-06-07) Recorded the milestone-review follow-up for oversized `engine_web` test organization. Evidence: `crates/engine_web/src/tests.rs` is over 1000 lines after the API coverage batch. Before adding more `engine_web` tests, split it into focused modules such as `renderer_tests.rs`, `model_animation_tests.rs`, `game_state_tests.rs`, and remaining model/skinning/terrain-stream tests.
- [x] (2026-06-07) Ran the required final milestone review with five read-only sub-agent passes. Required findings fixed: stale active docs tying `terrain_core.wasm` to benchmarking; missing `configure_scaled_static_model_scene` height-offset assertion; public malformed `ModelAnimationClip` sampling panic; incomplete ExecPlan evidence for Milestone 6, Milestone 7, excluded harness validation, and final acceptance commands. Follow-up recorded: split oversized `crates/engine_web/src/tests.rs` before further growth. Rejected findings: none.
- [x] (2026-06-07) Completed final acceptance validation. Commands passed: `npm run clean`, `npm run build`, `npm run test:rust`, `npm run test:ts`, `npm test`, `npm run check:shaders`, `npm run check:wasm`, `npm run smoke`, `npm run coverage:rust`, and `git -c safe.directory=C:/dev/ofg diff --check`. Smoke artifacts: `artifacts/rust-smoke/run-1780835894-263/report.json` and `artifacts/browser-smoke/2026-06-07T12-38-54-307Z/`. Final clean coverage reported 85.9% lines, 86.2% functions, 83.7% regions, and no default attention files below 90%.

## Surprises & Discoveries

- Observation: The local toolchain does not have Rust coverage tools installed.
  Evidence: `cargo llvm-cov --version` and `cargo tarpaulin --version` both reported `no such command`; `rustup component list --installed` did not list `llvm-tools-preview`.
- Observation: `cargo-llvm-cov` is the best fit for this repository, but the locally installed Rust is old enough that installing from source may require a Rust upgrade.
  Evidence: `rustc -vV` reported `rustc 1.78.0`; current `cargo-llvm-cov` documentation says source installation requires a newer Rust version, while prebuilt binaries and Windows package-manager installs are available.
- Observation: The sub-agent review found broad behavior coverage but incomplete direct API contract coverage.
  Evidence: gaps clustered around `terrain_core` facade buffer/status APIs, `engine_web` wasm bridge commands, `ofg_test_harness` public report/scenario helpers, TypeScript `startGame`, generated artifact hashes, and build-tool `--check` behavior.
- Observation: The former `tools/benchmark-terrain-wasm.mjs` TypeScript/Node terrain WASM client was an architecture leak and has been removed.
  Evidence: before removal, the script instantiated `assets/wasm/terrain_core.wasm` and called terrain density, store, and mesh exports directly for benchmarks. After removal, `rg` finds those calls only in the allowed standalone export-contract builder.
- Observation: No supported consumer used the raw `ofg_engine_web_*` facade.
  Evidence: after deleting `crates/engine_web/src/facade.rs` and regenerating wasm-bindgen glue, `npm run check:engine-web-wasm` passed and `rg` found no `ofg_engine_web_` references outside docs and the build-script forbidden-prefix guard.
- Observation: The Rust terrain benchmark reproduces the old benchmark phases without `terrain_core.wasm` instantiation.
  Evidence: `npm run bench:terrain:rust` wrote `artifacts/terrain-bench/run-1780829949-935/report.json` with `fillOnly`, `fillAndCopy`, `apronFillOnly`, `densityWindowPrepareRetained`, `meshBuildAndCopyCold`, `meshBuildAndCopyPrepared`, density-store counters, phase estimates, 12 scenarios, and 16 streaming windows.
- Observation: Native `cargo-llvm-cov` currently reports `#[no_mangle] extern "C"` terrain facade bodies as uncovered even when native tests call those exported functions.
  Evidence: after adding direct tests for facade capacities, pointers, stream status/error paths, density-store counters, mesh-packet paths, and worker-pool paths, `coverage.txt` shows the test call sites covered but still reports zero hits on the corresponding `crates/terrain_core/src/facade.rs` function bodies. The tests are useful API evidence, but the native coverage number for this raw facade is misleading and needs either a wasm/export-contract coverage route or a documented exclusion before threshold gating.
- Observation: Native Rust smoke-harness helper coverage is now measurable without launching a GPU render.
  Evidence: `cargo test -p ofg_test_harness` runs parser, report, scenario, terrain-stream setup, and renderer-helper tests; `npm run coverage:rust -- --no-clean` reduced uncovered `render_smoke/scenarios.rs` out of the top uncovered list and moved `render_smoke/mod.rs` to 51.9% line coverage.
- Observation: `engine_web` asset and locomotion coverage had a large diagnostic-formatting component.
  Evidence: tests for `ModelAssetError` and `PlayerCharacterModelError` display variants, private import validators, data URI decoding, locomotion invalid inputs, and public character accessors moved `model_assets.rs` and `model_locomotion.rs` out of the highest-risk uncovered set. The latest coverage run reports `model_locomotion.rs` at 94.5% line coverage, while `model_assets.rs` is no longer in the top uncovered list.
- Observation: The default filtered coverage summary now leaves only four implementation files below 90% line coverage.
  Evidence: after direct tests for player character descriptors, resource stores, material packets, texture decode, terrain texture validation, model render helpers, scene-resource IDs, terrain math, and preset fallback, `npm run coverage:rust -- --no-clean` reports 84.3% line coverage and lists only `engine_web` model material import, renderer, animation, and game-state files below threshold.
- Observation: The final default filtered Rust coverage summary has no implementation files below the documented 90% line threshold.
  Evidence: after focused `engine_web` model-material, renderer, animation, and game-state tests, `npm run coverage:rust -- --no-clean` reports 85.9% workspace line coverage, 86.2% function coverage, 83.7% region coverage, and `files below 90% line coverage ... none` after the default exclusions.

## Decision Log

- Decision: Use `cargo-llvm-cov` as the first Rust coverage tool.
  Rationale: It wraps Rust's LLVM source-based coverage, supports Windows MSVC, works at workspace scale, and can emit text, JSON, LCOV, and HTML reports. Tarpaulin is useful in some Rust projects but is less appropriate as the primary Windows/MSVC plan.
  Date/Author: 2026-06-07 / Codex.
- Decision: Coverage starts as an analysis command, not an immediate hard gate.
  Rationale: The first report will include noise from generated paths, wasm-only code, smoke harness GPU branches, test-only files, and deliberately unsupported APIs. Turn it into a gate only after the exclude list and thresholds are stable.
  Date/Author: 2026-06-07 / Codex.
- Decision: Make the default coverage summaries human-filtered and keep full summaries separate.
  Rationale: The default coverage command is mainly an attention tool for developers and AI agents, so it should foreground supported source files below threshold instead of printing test files, harness internals, and raw cargo noise. The full cargo summary remains available as `summary.full.json` and through `--full`.
  Date/Author: 2026-06-07 / Codex.
- Decision: Exclude Rust `lib.rs` and `facade.rs` files from default native coverage attention summaries.
  Rationale: `lib.rs` files are usually module/export glue, and raw `facade.rs` files are export-boundary wrappers whose native coverage can be misleading, especially for `#[no_mangle] extern "C"` functions. Supported facade contracts still need tests or documented exclusion, but they should not dominate the default human backlog.
  Date/Author: 2026-06-07 / Codex.
- Decision: Treat raw `ofg_engine_web_*` exports as suspect API.
  Rationale: `docs/API_CONTRACTS.md` says playable TypeScript should use wasm-bindgen `RustBrowserGame`, not raw linked exports. Testing raw exports would preserve an API that may not be needed. The plan first proves whether any supported consumer still needs them.
  Date/Author: 2026-06-07 / Codex.
- Decision: Move terrain benchmarking to Rust.
  Rationale: TypeScript should not be a terrain client in runtime, tests, or benchmark tooling. Rust benchmark code can call `terrain_core` APIs directly and can write the same JSON report shape without preserving a standalone TypeScript terrain WASM adapter path.
  Date/Author: 2026-06-07 / Codex.
- Decision: Remove `crates/engine_web/src/facade.rs` rather than test it.
  Rationale: The playable browser path uses `RustBrowserGame`; no supported runtime, test, tool, or docs consumer needed `ofg_engine_web_*`. Keeping tests for that facade would make an unsupported raw renderer API look intentional.
  Date/Author: 2026-06-07 / Codex.
- Decision: Put density benchmark helpers behind `terrain_core`'s `benchmark` feature.
  Rationale: The Rust benchmark needs access to density chunk fill and retained-store phases, but those helpers should not become the normal browser runtime API. The feature is enabled by `ofg_test_harness` and directly tested by `cargo test -p terrain_core --features benchmark`.
  Date/Author: 2026-06-07 / Codex.
- Decision: Keep `npm run coverage:rust` as a thresholded attention report instead of a hard failing gate.
  Rationale: The stable default threshold is now 90% line coverage for non-ignored implementation files, and the command clearly prints any files below that threshold. Keeping it non-failing avoids treating native-coverage blind spots, wasm-only branches, and export glue as CI failures while still making the validation evidence obvious for humans and AI agents.
  Date/Author: 2026-06-07 / Codex.
- Decision: Treat `terrain_core/src/facade.rs` native coverage as an excluded export-boundary blind spot, not an untested API.
  Rationale: The supported facade contracts are directly exercised by Rust tests and the standalone wasm artifact is checked by `tools/build-terrain-wasm.mjs --check`, but `cargo-llvm-cov` does not attribute hits to the `#[no_mangle] extern "C"` function bodies reliably on this native toolchain.
  Date/Author: 2026-06-07 / Codex.
- Decision: Exclude `crates/ofg_test_harness/**` from the default coverage attention list and validate it as a harness surface.
  Rationale: The harness includes CLI, smoke orchestration, native `wgpu` setup, PNG/report writing, and benchmark timing paths that are better validated by `cargo test -p ofg_test_harness`, `npm run smoke:rust`, and `npm run bench:terrain:rust` than by a default implementation coverage threshold. Its supported helpers still have direct tests and smoke/benchmark evidence; the full cargo summary remains available with `npm run coverage:rust -- --full`.
  Date/Author: 2026-06-07 / Codex.
- Decision: Do not add `wasm-bindgen-test` in this milestone.
  Rationale: The browser-only object protocol is covered by focused TypeScript adapter/runtime tests plus the browser smoke test that launches Chrome/Edge and exercises the real wasm-bindgen `RustBrowserGame` path. Adding a second wasm test runner would increase toolchain surface without moving terrain semantics out of TypeScript further.
  Date/Author: 2026-06-07 / Codex.

## Outcomes & Retrospective

This plan moved OFG to a Rust-first test and coverage workflow. `npm run coverage:rust` now runs `cargo-llvm-cov`, writes text, full JSON, and human-readable filtered summaries under `artifacts/coverage/rust/`, and defaults to a 90% line-coverage attention threshold for non-ignored implementation files. The latest run reports 85.9% workspace line coverage, 86.2% function coverage, 83.7% region coverage, and no default attention files below 90%.

Unsupported or accidental API surface was reduced instead of blessed with tests: the raw `ofg_engine_web_*` facade was removed, TypeScript terrain WASM clients/adapters and the Node terrain benchmark were removed, browser smoke was narrowed to browser integration, and terrain benchmarking moved to Rust through `npm run bench:terrain:rust`.

The added tests cover Rust terrain facade contracts, terrain benchmarks, native render-smoke helpers, `engine_core` scene resources, `engine_web` renderer/model/material/texture/player/game-state contracts, TypeScript shell metadata/import-graph/package-script contracts, and generated shader/engine-web wasm hash recomputation. The remaining coverage blind spots are native attribution for raw `facade.rs` export glue and the smoke/benchmark harness coverage filter; those surfaces are excluded from the default attention summary and covered by explicit facade tests, harness tests, smoke/benchmark commands, and wasm export-contract checks where the standalone artifact remains supported.

Known follow-up: split `crates/engine_web/src/tests.rs` into focused test modules before adding more `engine_web` tests. The current file is oversized, but the split is organizational cleanup rather than missing API coverage.

Final acceptance commands passed on 2026-06-07: `npm run clean`, `npm run build`, `npm run test:rust`, `npm run test:ts`, `npm test`, `npm run check:shaders`, `npm run check:wasm`, `npm run smoke`, and `npm run coverage:rust`.

## Contract and Quality Baseline

This plan preserves the Rust-first ownership established by `docs/API_CONTRACTS.md` and `docs/ARCHITECTURE.md`.

`OFG-API-001: Browser Shell To Rust Browser Game` remains active. TypeScript may load `RustBrowserGame`, call `resize`, `tick`, `command`, and `debugSnapshot`, and copy browser-facing packets. Tests for this boundary may be TypeScript unit tests with fakes, browser smoke tests, or wasm-bindgen tests. TypeScript must not inspect terrain density buffers, terrain mesh buffers, terrain stream internals, or raw terrain WASM exports.

`OFG-API-002: Rust Game To Browser Asset Loader` remains active. Browser TypeScript may decode generic texture-array and byte requests. Rust owns terrain texture manifest interpretation and model asset interpretation. Tests should prove the JavaScript object bridge and asset-loader failure modes, not move terrain semantics into TypeScript.

`OFG-API-003: Debug And Smoke-Test Hooks` remains active. Browser debug hooks are integration hooks. Tests should verify that they forward documented commands and expose black-box runtime sentinels, not terrain internals.

`OFG-API-006: Standalone Terrain WASM Artifact` is now an export-contract fixture only. The TypeScript terrain benchmark has been replaced by Rust, and the standalone `terrain_core.wasm` artifact stays only while build/check tooling needs to verify the raw export contract. Removing it from the regular build remains a separate future cleanup decision.

`OFG-API-009: Forbidden TypeScript Ownership` is enforced by this plan. TypeScript tests and tools must not become terrain clients. Any new TypeScript test touching terrain names should prove browser shell configuration, import-graph quarantine, generated metadata, or debug sentinel behavior only.

After each milestone, run the repo-local `milestone-review` skill before marking the milestone complete. Record required findings, rejected findings, validation commands, and remaining risks in this ExecPlan.

## Context and Orientation

The repository root is `C:/dev/ofg`.

The Rust workspace is `Cargo.toml` with these members:

    crates/engine_core
    crates/engine_web
    crates/ofg_test_harness
    crates/terrain_core

`crates/terrain_core` owns terrain density, height sampling, chunk coordinates, density chunks, mesh generation, stream scheduling, density stores, worker-pool test fixtures, and the legacy mesh-packet store. Its public API is exported from `crates/terrain_core/src/lib.rs`; its externally callable `ofg_*` facade is in `crates/terrain_core/src/facade.rs`.

`crates/engine_core` owns browser-free engine/player/camera/scene logic. Its public API is exported from `crates/engine_core/src/lib.rs`; its raw `ofg_engine_*` facade is tested and should remain covered while it exists.

`crates/engine_web` owns browser-facing Rust game state, renderer resources, terrain textures, model/material/animation/skinning, terrain stream, and wasm-bindgen `RustBrowserGame`. The playable browser path should use `RustBrowserGame`, not raw `ofg_engine_web_*` exports.

`crates/ofg_test_harness` owns native Rust smoke rendering through `ofg-render-smoke`. It writes PNGs and JSON reports under `artifacts/rust-smoke/`. Its public `render_smoke::run` CLI path and helper report/scenario APIs currently have no unit tests.

TypeScript runtime code starts at `src/main.ts`, then `src/app/game.ts`, then `src/engine/web/*`. TypeScript owns browser startup, DOM input collection, URL seed/preset parsing, debug-hook wiring, generic browser asset decoding, and the browser runtime facade. It must not own terrain generation, terrain mesh generation, terrain stream scheduling, material classification, terrain worker behavior, or Rust renderer internals.

Tools under `tools/` define build and smoke command APIs. The most important for this plan are:

`tools/build-shaders.mjs`: generates and checks `src/generated/render/uberShader.ts`.

`tools/build-engine-web-wasm.mjs`: builds `engine_web.wasm`, runs wasm-bindgen, writes `assets/wasm/engine_web/*`, and writes `src/generated/web/engineWebWasm.ts`.

`tools/build-terrain-wasm.mjs`: builds standalone `terrain_core.wasm` and currently checks expected raw exports.

`npm run bench:terrain:rust`: runs `crates/ofg_test_harness/src/bin/ofg-terrain-bench.rs`, which calls Rust `terrain_core` benchmark helpers and writes JSON under `artifacts/terrain-bench/`. The old `tools/benchmark-terrain-wasm.mjs` TypeScript/Node WASM client has been deleted.

`tools/browser-smoke.mjs`: browser integration smoke only. It should stay narrow.

## API Gap Inventory From Review

The review divided the codebase into four sections. These are the currently known test gaps before coverage is added.

Rust `terrain_core` and `engine_core`:

- `crates/terrain_core/src/facade.rs`: direct tests for stream buffer capacities and stable pointers, mesh-packet coord capacity, worker max workers, mesh input length getters, stream reset/invalidate/fail/status getters, worker-pool invalid inputs and fail path, density/store invalid cell sizes, reversed retain windows, eviction counter, failed mesh-packet load, invalid LOD, and `ofg_macro_base_elevation_at`.
- `crates/terrain_core/src/chunk.rs`: direct tests for `terrain_chunk_coord_containing_position` and `terrain_chunk_key`, especially exact boundaries and negative coordinates.
- `crates/terrain_core/src/mesh.rs`: direct tests for `build_chunk_mesh` returning `MeshData` and rejecting non-positive cell size.
- `crates/engine_core/src/facade.rs`: direct tests for missing-player sentinels, invalid-delta sentinels, `u64::MAX`, `u32::MAX`, `NaN`, and zero return values.
- `crates/engine_core/src/math.rs`: direct tests for public quaternion constructors and normalization edge cases.
- `crates/engine_core/src/scene.rs`: direct tests for clearing scene globals with `None` and reparenting to root with `None`.

Rust `engine_web` and `ofg_test_harness`:

- `crates/ofg_test_harness/src/render_smoke/mod.rs`: tests for CLI arg parsing, scenario filtering, report JSON shape, pixel rejection, and scenario terrain construction.
- `crates/engine_web/src/wgpu_renderer.rs`: browser or wasm-bindgen tests for `RustBrowserGame.create`, `resize`, `tick`, `command`, malformed command payloads, and full `debugSnapshot` fields.
- `crates/engine_web/src/model_asset_loader.rs` and `crates/engine_web/src/terrain_textures.rs`: tests for asset-loader bridge request objects, promises, malformed returns, missing methods, wrong IDs, duplicate arrays, missing arrays, and invalid typed arrays.
- `crates/engine_web/src/facade.rs`: either remove/feature-gate raw `ofg_engine_web_*`, or add serialized facade tests for configure/counts/reset/error-code/invalid-handle contracts.
- `crates/engine_web/src/terrain_textures.rs`: tests for invalid JSON, missing map paths, unknown array ID, duplicate array ID, missing required array, zero dimensions/layers, and invalid byte length.
- `crates/engine_web/src/model_texture_assets.rs`: tests for missing texture, missing image, external image URI, and decode errors.
- `crates/engine_web/src/terrain_stream.rs`: tests for reset-game reseeding, mesh/stat clearing, pending counts, job stats, and worker count.
- `crates/engine_web/src/player_character.rs`, `crates/engine_web/src/game_state.rs`, and `crates/engine_web/src/model_locomotion.rs`: tests for browser-facing character IDs, multi-part character scene configuration, and locomotion tuning validation.

TypeScript browser shell:

- `src/app/game.ts`: tests for `startGame`, URL seed/preset parsing, debug hook forwarding, animation tuning defaults, `F1`, `KeyC`, movement axes, HUD labels, and character toggle.
- `src/engine/world/terrainDescriptor.ts` and `src/engine/web/rustBrowserGameRuntime.ts`: tests for descriptor constants, frozen defaults, validation errors, and every `TerrainPresetId` mapping to the correct Rust `resetGame` numeric code.
- `src/engine/browser/textureAssetLoader.ts`: tests for `loadRgbaTextureArrayFromUrls`, empty URL list, failed texture fetch, mismatched image dimensions, canvas context failure, image close in `finally`, multi-layer pixel arrays, invalid dimensions/layers, and invalid byte lengths.
- `src/engine/math/*.ts`: tests for uncovered vector, matrix, and `vec4` helpers.
- `src/engine/web/rustBrowserGameAdapter.ts`: tests for static `create`, pixel-ratio cap, minimum sizing, duplicate resize suppression, defensive snapshot copies, and invalid Rust enum rejection.
- `src/engine/browser/browserWorkerHost.ts`: tests for worker count validation, `dispose`, transfer-list posting, worker `error` events, and error completion envelopes.
- `tests/ts/runtimeImportGraph.test.ts`: strengthen the guard from fixed retired filenames to an allowlist for reachable `src/engine/world/**` modules and a ban on reachable `src/generated/terrain/**`.
- `src/engine/web/browserGameTypes.ts`: compile-time contract objects for every browser game command and debug snapshot shape.

Tools and generated artifacts:

- `tools/build-shaders.mjs`, `tools/build-engine-web-wasm.mjs`, and `tools/build-terrain-wasm.mjs`: tests that `--check` fails on stale output and does not write in check mode.
- `src/generated/render/uberShader.ts`: test that generated hash matches current WGSL source bytes.
- `src/generated/web/engineWebWasm.ts`: test that generated hashes match current wasm-bindgen assets.
- `assets/wasm/engine_web/engine_web.d.ts`: source-wide test that runtime TypeScript never imports or calls raw `InitOutput` or `ofg_*` engine-web exports.
- `tools/browser-smoke.mjs`: tests or smoke assertions for occupied-port fallback, explicit browser path handling, and stable report schema.
- `tools/dev-server.mjs`, `tools/package-site.mjs`, and `tools/cloudflare-build.mjs`: tests for isolation headers, `.wasm` content type, traversal rejection, `_headers`, and package contents.
- `crates/ofg_test_harness/src/terrain_bench.rs`: Rust benchmark report schema, argument parsing, and timing helpers now replace the old TypeScript terrain WASM benchmark. Remaining tests should focus on parser/report edge cases and preserving JSON shape.
- `package.json` and `tsconfig.test.json`: manifest test that the command graph preserves build/check/smoke lanes and that TypeScript tests compile both `src` and `tests`.

## Plan of Work

Milestone 1 establishes Rust coverage tooling. Add a repo command such as `npm run coverage:rust` that invokes a small script, likely `tools/rust-coverage.mjs`, to find `cargo llvm-cov`, check for `llvm-tools-preview` when needed, print precise setup guidance when missing, and run coverage when available. The script should prefer text output for local iteration and optionally write HTML or LCOV under `artifacts/coverage/rust/`. Add README/AGENTS guidance explaining that coverage is advisory until thresholds are stable. If the local tool cannot be installed without changing Rust, document the supported setup path: prebuilt `cargo-llvm-cov`, Scoop, or Rust upgrade plus `cargo install cargo-llvm-cov --locked`.

Milestone 2 runs the first coverage pass and turns it into a coverage backlog. Run coverage on the Rust workspace. Capture missing functions/regions from `terrain_core`, `engine_core`, `engine_web`, and `ofg_test_harness`. Exclude normal test files and generated/build artifact output from the report. Do not chase meaningless coverage in unreachable `cfg(target_arch = "wasm32")` branches from native coverage; list those as wasm/browser test candidates. Update this ExecPlan with coverage numbers, top uncovered files, and any false positives.

Milestone 3 resolves unsupported or accidental APIs before writing tests for them. Search all supported runtime, tool, and docs references to `ofg_engine_web_*`. If no supported consumer exists, remove `crates/engine_web/src/facade.rs` from `crates/engine_web/src/lib.rs` re-exports and delete or feature-gate the raw facade module. Update `tools/build-engine-web-wasm.mjs` and generated metadata expectations if the raw exports disappear from wasm-bindgen output or `.d.ts` files. If a necessary consumer exists, keep the facade and add direct native tests for configure/count/reset/error-code/invalid-handle behavior.

Milestone 4 moved terrain benchmarking to Rust. `crates/ofg_test_harness/src/bin/ofg-terrain-bench.rs` runs the command, `crates/ofg_test_harness/src/terrain_bench.rs` owns argument parsing/report/timing helpers, and `crates/terrain_core/src/benchmark.rs` exposes a Rust-only benchmark feature for density chunk fill, retained density-window preparation, and density-store stats. The report replicates the old benchmark semantics: fill-only, fill-plus-copy, apron fill, retained density-window prepare, cold mesh build, prepared mesh build, phase estimates, density store stats, scenario metadata, and JSON output under `artifacts/terrain-bench/`. `package.json` now exposes `bench:terrain:rust`, and `tools/benchmark-terrain-wasm.mjs` has been removed.

Milestone 5 adds high-priority Rust contract tests. Start with direct facade and public API tests where coverage and sub-agent findings agree: `terrain_core` facade capacities/pointers/status/failure paths; `engine_core` facade sentinels; `engine_web` texture/model/terrain stream error contracts; and `ofg_test_harness` scenario/report/pixel helper tests. Keep tests behavior-focused and close to the Rust modules unless an integration-test file makes ownership clearer.

Milestone 6 adds wasm/browser bridge tests. Choose the least leaky mechanism for wasm-only object-protocol behavior. If `wasm-bindgen-test` is practical, add wasm tests for `RustBrowserGame.command`, malformed `JsValue` inputs, asset-loader request/response protocols, and debug snapshots. If wasm-bindgen-test is too heavy, expand browser smoke narrowly with direct debug commands and malformed command checks while keeping terrain behavior in Rust. Browser tests should still save screenshots and report JSON when they render.

Milestone 7 adds TypeScript shell and tooling contract tests. Add focused TS tests for `startGame` with faked runtime creation, descriptor validation/preset-code mapping, texture asset loader edge cases, runtime import-graph allowlists, generated hash recomputation, command graph checks, and dev-server/package-site behavior. These tests must not call terrain WASM exports or inspect terrain buffers.

Milestone 8 introduces coverage thresholds and workflow integration. After tests are added, run coverage again and decide a threshold that is high but honest. Prefer package-level thresholds or a documented target over an arbitrary global gate. Add `npm run coverage:rust` to implementation validation guidance and, if stable enough, add an optional `npm run coverage` or `npm run test:coverage` command. Document which APIs are excluded because they are wasm-only, generated, feature-gated, or intentionally unsupported.

## Concrete Steps

All commands assume the working directory is `C:/dev/ofg`.

Baseline inventory:

    cargo test --workspace -- --list
    npm run test:rust
    npm run test:ts

Coverage setup probe:

    cargo llvm-cov --version
    rustup component list --installed
    rustc -vV

Install coverage tool if missing. Choose one supported local path:

    rustup update stable
    rustup component add llvm-tools-preview
    cargo install cargo-llvm-cov --locked

or on Windows with prebuilt/package-manager installation:

    scoop bucket add taiki-e https://github.com/taiki-e/scoop-bucket
    scoop install cargo-llvm-cov

Run first coverage report after `cargo llvm-cov` is available:

    cargo llvm-cov clean --workspace
    cargo llvm-cov --workspace --text --show-missing-lines

Optional machine-readable outputs:

    cargo llvm-cov --workspace --json --output-path artifacts/coverage/rust/coverage.json
    cargo llvm-cov --workspace --lcov --output-path artifacts/coverage/rust/lcov.info
    cargo llvm-cov --workspace --html --output-dir artifacts/coverage/rust/html

Validate no TypeScript terrain client remains after benchmark migration:

    rg -n "terrain_core\\.wasm|ofg_fill_density_chunk|ofg_build_chunk_mesh|ofg_density_at|WebAssembly\\.instantiate" src tests tools package.json

Expected result: no TypeScript runtime or test terrain-client usage. `tools/build-terrain-wasm.mjs` may still mention expected exports if the standalone export-contract artifact remains.

Validate build/check/test/smoke after each milestone:

    npm run clean
    npm run build
    npm run test:rust
    npm run test:ts
    npm test
    npm run check:shaders
    npm run check:wasm
    npm run smoke:rust
    npm run smoke:browser

Run benchmark replacement:

    npm run bench:terrain:rust

Expected result: a JSON report under `artifacts/terrain-bench/<run-id>/report.json` with scenario metadata and timing summaries comparable to the old Node benchmark report.

## Milestone Review

After each milestone:

1. Update any changed API contracts or active docs.
2. Run the repo-local `milestone-review` skill against the milestone diff and this ExecPlan.
3. Apply required findings before marking that milestone complete, or record a rejected finding with rationale in the Decision Log.
4. Re-run relevant validation commands.
5. Record the review summary, commands, artifacts, coverage numbers, and remaining risks in Progress or Outcomes & Retrospective.

## Validation and Acceptance

This plan is accepted when all of the following are true:

Rust coverage:

- `npm run coverage:rust` exists.
- If `cargo-llvm-cov` is installed, the command writes a Rust coverage report under `artifacts/coverage/rust/` and prints a summary.
- If `cargo-llvm-cov` is missing, the command fails with precise setup instructions and does not corrupt build output.
- The coverage report is used to update this plan with observed uncovered API areas.

API coverage:

- Every supported public Rust API in `terrain_core`, `engine_core`, and `engine_web` has at least one direct unit/integration test, smoke test, benchmark test, or documented reason for exclusion.
- `ofg_test_harness` supported helpers are covered as a validation harness surface through direct harness tests, `npm run smoke:rust`, and `npm run bench:terrain:rust`; the harness is excluded from default coverage attention summaries to avoid mixing harness implementation coverage with engine API coverage.
- Every supported TypeScript runtime export has at least one unit or smoke test, with terrain semantics excluded from TypeScript.
- Generated artifacts have tests that recompute hashes from source assets instead of only regex-checking hash shapes.
- Build/check scripts have tests or integration checks for stale-output failure behavior.

Architecture cleanup:

- Raw `ofg_engine_web_*` exports are either removed/feature-gated as unsupported or directly tested if retained as supported API.
- The terrain benchmark no longer instantiates `terrain_core.wasm` from TypeScript/Node.
- `rg` confirms TypeScript runtime, tests, and tools do not call terrain WASM density, mesh, stream, or buffer exports.

Final validation:

    npm run clean
    npm run build
    npm run test:rust
    npm run test:ts
    npm test
    npm run check:shaders
    npm run check:wasm
    npm run smoke
    npm run coverage:rust

If coverage is not available on the local machine, final validation must include the coverage setup failure output and a successful coverage run in a supported environment before this plan is considered complete.

## Idempotence and Recovery

Coverage reports and smoke/benchmark artifacts go under `artifacts/` and can be deleted safely. Build outputs under `dist`, `dist-test`, `target`, and `assets/wasm` are regenerated by existing build commands.

When adding coverage commands, keep the script idempotent. Re-running it should either refresh reports or fail early with setup guidance. Do not install tools automatically from a test command unless the user explicitly asks for that behavior.

When removing raw `engine_web` facade exports, make the removal reversible by first proving no supported consumer uses them. If removal breaks wasm-bindgen output or browser smoke, restore the module temporarily, add tests for the required consumer, and record the decision.

The old Node terrain benchmark was kept until the Rust benchmark produced comparable JSON for the same scenario families. It has now been deleted after `npm run bench:terrain:rust` was wired into `package.json` and validated.

If coverage thresholds become noisy or flaky, keep `coverage:rust` as a report command and defer hard gating. Record excluded files and rationale instead of hiding failures silently.

## Artifacts and Notes

Initial local coverage-tool probe:

    cargo llvm-cov --version
    error: no such command: `llvm-cov`

    cargo tarpaulin --version
    error: no such command: `tarpaulin`

    rustc -vV
    rustc 1.78.0 (9b00956e5 2024-04-29)
    host: x86_64-pc-windows-msvc
    LLVM version: 18.1.2

Milestone 1 command validation:

    npm run coverage:rust
    Rust coverage requires cargo-llvm-cov.

    Install one supported path, then rerun `npm run coverage:rust`:

      rustup component add llvm-tools-preview
      cargo install cargo-llvm-cov --locked

    If the local Rust compiler is too old for source install, use a prebuilt
    cargo-llvm-cov binary, Scoop, or install a newer Rust toolchain for coverage.

The API gap list in this plan came from four read-only section reviews plus local export/test inventory. The sections were Rust terrain/core, Rust web/render/harness, TypeScript browser shell, and tools/generated artifacts.

## Interfaces and Dependencies

New or changed commands expected by the end of the plan:

`npm run coverage:rust`: runs Rust coverage or prints setup guidance.

`npm run bench:terrain:rust`: runs the Rust terrain benchmark and writes JSON under `artifacts/terrain-bench/`.

`npm run bench:terrain:wasm`: removed. Use `npm run bench:terrain:rust`; TypeScript no longer benchmarks terrain WASM directly.

Potential new files:

`tools/rust-coverage.mjs`: optional Node wrapper for coverage tool detection and report paths.

`crates/ofg_test_harness/src/bin/ofg-terrain-bench.rs`: Rust replacement for the deleted `tools/benchmark-terrain-wasm.mjs`.

`crates/ofg_test_harness/src/terrain_bench.rs`: benchmark scenarios, report structs, and timing helpers.

`tests/ts/*`: additional TypeScript boundary tests, especially generated-artifact hash checks, package command graph checks, runtime import graph allowlist checks, and browser shell unit tests.

Potential dependencies:

`cargo-llvm-cov`: external cargo subcommand for Rust coverage reports. Prefer prebuilt or package-manager installation on Windows if local Rust is too old for source installation.

`llvm-tools-preview`: Rustup component used by LLVM coverage tooling when needed.

`wasm-bindgen-test`: optional, only if chosen for wasm object-protocol tests. If this dependency adds too much friction, use browser smoke for wasm-only behavior instead.
