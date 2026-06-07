# Add Mobile Touch Controls

This ExecPlan is a living document. The sections Progress, Surprises &
Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as
work proceeds.

Once this plan is started, proceed independently for as long as possible.
Return to the user only for critical input that cannot be safely inferred, or
when the plan is complete.

If `PLANS.md` is present in the repo, maintain this document in accordance with
it.

## Purpose / Big Picture

OFG should be playable enough from a mobile browser to walk around the terrain on
the remotely deployed build. A user opening the Cloudflare deployment on a
WebGPU-capable phone or tablet should see unobtrusive touch controls, use the
left thumb to move the player, use the right thumb to look around, and tap a
small control to cycle the same player modes as the desktop `C` / `F1` shortcut:
first-person, third-person, and debug fly.

This work must preserve the current Rust ownership boundary. Rust already owns
player movement and camera behavior through `engine_web.wasm`. TypeScript should
only collect browser touch input and translate it into the existing
`BrowserFrameInput` object passed to Rust each frame.

The first successful version is intentionally small: virtual movement stick,
right-side look drag, camera toggle button, tests, and smoke coverage. More
advanced mobile UI, jump/crouch, gestures, inventory controls, and mobile HUD
polish are future work.

## Progress

- [x] (2026-06-06 10:09Z) Read `PLANS.md` before drafting this ExecPlan.
- [x] (2026-06-06 10:09Z) Inspected `index.html`, `src/app/styles.css`,
  `src/app/game.ts`, `src/engine/input/inputTracker.ts`,
  `src/engine/input/inputTracker.test.ts`, and
  `src/engine/web/browserGameTypes.ts`.
- [x] (2026-06-06 10:09Z) Refreshed this plan against the current
  `AGENTS.md`, `PLANS.md`, `docs/ARCHITECTURE.md`, and
  `docs/API_CONTRACTS.md`. The Rust conversion plan is now archived and
  historical only.
- [x] (2026-06-07 12:17Z) Refreshed this plan against the current
  `C:\dev\ofg-touch-controls` worktree. The plan now accounts for the clean
  worktree, the existing character toggle HUD button, and the current
  `firstPerson -> thirdPerson -> debugFly -> firstPerson` player-mode cycle.
- [x] (2026-06-07 12:27Z) Updated local run guidance for this worktree to avoid
  dev-server port collisions with other active worktrees.
- [x] (2026-06-07 13:01Z) Merged updated `origin/main`, reread `PLANS.md`, and
  added the new coverage completion gate to this ExecPlan.
- [x] (2026-06-07 13:01Z) Split mobile touch browser-smoke helpers into
  `tools/browser-smoke-mobile-touch.mjs` after local milestone review flagged
  `tools/browser-smoke.mjs` crossing the 600-line split-pressure threshold.
- [x] (2026-06-07 13:18Z) Added the touch-control DOM overlay in `index.html`
  and CSS in `src/app/styles.css`. The overlay provides a left joystick,
  right look zone, and compact camera-toggle button, with stable fixed
  dimensions and `touch-action: none`.
- [x] (2026-06-07 13:18Z) Extended `src/engine/input/inputTracker.ts` so touch
  joystick movement and right-side look drags are collected as browser input
  and merged into the existing `BrowserFrameInput` shape through
  `src/app/frameInput.ts`.
- [x] (2026-06-07 13:18Z) Added focused TypeScript tests for joystick movement,
  pointer release, document pointer cancel, lost pointer capture, look-delta
  accumulation/clearing, keyboard plus touch behavior, and frame-input
  clamping.
- [x] (2026-06-07 13:18Z) Extended browser smoke coverage with a mobile touch
  path in `tools/browser-smoke-mobile-touch.mjs`, imported by
  `tools/browser-smoke.mjs`.
- [x] (2026-06-07 13:18Z) Validated locally with `npm run check:wasm`,
  `npm test`, `npm run coverage:rust`, and
  `$env:OFG_SMOKE_PORT='5184'; npm run smoke:browser`. Browser smoke artifacts
  were written to
  `artifacts/browser-smoke/2026-06-07T13-09-33-897Z/`.
- [x] (2026-06-07 13:18Z) Ran the repo-local `milestone-review` workflow
  locally across contract, code-quality, legacy, correctness, and validation
  passes. No required findings remained; no API contract doc update was needed.
- [x] (2026-06-07 13:21Z) Committed the local implementation as `04bd40e` and
  pushed branch `touch-controls` to `origin`.
- [x] (2026-06-07 13:32Z) Fast-forwarded `main` to `a8fe94a` from the
  `C:\dev\ofg` worktree and pushed `origin/main`, allowing the stable
  Cloudflare deployment to pick up the touch-controls commit.
