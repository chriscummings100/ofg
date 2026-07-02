# Add Player Component And Component-Driven Camera Controls

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

Maintain this document in accordance with `C:\dev\ofg\PLANS.md`.

## Purpose / Big Picture

After this change, the browser scene has a visible player entity: a roughly human-sized box standing on the flat ground plane. The user can press the backquote key, the key normally labeled `` ` ``, to cycle the single scene camera between debug fly camera mode, first-person player mode, and third-person player mode. Debug mode preserves the current free camera behavior. First-person and third-person modes use W/A/S/D for flat-plane player movement and mouse look for camera/player rotation. There is no collision or terrain handling yet; the player walks on a flat plane.

This work also adds the first small component update path. The `Game` facade stores the latest `ControlInput` snapshot received from the browser. The `Player` component owns player movement logic and reads the player-relevant controls during its update. The `Camera` component owns camera mode state and small camera control helpers, and it reads the camera-relevant controls during its update. The TypeScript browser host still owns DOM input collection only; it sends named raw controls into C++ as `ControlInput`, and C++ interprets them.

The visible success case is easy to observe: run the local app, click the canvas for pointer lock, press `` ` `` to cycle modes, and move a player-sized box around the ground plane with W/A/S/D while the single camera switches between debug, first-person, and third-person behavior.

## Progress

- [x] (2026-07-01 19:55Z) Authored this ExecPlan from the agreed architecture: component updates, one camera entity, generalized `ControlInput`, player logic in `Player`, camera mode logic in `Camera`.
- [x] (2026-07-01 20:04Z) Revised proposed camera interface so camera component behavior uses a single `Camera::update(...)` path unless implementation discovers a real need for another phase.
- [x] (2026-07-01 20:15Z) Clarified that controls are stored on `Game`, components read relevant controls from the update context, player movement no longer uses a separate `request_movement` API, OFG is Y-up, and `Scene::update(...)` runs player components before camera components.
- [x] (2026-07-01 21:12Z) Milestone 1: Added native `ControlInput`, browser input forwarding, TypeScript `ControlInput` wrapper APIs, and one-frame backquote camera-mode cycling.
- [x] (2026-07-01 21:28Z) Milestone 2: Added `Player` component storage, `SceneUpdateContext`, and an explicit scene update path that validates controls, updates players, then updates cameras.
- [x] (2026-07-01 21:46Z) Milestone 3: Moved debug camera behavior into the `Camera` component and removed the old game-owned debug camera controller.
- [x] (2026-07-01 22:05Z) Milestone 4: Added first-person and third-person player camera behavior using one camera entity.
- [x] (2026-07-01 22:20Z) Milestone 5: Added the visible player box to the demo scene and updated browser/native smoke expectations.
- [x] (2026-07-01 22:42Z) Ran milestone review against the current diff and applied required fixes: component update validation now happens before mutation, controls are gated to the primary player and main camera, camera-mode cycle input is consumed after one `Game` update, first-person camera placement avoids the player mesh, and TypeScript control-input file/API names are generalized.
- [x] (2026-07-01 22:08Z) Milestone 6: Updated docs and coverage summaries, ran full validation, inspected browser screenshots, and completed the retrospective.

## Surprises & Discoveries

