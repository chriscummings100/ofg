# Terrain Variant Descriptor And Editor Preview

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

Maintain this document in accordance with `PLANS.md`.

## Purpose / Big Picture

The goal is to make OFG's existing Rust terrain generator tunable through editor tools without moving terrain ownership back into TypeScript. After this work, a developer can create and adjust a list of terrain shape presets, preview the active draft at the world origin in front of the player, press Enter or use an explicit apply gesture to regenerate terrain immediately, and export/import the tuned preset list for later curation.

This plan intentionally targets terrain shape and material-bias tuning first. A shape preset is a deterministic set of generator parameters for broad landform geometry: base height, relief scale, ridge strength, domain warp, cellular breakup, and local detail. Biomes remain a separate future layer. The expected long-term composition is:

World = seed + terrain shape preset + climate/biome layer + material palette + local feature modifiers.

In that model, terrain shape presets define general ground form, while biomes use climate, altitude, slope, wetness, and future hydrology to choose materials, props, water, and small local modifications. Shape presets may later expose biome-aware modulation hooks, but v1 must not make every shape preset a hard-coded biome.

## Progress

- [x] (2026-06-10) Created this ExecPlan from the terrain generator/editor requirements.
- [x] (2026-06-10) Milestone 1: Added a Rust-owned terrain variant descriptor, validation, shape-preset catalog helpers, and generated browser preset metadata.
- [x] (2026-06-10) Milestone 2: Routed variant descriptors through the Rust browser game reset, worker-build, stream revision, stale-completion rejection, and debug snapshot paths.
- [x] (2026-06-10) Milestone 3: Added an in-browser terrain variant editor panel with draft duplication/reset, numeric field editing, import/export, instant regeneration, and origin preview.
- [x] (2026-06-10) Milestone 4: Added Rust-owned origin probe readouts, custom edited-variant smoke coverage, and validation coverage for descriptor routing.
- [x] (2026-06-10) Milestone 5: Updated active API/architecture docs and recorded the shape-preset versus biome handoff.
- [x] (2026-06-10) Follow-up tuning: Retuned built-in shape presets to meter-scale landform wavelengths and verified them with Rust terrain tests, TypeScript/wasm build tests, and terrain preset smoke.

## Surprises & Discoveries

- Observation: The current generator already has useful shape knobs, but they are private constants in `crates/terrain_core/src/presets.rs`.
  Evidence: `TerrainPresetDefinition` includes `base_height`, `height_scale`, `large_feature_noise`, `ridge_height_scale`, `ridge_noise`, `warp`, `cellular_height_scale`, `detail_noise`, and `detail_amplitude`.
- Observation: The browser descriptor already names `seaLevel`, `climatePreset`, and `materialPalette`, but runtime reset currently sends only `terrainSeed` and numeric `terrainPreset`.
  Evidence: `src/engine/web/rustBrowserGameRuntime.ts` maps `descriptor.terrainPreset` to a WASM code in the `resetGame` command.
- Observation: Browser worker terrain builds currently call `terrain_core.wasm` with only seed, preset code, chunk coordinate, and cell size.
  Evidence: `src/engine/web/terrainBuildWorker.ts` calls `ofg_build_chunk_mesh(request.seed, request.preset, request.x, request.y, request.z, request.cellSize)`.
- Observation: Milestone 1 can expose catalog metadata without adding new runtime WASM calls.
  Evidence: `tools/build-terrain-preset-metadata.mjs` reads Rust `TERRAIN_PRESET_METADATA` and emits `src/generated/world/terrainPresets.ts`; `src/engine/world/terrainDescriptor.ts` and `src/engine/web/rustBrowserGameRuntime.ts` consume that generated artifact.
- Observation: Native Rust tests did not catch one wasm-target error path because `TerrainVariantValidationError` was formatted through the wasm JS error helper only during the release wasm build.
  Evidence: the first `npm run build:wasm` failed until `TerrainVariantValidationError` gained stable `Display` and `Error` impls plus a display-text regression test.
- Observation: The editor can stay responsive without a new terrain API surface by treating `resetGame` with a Rust flat descriptor as the one command lane.
  Evidence: in the in-app browser pass, clicking Apply changed the editor status from `rev 2 | ready` to `rev 3 | pending`, and clicking Origin changed it to `rev 4 | pending` while the HUD camera mode read `FLY`.
- Observation: Stock preset smoke was not enough to prove edited descriptors render.
  Evidence: the Rust smoke harness now has a `variants` scenario filter with `variant-low-rolling` and `variant-ridge-heavy`; `cargo run -p ofg_test_harness --bin ofg-render-smoke -- --out artifacts/rust-smoke --scenario variants` passed and wrote `artifacts/rust-smoke/run-1781092200-073/report.json`.
