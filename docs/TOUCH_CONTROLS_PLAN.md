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
left thumb to move the first-person player, use the right thumb to look around,
and tap a small control to toggle between first-person and debug fly mode.

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
- [ ] Add a touch-control DOM overlay that is hidden on desktop and visible for
  coarse pointers or after touch input.
- [ ] Extend browser input collection so touch movement and touch look feed the
  existing `BrowserFrameInput` shape.
- [ ] Add unit tests for joystick movement, look drag, pointer release, pointer
  cancel, and keyboard/touch combination behavior.
- [ ] Add or extend browser smoke coverage for a mobile viewport touch-control
  path.
- [ ] Validate locally with `npm test` and `npm run smoke:browser`.
- [ ] Deploy and verify on the Cloudflare remote URL from an actual
  WebGPU-capable mobile device.

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

- Observation: There are unrelated uncommitted engine/WebGPU changes in the
  working tree at the time this plan was created.
  Evidence: `git -c safe.directory=C:/dev/ofg status --short` listed modified
  files under `crates/engine_web/src/` and `src/engine/web/`. This plan creation
  does not alter those files.

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

## Outcomes & Retrospective

This plan has not been implemented yet. The expected outcome is a deployed OFG
build that can be moved and looked around from a mobile browser, while preserving
desktop keyboard and mouse behavior.

Remaining gaps are all implementation and verification work described below.

## Context and Orientation

The repository root is `C:\dev\ofg`. OFG is a browser-native WebGPU game
prototype. Browser startup, DOM input collection, HUD updates, and calls into the
Rust runtime live under `src/app` and `src/engine/web`. Rust owns player and
camera behavior through `crates/engine_core` and `crates/engine_web`.

The active browser game loop is in `src/app/game.ts`. `startGame` creates an
`InputTracker`, attaches it to the canvas, consumes input once per animation
frame, builds a `BrowserFrameInput`, and calls `game.tick(frameInput)`.

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
`ShiftLeft/ShiftRight` map to fast movement. Mouse look comes from
`InputTracker.consumeFrameSnapshot()` and only accumulates while the canvas has
pointer lock.

The current visual shell is minimal. `index.html` contains the game canvas, a
terrain debug overlay canvas, and a small HUD. `src/app/styles.css` makes the
game canvas fill the viewport and prevents body scrolling with `overflow:
hidden`.

The browser smoke script is `tools/browser-smoke.mjs`. It builds the app,
launches Chrome or Edge with WebGPU flags, opens a local dev server, verifies
the page renders non-blank frames, toggles camera mode with keyboard input, and
uses debug hooks to verify terrain streaming. This script should remain passing
after touch controls are added.

## Plan of Work

Milestone 1 adds the DOM structure and CSS for the mobile controls without
connecting gameplay. Update `index.html` to add a fixed `#touch-controls`
element after the HUD. It should contain a left joystick zone with a base and
thumb element, a right look zone that can be visually subtle or transparent, and
a camera-toggle button. Update `src/app/styles.css` with stable dimensions,
`touch-action: none`, `user-select: none`, and media queries that show controls
on coarse pointers while keeping them hidden on desktop. The overlay must not
cover the HUD and must not cause layout shifts.

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
`src/app/game.ts` so `readFrameInput` combines keyboard axes and touch axes with
clamping. For example, keyboard `W` plus joystick forward should still produce
at most `1`, not `2`. The look input should add pointer-lock mouse deltas and
touch look deltas. The camera toggle button should call the same command as
`KeyC` / `F1`: `game.command({ type: "togglePlayerMode" })`.

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
player position changed through `window.__ofgDebug.getPlayerPosition()` if that
debug hook exists. If the current debug API does not expose player position, add
a narrow debug getter in `src/app/game.ts` that reads
`game.debugSnapshot().playerPosition`.

Milestone 7 validates on the real remote deployment. After tests pass locally,
build, commit, push, wait for the Cloudflare deployment, then open the remote URL
from a WebGPU-capable mobile browser. Verify movement, look, camera toggle, page
scroll prevention, and desktop behavior.

## Concrete Steps

Run these commands from `C:\dev\ofg` before editing, to understand the starting
state:

    git -c safe.directory=C:/dev/ofg status --short
    npm test
    npm run smoke:browser

Expected result: tests and smoke pass before the touch-control work. If unrelated
work is already present, do not revert it; either build on it if needed or keep
the touch-control changes separate.

After Milestones 1 through 5:

    npm test

Expected result: TypeScript builds, Rust/WASM artifacts build, and all Mocha
tests pass including the new touch-control tests.

After Milestone 6:

    npm run smoke:browser

Expected result: desktop smoke still passes, screenshots are written under
`artifacts/browser-smoke/`, the camera toggle is verified, and any new mobile
touch smoke step verifies player movement from a simulated touch drag.

After remote deployment:

    curl.exe -I <remote-url>/

Expected result: status is `200`, the response is HTTPS, and the response keeps
the cross-origin isolation headers needed by the WebGPU/WASM app:

    Cross-Origin-Embedder-Policy: require-corp
    Cross-Origin-Opener-Policy: same-origin

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
7. The camera toggle button switches the HUD between `FIRST` and `FLY`.
8. The page does not scroll, zoom, select text, or open context menus while
   using the controls.
9. `npm test` passes.
10. `npm run smoke:browser` passes.
11. The deployed Cloudflare build works from an actual WebGPU-capable mobile
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
      <button id="touch-camera-toggle" type="button" aria-label="Toggle camera mode">C</button>
    </div>

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
      readonly lookDeltaX: number;
      readonly lookDeltaY: number;
      readonly touchMovementForward: number;
      readonly touchMovementRight: number;
      readonly touchTogglePlayerMode: boolean;
    };

If preserving the existing `mouseDeltaX` / `mouseDeltaY` names makes the change
smaller, keep them and add touch-specific fields, but `src/app/game.ts` must be
the only place that combines them into `BrowserFrameInput`.

`src/app/game.ts` should remain the bridge from browser input to Rust game
commands. It should not duplicate player movement rules. It should continue to
call `game.tick(frameInput)` once per animation frame.

`src/engine/web/browserGameTypes.ts` should not need touch-specific fields. The
existing `BrowserFrameInput` movement and look fields are the stable contract.

`src/app/styles.css` should own the touch overlay styling. The controls should
use stable fixed dimensions, avoid layout shifts, and use `touch-action: none`
on touch-interactive regions.

`tools/browser-smoke.mjs` may gain a mobile touch path, but the existing desktop
smoke behavior and screenshots must remain intact.
