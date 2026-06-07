# Character Selection And Locomotion Tuning

This ExecPlan is a living document. The sections Progress, Surprises &
Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as
work proceeds.

Once this plan is started, proceed independently for as long as possible. Return
to the user only for critical input that cannot be safely inferred, or when the
plan is complete.

This plan follows `PLANS.md`.

## Purpose / Big Picture

Let the player inspect a more useful humanoid character in third-person mode.
The user should be able to click a browser HUD button to toggle between male and
female characters, press `C` once to enter third-person, move with `WASD`, and
hold `Shift` to see the locomotion animation move toward a run/sprint clip
instead of only speeding up the same walk.

The animation stack should use a calmer arms-down idle instead of the current
arms-folded idle. It should expose simple numeric playback-speed tuning so foot
sliding can be reduced by adjusting walk/run animation playback rates before
adding inverse kinematics.

## Progress

- [x] (2026-06-07) Started the plan after the third-person camera commit.
- [x] (2026-06-07) Verified that the local free Quaternius Base Characters
  Standard zip includes Superhero male/female full-body GLTFs, but not the
  requested Regular male/female GLTFs.
- [x] (2026-06-07) Downloaded Quaternius Universal Animation Library Standard
  from itch.io into `artifacts/quaternius-downloads/` and verified that its GLB
  includes `Idle_Loop`, `Walk_Loop`, `Jog_Fwd_Loop`, and `Sprint_Loop`.
- [x] (2026-06-07) Added selected checked-in player assets and source notes.
- [x] (2026-06-07) Extended Rust player character loading for separate body mesh and animation
  source assets.
- [x] (2026-06-07) Added male/female character toggle command, HUD button, debug fields, and
  smoke coverage.
- [x] (2026-06-07) Added walk/run blend, playback tuning state, tests, docs,
  validation, and milestone review.
- [x] (2026-06-07) Archived this plan after implementation and validation.

## Surprises & Discoveries

- Observation: The free Standard base-character zip currently available locally
  has only Superhero male/female full-body model files.
  Evidence: `tar -tf artifacts/quaternius-downloads/universal-base-characters-standard.zip`
  lists `Superhero_Female_FullBody.gltf` and `Superhero_Male_FullBody.gltf`, but
  no `Regular_*_FullBody` entries.
- Observation: The Superhero base body skeleton and UAL animation skeletons line
  up for the animated joint range.
  Evidence: Node indices `0..64` match names between
  `quaternius-ual2-standard.glb` and `quaternius-superhero-male.glb`; the extra
  Superhero mesh nodes are eyebrows, eyes, and body after the animated joints.
- Observation: UAL2 Standard lacks a normal run clip, but UAL1 Standard has the
  basic locomotion clips this milestone needs.
  Evidence: UAL1 `UAL1_Standard.glb` imports with 45 clips including
  `Idle_Loop`, `Walk_Loop`, `Jog_Fwd_Loop`, and `Sprint_Loop`.

## Decision Log

- Decision: Use the checked-in Superhero male/female bodies as temporary male
  and female descriptors, while naming the descriptor layer generically enough
  that Regular GLBs can replace them later.
  Rationale: The user chose Regular male/female, but those files are not in the
  free Standard zip available to this repo. Blocking on unavailable paid/source
  assets would stall the engine work; the same skeleton path can be reused when
  better bodies arrive.
  Date/Author: 2026-06-07 / Codex.
- Decision: Use Quaternius Universal Animation Library 1 Standard for
  locomotion.
  Rationale: UAL1 Standard has arms-down `Idle_Loop`, `Walk_Loop`, and
  `Sprint_Loop`. UAL2 Standard's current checked-in GLB has `Idle_No_Loop` and
  `Walk_Carry_Loop`, but no normal run clip.
  Date/Author: 2026-06-07 / Codex.
- Decision: Start with numeric playback speed tuning and debug fields, not
  automatic foot-contact extraction or IK.
  Rationale: The user explicitly allowed numeric tuning. Foot-contact analysis
  and IK are larger animation-quality milestones and need more visualization
  support to be worth doing rigorously.
  Date/Author: 2026-06-07 / Codex.

## Outcomes & Retrospective

Implementation is in progress.

Implemented checked-in Superhero male/female placeholder bodies, shared UAL1
animation loading, Rust-owned `male`/`female` selection, `Idle_Loop`,
`Walk_Loop`, and `Sprint_Loop` locomotion, walk/run blend debug fields, numeric
animation tuning command/debug helpers, a HUD toggle button, TypeScript
command/debug typing, docs updates, and browser smoke coverage.