- Observation: Adding the edited-variant smoke scenarios pushed the scenario module over the local split-pressure threshold.
  Evidence: moving the scenario data table to `crates/ofg_test_harness/src/render_smoke/scenario_catalog.rs` reduced `crates/ofg_test_harness/src/render_smoke/scenarios.rs` to 518 lines while keeping `cargo test -p ofg_test_harness render_smoke::scenarios --lib` green.
- Observation: The original built-in preset frequencies made most macro features land around 150-350 m, so the presets read like local texture rather than landscape-scale terrain.
  Evidence: `crates/terrain_core/src/presets.rs` now documents frequencies as cycles per meter and guards the built-in preset wavelengths with `terrain_preset_wavelengths_are_landform_scaled`.

## Decision Log

- Decision: Build a terrain shape preset editor first, not a full biome/world editor.
  Rationale: The existing generator can produce useful rolling, ridged, warped, rocky, and highland variants, but it does not yet have rivers, water, caves, erosion, vegetation, or a real climate solver. Shape tuning will expose the generator's current expressive range and make the missing feature layer clearer.
  Date/Author: 2026-06-10 / Codex.
- Decision: Keep TypeScript as editor UI and command forwarding only.
  Rationale: `docs/API_CONTRACTS.md` forbids TypeScript terrain generation, density sampling, scheduling, material assignment, and mesh ownership. Editor controls may send validated packets and display Rust debug snapshots, but Rust must own interpretation and regeneration.
  Date/Author: 2026-06-10 / Codex.
- Decision: Treat "make the preset I am working on the one at [0,0,0]" as an origin preview command in v1.
  Rationale: The current runtime has one global terrain preset at a time, not spatially mixed preset regions. V1 can satisfy the user workflow by applying the active draft globally, resetting terrain around the origin, and moving the player/debug camera to an origin-facing preview. A future biome/region mixer can turn this into spatial assignment.
  Date/Author: 2026-06-10 / Codex.
- Decision: Regeneration must happen without page reload.
  Rationale: Tuning requires a tight feedback loop. Applying a numeric field should invalidate stale terrain work, clear current terrain meshes, and schedule new Rust-owned terrain nodes immediately through the existing stream path.
  Date/Author: 2026-06-10 / Codex.
- Decision: Biomes should mix with shape presets as a later orthogonal layer.
  Rationale: Shape presets define geometry archetypes. Biomes/climate should consume terrain fields and add material, wetness, water, vegetation, and local modifiers. Keeping them separate avoids turning each shape preset into a brittle all-in-one world type.
  Date/Author: 2026-06-10 / Codex.
- Decision: Generate browser preset IDs and Rust reset codes from Rust catalog metadata.
  Rationale: `terrain_core` must remain the source of truth for runtime preset interpretation, but TypeScript needs stable URL/debug IDs and numeric reset codes. Generating `src/generated/world/terrainPresets.ts` from `crates/terrain_core/src/variant.rs` removes the duplicated browser-side mapping tables before adding editor catalogs.
  Date/Author: 2026-06-10 / Codex.
- Decision: Add neutral material-bias fields to the descriptor now, but do not change material classification until the descriptor is routed through runtime builds.
  Rationale: The editor needs an explicit contract for future material tuning, while Milestone 1 should preserve current terrain output and keep material behavior unchanged.
  Date/Author: 2026-06-10 / Codex.
- Decision: Route custom variants as a fixed Rust flat `f64` descriptor block rather than JSON in the worker path.
  Rationale: Browser workers already copy numeric build inputs into `terrain_core.wasm`. A fixed layout lets TypeScript remain a transport/editor shell while Rust validates the descriptor and owns height, density, material, and mesh interpretation.
  Date/Author: 2026-06-10 / Codex.
- Decision: Treat numeric editor inputs as committed on Enter or native `change`; v1 does not add sliders.
  Rationale: This satisfies the instant-regeneration loop without flooding the terrain stream on every keystroke. Apply and Origin use the same Rust reset command lane, so every committed edit invalidates stale terrain work without a page reload.
  Date/Author: 2026-06-10 / Codex.
- Decision: Keep origin preview as a global descriptor reset plus debug-fly camera/player repositioning.
  Rationale: The runtime still has one active terrain descriptor, not spatial terrain-variant regions. Applying the draft globally and moving the camera to an origin-facing preview directly supports the requested `[0,0,0]` workflow while leaving future spatial mixing to a biome/region layer.
  Date/Author: 2026-06-10 / Codex.