- Observation: The current worktree has an untracked `C:\dev\ofg\assets\` tree before implementation starts.
  Evidence: `git status --short` reported `?? assets/` on 2026-07-01. Leave this user/generated work untouched unless later implementation explicitly requires files under that tree.

- Observation: `C:\dev\ofg\cpp\src\web\browser_game.cpp` is now 796 lines.
  Evidence: `(Get-Content cpp\src\web\browser_game.cpp).Count` on 2026-07-01. This is above the 500-line concern threshold and should be split in a focused follow-up, likely by extracting setup-phase input/status parsing and WebGPU setup helpers.

## Decision Log

- Decision: Replace the browser-facing "debug camera input" concept with a generalized C++ `ControlInput` snapshot.
  Rationale: The browser host should report named raw controls such as forward, left, fast, and cycle camera mode. Camera/player components should interpret those controls in C++ so gameplay rules do not move into TypeScript.
  Date/Author: 2026-07-01 / Codex

- Decision: Use one camera entity for all camera modes.
  Rationale: The active mode changes where the camera is and how it interprets input. This avoids multiple camera entities and keeps the renderer-facing `Scene::main_camera()` contract simple.
  Date/Author: 2026-07-01 / Codex

- Decision: Put player movement mechanics in a `Player` component, not in a separate bespoke player controller.
  Rationale: This starts the component update architecture the project needs. Future animation/model loading can attach to the player entity without moving the core player behavior elsewhere.
  Date/Author: 2026-07-01 / Codex

- Decision: Put camera mode state and the current small control helpers inside the `Camera` component.
  Rationale: The old game-owned debug camera controller was camera behavior, not global `Game` behavior. The first implementation keeps `Debug`, `FirstPerson`, and `ThirdPerson` behavior as private `Camera` helper methods because the code is still compact; a later camera-controller abstraction can extract those helpers once the behavior grows.
  Date/Author: 2026-07-01 / Codex

- Decision: Add an explicit, simple component update pass rather than a generic ECS scheduler.
  Rationale: The project needs update functions, but only `Player` and `Camera` need them now. A small explicit pass is easier to test and can be generalized once more component types exist.
  Date/Author: 2026-07-01 / Codex

- Decision: Store the latest `ControlInput` on `Game`, and pass it into component updates through `SceneUpdateContext`.
  Rationale: Browser input is a per-frame runtime snapshot. `Game` already owns frame update orchestration and can validate/store the latest accepted controls, while `Player` and `Camera` components each interpret only the controls relevant to them.
  Date/Author: 2026-07-01 / Codex

- Decision: Use `Camera::update(...)` as the planned camera component update entry point, without a planned `late_update` phase.
  Rationale: A second update phase is unnecessary for the current one-camera/player feature. Add another phase only if implementation finds a concrete ordering issue that cannot be solved clearly with the explicit component update order.
  Date/Author: 2026-07-01 / Codex

- Decision: `Scene::update(...)` updates all `Player` components before all `Camera` components.
  Rationale: Player components apply same-frame movement from the stored controls first. Camera components then read camera controls and follow the already-updated player transform in the same frame.
  Date/Author: 2026-07-01 / Codex

- Decision: OFG uses a Y-up world convention for this feature.
  Rationale: The existing renderer demo ground lies on the XZ plane with positive Y as height. Player movement for this plan is constrained to XZ, and the flat ground plane is `y = 0`.
  Date/Author: 2026-07-01 / Codex

- Decision: Use repo-style `m_` names for C++ `ControlInput` and `SceneUpdateContext` fields, while keeping camelCase names in TypeScript and debug JSON.
  Rationale: `AGENTS.md` requires member variables to use `m_name_with_underscores`. The interface sketch in this plan originally used unprefixed fields; the implementation follows the repo convention and the TypeScript wrapper preserves the browser-facing shape.
  Date/Author: 2026-07-01 / Codex

- Decision: Only the primary player and main camera consume the shared control snapshot.
  Rationale: `Game` stores one current local-player input snapshot. Guarding `Player::update(...)` and `Camera::update(...)` against non-primary components prevents accidental duplicate movement if later scenes contain more players or cameras.
  Date/Author: 2026-07-01 / Codex

- Decision: `Game` clears the `m_cycle_camera_mode` edge after one scene update.
  Rationale: Backquote is an action edge, not a held state. Clearing it after update protects non-browser callers and tests from sticky mode cycling even if they reuse the same `ControlInput` object.
  Date/Author: 2026-07-01 / Codex

- Decision: First-person camera mode uses a small forward eye offset in addition to eye height.
  Rationale: The temporary player is a visible box. A centerline eye position can place the camera inside the mesh, so the small forward offset keeps the view usable until real animated models and first-person visibility rules exist.
  Date/Author: 2026-07-01 / Codex

- Decision: Defer splitting `BrowserGame` even though the file is now 796 lines.
  Rationale: The current change already touches runtime setup, input, scene updates, smoke, and docs. A follow-up should extract setup-phase input/status parsing and WebGPU setup helpers from `browser_game.cpp`, but doing that refactor here would increase review scope without changing player behavior.
  Date/Author: 2026-07-01 / Codex

## Outcomes & Retrospective

Implemented. The scene now has a cyan, human-proportioned temporary player box standing on the Y-up flat ground plane. `Game` stores the latest `ControlInput` snapshot, `Scene::update(...)` validates it before mutation, updates `Player` components first, then updates `Camera` components. The single camera owns debug, first-person, and third-person modes, and `RuntimeDebugStatus` reports `cameraMode` for tests and smoke.

Browser smoke shows the player box clearly in debug and third-person modes. First-person mode no longer renders from inside the cyan player box because the temporary eye position includes a small forward offset. Backquote mode cycling is treated as a one-frame edge in both TypeScript collection and C++ `Game` consumption.

Validation passed on 2026-07-01:

- `npm run format:cpp:check`
- `npm test`
- `npm run smoke:browser`
- `npm run smoke:render`
- `npm run coverage`

Screenshot evidence:

- `C:\dev\ofg\artifacts\browser-smoke\player-box-debug.png`
- `C:\dev\ofg\artifacts\browser-smoke\first-person-mode.png`
- `C:\dev\ofg\artifacts\browser-smoke\third-person-mode.png`
- `C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png`

Remaining follow-up: split `browser_game.cpp` into smaller focused units before it accumulates more browser runtime behavior. Collision, terrain following, jumping, camera obstruction handling, and animated/model-loaded player rendering remain intentionally out of scope for this plan.

## Contract and Quality Baseline

This plan intentionally changes and preserves the active contracts in `C:\dev\ofg\docs\API_CONTRACTS.md`.

`OFG-BOOT-001 TypeScript Host Ownership` is preserved in spirit and has been updated in wording. TypeScript may collect DOM keyboard/mouse state, but it must not own player movement, camera mode behavior, scene graph state, or renderer state. The old debug-camera-only input phrasing is now raw control-input event collection.

`OFG-BOOT-002 C++ Runtime Ownership` is extended. C++ continues to own frame state, scene graph state, camera behavior, and renderer submission. It will additionally own storage of the latest accepted `ControlInput` on `Game`, the `Player` component, component update pass, camera control mode state, and interpretation of `ControlInput`.

`OFG-BOOT-003 WASM Facade` is changed narrowly. The facade should accept one raw `ControlInput` snapshot per frame instead of a debug-camera-only snapshot. The facade still must not expose scene internals, raw camera pointers, GPU handles, or mutable scene ownership to TypeScript.

`OFG-BOOT-004 Renderer Compatibility` and `OFG-BOOT-005 WebGPU Baseline` are preserved. The renderer still consumes `Scene::main_camera()` and mesh renderers. The visible scene gains one additional player-sized mesh; smoke thresholds may need careful adjustment, but the underlying draw-list renderer contract should not change.

`OFG-BOOT-006 Resource Lifetime` is preserved. The player box should reuse normal durable `Resources` material/mesh objects. Per-frame updates may mutate transforms and camera/player state, but they must not recreate meshes, textures, materials, shaders, pipelines, or buffers every frame.

`OFG-BOOT-009 Coverage` applies. Each modified implementation file must pass the coverage attention gate, currently about 90% line coverage unless a clear exception is recorded in this plan with rationale.

The repo readability requirements in `C:\dev\ofg\AGENTS.md` and `C:\dev\ofg\docs\GUIDES.md` apply: files need purpose comments, functions need comments/docstrings, large functions should have internal comments, and C++ uses the repo clang-format config through `npm run format:cpp`.

## Context and Orientation

The repository root is `C:\dev\ofg`.

The browser host lives under `C:\dev\ofg\src\app`. `C:\dev\ofg\src\app\controlInput.ts` collects DOM keyboard, pointer-lock, mouse-delta, modifier, and camera-cycle input into a raw `ControlInput` snapshot. `C:\dev\ofg\src\app\wasmRuntime.ts` forwards that snapshot through the Embind runtime. `C:\dev\ofg\src\app\main.ts` consumes one snapshot per animation frame before calling `runtime.frame(timeMs)`.

The C++ browser facade lives in `C:\dev\ofg\cpp\include\ofg\web\browser_game.hpp`, `C:\dev\ofg\cpp\src\web\browser_game.cpp`, and `C:\dev\ofg\cpp\src\web\embind_module.cpp`. It parses scalar Embind arguments, buffers setup-phase input until WebGPU is ready, then forwards input to `Game`.

The C++ `Game` facade lives in `C:\dev\ofg\cpp\include\ofg\game\game.hpp` and `C:\dev\ofg\cpp\src\game\game.cpp`. It owns the current scene pointer, frame state, debug status, current demo scene bindings, and latest stored `ControlInput` snapshot. It builds a `SceneUpdateContext`, updates the scene, reports `cameraMode`, and clears the one-frame camera-cycle edge after update.

The old debug camera controller files were removed. Debug fly camera behavior now lives on `Camera`, which reads the same `ControlInput` snapshot as first-person and third-person camera modes.

The scene graph lives under `C:\dev\ofg\cpp\include\ofg\scene` and `C:\dev\ofg\cpp\src\scene`. `Scene` owns entities and flat component storage. `Entity` owns local transform data and typed non-owning pointers to components. Existing components are `Camera` and `MeshRenderer`.

The renderer demo scene lives in `C:\dev\ofg\cpp\include\ofg\render\demo_scene.hpp` and `C:\dev\ofg\cpp\src\render\demo_scene.cpp`. It creates one camera entity, a checker ground plane, and four animated cube entities. This plan extends that scene with a player entity and one camera component that supports control modes.

Definitions used in this plan:

`ControlInput` means one per-frame C++ input snapshot containing named raw controls and mouse deltas. It does not contain gameplay behavior. `Game` stores the latest validated snapshot and passes it to component updates through `SceneUpdateContext`.

`Player` means a scene component attached to the player entity. It owns movement speed, flat-plane movement application, and grounding on the current plane. It reads movement controls during `Player::update(...)`; it does not receive movement through a separate request API.

`Camera control mode` means the active interpretation of `ControlInput` by the single `Camera` component. The initial modes are debug, first person, and third person.

`Component update path` means C++ code that gives scene components a chance to mutate entity transforms during `Game::update_impl` before rendering.

`Y-up flat plane` means world-space positive Y is height, player walking movement is constrained to the XZ plane, and the ground plane is `y = 0`.

## Plan of Work

Milestone 1 adds `ControlInput` while preserving current behavior. Create `C:\dev\ofg\cpp\include\ofg\core\control_input.hpp` and `C:\dev\ofg\cpp\src\core\control_input.cpp`. Define `struct ControlInput` with zero/false default member initializers for finite float fields `m_move_x`, `m_move_y`, `m_move_z`, `m_look_delta_x`, and `m_look_delta_y`, plus bool fields `m_look_active`, `m_fast`, `m_slow`, and `m_cycle_camera_mode`. Add `validate_control_input(ControlInput input)` that throws `EngineError` for non-finite numeric values. Store the latest accepted snapshot on `Game`, including setup-phase pending storage in `BrowserGame`, so components can read one stable input snapshot per update. Replace the debug-camera-only input path so the browser forwards the new generalized snapshot. On the TypeScript side, use `ControlInput`, update DOM collection to include a one-frame backquote edge trigger, and update wrapper tests.

Milestone 2 adds the player component and update scaffolding. Add `C:\dev\ofg\cpp\include\ofg\scene\player.hpp` and `C:\dev\ofg\cpp\src\scene\player.cpp`. Extend `ComponentType`, `Scene`, and `Entity` so an entity can create and expose one `Player` component. Add tests in `C:\dev\ofg\cpp\tests\scene_test.cpp` or a new `C:\dev\ofg\cpp\tests\player_component_test.cpp`. Add a small `SceneUpdateContext` type, likely in `C:\dev\ofg\cpp\include\ofg\scene\scene_update.hpp`, carrying a `const ControlInput&` that references the latest snapshot stored on `Game`, current time in milliseconds, clamped delta seconds, and non-owning scene bindings such as the primary player and main camera when needed. Add explicit update hooks rather than a generic scheduler: `Scene::update(context)` updates all `Player` components first and all `Camera` components second. `Player::update(...)` reads the controls relevant to player movement, applies movement on the XZ plane, and keeps the player on the Y-up flat ground convention. `Camera::update(...)` reads camera controls, including mode cycling, mouse look, debug fly movement, and player-follow behavior.

Milestone 3 moves debug camera behavior into the `Camera` component. Add camera control mode enum and camera-owned helper methods directly in `camera.hpp`/`camera.cpp` while the code remains small. Adapt the existing debug fly camera math so it operates on the owning `Camera` rather than scanning `Scene::main_camera()`. Remove the old game-owned debug camera controller after tests are moved. Keep behavior compatible: first movement update has zero delta, large deltas are clamped, diagonal movement is normalized, fast/slow modifiers work, pitch is clamped, and invalid numeric input is rejected.

Milestone 4 adds first-person and third-person camera modes. The single `Camera` component owns `CameraControlMode::Debug`, `CameraControlMode::FirstPerson`, and `CameraControlMode::ThirdPerson`. Pressing the backquote edge cycles modes in a deterministic order: debug, first person, third person, then debug again. First-person mode should keep the camera at an eye-height offset from the player entity and rotate with mouse yaw/pitch. Third-person mode should place the camera behind and above the player using the same yaw/pitch state, with a stable follow offset. In player camera modes, `Player::update(...)` reads W/A/S/D-style movement controls from the `Game`-stored `ControlInput` snapshot and applies speed, normalization, time delta, and ground-plane constraints. `Camera::update(...)` runs after player updates, reads camera-relevant controls, and follows the updated player transform in the same frame.

Milestone 5 makes the player visible in the demo scene. Extend `DemoScene` to create a distinct player material and attach both `Player` and `MeshRenderer` to one player entity. Use the existing cube mesh temporarily, scaled to roughly human proportions such as width 0.6, height 1.8, depth 0.35. Put the box on the ground plane by setting its local position to an eye/center convention that makes its feet sit at y = 0. If using a unit cube centered at the origin, a scale of y = 1.8 and position y = 0.9 places the bottom face on the plane. Do not introduce model loading in this plan.

Milestone 6 updates docs and validates. Update `C:\dev\ofg\docs\SYSTEMS.md` and `C:\dev\ofg\docs\API_CONTRACTS.md` to describe `ControlInput`, `Player`, camera mode ownership, and the component update path. Update browser and native smoke expectations if the additional visible player box changes pixel ratios. Run unit tests, smoke tests, formatting, and coverage. Capture screenshot artifacts during visual work and present them in chat.

## Concrete Steps

Run all commands from `C:\dev\ofg` unless a command says otherwise.

Start by checking the worktree and reading the active plan:

    git status --short
    Get-Content -Raw docs\plans\player-component-camera-controls-plan.md

After Milestone 1:

    npm run test:ts
    npm run test:cpp

Expected result: TypeScript tests pass, C++ tests pass, and existing debug camera behavior remains covered through updated `ControlInput` tests.

After each C++ edit batch:

    npm run format:cpp
    npm run format:cpp:check
    npm run test:cpp

Expected result: clang-format check passes and doctest/CTest reports success.

After each TypeScript input/runtime edit batch:

    npm run test:ts

Expected result: Mocha reports all TypeScript tests passing.

For visual milestones, keep a dev server available:

    npm run dev

Expected result: the command prints a local URL, normally `http://127.0.0.1:5173`. If port 5173 is busy, the server prints the next available port. Report that URL in chat when started or restarted.