The requested Regular male/female bodies remain an asset sourcing gap because
the free Standard zip available to this repo does not include them. The current
same-rig placeholders prove the engine path and can be swapped once better GLBs
are available. Foot-contact extraction and IK remain future animation-quality
work after numeric speed tuning.

## Contract and Quality Baseline

This plan preserves `OFG-API-001`: browser code sends user/debug controls through
`game.command(...)` and reads Rust debug state through `debugSnapshot()`.

This plan updates `OFG-API-003` and `OFG-API-010`: debug snapshots may expose
the selected character ID/label, locomotion speed, walk/run blend weight, and
animation playback scale. Rust remains the owner of GLTF parsing, skeletons,
skinning, animation clip selection, animation blending, and renderer resource
resolution.

This plan preserves `OFG-API-009`: TypeScript must not parse GLTF/GLB data,
inspect model nodes, choose clips from model internals, skin vertices, or own a
scene graph. TypeScript may provide the click button and forward a typed command.

Validation targets:

    cargo fmt
    cargo test -p engine_web
    npm run check:wasm
    npm test
    npm run smoke:browser
    git -c safe.directory=C:/dev/ofg diff --check

Because this changes browser UI, input commands, renderer model resources, and
visual behavior, `npm run smoke:browser` is required.

## Context and Orientation

The playable browser runtime is Rust-owned through
`crates/engine_web/src/wgpu_renderer.rs`. TypeScript starts the WASM game and
forwards DOM input from `src/app/game.ts`.

The current player character logic lives in
`crates/engine_web/src/model_locomotion.rs`. Before this plan it hard-coded
Quaternius UAL2 `Idle_FoldArms_Loop` and `Walk_Carry_Loop`, assumed the mesh
and animation clips came from the same GLB, CPU-skinned one primitive each
frame, and updated one WebGPU vertex buffer. This plan changes that path to
load male/female body GLBs against a shared UAL1 animation GLB.

The current checked-in live animation asset is
`assets/models/player/quaternius-ual2-standard.glb`. The downloaded free UAL1
asset to use for this milestone is in
`artifacts/quaternius-downloads/extracted-ual1/Universal Animation Library[Standard]/Unreal-Godot/UAL1_Standard.glb`.
The free base-character Standard zip has Superhero male/female GLTF files under
`artifacts/quaternius-downloads/extracted/Universal Base Characters[Standard]/Base Characters/Godot - UE/`.

## Plan of Work

First, add the selected animation/body assets under `assets/models/player/` and
update `assets/models/player/SOURCE.md`. Convert the female Superhero GLTF plus
external `.bin` to GLB like the existing male asset. Add UAL1 Standard GLB as
the animation-source asset. Record that Regular bodies are desired but not
present in the free Standard zip.

Next, extend `model_locomotion.rs` so `PlayerCharacterModel` can be built from a
body model plus a separate animation source model. Select the largest skinned
primitive from the body model so the Superhero body is used instead of eyebrows
or eyes. Use UAL1 clip names `Idle_Loop`, `Walk_Loop`, and `Sprint_Loop`.

Then, add a Rust character descriptor table for male/female characters. Load both
body models and the shared UAL1 animation model during `RustBrowserGame::create`.
Register one mesh/material pair per character, keep the selected character in
`RustBrowserGame`, and update only the selected character mesh each frame.
Implement `togglePlayerCharacter` and `setPlayerCharacter` commands.

Then, update TypeScript browser types, debug hooks, `index.html`, `src/main.ts`,
`src/app/game.ts`, and `src/app/styles.css` with a compact HUD button that
forwards the toggle command and shows the selected character label from Rust.

Finally, update tests and browser smoke so the smoke path verifies the selected
character, toggles to the other character with the HUD button/command, verifies
third-person visibility, verifies arms-down idle, verifies walk on normal
movement, and verifies sprint/run blend when `Shift` is held.

## Concrete Steps

Run from `C:\dev\ofg`:

    cargo fmt
    cargo test -p engine_web
    npm run check:wasm
    npm test
    npm run smoke:browser
    git -c safe.directory=C:/dev/ofg diff --check

Inspect browser smoke screenshots under `artifacts/browser-smoke/<run>/`,
especially the third-person male/female and sprint screenshots.

## Milestone Review

After implementation:

1. Update `docs/API_CONTRACTS.md` and `docs/ARCHITECTURE.md` for the selected
   character command/debug surface and UAL1 locomotion source.
2. Run the repo-local `milestone-review` skill against this milestone.
3. Apply required findings before marking the milestone complete, or record a
   rejected finding with rationale.
4. Re-run relevant validation commands.
5. Archive this plan under `docs/archived/` once complete.

Review result on 2026-06-07:

- Scope: character asset selection, Rust GLTF body-plus-animation loading,
  male/female command/HUD/debug path, walk/sprint blending, numeric tuning
  command, docs, tests, generated wasm, and browser smoke.
- Reviewers: local contract, code quality, legacy, correctness, and validation
  passes. Sub-agent reviewers were not spawned because the user did not
  explicitly ask for delegated milestone review.
- Required findings fixed: numeric animation tuning command/debug helper was
  missing; added `setPlayerAnimationTuning`, debug fields, smoke coverage, and
  docs.
- Follow-ups recorded: `crates/engine_web/src/wgpu_renderer.rs` remains a very
  large facade file, and `crates/engine_web/src/model_locomotion.rs` is now over
  600 lines. Future character/render milestones should split player-character
  loading/debug/tuning out of the WebGPU facade and consider separating
  locomotion control from CPU skinning.
- Rejected findings: none.
- Validation rerun: `cargo fmt`, `cargo test -p engine_web`, `npm test -- --runInBand`,
  `npm run check:wasm`, `npm run smoke:browser`, and
  `git -c safe.directory=C:/dev/ofg diff --check`.
- Remaining risk: the checked-in male/female bodies are still Superhero
  placeholders, not the desired Regular bodies with clothing; numeric tuning is
  available but foot-contact extraction and IK are intentionally future work.

## Validation and Acceptance

Acceptance is observable when:

- `http://127.0.0.1:5173/` loads with a character toggle button in the HUD.
- Pressing `C` once enters `THIRD`.
- Clicking the character button switches between the male and female character
  descriptor labels.
- Standing still uses `Idle_Loop`, not `Idle_FoldArms_Loop`.
- Holding `W` uses `Walk_Loop`.
- Holding `Shift+W` drives locomotion toward `Sprint_Loop` and reports a high
  walk/run blend weight.
- Browser smoke saves screenshots and report JSON proving the above.

## Idempotence and Recovery

The downloaded zips in `artifacts/quaternius-downloads/` are not committed. If
asset conversion fails, rerun the conversion from the extracted source GLTF/bin
files. If the selected GLBs are wrong, delete only the newly added
`assets/models/player/` GLBs for this plan and restore constants to the previous
UAL2-only path.

The unrelated dirty file `docs/TOUCH_CONTROLS_PLAN.md` existed before this plan
and must remain unstaged unless the user separately asks to commit it.

## Artifacts and Notes

Quaternius source links:

- `https://quaternius.com/packs/universalbasecharacters.html`
- `https://quaternius.com/packs/universalanimationlibrary.html`
- `https://quaternius.com/packs/universalanimationlibrary2.html`

Archive note, 2026-06-07: This plan is complete. The active source of truth for
the implemented character command/debug surface is now `docs/API_CONTRACTS.md`,
`docs/ARCHITECTURE.md`, `crates/engine_web/src/player_character.rs`,
`crates/engine_web/src/model_locomotion.rs`, and
`crates/engine_web/src/wgpu_renderer.rs`.

## Interfaces and Dependencies

At the end of the plan, these interfaces should exist:

    export type PlayerCharacterId = "male" | "female";
    { type: "togglePlayerCharacter" }
    { type: "setPlayerCharacter", character: PlayerCharacterId }

The Rust debug snapshot should expose:

    playerCharacterId
    playerCharacterLabel
    modelAnimationWalkRunBlendWeight
    modelAnimationPlaybackScale
    modelAnimationLocomotionSpeedMetersPerSecond
    modelAnimationWalkSpeedMetersPerSecond
    modelAnimationRunSpeedMetersPerSecond
    modelAnimationIdlePlaybackScale
    modelAnimationWalkPlaybackScale
    modelAnimationRunPlaybackScale

Animation tuning should be adjustable through:

    { type: "setPlayerAnimationTuning", walkSpeedMetersPerSecond,
      runSpeedMetersPerSecond, idlePlaybackScale, walkPlaybackScale,
      runPlaybackScale }

The Rust locomotion clips should be:

    Idle_Loop
    Walk_Loop
    Sprint_Loop