- Decision: Add edited terrain variants to the Rust image smoke harness as a separate `variants` scenario group.
  Rationale: Original preset smoke proves catalog compatibility, but it does not prove custom descriptor edits render through the stream/mesh path. A focused group gives cheap visual coverage for one low rolling and one ridge-heavy tuned descriptor.
  Date/Author: 2026-06-10 / Codex.
- Decision: Retune built-in presets around real-world horizontal landform scales while keeping vertical relief conservative for the current terrain shell.
  Rationale: Rolling-hill and glacial lowland forms are plausibly hundreds of meters to a couple kilometers across, while mountain valleys need kilometer-scale structure. The current runtime still has a narrow practical surface-search band and no far-field erosion/hydrology layer, so the built-in defaults should fix miniature horizontal scale now without pretending to support full alpine relief yet.
  Date/Author: 2026-06-10 / Codex.

## Outcomes & Retrospective

Milestone 1 established the descriptor foundation without changing terrain output. `terrain_core` now exposes `TerrainVariantDescriptor`, `TerrainShapeParameters`, `TerrainMaterialBias`, validation, preset metadata, and catalog descriptor helpers. The original numeric preset path remains compatible, and browser preset IDs/codes now come from generated metadata instead of local duplicate maps.

Milestone 2 made custom descriptors real runtime inputs. `engine_web` stores the active descriptor and variant revision, reset commands can carry a Rust flat terrain descriptor, player grounding and terrain builds sample through the descriptor, browser workers copy the descriptor into `terrain_core.wasm`, and Rust rejects worker completions whose variant revision is stale. The descriptor cache key now prevents old density chunks from being reused across tuned variants.

Milestone 3 delivered the browser editor workflow. The `TER` panel lets the developer select catalog drafts, duplicate and rename a draft, edit shape/material-bias numeric fields, Apply, Reset, Preview Origin, and import/export JSON. Apply and Enter/change commits regenerate terrain through the Rust command lane without page reload. Origin preview applies the draft, switches to debug fly, and points the preview at world origin.

Milestone 4 made tuning less blind. Rust debug snapshots now include the active terrain variant, variant revision, Rust preset catalog descriptors, and an origin probe summary with height, slope, macro, material, and biome-weight readouts. The Rust smoke harness renders two edited descriptors, `variant-low-rolling` and `variant-ridge-heavy`, alongside the stock preset/seam/LOD smoke coverage.

Milestone 5 updated the active contracts and architecture docs. The editor is explicitly a shape-preset tool, not a biome editor. Biomes, climate, hydrology, water, vegetation, prop placement, material palettes, and local feature modifiers remain future Rust-owned layers that can compose with shape presets later.

Remaining gaps are future terrain features, not unfinished acceptance for this plan: no spatial variant-region mixer, no real biome/climate/water controls, no hydrology/rivers/caves/erosion, and material-bias fields are descriptor-ready but still intentionally conservative until Rust material tuning is designed.

Follow-up preset tuning made the built-in catalog a better starting point for editor work. The stock frequencies now use meter-scale defaults: broad seed/rolling forms are roughly 625-950 m, mountain valley macro form is roughly 1.8 km with ridge structure around 950 m, and rocky highland combines roughly 1 km massing with 50-200 m roughness. A sampled 4 km x 4 km grid from `assets/wasm/terrain_core.wasm` reports about 40 m of relief for Rolling Hills, about 68 m for Mountain Valley, and about 63 m for Rocky Highland; larger true-mountain relief remains a future vertical-shell and far-field terrain feature rather than a preset-only change. The exact current numbers, real-world scale rationale, constrained fields, and future target ranges are recorded in `docs/TERRAIN_PRESET_SCALE.md`.

## Contract and Quality Baseline

This plan preserves and intentionally extends these contracts:

- `OFG-API-001`: Browser shell to Rust browser game remains the supported runtime boundary. New editor behavior must be added through `GameCommand`, create/reset payloads, and `debugSnapshot()`, not new public raw WASM methods on the playable path.
- `OFG-API-003`: Debug hooks may display Rust-owned terrain preset, variant, probe, and stream data. They must not compute terrain state or material selection in TypeScript.
- `OFG-API-004`: Terrain vertex layout remains 19 floats per vertex. This plan should not change position, color, normal, UV, material-index, or material-weight layout unless a later milestone explicitly updates every Rust and WGSL site.
- `OFG-API-005`: Terrain presets and world descriptor codes are currently duplicated. This plan should reduce that risk by introducing generated or shared preset metadata before adding more catalog entries.
- `OFG-API-006`: The standalone `terrain_core.wasm` artifact remains fixture and worker-build implementation only. If new worker exports are added for custom variant descriptors, TypeScript may call them only inside `src/engine/web/terrainBuildWorker.ts` to fulfill Rust-issued opaque build requests.
- `OFG-API-009`: TypeScript must not become terrain generator, density sampler, scheduler, mesh builder, material classifier, or renderer.