- [x] (2026-06-07 13:37Z) Verified the stable Cloudflare URL serves the
  touch-control HTML and passes remote Chrome mobile-emulation smoke with
  WebGPU, visible controls, touch joystick movement, and touch camera toggle.
- [ ] Verify on the Cloudflare remote URL from an actual WebGPU-capable mobile
  device. The user has offered to perform this final real-device check.

## Surprises & Discoveries

- Observation: The current frame boundary already has the axes needed for touch
  controls.
  Evidence: `src/engine/web/browserGameTypes.ts` defines
  `BrowserFrameInput.movement.forward`, `.right`, `.up`, `.fast`, and
  `BrowserFrameInput.look.deltaX` / `.deltaY`.

- Observation: The current `InputTracker` only collects keyboard presses and
  pointer-locked mouse movement.
  Evidence: `src/engine/input/inputTracker.ts` tracks `keysDown`,
  `keysPressed`, `mouseDeltaX`, and `mouseDeltaY`, and only listens to
  `keydown`, `keyup`, `mousemove`, and canvas `click`.

- Observation: The existing page is a full-screen canvas with a fixed HUD and no
  interactive overlay.
  Evidence: `index.html` contains `#game-canvas`, `#terrain-debug-overlay`, and
  `#hud`; `src/app/styles.css` sets the canvas to `100vw` by `100vh`.

- Observation: The touch-controls worktree is clean before implementation.
  Evidence: `git -c safe.directory=C:/dev/ofg-touch-controls status --short`
  produced no output on 2026-06-07 12:17Z.

- Observation: The current player-mode toggle cycle includes third-person.
  Evidence: `crates/engine_core/src/engine.rs` toggles
  `FirstPerson -> ThirdPerson -> DebugFly -> FirstPerson`; `src/app/game.ts`
  labels those modes as `FIRST`, `THIRD`, and `FLY`; and
  `tools/browser-smoke.mjs` verifies `KeyC` changes the HUD from `FIRST` to
  `THIRD`.

- Observation: The HUD now has a character toggle button in addition to camera
  mode and frame time.
  Evidence: `index.html` contains `#character-toggle`, `src/main.ts` requires
  it before calling `startGame`, and `src/app/game.ts` wires it to
  `game.command({ type: "togglePlayerCharacter" })`.

- Observation: This worktree should not use the default dev-server ports while
  other OFG worktrees are active.
  Evidence: `tools/dev-server.mjs` reads `PORT` and defaults to `5173`;
  `tools/browser-smoke.mjs` reads `OFG_SMOKE_PORT` and defaults to `5174`.
  For this worktree, use `PORT=5183` for manual dev runs and
  `OFG_SMOKE_PORT=5184` for browser smoke unless those are also occupied.

- Observation: The current source already exposes player position through the
  debug hook.
  Evidence: `src/app/game.ts` defines `window.__ofgDebug.getPlayerPosition`,
  which returns `game.debugSnapshot().playerPosition`.

- Observation: `docs/API_CONTRACTS.md` is now the active ownership and boundary
  document for this work.
  Evidence: `AGENTS.md` directs ownership questions to `docs/API_CONTRACTS.md`
  and `docs/ARCHITECTURE.md`; `docs/archived/RUST_CONVERSION_PLAN.md` starts
  with an archived note.

- Observation: The updated `PLANS.md` requires a coverage completion gate for
  implementation ExecPlans.
  Evidence: after merging `origin/main` on 2026-06-07, `PLANS.md` says an
  implementation plan is not complete until modified implementation files pass
  the default coverage attention gate, currently about 90% line coverage, or
  the plan records an explicit exception with rationale.

- Observation: The current repo coverage command is Rust-focused, while this
  plan changes TypeScript browser/input implementation files and browser smoke
  tooling.
  Evidence: `AGENTS.md` defines `npm run coverage:rust` as the command for
  Rust API coverage and says its default output reports implementation files
  below 90% line coverage, excluding tests, smoke/benchmark harnesses, and Rust
  export glue. This touch-control implementation does not modify Rust
  implementation files.

- Observation: The mobile browser smoke path now proves local touch movement
  reaches Rust-owned player state.
  Evidence: `artifacts/browser-smoke/2026-06-07T13-09-33-897Z/report.json`
  records `mobileTouch.touchControls.display` as `block`,
  `mobileTouch.touchControls.visibility` as `visible`, movement from
  `{x: 0, z: 0}` to approximately `{x: 0.393, z: 0.619}`, and
  `movementDistance` as `0.7328200837689752`.

- Observation: The updated coverage gate passed for this TypeScript/browser
  slice.
  Evidence: `npm run coverage:rust` completed on 2026-06-07 and reported
  `files below 90% line coverage ... none`; this plan changes no Rust
  implementation files.