Before final acceptance:

    npm test
    npm run smoke:browser
    npm run smoke:render
    npm run coverage

Expected result: unit/integration tests pass, browser smoke writes `C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png` and `report.json`, native render smoke writes `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png` and `report.json`, and coverage gates pass without modified implementation files appearing in the default attention output.

## Milestone Review

After each milestone:

1. Update this ExecPlan's Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective as needed.
2. Update `C:\dev\ofg\docs\API_CONTRACTS.md` or `C:\dev\ofg\docs\SYSTEMS.md` if the milestone changed ownership or public contracts.
3. Run the repo-local `milestone-review` skill against the milestone diff and this ExecPlan before marking the milestone complete.
4. Apply required findings before marking the milestone complete, or record a rejected finding with rationale in the Decision Log.
5. Re-run relevant validation commands after applying review findings.
6. Record the review summary, commands, screenshots, artifacts, and remaining risks in Progress or Outcomes & Retrospective.

Current milestone review record:

- 2026-07-01: Review found stale plan wording, stale TypeScript input source paths, missing pre-mutation control validation in the component update path, possible duplicate control consumption by future secondary players/cameras, sticky C++ camera-cycle input, first-person camera placement inside the temporary player mesh, and `BrowserGame` file-size pressure.
- Required fixes applied: renamed the TypeScript collector and tests to `controlInput`, updated docs/plan wording, moved validation to `Scene::update(...)` before component mutation, gated control consumption to the primary player and main camera, cleared `m_cycle_camera_mode` after one `Game` update, added a first-person forward eye offset, and recorded `BrowserGame` split pressure as a follow-up.
- Completion validation: formatting, unit tests, browser/native smoke, coverage, coverage-summary refresh, and screenshot inspection are complete for this plan.