Before marking this plan complete, run the default Rust coverage attention gate:

    npm run coverage:rust

The plan is complete only when changed Rust implementation files do not appear in the default filtered coverage output, or this plan records an explicit exception with rationale.

## Context and Orientation

The current terrain generator is Rust-owned in `crates/terrain_core`. `crates/terrain_core/src/presets.rs` defines four numeric presets. `crates/terrain_core/src/field.rs` builds macro terrain from simplex-style fractal noise, ridged noise, domain warp, cellular breakup, and local 3D detail noise. `crates/terrain_core/src/material.rs` classifies materials from slope, altitude, macro fields, and lightweight biome weights. `crates/terrain_core/src/mesh.rs` builds Dual Contouring meshes from density chunks.

The browser game runtime is Rust-owned in `crates/engine_web`. `crates/engine_web/src/terrain_stream.rs` owns the browser terrain stream and default LOD0 through LOD4 bands. `crates/engine_web/src/wgpu_renderer.rs` owns the wasm-bindgen facade, command parsing, terrain mesh upload/pruning, renderer resources, and debug snapshot conversion.

The TypeScript browser shell lives in `src/app` and `src/engine/web`. `src/engine/world/terrainDescriptor.ts` currently exposes URL-facing seed and preset IDs, plus placeholder `seaLevel`, `climatePreset`, and `materialPalette`. `src/engine/web/rustBrowserGameRuntime.ts` sends `resetGame` with `terrainSeed` and numeric `terrainPreset`. `src/engine/web/terrainBuildWorker.ts` is the only compiled TypeScript path allowed to call raw `terrain_core.wasm` terrain mesh exports, and only for Rust-issued worker requests.

Definitions for this plan:

- Terrain shape preset: a named parameter set that controls broad landform geometry and simple material biases.
- Terrain variant descriptor: the validated runtime packet Rust uses to sample height, density, materials, stream keys, and worker builds.
- Origin preview: an editor action that applies the active draft descriptor, clears/rebuilds terrain, and places the player/debug camera at or near world coordinate `[0, 0, 0]` so the tuned terrain appears in front of the user.
- Instant regeneration: a no-reload terrain reset triggered by pressing Enter in a numeric field, changing a committed select value, or clicking Apply. Existing worker completions from the old descriptor must become stale.
- Biome layer: a future climate/material/wetness/prop system that consumes terrain shape outputs but is not part of v1 shape preset tuning.

## Plan of Work

Milestone 1 introduces a Rust-owned terrain variant descriptor. Add focused terrain config types in `crates/terrain_core`, likely in a new `src/variant.rs` module, and re-export only the safe public pieces from `src/lib.rs`. The descriptor should include a stable version, optional catalog ID, shape parameters derived from the current four presets, material bias parameters for existing heuristic material classification, and validation that rejects NaN, infinities, invalid octave counts, non-positive frequencies, non-positive lacunarity, negative amplitudes where they would break generation, and out-of-range catalog codes. Refactor `terrain_preset(preset)` into a compatibility wrapper around `terrain_variant_for_preset(preset)` so current calls continue to work. Add tests proving current preset codes still produce deterministic finite height/density samples and meaningfully different surfaces.

Milestone 1 should also introduce a single metadata path for the browser preset list before adding new preset names. Prefer a checked-in generated TypeScript artifact under `src/generated/world/` or a small shared manifest generated by a tool. The source of truth for runtime interpretation must remain Rust-owned. Acceptance for this milestone is that adding a new shape preset would not require manually editing three unrelated mapping tables.

Milestone 2 routes the descriptor through runtime reset and worker builds. Extend `src/engine/web/browserGameTypes.ts` with a terrain variant command shape, for example an optional `terrainVariant` object on `resetGame` or a new `previewTerrainVariant` / `setTerrainVariant` command. Extend `crates/engine_web/src/wgpu_renderer.rs` command parsing to validate the descriptor, update `BrowserGameState`, clear terrain meshes, invalidate stale stream work, and reset `BrowserTerrainStream` with a descriptor revision. Extend `crates/engine_web/src/terrain_stream.rs` so every `BrowserTerrainBuildRequest` carries the active descriptor or an opaque flat parameter block plus a revision number. Worker completions must be rejected if request generation, node key, or variant revision no longer matches.