- Observation: The stable Cloudflare URL is reachable and has the required
  cross-origin isolation headers, and after `main` was pushed it served the
  touch-control HTML.
  Evidence: on 2026-06-07 13:21Z, `curl.exe -I
  https://ofg.chriscummings1024.workers.dev/` returned `200 OK`,
  `cross-origin-embedder-policy: require-corp`, and
  `cross-origin-opener-policy: same-origin`. The fetched HTML from the same URL,
  and from `?touch-controls=04bd40e` at 13:23Z, did not include
  `#touch-controls`. After fast-forwarding and pushing `main`, a no-cache fetch
  at 13:32Z returned HTML containing `#touch-controls`.

- Observation: Remote Chrome mobile-emulation smoke passed against the stable
  Cloudflare deployment.
  Evidence: the smoke report at
  `artifacts/remote-browser-smoke/2026-06-07T13-37-17-644Z/report.json`
  recorded COOP `same-origin`, COEP `require-corp`, `crossOriginIsolated:
  true`, `SharedArrayBuffer` available, `navigator.gpu` available, Rust/wgpu
  renderer configured, touch controls visible, movement distance
  `0.3666575458997926`, and touch camera toggle changing the HUD from `FIRST`
  to `THIRD`.

## Decision Log

- Decision: Keep touch controls in TypeScript browser input code.
  Rationale: Touch input is browser-specific. Rust already receives generic
  movement and look intent, so adding Rust-specific touch concepts would blur the
  current ownership boundary without improving gameplay.
  Date/Author: 2026-06-06 / Codex

- Decision: Use Pointer Events for touch controls rather than raw Touch Events.
  Rationale: Pointer Events provide one API for touch, mouse, pen, pointer IDs,
  and pointer capture. This makes unit tests simpler and avoids parallel event
  paths.
  Date/Author: 2026-06-06 / Codex

- Decision: Build the first version as two virtual regions: left joystick and
  right look drag.
  Rationale: This maps directly onto the existing `movement` and `look` frame
  input fields. It is also familiar for mobile first-person controls and can be
  verified with synthetic pointer events.
  Date/Author: 2026-06-06 / Codex

- Decision: Hide the controls by default on pointer-accurate desktop layouts.
  Rationale: Desktop keyboard/mouse play should stay visually clean. On mobile
  or coarse-pointer devices, the controls should appear without requiring a
  settings screen.
  Date/Author: 2026-06-06 / Codex

- Decision: Preserve `OFG-API-001` without adding touch-specific fields to
  `BrowserFrameInput`.
  Rationale: Touch is a browser input source, not a Rust game API. The stable
  Rust-facing frame packet already has movement and look fields.
  Date/Author: 2026-06-06 / Codex

- Decision: Preserve `OFG-API-003` by using debug hooks only for verification.
  Rationale: Mobile smoke may read `getPlayerPosition()` to prove movement, but
  TypeScript must not derive player, terrain, renderer, or stream state itself.
  Date/Author: 2026-06-06 / Codex

- Decision: Preserve `OFG-API-009` by keeping the work limited to DOM input and
  HUD/control UI.
  Rationale: Touch controls must not reintroduce TypeScript scene, terrain,
  renderer, simulation, or world ownership.
  Date/Author: 2026-06-06 / Codex

- Decision: Use alternate local ports for this worktree during touch-control
  implementation.
  Rationale: Other active worktrees may already be using the repo defaults
  `5173` and `5174`. Running manual dev with `PORT=5183` and browser smoke with
  `OFG_SMOKE_PORT=5184` avoids unnecessary local conflicts while preserving the
  existing scripts.
  Date/Author: 2026-06-07 / Codex

- Decision: Treat `npm run coverage:rust` as the required coverage command for
  this plan, and record a TypeScript coverage exception.
  Rationale: The default repository coverage attention report is currently
  Rust-only. This implementation modifies TypeScript input/app files and browser
  smoke tooling, with focused unit tests and browser smoke covering the changed
  behavior. Because no Rust implementation files are modified, the coverage gate
  is satisfied when `npm run coverage:rust` runs and does not list changed Rust
  implementation files. TypeScript line-coverage tooling should be added by a
  separate testing-plan milestone rather than invented inside this touch-control
  slice.
  Date/Author: 2026-06-07 / Codex

- Decision: Keep the mobile touch smoke scenario in a separate tool module.
  Rationale: The local milestone review flagged `tools/browser-smoke.mjs` above
  the 600-line split-pressure threshold. Moving the mobile scenario to
  `tools/browser-smoke-mobile-touch.mjs` keeps the desktop smoke script compact
  and gives touch-specific browser automation a clear owner.
  Date/Author: 2026-06-07 / Codex