## Validation and Acceptance

The plan is complete only when all of these observable behaviors and validation gates are true.

The browser app shows the existing ground/cube scene plus a player-sized box standing on the ground plane. The player box is visibly distinct from the four animated cubes.

The browser app uses one scene camera entity. Pressing `` ` `` cycles the active camera mode through debug, first person, third person, and back to debug. Repeated holding should not cycle continuously unless the final implementation explicitly documents browser key repeat behavior and tests it.

Debug mode preserves the existing fly camera controls: W/A/S/D, Space/C vertical movement, mouse look while pointer locked, and fast/slow modifiers.

First-person mode moves the player on the flat XZ plane with W/A/S/D and rotates view with the mouse. The camera stays at a plausible eye-height offset relative to the player.

Third-person mode moves the same player on the flat XZ plane with W/A/S/D and follows from behind/above using the mouse-controlled yaw/pitch. There is no collision avoidance, camera obstruction handling, jumping, or terrain adaptation in this plan.

The TypeScript host exposes only raw `ControlInput` collection and forwarding. It does not own player position, camera mode behavior, scene graph mutation, renderer resources, or draw submission.

The C++ runtime owns the latest validated `ControlInput` snapshot on `Game`, `Player`, camera control mode state, control interpretation, component updates, and scene transforms. `Scene::update(...)` must update player components before camera components so same-frame camera follow observes player movement.

Validation commands that must pass:

    npm run format:cpp:check
    npm test
    npm run smoke:browser
    npm run smoke:render
    npm run coverage

Coverage acceptance: modified implementation files must not appear in the default filtered coverage attention output because they meet the documented threshold, currently about 90% line coverage. If a file needs an exception, record the exact file, reason, and compensating smoke/test evidence in the Decision Log before completion.

Screenshot acceptance: during implementation, capture and present screenshots after the first visible player box, after first/third-person controls work, and at final browser smoke. Durable screenshot artifacts should be stored under `C:\dev\ofg\artifacts\browser-smoke` or a clearly named subdirectory such as `C:\dev\ofg\artifacts\player-controls`. The human reviewer should verify the player box is on the ground, the scene remains readable, and camera modes are visually distinct.

## Idempotence and Recovery

Most steps are additive and can be retried. If formatting changes too much, inspect `git diff` and keep only relevant source formatting. Do not revert unrelated user work, especially the pre-existing untracked `C:\dev\ofg\assets\` tree.

If the input rename becomes too large, preserve a temporary compatibility wrapper in TypeScript or C++ and record the decision. The final state should still use `ControlInput` as the conceptual contract.

If browser smoke thresholds fail only because the player box adds expected visible pixels, inspect the generated screenshot and update `C:\dev\ofg\tools\smoke-contract.json` only with evidence recorded in this plan. Do not weaken smoke thresholds to hide a blank or broken render.

If the component update pass grows beyond `Player` and `Camera`, stop and record a design decision before creating a generic scheduler. The intended first implementation is explicit and small.

If WebGPU smoke cannot run because no Chromium-family browser is installed, record the failure, run all unit tests and native render smoke, and ask the user whether to install/configure `OFG_BROWSER_PATH`. Do not mark the visual milestone complete without browser screenshot evidence unless the user explicitly accepts that gap.

## Artifacts and Notes

Initial worktree note:

    2026-07-01: `git status --short` showed only `?? assets/` before this ExecPlan was added.

Planned/final browser screenshot artifacts:

    C:\dev\ofg\artifacts\browser-smoke\player-box-debug.png
    C:\dev\ofg\artifacts\browser-smoke\first-person-mode.png
    C:\dev\ofg\artifacts\browser-smoke\third-person-mode.png
    C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png

Planned smoke reports:

    C:\dev\ofg\artifacts\browser-smoke\report.json
    C:\dev\ofg\artifacts\render-smoke\report.json

## Interfaces and Dependencies

At the end of this plan, these stable interfaces should exist or have an equivalent documented final name.

`C:\dev\ofg\cpp\include\ofg\core\control_input.hpp`:

    namespace ofg {
    struct ControlInput {
        float m_move_x{0.0f};
        float m_move_y{0.0f};
        float m_move_z{0.0f};
        float m_look_delta_x{0.0f};
        float m_look_delta_y{0.0f};
        bool m_look_active{false};
        bool m_fast{false};
        bool m_slow{false};
        bool m_cycle_camera_mode{false};
    };
    void validate_control_input(ControlInput input);
    }

The validation function throws `EngineError` when any numeric field is non-finite. It does not clamp, log-and-ignore, or silently replace invalid values.

`C:\dev\ofg\cpp\include\ofg\scene\player.hpp`:

    namespace ofg {
    class Player : public Component {
    public:
        explicit Player(Entity* entity) noexcept;
        void update(const SceneUpdateContext& context);
    };
    }

`Player::update(...)` reads movement controls from `context.m_controls`, applies player movement mechanics, and clears no separate request state because movement is derived directly from the current frame's stored controls. Player movement mechanics must remain in `Player`, not in `Game` or TypeScript.

`C:\dev\ofg\cpp\include\ofg\scene\camera.hpp` or a nearby camera-controls header:

    namespace ofg {
    enum class CameraControlMode {
        Debug,
        FirstPerson,
        ThirdPerson,
    };

    class Camera : public Component {
    public:
        CameraControlMode control_mode() const noexcept;
        void set_control_mode(CameraControlMode mode);
        void update(const SceneUpdateContext& context);
    };
    }

The exact helper class layout may change, but the final code must support camera mode cycling, debug behavior, first-person behavior, and third-person behavior from one camera entity without adding a second update phase unless a concrete ordering need is recorded in the Decision Log.

`C:\dev\ofg\cpp\include\ofg\scene\scene.hpp` should expose enough update plumbing for `Game::update_impl` to update scene components without reaching into every component manually. A narrow explicit API such as `Scene::update(const SceneUpdateContext& context)` is preferred for the first implementation. Its contract is explicit: update every `Player` component first, then update every `Camera` component. Do not add a second camera phase for this plan unless a concrete ordering problem is discovered and recorded in the Decision Log.

`C:\dev\ofg\cpp\include\ofg\scene\scene_update.hpp` should define the first update context. The shape may evolve, but it should include these concepts:

    namespace ofg {
    struct SceneUpdateContext {
        const ControlInput& m_controls;
        double m_time_ms;
        float m_delta_seconds;
        Player* m_primary_player;
        Camera* m_main_camera;
    };
    }

The `controls` reference points at `Game`'s latest validated stored snapshot and remains valid for the duration of `Game::update_impl`. The `primary_player` and `main_camera` pointers are non-owning pointers to entities/components in the current scene generation; implementation should validate or refresh cached bindings when the scene generation changes.

`C:\dev\ofg\src\app\wasmRuntime.ts` should expose `ControlInput` and a runtime method such as `setControlInput(input: ControlInput): void`. The raw Embind method may use scalar arguments, but the app-facing TypeScript wrapper should validate finite numeric fields before crossing into WASM.

`C:\dev\ofg\src\app\controlInput.ts` collects DOM keyboard/mouse state into generalized control input. Its public comments and exported types describe generalized control input, not debug-camera-only behavior.

`C:\dev\ofg\cpp\src\web\embind_module.cpp` and `C:\dev\ofg\cpp\src\web\browser_game.cpp` should expose and parse the generalized control input fields, including the backquote cycle edge.

`C:\dev\ofg\cpp\src\runtime\runtime_debug_status.cpp` and `C:\dev\ofg\src\app\wasmRuntime.ts` should include a stable debug-status field such as `cameraMode`, with values `debug`, `first_person`, and `third_person`, so tests and smoke can observe the active mode without inspecting scene internals.