Milestone 2 must update `src/engine/web/terrainBuildWorker.ts` and `crates/terrain_core/src/facade.rs` so browser workers can build custom descriptors, not only preset codes. Keep TypeScript generic: it may pass a Rust-authored request payload to `terrain_core.wasm`, but it must not interpret shape parameters, classify materials, schedule nodes, or choose visibility. A simple flat numeric buffer is acceptable if it avoids adding JSON parsing to `terrain_core`. Update `tools/build-terrain-wasm.mjs` export checks if new fixture exports are required.

Milestone 3 builds the editor workflow. Add a terrain editor panel in `src/app`, preferably as a separate module such as `src/app/terrainVariantEditor.ts` rather than growing `renderDebugUi.ts` too far. The UI should expose a preset catalog, duplicate/rename draft actions, numeric inputs for the descriptor fields, Apply, Reset Draft, Preview At Origin, Import JSON, and Export JSON. The user must be able to select the draft they are working on and make it the active terrain at `[0,0,0]`. In v1 this means applying it globally and moving/reorienting the player or debug camera to the origin preview location. Future spatial mixing can reuse the same intent but assign a variant region at the origin instead.

Milestone 3 must support instant regeneration on committed edits. Pressing Enter in a numeric input or changing a select should call the Rust command lane, increment the Rust terrain generation or variant revision, clear old terrain meshes, and start streaming new terrain without a page reload. For sliders, either commit on pointer release/change or use a short debounce that still feels immediate; record the chosen behavior in the Decision Log. The editor should show the active descriptor ID/revision and terrain stream pending/rendered status from `debugSnapshot()`.

Milestone 4 adds probe/debug data so tuning is not guesswork. Add Rust-owned debug snapshot fields or a debug command that reports samples around the origin: height range, slope range, macro elevation, mountainness, ridge, cellular edge, material top layers, and biome weights if they are already available. TypeScript may display these values as tables or small summaries, but must not sample density or infer materials itself. Add Rust tests for descriptor probes and TypeScript tests for UI validation/display.

Milestone 4 should also add native smoke or harness coverage for at least two edited descriptors: one ridge-heavy variant and one low rolling variant. Existing `npm run smoke:terrain-presets` should continue to cover the original catalog presets. If custom variants change generation cost, run `npm run bench:terrain:rust` and record report paths.

Milestone 5 documents the biome handoff. Update `docs/API_CONTRACTS.md`, `docs/ARCHITECTURE.md`, and this plan to explain that shape presets are geometry templates, while biomes/climate/material palettes remain Rust-owned future layers. If placeholder `seaLevel`, `climatePreset`, or `materialPalette` are touched, either wire them to real Rust-owned behavior or clearly keep them descriptor placeholders. Do not expose fake controls that imply working biomes, water, or climate if those systems are not implemented.

## Concrete Steps

All commands use working directory `C:\dev\ofg`.

Before implementation:

    git -c safe.directory=C:/dev/ofg status --short
    npm run test:rust

After Milestone 1:

    cargo test -p terrain_core
    npm run test:rust

Expected result: terrain descriptor/variant tests pass, current preset compatibility tests pass, and no finite-sample or mesh-buffer regressions occur.

After Milestone 2:

    cargo test -p terrain_core
    cargo test -p engine_web
    npm run check:wasm
    npm run test:ts

Expected result: generated WASM export checks pass, worker request/completion tests cover variant revisions, and TypeScript still routes worker payloads without terrain ownership.

After Milestone 3:

    npm run test:ts
    npm run smoke:browser

Expected result: browser smoke still boots, renders nonblank terrain, reports Rust ownership sentinels, and editor-specific tests prove Enter/apply sends the variant command and preview-at-origin uses the Rust command lane.

After Milestone 4:

    npm run smoke:terrain-presets
    npm run smoke:rust
    npm run bench:terrain:rust

Expected result: original presets still render, custom tuned descriptors render through Rust smoke/harness coverage, and benchmark reports record no unacceptable terrain generation regression. Any regression must be recorded with exact report path and rationale.

Before completing the plan:

    npm test
    npm run coverage:rust
    npm run smoke:browser

Expected result: all tests pass, changed Rust implementation files do not appear in the default filtered coverage attention report, browser smoke passes with editor code present, and generated artifacts are either checked in intentionally or left clean according to repo policy.