- Decision: Do not change `docs/API_CONTRACTS.md` for this slice.
  Rationale: The implementation preserves `BrowserFrameInput` and `GameCommand`
  as the Rust-facing contracts. Touch-specific state remains browser-local in
  `InputTracker` and `buildBrowserFrameInput`, and smoke verification uses
  existing black-box debug hooks.
  Date/Author: 2026-06-07 / Codex

## Outcomes & Retrospective

The local implementation is complete. OFG now has a touch overlay with a
movement joystick, right-side look area, and camera-mode toggle button. Touch
input is translated into the existing movement/look frame packet that Rust
already owns, so the player/camera ownership boundary stayed intact.

Local verification passed after merging updated `origin/main`: `npm run
check:wasm`, `npm test`, `npm run coverage:rust`, and
`$env:OFG_SMOKE_PORT='5184'; npm run smoke:browser`. The mobile smoke report
shows visible controls, Rust-owned player movement from a synthetic touch
joystick drag, a nonblank mobile screenshot, and a touch camera toggle changing
the HUD from `FIRST` to `THIRD`.

The remaining gap is the final deployment milestone: commit, push, wait for the
Cloudflare deployment, then verify the remote URL on an actual WebGPU-capable
mobile device. Local Chrome mobile emulation is evidence for the implementation,
but it is not a substitute for the real-device acceptance item.

The implementation commit has been pushed to both `origin/touch-controls` and
`origin/main`. The stable Cloudflare URL now serves the touch-control HTML and
passed an automated remote Chrome mobile-emulation smoke check. The only
remaining acceptance item is the user's actual WebGPU-capable mobile-device
verification.

## Contract and Quality Baseline

This plan must preserve these current contracts from `docs/API_CONTRACTS.md`:

`OFG-API-001: Browser Shell To Rust Browser Game` is active. The supported
per-frame call remains `game.tick(frame)`, where `frame` is a
`BrowserFrameInput` object with `movement` and `look` fields. Touch controls may
change browser-side input collection, but must not add scalar wasm-bindgen frame
methods, raw wasm calls, or touch-specific Rust API fields. If a new user
control is needed, add it through the existing `GameCommand` lane.

`OFG-API-003: Debug And Smoke-Test Hooks` is active. Touch-control smoke may use
`window.__ofgDebug.getPlayerPosition()` and HUD state to verify behavior. Debug
hooks must remain browser test affordances; they must not compute or mirror
terrain, renderer, player, or stream state in TypeScript.

`OFG-API-009: Forbidden TypeScript Ownership` is binding. This plan must not
create a TypeScript scene graph, ECS, terrain generator, terrain manager,
terrain worker protocol, WebGPU renderer, render packet owner, or simulation
owner. TypeScript may collect DOM input, update HTML controls, parse URL
parameters, start WASM, expose debug hooks, and forward typed packets/commands to
Rust.

The relevant `AGENTS.md` validation gates are `npm test` for logic changes and
`npm run smoke:browser` for input, camera behavior, HUD behavior, browser
integration, or rendering-adjacent changes. In this worktree, run browser smoke
as `$env:OFG_SMOKE_PORT='5184'; npm run smoke:browser` unless that port is
occupied. Terrain seam and preset smoke tests are not required for this plan
unless the implementation unexpectedly changes terrain mesh, material, preset,
descriptor, or terrain visual behavior.

The updated `PLANS.md` coverage completion gate also applies. Run
`npm run coverage:rust` before final delivery. Expected result: the command
completes or, if `cargo-llvm-cov` is unavailable, prints setup guidance without
mutating build output as documented in `AGENTS.md`. If it completes, the default
filtered output must not list any changed Rust implementation file. This plan
changes no Rust implementation files; changed TypeScript implementation files
are covered by focused TypeScript unit tests and browser smoke until the repo
adds a TypeScript coverage lane.

After each implementation milestone, run the repo-local `milestone-review` skill
before marking that milestone complete. Required findings must be fixed, or a
rejected finding must be recorded in this plan's Decision Log with rationale.

## Context and Orientation

The repository root for this worktree is `C:\dev\ofg-touch-controls`. OFG is a
browser-native WebGPU game prototype. Browser startup, DOM input collection, HUD
updates, and calls into the Rust runtime live under `src/app` and
`src/engine/web`. The current architecture says TypeScript is browser shell: DOM
input, URL seed/preset parsing, HUD/debug UI, WASM startup, generic browser
asset loading, and debug hooks. Rust owns player/camera state, terrain
streaming, texture semantics, GLTF/model logic, WebGPU resources, frame
construction, and draw submission through `crates/engine_core`,
`crates/terrain_core`, and `crates/engine_web`.

