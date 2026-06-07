# Third-Person Character Camera

Archived note: Completed on 2026-06-07. Active source of truth moved to
`docs/API_CONTRACTS.md`, `docs/ARCHITECTURE.md`, and the Rust/TypeScript
camera-mode tests.

This ExecPlan is a living document. The sections Progress, Surprises &
Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as
work proceeds.

## Purpose / Big Picture

Add a Rust-owned third-person camera mode so the player can watch the imported
GLTF character walk while still using normal grounded player movement. Pressing
`C` or `F1` should cycle the browser HUD through `FIRST`, `THIRD`, and `FLY`.
`FIRST` remains body-hidden, `THIRD` shows the character from a chase camera,
and `FLY` remains the detached debug camera.

## Progress

- [x] (2026-06-07) Started the plan after the GLTF character milestone was
  complete and committed.
- [x] (2026-06-07) Added `PlayerMode::ThirdPerson` to Rust engine/player
  contracts with grounded movement and chase-camera placement.
- [x] (2026-06-07) Made the browser player character visible in third-person
  mode while keeping first-person body-hidden and debug-fly detached.
- [x] (2026-06-07) Updated TypeScript mode names, HUD labels, docs, and smoke
  tests for the `FIRST -> THIRD -> FLY -> FIRST` cycle.
- [x] (2026-06-07) Ran milestone review and final validation, then archived
  this plan for commit.

## Surprises & Discoveries

- Observation: The browser already has the correct character scene item; the
  missing piece is only camera mode ownership.
  Evidence: `BrowserGameState::sync_player_character_scene` currently toggles
  the character scene item visible only when `PlayerMode::DebugFly`.
- Observation: The existing renderer, browser game-state tests, and browser
  smoke script remain under split pressure after this feature.
  Evidence: `crates/engine_web/src/wgpu_renderer.rs`,
  `crates/engine_web/src/tests.rs`, and `tools/browser-smoke.mjs` are above the
  repo's 600-line review threshold.

## Decision Log

- Decision: Add third-person as a Rust `PlayerMode`, not a TypeScript camera
  hack.
  Rationale: Rust owns player/camera state, render snapshots, and the GLTF
  player-character scene item. TypeScript should continue to forward input and
  display debug/HUD state only.
  Date/Author: 2026-06-07 / Codex.
- Decision: Cycle `FIRST -> THIRD -> FLY -> FIRST`.
  Rationale: `C`/`F1` remain one-button camera controls while keeping debug fly
  accessible.
  Date/Author: 2026-06-07 / Codex.

## Milestone Review

- Scope: Rust third-person camera mode, browser character visibility, TypeScript
  mode labels/contracts, docs, generated WASM, and browser smoke coverage.
- Reviewers: contract, code quality, legacy, correctness, and validation passes
  were run locally. Sub-agents were not used because the available delegation
  tool requires an explicit user request for sub-agents.
- Required findings fixed: none.
- Follow-ups recorded: split pressure remains in the existing large renderer,
  game-state test, and browser smoke files; avoid extending those files further
  without a split plan.
- Rejected findings: none.
- Validation rerun: `cargo fmt`, `cargo test -p engine_core`,
  `cargo test -p engine_web`, `npm run check:shaders`, `npm run build:wasm`,
  `npm run check:wasm`, `npm test`, `npm run smoke:browser`, in-app browser
  reload/THIRD screenshot check, and `git -c safe.directory=C:/dev/ofg diff
  --check`.
- Remaining risk: the chase camera is fixed-distance and does not yet handle
  terrain occlusion, smoothing, orbit controls, or shoulder switching.

## Outcomes & Retrospective

Implementation is in place. `PlayerMode::ThirdPerson` is a grounded Rust player
mode with a chase camera behind and above the player. The GLTF player character
is visible in third-person and debug-fly, hidden in first-person, and still
driven by Rust locomotion state. Browser smoke passed with artifacts at
`C:\dev\ofg\artifacts\browser-smoke\2026-06-07T05-49-54-444Z`; the
`third-person-walk.png` screenshot shows the orange Quaternius character walking
from behind with HUD mode `THIRD`.
The in-app browser at `http://127.0.0.1:5173/` was also reloaded and verified
showing HUD mode `THIRD` with the character visible from behind.

## Contract and Quality Baseline

This plan updates `OFG-API-001` to include `thirdPerson` in the supported
browser command/debug player mode surface. It preserves the Rust ownership rules
in `OFG-API-009`: TypeScript must not own camera math, scene graph state, or
model visibility semantics.

Validation targets:

    cargo test -p engine_core
    cargo test -p engine_web
    npm run check:shaders
    npm run build:wasm
    npm run check:wasm
    npm test
    npm run smoke:browser
    git -c safe.directory=C:/dev/ofg diff --check

## Plan of Work

Update `crates/engine_core/src/player.rs` and `crates/engine_core/src/engine.rs`
so third-person is a grounded player mode with chase-camera eye placement behind
and above the player. Update Rust tests for mode codes, toggle order, grounded
movement, and camera transform.

Update `crates/engine_web/src/game_state.rs` so terrain grounding treats
third-person like first-person and the GLTF character scene item is visible in
third-person. Update `crates/engine_web/src/wgpu_renderer.rs`, TypeScript mode
types, app HUD labels, and smoke tests for the new `thirdPerson` mode.

Update active docs, run the milestone review, archive this plan once complete,
then commit the feature.

## Acceptance

The user can open `http://127.0.0.1:5173/`, press `C` once to see `THIRD`, hold
`W`, and watch the Quaternius character play the walk animation while grounded
movement continues. Pressing `C` again reaches `FLY`, and again returns to
`FIRST`.