## Milestone Review

After each milestone:

1. Update this ExecPlan's Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective.
2. Update `docs/API_CONTRACTS.md` and `docs/ARCHITECTURE.md` if commands, descriptor fields, worker payloads, debug snapshots, or ownership boundaries changed.
3. Run the repo-local `milestone-review` skill against the milestone diff and this ExecPlan.
4. Apply required findings before marking the milestone complete, or record a rejected finding with rationale in the Decision Log.
5. Re-run the relevant validation commands and record concise evidence here.

## Validation and Acceptance

The plan is accepted when these behaviors are observable:

- The current four terrain presets still exist, keep stable browser IDs, and can be selected from URL/debug/editor paths.
- A new editable terrain variant descriptor can be created from an existing preset and validated in Rust.
- Pressing Enter or applying a numeric terrain parameter change regenerates terrain without page reload. The Rust terrain stream generation or variant revision changes, stale worker completions are rejected, old terrain meshes are cleared or pruned, and new worker builds produce visible terrain.
- The editor can make the active draft "the one at `[0,0,0]`" by applying the draft and moving/reorienting the player or debug camera to an origin preview. The tuned terrain appears in front of the user and `debugSnapshot()` reports the active descriptor/revision.
- TypeScript editor code never samples density, builds terrain meshes, classifies materials, computes desired terrain nodes, or chooses terrain visibility.
- Rust probe/debug output exposes enough information to tune shape intentionally: height, slope, macro/ridge/cellular signals, material top layers, and stream status around the preview area.
- Original preset, seam, Rust smoke, browser smoke, and coverage gates pass or record explicit accepted exceptions.

## Idempotence and Recovery

The descriptor migration should keep numeric preset compatibility until the editor path is stable. If custom descriptors fail in browser workers, fall back by disabling custom descriptor commands while retaining the existing numeric preset path. If the editor UI causes smoke instability, keep the Rust descriptor and command tests but hide the panel behind a debug flag until browser smoke is stable. If worker payloads become too complex, use a flat numeric parameter buffer owned by Rust and copied opaquely by TypeScript rather than adding semantic parsing in the browser shell.

Do not use `git reset --hard` or revert unrelated user changes. Generated WASM and shader artifacts should only be regenerated by the documented npm scripts and should be included only when the relevant build script expects checked-in output changes.

## Artifacts and Notes

Expected artifact locations:

- Browser smoke screenshots and reports under `artifacts/browser-smoke/`.
- Rust image smoke screenshots and reports under `artifacts/rust-smoke/`.
- Terrain benchmark reports under `artifacts/terrain-bench/`.
- Rust coverage summaries under `artifacts/coverage/rust/`.

When implementation begins, paste concise evidence here after each milestone: command names, pass/fail summary, artifact paths, and any important timings or screenshots.

Milestone 1 evidence:

- `cargo test -p terrain_core terrain_variant --lib`: passed, 3 terrain variant tests.
- `cargo test -p terrain_core --lib`: passed, 50 tests.
- `node tools/build-terrain-preset-metadata.mjs --check`: passed, generated preset metadata is fresh.
- `tsc -p tsconfig.app.json`: passed.
- `npm run test:rust`: passed, Rust workspace tests. Existing `engine_web` dead-code warnings in `terrain_stream.rs` remain unrelated to this milestone.

Milestone review:

- Scope: Rust descriptor/catalog validation, generated TypeScript terrain preset metadata, and browser preset consumers.
- Reviewers: local contract, code quality, legacy, correctness, and validation passes. Sub-agents were not used because the available delegation tool requires an explicit user request for sub-agents.
- Required findings fixed: browser preset metadata was still duplicated after the first descriptor patch; fixed by generating `src/generated/world/terrainPresets.ts` from Rust metadata and routing TypeScript consumers through it.
- Follow-ups recorded: custom descriptor runtime routing, worker request payloads, origin preview, probes, smoke/coverage gates remain in later milestones.
- Rejected findings: none.
- Validation rerun: `node tools/build-terrain-preset-metadata.mjs --check`, `tsc -p tsconfig.app.json`, `cargo test -p terrain_core --lib`, and `npm run test:rust`.
- Remaining risk: Milestone 1 validates and catalogs shape descriptors but does not yet let the browser apply a custom descriptor; that starts in Milestone 2.

Milestone 2 evidence:

- `cargo test -p terrain_core --lib`: passed, including descriptor cache-key and flat-buffer mesh facade coverage.
- `cargo test -p engine_web --lib`: passed, including custom terrain variant reset height and stale variant-revision completion rejection.
- `npm run build:wasm`: initially failed because `TerrainVariantValidationError` lacked `Display` on the wasm target; fixed with stable display text and reran successfully.
- `npm run check:wasm`: passed after refreshing `assets/wasm/terrain_core.wasm`, `assets/wasm/engine_web/*`, and `src/generated/web/engineWebWasm.ts`.
- `npm run test:ts`: passed, rebuilding generated terrain preset metadata, shaders, and WASM artifacts.

Milestone review:

- Scope: descriptor routing through `BrowserGameState`, `BrowserTerrainStream`, wasm reset command parsing, worker request/completion packets, standalone `terrain_core.wasm` fixture exports, density-store cache keys, debug snapshot fields, and generated WASM artifacts.
- Reviewers: local contract, code quality, legacy, correctness, and validation passes. Sub-agents were not used because the available delegation tool requires an explicit user request for sub-agents.
- Required findings fixed: added stable `Display`/`Error` output for `TerrainVariantValidationError` after the wasm build exposed the missing bound; updated `docs/API_CONTRACTS.md` for optional `resetGame.terrainVariant`, variant revision/debug fields, generated preset metadata, and worker-only flat descriptor exports.
- Follow-ups recorded: `crates/terrain_core/src/facade.rs` and `crates/engine_web/src/wgpu_renderer.rs` remain pre-existing oversized boundary files; future broadening should split focused facade/renderer modules rather than continuing to grow them.
- Rejected findings: none.
- Validation rerun: `cargo fmt`, `cargo test -p terrain_core --lib`, `cargo test -p engine_web --lib`, `npm run build:wasm`, `npm run check:wasm`, and `npm run test:ts`.
- Remaining risk: the runtime accepted descriptors, but the user-facing editor workflow and probe readouts belonged to Milestones 3 and 4.

Milestone 3 evidence:

- `npm run test:ts`: passed with `terrainVariantEditor` helper coverage for field-index mapping, immutable updates, integer octave clamping, and invalid descriptor lengths.
- `npm run smoke:browser`: passed and wrote screenshots/report under `artifacts/browser-smoke/2026-06-10T11-40-58-233Z`.
- In-app browser verification against `http://127.0.0.1:5175`: opening the `TER` panel showed the draft controls and live status; clicking Apply changed status from `rev 2 | ready` to `rev 3 | pending`; clicking Origin changed status from `rev 3 | ready` to `rev 4 | pending` and the HUD camera mode to `FLY`.

Milestone review:

- Scope: `index.html` editor panel markup, `src/app/terrainVariantEditor.ts`, app startup/debug hook wiring, styles, main element lookup, and TypeScript editor tests.
- Reviewers: local contract, code quality, legacy, correctness, and validation passes. Sub-agents were not used because the available delegation tool requires an explicit user request for sub-agents.
- Required findings fixed: kept the editor in `src/app` as UI/command forwarding only; verified it does not sample density, build meshes, classify materials, choose visibility, or call raw terrain WASM; verified the explicit origin preview path applies through Rust commands and moves to debug fly.
- Follow-ups recorded: editor field schema is still manually mirrored against the Rust flat descriptor layout; generate an editor field schema if the descriptor grows beyond this first tool.
- Rejected findings: no rejected findings.
- Validation rerun: `npm run test:ts`, `npm run smoke:browser`, and direct in-app browser Apply/Origin checks.
- Remaining risk: v1 editor uses committed numeric inputs, not continuous sliders. That is intentional for a stable first tuning loop.

Milestone 4 evidence:

- `cargo test -p terrain_core terrain_variant_probe --lib`: passed after adding origin probe summaries.
- `cargo test -p ofg_test_harness render_smoke::scenarios --lib`: passed after adding and then splitting custom variant smoke scenarios.
- `cargo run -p ofg_test_harness --bin ofg-render-smoke -- --out artifacts/rust-smoke --scenario variants`: passed after the final smoke catalog split and wrote `artifacts/rust-smoke/run-1781092200-073/report.json`.
- `npm run smoke:terrain-presets`: passed and wrote `artifacts/rust-smoke/run-1781089817-864/report.json`.
- `npm run smoke:terrain-seams`: passed and wrote `artifacts/rust-smoke/run-1781089842-037/report.json`.
- `npm run smoke:rust`: passed and wrote `artifacts/rust-smoke/run-1781091332-015/report.json`, including the custom variant images before the catalog split.
- `npm run bench:terrain:rust`: passed and wrote `artifacts/terrain-bench/run-1781090806-238/report.json`; the multi-LOD probe rendered 347 nodes, reached max LOD 4, and spanned 4608m by 4608m.
- `npm run coverage:rust`: passed; the filtered output listed no implementation files below the 90% line-coverage attention threshold and wrote summaries under `artifacts/coverage/rust/`.