The active browser game loop is in `src/app/game.ts`. `startGame` creates an
`InputTracker`, attaches it to the canvas, consumes input once per animation
frame, builds a `BrowserFrameInput`, and calls `game.tick(frameInput)`.
`src/main.ts` currently queries the canvas, camera-mode label, character-toggle
button, and frame-time label before calling `startGame`.

`BrowserFrameInput` is defined in `src/engine/web/browserGameTypes.ts`:

    deltaSeconds: number
    movement.forward: number
    movement.right: number
    movement.up: number
    movement.fast: boolean
    look.deltaX: number
    look.deltaY: number

The Rust side already understands those fields. The current app maps keyboard
keys to movement axes in `src/app/game.ts`: `W/S` map to forward/back,
`D/A` map to right/left, `Space/ControlLeft` map to up/down in debug fly, and
`ShiftLeft/ShiftRight` map to fast movement. `C` and `F1` send
`game.command({ type: "togglePlayerMode" })`, which currently cycles
`FIRST -> THIRD -> FLY -> FIRST`. Mouse look comes from
`InputTracker.consumeFrameSnapshot()` and only accumulates while the canvas has
pointer lock.

The current visual shell is minimal. `index.html` contains the game canvas, a
terrain debug overlay canvas, and a small HUD with `#camera-mode`,
`#character-toggle`, and `#frame-time`. `src/app/styles.css` makes the game
canvas fill the viewport and prevents body scrolling with `overflow: hidden`.

The browser smoke script is `tools/browser-smoke.mjs`. It builds the app,
launches Chrome or Edge with WebGPU flags, opens a local dev server, verifies
the page renders non-blank frames, toggles camera mode with keyboard input, and
uses debug hooks to verify terrain streaming. This script should remain passing
after touch controls are added. In this worktree, run browser smoke with
`OFG_SMOKE_PORT=5184` so its temporary dev server starts away from other active
worktrees. If port `5184` is occupied, choose the next free nearby port and
record the actual port in Progress.

## Plan of Work

Milestone 1 adds the DOM structure and CSS for the mobile controls without
connecting gameplay. Update `index.html` to add a fixed `#touch-controls`
element after the HUD. It should contain a left joystick zone with a base and
thumb element, a right look zone that can be visually subtle or transparent, and
a camera-toggle button. Update `src/app/styles.css` with stable dimensions,
`touch-action: none`, `user-select: none`, and media queries that show controls
on coarse pointers while keeping them hidden on desktop. Do not wire gameplay in
this milestone; the visible controls are allowed to be inert until the input
tracking milestone. The overlay must not cover the HUD, character-toggle button,
or debug overlay and must not cause layout shifts.

Milestone 2 adds pointer-event tracking. Prefer extending
`src/engine/input/inputTracker.ts` with touch-control state if the resulting file
stays readable. If the implementation starts making `InputTracker` large or
hard to test, add a focused helper module such as
`src/engine/input/touchControls.ts` and let `InputTracker` compose it. Track one
active movement pointer on the left zone and one active look pointer on the
right zone. Use `setPointerCapture` when available. Clear active pointers on
`pointerup`, `pointercancel`, and `lostpointercapture`.

Milestone 3 converts pointer positions into frame intent. The left joystick
stores an origin point from `pointerdown`, computes an offset on each
`pointermove`, clamps it to a fixed radius, applies a dead zone, and exposes
normalized `forward` and `right` axes in the range `[-1, 1]`. Positive Y screen
movement should become negative forward, so dragging upward moves the player
forward. The right look zone accumulates pixel deltas from pointer moves into
`lookDeltaX` and `lookDeltaY`, then clears those deltas when the frame snapshot
is consumed.

Milestone 4 merges touch input into the existing frame input. Update
`src/main.ts` to query the touch-control elements added in Milestone 1 and pass
them to `startGame`. Update `src/app/game.ts` so `readFrameInput` combines
keyboard axes and touch axes with clamping. For example, keyboard `W` plus
joystick forward should still produce at most `1`, not `2`. The look input
should add pointer-lock mouse deltas and touch look deltas. The camera toggle
button should follow the existing character-toggle button pattern in
`startGame`: call `game.command({ type: "togglePlayerMode" })`, update visible
state on the next frame, and return focus to the canvas without scrolling. The
first tap from the default HUD state should change `FIRST` to `THIRD`, matching
the current desktop smoke expectation.

Milestone 5 adds focused tests. Extend `src/engine/input/inputTracker.test.ts`
or add a new nearby test file for touch behavior. The fake element harness
should support pointer listeners, pointer capture calls, and event methods such
as `preventDefault`. Tests should prove that joystick drag produces normalized
movement, release clears movement, pointer cancel clears movement, right drag
accumulates look delta, frame consumption clears look delta, and keyboard/mouse
behavior still works.

Milestone 6 extends browser verification. Keep the existing desktop smoke path
green. Add a mobile viewport path in `tools/browser-smoke.mjs` or a separate
`tools/mobile-touch-smoke.mjs` if that keeps the desktop smoke clearer. The
mobile smoke should open a small viewport with touch enabled when Playwright
supports it, wait for playable terrain, drag the joystick region, and verify the
player position changed through the existing
`window.__ofgDebug.getPlayerPosition()` hook. The smoke must still inspect
screenshots/report JSON when visual behavior changes, as required by
`OFG-API-003` and `AGENTS.md`.

Milestone 7 validates on the real remote deployment. After tests pass locally,
build, commit, push, wait for the Cloudflare deployment, then open the remote URL
from a WebGPU-capable mobile browser. Verify movement, look, camera toggle, page
scroll prevention, and desktop behavior.

## Concrete Steps

Run these commands from `C:\dev\ofg-touch-controls` before editing, to
understand the starting state:

    git -c safe.directory=C:/dev/ofg-touch-controls status --short
    npm run check:wasm
    npm test
    $env:OFG_SMOKE_PORT='5184'; npm run smoke:browser

Expected result: WASM generated artifacts are current, tests pass, and browser
smoke passes before the touch-control work. If unrelated work is already
present, do not revert it; either build on it if needed or keep the
touch-control changes separate.

After each milestone that changes code, run:

    npm test

Expected result: TypeScript builds, Rust/WASM artifacts build, and all Mocha
tests pass including any new touch-control tests.

After each milestone that changes input, camera, HUD, browser integration, or
rendering-adjacent behavior, run:

    $env:OFG_SMOKE_PORT='5184'; npm run smoke:browser

Expected result: desktop smoke still passes, screenshots are written under
`artifacts/browser-smoke/`, the camera toggle is verified, and any new mobile
touch smoke step verifies player movement from a simulated touch drag.

After each implementation milestone, run the repo-local milestone review:

    Use the repo-local milestone-review skill against the milestone diff and this ExecPlan.

Expected result: required findings are fixed before the milestone is marked
complete, or rejected findings are recorded in the Decision Log with rationale.

Before final delivery, run:

    npm run check:wasm
    npm test
    npm run coverage:rust
    $env:OFG_SMOKE_PORT='5184'; npm run smoke:browser

Expected result: generated WASM metadata is current, all tests pass, Rust
coverage either passes its default attention gate without listing changed Rust
implementation files or prints documented missing-tool guidance, browser smoke
passes, and relevant screenshot/report artifacts are inspected.

After remote deployment:

    curl.exe -I <remote-url>/

Expected result: status is `200`, the response is HTTPS, and the response keeps
the cross-origin isolation headers needed by the WebGPU/WASM app:

    Cross-Origin-Embedder-Policy: require-corp
    Cross-Origin-Opener-Policy: same-origin

## Milestone Review

After each milestone:

1. Update this ExecPlan's Progress, Surprises & Discoveries, Decision Log, and
   Outcomes & Retrospective sections.
2. Confirm whether `docs/API_CONTRACTS.md`, `docs/ARCHITECTURE.md`, or active
   feature plans need updates. For this touch-control plan, a contract-doc
   update should be unnecessary unless the implementation changes
   `BrowserFrameInput`, `GameCommand`, debug-hook semantics, or TypeScript/Rust
   ownership.
3. Run the repo-local `milestone-review` skill against the milestone diff and
   this ExecPlan.
4. Apply required review findings before marking the milestone complete. If a
   finding is rejected, record the rejection and rationale in the Decision Log.
5. Re-run the relevant validation commands, at minimum `npm test` and, for any
   input/HUD/browser behavior milestone,
   `$env:OFG_SMOKE_PORT='5184'; npm run smoke:browser`.
6. Record commands, screenshots/report paths, review summary, and remaining risk
   in Progress or Outcomes & Retrospective.