Milestone review:

- Scope: Rust terrain variant probe summaries, debug snapshot probe conversion, browser snapshot typing/copying, editor status display, custom variant render smoke scenarios, and terrain benchmark/coverage evidence.
- Reviewers: local contract, code quality, legacy, correctness, and validation passes. Sub-agents were not used because the available delegation tool requires an explicit user request for sub-agents.
- Required findings fixed: added edited custom descriptor visual coverage after the first validation set only covered stock presets; split `crates/ofg_test_harness/src/render_smoke/scenario_catalog.rs` out of `scenarios.rs`, reducing `scenarios.rs` to 518 lines; reran focused scenario tests and variant smoke.
- Follow-ups recorded: `crates/engine_web/src/wgpu_renderer.rs`, `crates/engine_web/src/tests.rs`, `crates/terrain_core/src/tests.rs`, and `crates/terrain_core/src/facade.rs` remain oversized files with pre-existing split pressure. Keep future terrain/editor expansion out of those files where practical.
- Rejected findings: no rejected findings.
- Validation rerun: `cargo test -p terrain_core terrain_variant_probe --lib`, `cargo test -p ofg_test_harness render_smoke::scenarios --lib`, `cargo run -p ofg_test_harness --bin ofg-render-smoke -- --out artifacts/rust-smoke --scenario variants`, `npm run smoke:terrain-presets`, `npm run smoke:terrain-seams`, `npm run smoke:rust`, `npm run bench:terrain:rust`, and `npm run coverage:rust`.
- Remaining risk: probe readouts are scalar summaries around origin, not a map/graph. That is enough for first-pass tuning but future editor ergonomics should add visual probe plots or thumbnails.

Milestone 5 evidence:

- `docs/API_CONTRACTS.md` now documents optional flat `resetGame.terrainVariant`, terrain variant debug fields, Rust-generated preset metadata, worker-only flat descriptor exports, allowed TypeScript editor responsibilities, and the future biome handoff.
- `docs/ARCHITECTURE.md` now records Rust ownership of descriptor validation/probes/revisions, TypeScript editor UI limits, generated preset metadata, worker revision echoing, and shape presets as distinct from future biome/climate/material layers.
- Final generated-artifact checks passed: `npm run check:terrain-presets`, `npm run check:shaders`, and `npm run check:wasm`.
- Final combined test gate passed: `npm test`, including Rust workspace tests, regenerated TypeScript build artifacts, and 107 TypeScript tests.
- `git -c safe.directory=C:/dev/ofg diff --check`: passed with Windows line-ending warnings only.

Milestone review:

- Scope: active API/architecture docs, this ExecPlan, and final validation evidence.
- Reviewers: local contract, code quality, legacy, correctness, and validation passes. Sub-agents were not used because the available delegation tool requires an explicit user request for sub-agents.
- Required findings fixed: active docs initially still described preset maps as duplicated and reset as create-time-only; updated them to match the generated metadata and editor command reality.
- Follow-ups recorded: real biome/climate/hydrology/material-palette controls need their own Rust-owned plan and should not be faked in this editor.
- Rejected findings: no rejected findings.
- Validation rerun: docs diff review and `git -c safe.directory=C:/dev/ofg diff --check`.
- Remaining risk: none for the documented handoff; the biome systems themselves remain future work.

## Interfaces and Dependencies

Expected end-state interfaces may adjust during implementation, but should preserve this shape:

- `terrain_core` exposes Rust-owned `TerrainVariantDescriptor`, `TerrainShapeParameters`, validation helpers, preset catalog helpers, and mesh/height/density build functions that accept either preset codes or descriptors.
- `engine_web` stores the active descriptor and revision in `BrowserGameState` and `BrowserTerrainStream`.
- `BrowserTerrainBuildRequest` carries enough Rust-authored descriptor data for browser workers to build nodes for custom variants.
- `GameCommand` includes a way to set or preview the active terrain variant without adding raw public WASM methods.
- `debugSnapshot()` reports active terrain variant identity/revision and probe summaries needed by the editor.
- `src/app/terrainVariantEditor.ts` or equivalent implements only UI, validation of browser input shape, command forwarding, and debug display.
- Generated or shared preset metadata prevents future shape preset additions from requiring hand-edited Rust/TypeScript mapping drift.