Review result for the local implementation milestone on 2026-06-07:

    Scope: touch-control DOM/CSS, browser input collection, frame-packet merge,
    TypeScript tests, mobile browser smoke helper, generated WASM artifacts, and
    this ExecPlan.

    Reviewers: contract, code quality, legacy, correctness, validation. The
    review was performed locally because the available sub-agent tool policy
    permits spawning only when the user explicitly requests sub-agents.

    Required findings fixed: one earlier split-pressure finding for
    `tools/browser-smoke.mjs` was fixed by moving the mobile scenario to
    `tools/browser-smoke-mobile-touch.mjs`. Current line counts are 473 for
    `tools/browser-smoke.mjs` and 275 for
    `tools/browser-smoke-mobile-touch.mjs`.

    Follow-ups recorded: actual Cloudflare mobile-device verification remains
    pending in Progress and Acceptance. Remote Chrome mobile-emulation smoke has
    since passed against the stable Cloudflare URL.

    Rejected findings: none.

    Validation rerun: `npm run check:wasm`, `npm test`, `npm run
    coverage:rust`, `$env:OFG_SMOKE_PORT='5184'; npm run smoke:browser`, and
    `git diff --check`.

    Remaining risk: local Chrome mobile emulation and in-app browser visual
    inspection do not prove behavior on an actual phone/tablet or deployed
    Cloudflare headers.

## Validation and Acceptance

The touch-control work is accepted when all of these are true:

1. On desktop, keyboard movement and pointer-lock mouse look still behave as
   before.
2. On mobile or a coarse-pointer viewport, the controls appear without covering
   the HUD.
3. Dragging the left joystick upward moves the first-person player forward over
   terrain.
4. Dragging the left joystick left or right strafes the player.
5. Releasing or canceling the joystick pointer immediately stops touch movement.
6. Dragging the right look area changes the camera yaw/pitch through the
   existing Rust frame input.
7. The camera toggle button uses the same player-mode cycle as `C` / `F1`;
   from a fresh load the HUD changes from `FIRST` to `THIRD`, then to `FLY`,
   then back to `FIRST` on subsequent taps.
8. The page does not scroll, zoom, select text, or open context menus while
   using the controls.
9. `npm test` passes.
10. `$env:OFG_SMOKE_PORT='5184'; npm run smoke:browser` passes, or the same
    command passes with another recorded non-default free port.
11. `npm run check:wasm` passes unless the implementation did not touch any
    generated WASM-facing contract or artifact. If skipped, record why.
12. `npm run coverage:rust` runs before completion. If `cargo-llvm-cov` is
    installed, the default filtered output does not list changed Rust
    implementation files; if the coverage tool is missing, the command prints
    documented setup guidance and this plan records the limitation. The
    TypeScript coverage exception in the Decision Log remains in force until a
    TypeScript coverage lane exists.
13. `OFG-API-001`, `OFG-API-003`, and `OFG-API-009` are preserved, or
    `docs/API_CONTRACTS.md` is intentionally updated in the same milestone.
14. Each implementation milestone has a recorded milestone-review result before
    being marked complete.
15. The deployed Cloudflare build works from an actual WebGPU-capable mobile
    browser.

## Idempotence and Recovery

The implementation should be additive. If the touch UI causes trouble, disable it
by hiding `#touch-controls` in CSS while leaving desktop controls intact. The
Rust runtime boundary should not need rollback because touch input maps into the
existing `BrowserFrameInput` shape.

Pointer state must always clear on `pointerup`, `pointercancel`, and
`lostpointercapture`; this prevents the player from continuing to move after a
browser gesture interruption or app switch.

If mobile smoke proves flaky in CI or on the local Windows browser setup, keep
the deterministic unit tests and desktop smoke as required validation, record the
mobile smoke limitation in Surprises & Discoveries, and verify the deployed
mobile path manually until Playwright mobile input is made reliable.

If the first mobile deployment renders blank, debug deployment headers and WebGPU
support first. Touch controls should not change renderer ownership or terrain
streaming.

## Artifacts and Notes

Suggested DOM shape:

    <div id="touch-controls" aria-hidden="false">
      <div id="touch-move-zone">
        <div id="touch-move-base">
          <div id="touch-move-thumb"></div>
        </div>
      </div>
      <div id="touch-look-zone"></div>
      <button id="touch-camera-toggle" type="button" aria-label="Toggle camera mode"></button>
    </div>

The camera toggle should use a compact symbol or CSS-drawn icon with an
`aria-label`, not explanatory visible text. Do not add onboarding copy or visible
instructions to the game surface.

Validation evidence from 2026-06-07:

    npm run check:wasm
    Result: passed.

    npm test
    Result: passed. Rust workspace tests passed, and TypeScript reported
    73 passing Mocha tests including the new touch-control tests.

    npm run coverage:rust
    Result: passed. Rust coverage totals were 13280/15455 lines (85.9%)
    overall, and the default filtered attention report listed no files below
    90% line coverage.

    $env:OFG_SMOKE_PORT='5184'; npm run smoke:browser
    Result: passed. Artifacts:
    artifacts/browser-smoke/2026-06-07T13-09-33-897Z/
    Mobile touch report evidence: controls visible, movementDistance
    0.7328200837689752, and touch camera toggle HUD FIRST -> THIRD.

    In-app browser visual check on PORT=5183
    Result: terrain rendered with HUD top-left, camera toggle top-right, and
    joystick bottom-left without covering the HUD.

    git push origin touch-controls
    Result: passed. Branch `touch-controls` advanced from `62a0b78` to
    `04bd40e`.

    curl.exe -I https://ofg.chriscummings1024.workers.dev/
    Result: `200 OK` with `cross-origin-embedder-policy: require-corp` and
    `cross-origin-opener-policy: same-origin`, but fetched HTML still lacked
    `#touch-controls`.

    git merge --ff-only origin/touch-controls
    git push origin main
    Result: passed from the `C:\dev\ofg` main worktree. `origin/main` advanced
    from `fe86913` to `a8fe94a`.

    curl.exe -s -H "Cache-Control: no-cache" -H "Pragma: no-cache" \
      "https://ofg.chriscummings1024.workers.dev/?deploy-check=a8fe94a-nocache"
    Result: passed. The fetched HTML included `#touch-controls`.

    Remote Chrome mobile-emulation smoke against
    https://ofg.chriscummings1024.workers.dev/?remote-smoke=a8fe94a
    Result: passed. Report:
    artifacts/remote-browser-smoke/2026-06-07T13-37-17-644Z/report.json
    Screenshot:
    artifacts/remote-browser-smoke/2026-06-07T13-37-17-644Z/remote-mobile-touch.png
    Evidence: COOP/COEP present, WebGPU frame rendered, touch controls visible,
    movementDistance 0.3666575458997926, camera HUD FIRST -> THIRD.

Suggested joystick defaults:

    radius: 54 CSS pixels
    deadZone: 0.12
    visual thumb clamp: same as radius
    movement axis range: -1 to 1

Suggested touch look defaults:

    touchLookSensitivity: 1.0 initially
    deltaX: currentPointerX - previousPointerX
    deltaY: currentPointerY - previousPointerY

If touch look feels too slow or too fast on real hardware, adjust the
sensitivity constant in the TypeScript input layer rather than Rust player code.

## Interfaces and Dependencies

`src/engine/input/inputTracker.ts` should expose a frame snapshot that includes
both pointer-lock mouse look and touch-control intent. One acceptable final
shape is:

    export type InputSnapshot = {
      readonly mouseDeltaX: number;
      readonly mouseDeltaY: number;
      readonly touchLookDeltaX: number;
      readonly touchLookDeltaY: number;
      readonly touchMovementForward: number;
      readonly touchMovementRight: number;
    };

This keeps the existing `mouseDeltaX` / `mouseDeltaY` names and adds
touch-specific fields, but `src/app/game.ts` must be the only place that
combines them into `BrowserFrameInput`. The touch camera button can be wired
directly in `src/app/game.ts`, like the existing `#character-toggle` button,
instead of being represented as per-frame input.

`src/app/game.ts` should remain the bridge from browser input to Rust game
commands. It should not duplicate player movement rules. It should continue to
call `game.tick(frameInput)` once per animation frame.

`src/main.ts` should continue to fail fast if required root DOM elements are
missing. Add touch-control element queries there only when those elements are
added to `index.html`.

`src/engine/web/browserGameTypes.ts` should not need touch-specific fields. The
existing `BrowserFrameInput` movement and look fields are the stable contract.
Do not change this file for touch controls unless `OFG-API-001` is intentionally
updated in `docs/API_CONTRACTS.md` in the same milestone.

`src/app/styles.css` should own the touch overlay styling. The controls should
use stable fixed dimensions, avoid layout shifts, and use `touch-action: none`
on touch-interactive regions.

`tools/browser-smoke.mjs` imports the mobile touch scenario from
`tools/browser-smoke-mobile-touch.mjs`; the existing desktop smoke behavior and
screenshots must remain intact.

For manual browser checks in this worktree, start the dev server with a
non-default port:

    $env:PORT='5183'; npm run dev

Then open `http://127.0.0.1:5183/`. If that port is occupied, choose another
nearby free port and record it in Progress.

## Revision Note

2026-06-07: Refreshed this ExecPlan for the `C:\dev\ofg-touch-controls`
worktree. The refresh updated stale repository paths, recorded the clean
starting state, aligned camera-toggle expectations with the current
first-person/third-person/debug-fly cycle, and accounted for the existing
character-toggle HUD button.

2026-06-07: Added worktree-specific port guidance: use `PORT=5183` for manual
dev server runs and `OFG_SMOKE_PORT=5184` for browser smoke unless either port
is occupied.

2026-06-07: Merged updated `origin/main`, incorporated the new `PLANS.md`
coverage completion gate, and recorded the TypeScript coverage exception for
this browser/input slice.
