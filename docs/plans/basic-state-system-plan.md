# Add Basic Hierarchical State System

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

Maintain this document in accordance with `C:\dev\ofg\PLANS.md`.

## Purpose / Big Picture

After this change, OFG has a reusable C++ hierarchical state system that can express whole-game flow such as booting, playing, paused, editor mode, transient menus, and future loading or transition phases. A state has one primary child state and any number of indexed substates that run alongside that child. A parent can request replacement of a child or substate without tearing the old state down abruptly: the existing state is asked to leave, finishes its leave callbacks, and only then is the pending replacement activated.

The first user-visible behavior should remain the same browser scene as before. The architectural result is that `Game` no longer directly hardcodes the future high-level flow in `game.cpp`. Instead, `Game` owns a root state and starts the runtime by spawning a boot flow state, which can then hand off to a `GameState`. `GameState` represents the in-game container and owns one active `LevelState` child. The current demo scene becomes `DemoLevelState`, our first concrete level. Future pause/editor/menu flows can be added as children or substates of the appropriate state rather than expanding the `Game` facade.

This is mostly native C++ runtime work. There is no intended visual redesign, so screenshots are not required unless implementation changes browser-rendered output or debug UI. Browser smoke should still run before final acceptance to prove the state integration did not break startup, WASM loading, model-resource loading, rendering, or the debug-status facade.

## Progress

- [x] (2026-07-04 09:25Z) Authored this proposed ExecPlan after reading `C:\dev\ofg\PLANS.md`, `C:\dev\ofg\docs\GUIDES.md`, `C:\dev\ofg\docs\API_CONTRACTS.md`, `C:\dev\ofg\docs\SYSTEMS.md`, the current `Game` facade, scene update context, and current active plan style.
- [ ] Implement the portable state-machine core and native doctests.
- [ ] Integrate root, boot, `GameState`, `LevelState`, and `DemoLevelState` into the `Game` lifecycle without moving platform frame-driver ownership out of `BrowserGame` or native smoke.
- [ ] Update active docs and contracts.
- [ ] Run milestone review after each implementation milestone and apply required findings.
- [ ] Run formatting, tests, browser/native smoke, and coverage.

## Surprises & Discoveries

- Observation: The worktree already contained unrelated active bloom/render changes before this plan was added.
  Evidence: `git status --short` on 2026-07-04 showed modified renderer, docs, and bloom files plus untracked bloom/temp-buffer files. Implementation of this plan must not revert or reformat those unrelated changes.

- Observation: `Game` is already documented as a thin facade and `docs\GUIDES.md` explicitly warns against letting `game.cpp` collect feature-specific behavior.
  Evidence: `C:\dev\ofg\docs\GUIDES.md` has a "Facade ownership" rule dated 2026-07-04, and `OFG-BOOT-002` says `Game` owns orchestration/status glue while gameplay behavior belongs behind owned APIs.

- Observation: The current runtime already has `ControlInput`, `Player`, `Camera`, `Scene::update`, and player-model loading owned in C++.
  Evidence: `C:\dev\ofg\cpp\include\ofg\scene\scene_update.hpp`, `C:\dev\ofg\cpp\include\ofg\scene\player.hpp`, and `C:\dev\ofg\cpp\src\game\game.cpp` show the current per-frame update path.

## Decision Log

- Decision: Implement the state system as a portable C++ module under `cpp/include/ofg/state` and `cpp/src/state`, not as private code inside `Game`.
  Rationale: The state machine is a reusable runtime primitive. Keeping it independent protects `Game` from becoming a dumping ground and lets doctests cover almost all behavior without WebGPU.
  Date/Author: 2026-07-04 / Codex

- Decision: Use repo-style C++ names for override hooks: `on_enter_start`, `on_enter`, `on_enter_end`, `on_main`, `on_leave_start`, `on_leave`, and `on_leave_end`.
  Rationale: The requested API names map directly to these hooks, but `C:\dev\ofg\AGENTS.md` requires functions to use lowercase_with_underscores. Comments should name the conceptual `OnEnterStart` style terms so the design stays recognizable.
  Date/Author: 2026-07-04 / Codex

- Decision: Use `std::unique_ptr<State>` for spawn APIs and return raw observing pointers from successful spawn calls.
  Rationale: The user sketch used `State*`, but ownership is otherwise ambiguous. The parent should own active and pending child states deterministically. Returning a raw pointer keeps convenience without transferring ownership back to callers.
  Date/Author: 2026-07-04 / Codex

- Decision: Default state behavior should finish enter and leave immediately, but remain in main until a derived state asks to leave or returns true from `on_main`.
  Rationale: A plain root state should be able to host a child indefinitely. Tests can still prove that a state whose enter, main, and leave callbacks all return true finishes within one update call.
  Date/Author: 2026-07-04 / Codex

- Decision: `leave()` is idempotent and state callbacks can see that leave was requested.
  Rationale: A state can be asked to leave while it is still entering. It must complete enter before leave starts, but it may use `leave_requested()` to finish an expensive enter quickly.
  Date/Author: 2026-07-04 / Codex

- Decision: Pending children and pending substates are discarded if their owner begins leaving before they are activated.
  Rationale: If a state skips main after enter because it was asked to leave, pending descendants should not be activated only to immediately leave. Active descendants still receive `leave()` and must finish before the parent starts its own leave callbacks.
  Date/Author: 2026-07-04 / Codex

- Decision: Use deterministic indexed substates backed by ordered storage.
  Rationale: Substates are keyed by an integer slot. Updating slots in ascending key order gives repeatable tests and predictable behavior when multiple parallel substates are active.
  Date/Author: 2026-07-04 / Codex

- Decision: Add a bounded same-frame transition budget.
  Rationale: The state machine must support immediate enter, main, replacement, and leave work in one frame, but accidental self-replacement loops should fail clearly instead of hanging the browser or native tests.
  Date/Author: 2026-07-04 / Codex

- Decision: The first Game integration should create `RootState`, `BootFlowState`, `GameState`, `LevelState`, and `DemoLevelState`.
  Rationale: `GameState` is the in-game container and `LevelState` is the contract for level-specific scene ownership. `DemoLevelState` is simply the first level. Proving this root-to-game-to-level path keeps the first integration small while leaving pause, editor, UI, multiplayer, and level-loading decisions for follow-up plans.
  Date/Author: 2026-07-04 / Codex

## Outcomes & Retrospective

Not implemented yet. This section should be updated after the state core and Game integration land, including any API changes made during implementation, validation command results, coverage notes, and remaining follow-ups such as pause/editor state implementations.

## Contract and Quality Baseline

This plan preserves and extends the active contracts in `C:\dev\ofg\docs\API_CONTRACTS.md`.

`OFG-BOOT-001 TypeScript Host Ownership` is preserved. TypeScript still owns DOM boot, raw input collection, WASM loading, resize, and blob fetch transport. It must not choose game state transitions, own pause/editor behavior, mutate scene graphs, or inspect state objects.

`OFG-BOOT-002 C++ Runtime Ownership` is extended. C++ will own the hierarchical state system, root state, boot flow state, in-game state, level state contract, and first demo level state. `Game` may hold the root state and report compact status, but specific state transition behavior belongs in state classes or a small game-flow module, not in broad `game.cpp` branches.

`OFG-BOOT-003 WASM Facade` is preserved unless implementation adds an optional diagnostic debug-status field. The facade must not expose mutable state pointers or let TypeScript spawn C++ states directly. If a debug field such as `gameStatePath` is added, it is read-only diagnostics.

`OFG-BOOT-004 Renderer Compatibility` and `OFG-BOOT-005 WebGPU Baseline` are preserved. The rendered scene, WebGPU feature requirements, target acquisition, and renderer pass behavior should not change as part of the state core. Browser/native smoke should produce the same visual class of output.

`OFG-BOOT-006 Resource Lifetime` is preserved. State transitions may create, update, or clear scene-level objects through existing runtime/resource APIs, but they must not recreate durable renderer or resource objects every frame. `Resources::advance_loads()` should still run once per accepted `Game::update` before gameplay components observe resource state.

`OFG-BOOT-009 Coverage` applies. New C++ implementation files in `cpp/src/state` and any modified portable game-flow files must pass the default C++ coverage attention gate unless this plan records a specific exception with rationale. Browser-only glue remains validated through build and smoke where coverage cannot directly execute it.

The readability and naming rules in `C:\dev\ofg\AGENTS.md` and `C:\dev\ofg\docs\GUIDES.md` apply: C++ uses four-space clang-format, class names are CamelCase, function names are lowercase_with_underscores, member names use `m_`, files need purpose comments, and functions need comments describing their purpose.

## Context and Orientation

The repository root is `C:\dev\ofg`.

The current browser/native runtime uses `Game` as a static C++ facade. Its public interface is in `C:\dev\ofg\cpp\include\ofg\game\game.hpp`, and its implementation is in `C:\dev\ofg\cpp\src\game\game.cpp`. `BrowserGame` and native smoke create the WebGPU device and call `Game::create`, repeatedly call `Game::prepare` until ready, then call `Game::update` and `Game::render` for frames. `BrowserGame` owns browser-specific surface acquisition and queue submission; that must not move into the state system.

`Game::prepare_impl` currently prepares `Resources`, creates the current demo `Scene`, calls `build_demo_scene`, `setup_demo_scene`, and `update_demo_scene`, then prepares `Renderer`. `Game::update_impl` validates time, advances `Resources::advance_loads`, updates demo transforms, calls `Scene::update`, publishes player/camera debug status, and clears one-frame input edges. The state integration should move the high-level "which flow is active" responsibility out of direct `Game` branches without hiding the existing frame order.

Scene and component update code lives under `C:\dev\ofg\cpp\include\ofg\scene` and `C:\dev\ofg\cpp\src\scene`. `Scene::update` validates controls, updates `Environment`, updates players, updates animation players, updates CPU-skinned mesh renderers, then updates cameras. This order is part of the active contract and should stay intact.

Renderer demo setup lives in `C:\dev\ofg\cpp\include\ofg\render\demo_scene.hpp` and `C:\dev\ofg\cpp\src\render\demo_scene.cpp`. It is acceptable for `DemoLevelState` to reuse the current demo scene functions so this plan does not mix state-machine work with scene content changes.

Definitions used by this plan:

`State` means a C++ object that has enter, main, and leave phases. Enter and leave can span multiple frames by returning false from their repeated hook. Main can span indefinitely by returning false from `on_main`; returning true from `on_main` means this state's main work is complete and it should leave.

`Root state` means the top-level state object owned by `Game`. It usually stays alive for the whole runtime and hosts one active child flow.

`Child state` means the one primary child owned by a state. A child is mutually exclusive with any pending replacement child.

`Substate` means an indexed auxiliary state owned by a parent and updated alongside the child. For example, a future gameplay state might have a pause-menu substate or notification overlay substate while keeping the main gameplay child alive.

`Pending state` means a child or substate that has been accepted by a parent but cannot activate yet because the parent has not completed enter or because an existing state in that slot is still leaving.

`Inhibit control on child` means a boolean option on a parent state. When true and the parent has an active primary child, the parent updates descendants but does not call its own `on_main`. Substates do not count as the primary child for this option.

## Plan of Work

Milestone 1 adds the portable state core. Create `C:\dev\ofg\cpp\include\ofg\state\state.hpp` and `C:\dev\ofg\cpp\src\state\state.cpp`, add them to `C:\dev\ofg\cpp\CMakeLists.txt`, and add native doctests in `C:\dev\ofg\cpp\tests\state_machine_test.cpp`. The core should be independent of WebGPU, `Game`, `Scene`, `Resources`, and TypeScript. It may depend on `EngineError` for validation failures.

The `State` class should be non-copyable and non-movable. It owns active child, pending child, active substates, and pending substates as `std::unique_ptr<State>`. It stores a raw parent pointer because the parent owns the child. It exposes accessors for parent, active child, pending child presence, substate lookup, phase, leave-requested status, finished status, and the inhibit option. The class should provide `leave`, `spawn_child`, `spawn_substate`, `spawn_sibling`, and `update`.

Milestone 2 hardens lifecycle semantics with focused tests. Cover immediate single-frame completion, multi-frame enter, multi-frame leave, leave requested during enter, active child replacement, pending child replacement by a newer pending child, substate replacement by index, parent leave waiting for active descendants, pending descendants discarded when parent leaves before activation, `spawn_sibling` replacing the current active child through the parent, deterministic substate update order, `inhibit_control_on_child`, null spawn rejection, and recursive update rejection. Include a transition-budget test or implementation guard so immediate replacement loops cannot hang.

Milestone 3 adds the first game-flow state classes without broad gameplay changes. Add a small game-flow module such as `C:\dev\ofg\cpp\include\ofg\game\game_flow.hpp` and `C:\dev\ofg\cpp\src\game\game_flow.cpp`, or keep private classes in `game_flow.cpp` if no public type is needed. The first concrete states should be:

`RootState`, a thin host state whose default main does not complete and whose only job is to own the active high-level flow.

`BootFlowState`, an initial child of the root. For the first implementation it may immediately hand off to `GameState`. It exists so later boot/loading/account/server setup can grow without changing root ownership.

`GameState`, the in-game container. It owns broad game-session state and starts one active `LevelState` child. Later pause/menu/editor behavior can be modeled as children or substates of `GameState` once those features are designed.

`LevelState`, the abstract base for level-specific scene ownership. It should define the contract for creating, updating, and leaving the active level scene without knowing whether that level is a demo, generated world, editor sandbox, or multiplayer-hosted level.

`DemoLevelState`, the first concrete level. This replaces the earlier placeholder name `DemoGameState`. It inherits from `LevelState`, reuses the existing demo scene setup and per-frame update behavior, and must not move renderer command encoding, browser surface acquisition, or queue submission into state code.

The concrete game states need access to a narrow host/service object rather than all of `Game`. A small service interface can expose only what these states need, such as creating the demo scene, updating the current scene for the frame, clearing the current scene during leave, reading current time/delta/control input, and publishing active-scene diagnostics. If this is overkill during implementation, keep helper functions private to the game-flow module and record the simpler decision here.

Milestone 4 integrates the root state into `Game`. Add a `std::unique_ptr<State> m_root_state` or equivalent concrete root owner to `Game`. During prepare, after `Resources::prepare()` is complete and before `Renderer::prepare()` needs scene resources, create the root state, spawn `BootFlowState`, and update the root until `BootFlowState` has handed off to `GameState` and `GameState` has activated its initial `DemoLevelState` child, or until a state reports it is still asynchronously entering. During `Game::update_impl`, keep the existing frame order: tick runtime, compute delta, advance resource loads, update the root state, publish active scene status, clear one-frame control edges, and record the latest time. During render, `Game` still validates the target and passes the active scene to `Renderer::render`.

Milestone 5 integrates release and diagnostics. `Game::release_impl` should request root-state leave and update it until finished before clearing the scene and destroying the root. This ensures active game states receive leave callbacks instead of being silently destroyed during ordinary runtime shutdown. If the existing `GameLifecycleState` values are not expressive enough, add a compact lifecycle step such as `Rel_States`; otherwise use the existing scene-release phase and document the exact ordering. Optionally add a read-only debug-status field such as `gameStatePath` if tests or browser diagnostics need to observe the active high-level state without exposing state pointers.

Milestone 6 updates documentation and validates. Update `C:\dev\ofg\docs\SYSTEMS.md` with a new CppStateSystem or CppGameFlow section, and update `C:\dev\ofg\docs\API_CONTRACTS.md` to say C++ owns high-level game-flow state transitions. Run the repo formatting, test, smoke, and coverage gates. Because this is runtime behavior, run browser and native smoke even if visuals are intended to be unchanged.

## Concrete Steps

Run all commands from `C:\dev\ofg` unless a command says otherwise.

Before implementation:

    git status --short
    Get-Content -Raw docs\plans\basic-state-system-plan.md

After adding the state core and tests:

    npm run format:cpp
    npm run format:cpp:check
    npm run test:cpp

Expected result: clang-format check passes and doctest/CTest reports success. The new `state_machine_test.cpp` cases should cover the seven hook lifecycle, child/substate replacement, leave drainage, and inhibit behavior.

After integrating state flow with `Game`:

    npm run format:cpp:check
    npm test

Expected result: C++ and TypeScript tests pass. Existing Game tests should still prove prepare, update, render, debug status, and control-edge consumption.

For browser review if a dev server is needed:

    npm run dev

Expected result: the command prints a local URL, normally `http://127.0.0.1:5173`. If port 5173 is busy, the server prints the next available port. Report the URL in chat when started or restarted.

Before final acceptance:

    npm run format:cpp:check
    npm test
    npm run smoke:browser
    npm run smoke:render
    npm run coverage

Expected result: all commands pass. Browser smoke should still report loaded player model status and no fatal debug error. Native render smoke should still write a passing report under `C:\dev\ofg\artifacts\render-smoke`.

## Milestone Review

After each implementation milestone:

1. Update this ExecPlan's Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective as needed.
2. Update `C:\dev\ofg\docs\API_CONTRACTS.md` or `C:\dev\ofg\docs\SYSTEMS.md` if the milestone changed ownership, public interfaces, lifecycle ordering, or debug-status fields.
3. Run the repo-local `milestone-review` skill against the milestone diff and this ExecPlan before marking the milestone complete.
4. Apply required findings before marking the milestone complete, or record a rejected finding with rationale in the Decision Log.
5. Re-run relevant validation commands after applying review findings.
6. Record the review summary, commands, artifacts, and remaining risks in Progress or Outcomes & Retrospective.

## Validation and Acceptance

The portable state core is accepted when native tests prove:

An immediate state that returns true from enter, main, and leave finishes in one `update()` call, and the hook order is exactly enter start, enter, enter end, main, leave start, leave, leave end.

A state that returns false from `on_enter` remains entering across frames, does not run main, and does not activate pending child or pending substates until enter completes.

Calling `leave()` while a state is entering sets a visible leave-requested flag, still allows enter to complete, skips main, discards never-activated pending descendants, then runs local leave.

Calling `leave()` on a parent with active descendants calls `leave()` on the child and active substates, waits for them to finish, and only then calls the parent's `on_leave_start`.

Calling `spawn_child` while a child is active stores a pending child and asks the active child to leave. If another child is spawned before the active child finishes, the pending child is replaced by the newest pending child. The replacement activates only after the old active child finishes.

Calling `spawn_substate(index, state)` follows the same replacement semantics per index. Replacing substate 2 does not affect substate 1, and active substates update in deterministic index order.

`spawn_sibling` works only for the active primary child and delegates to the parent child replacement path. Invalid sibling spawns fail clearly.

When `inhibit_control_on_child` is true, a parent with an active primary child does not call its own `on_main`, but it still updates that child and active substates. When the child finishes, parent main resumes on a later update.

The Game integration is accepted when:

`Game` owns a root state for the active runtime lifetime and starts by spawning `BootFlowState`.

The boot flow hands off to `GameState`, and `GameState` activates `DemoLevelState` as its initial `LevelState` child without changing the visible startup result.

`Game::update_impl` advances resources before scene components observe resource state, as required by `OFG-BOOT-006`.

`Game::render_impl` still renders the active scene through `Renderer::render`, and browser/native frame drivers still own command-buffer finish, queue submit, presentation, and readback.

`Game::release_impl` asks the root state tree to leave before clearing scene-owned runtime state.

No mutable state objects, child pointers, substate pointers, renderer internals, or scene ownership are exposed to TypeScript.

Validation commands that must pass:

    npm run format:cpp:check
    npm test
    npm run smoke:browser
    npm run smoke:render
    npm run coverage

Coverage acceptance: new and modified C++ implementation files must not appear in the default filtered coverage attention output because they meet the documented threshold, currently about 90% line coverage. If an implementation file is intentionally smoke-only or difficult to exercise through native tests, record the exact file, reason, and compensating test/smoke evidence in the Decision Log before completion.

Screenshot acceptance: no screenshot trail is required unless the implementation changes browser UI, rendering, visual output, or deployment output. If the rendered scene changes unexpectedly, capture browser screenshots under `C:\dev\ofg\artifacts\browser-smoke` or a named subdirectory and present them in chat before finalizing.

## Idempotence and Recovery

The state core should be additive. If implementation discovers that the API names or file layout need to change, update the Interfaces and Dependencies section before continuing so the plan remains self-contained.

Because the worktree already contains unrelated renderer/bloom changes, inspect `git diff --stat` and `git status --short` before large edits. Do not revert, reformat, or rename unrelated files.

If the Game integration becomes too large, stop after the portable state core and record a follow-up milestone. It is better to land a heavily tested independent state machine than to hide broad gameplay-flow churn inside the same change.

If browser smoke fails with a visual difference, inspect the generated screenshot and report. Do not weaken smoke thresholds unless the screenshot proves the change is expected and this plan records the rationale.

If coverage fails on a new state file, add native doctests. The state core should not need a coverage exception because it is device-independent.

## Artifacts and Notes

Initial worktree note:

    2026-07-04: `git status --short` showed unrelated modified and untracked bloom/render/doc files before this plan was added.

Likely new source files:

    C:\dev\ofg\cpp\include\ofg\state\state.hpp
    C:\dev\ofg\cpp\src\state\state.cpp
    C:\dev\ofg\cpp\tests\state_machine_test.cpp
    C:\dev\ofg\cpp\include\ofg\game\game_flow.hpp
    C:\dev\ofg\cpp\src\game\game_flow.cpp

Final smoke artifacts should remain the standard ones unless implementation adds state-specific diagnostics:

    C:\dev\ofg\artifacts\browser-smoke\report.json
    C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png
    C:\dev\ofg\artifacts\render-smoke\report.json
    C:\dev\ofg\artifacts\render-smoke\opaque-demo.png

## Interfaces and Dependencies

At the end of the portable state milestone, this interface or an intentionally documented equivalent should exist.

    namespace ofg {

    enum class StatePhase {
        Entering,
        Main,
        Leaving,
        Finished,
    };

    [[nodiscard]] const char* state_phase_name(StatePhase phase) noexcept;

    class State {
    public:
        State(const State&) = delete;
        State& operator=(const State&) = delete;
        State(State&&) = delete;
        State& operator=(State&&) = delete;
        virtual ~State();

        void leave();

        [[nodiscard]] State* spawn_child(std::unique_ptr<State> state);
        [[nodiscard]] State* spawn_substate(int substate_index, std::unique_ptr<State> state);
        [[nodiscard]] State* spawn_sibling(std::unique_ptr<State> state);

        void update();

        [[nodiscard]] State* parent() noexcept;
        [[nodiscard]] const State* parent() const noexcept;
        [[nodiscard]] State* child() noexcept;
        [[nodiscard]] const State* child() const noexcept;
        [[nodiscard]] State* substate(int substate_index) noexcept;
        [[nodiscard]] const State* substate(int substate_index) const noexcept;
        [[nodiscard]] StatePhase phase() const noexcept;
        [[nodiscard]] bool leave_requested() const noexcept;
        [[nodiscard]] bool finished() const noexcept;
        [[nodiscard]] bool has_pending_child() const noexcept;
        [[nodiscard]] bool inhibit_control_on_child() const noexcept;
        void set_inhibit_control_on_child(bool inhibit) noexcept;

    protected:
        virtual void on_enter_start();
        virtual bool on_enter();
        virtual void on_enter_end();
        virtual bool on_main();
        virtual void on_leave_start();
        virtual bool on_leave();
        virtual void on_leave_end();
    };

    }

The exact enum may include more internal-facing phases if implementation needs to distinguish "leave requested but waiting for descendants" from "local leave callbacks are running." If so, update this plan and tests so public diagnostics are explicit.

Template helpers may be added for ergonomics:

    template <typename T, typename... Args>
    [[nodiscard]] T& emplace_child(Args&&... args);

    template <typename T, typename... Args>
    [[nodiscard]] T& emplace_substate(int substate_index, Args&&... args);

These helpers should forward to the unique-pointer spawn APIs and should not obscure ownership.

At the end of Game integration, the stable concepts should exist even if concrete class names shift:

`RootState` is owned by `Game` for the runtime lifetime.

`BootFlowState` is the first root child and can hand off to `GameState` by spawning a sibling.

`GameState` represents the in-game container and owns one active `LevelState` child.

`LevelState` is the base type for level-specific scene ownership.

`DemoLevelState` inherits from `LevelState`, represents the current default playable level, and reuses existing scene/component/resource update order.

`Game` remains the lifecycle facade used by browser and native frame drivers. It can own the root state and compact diagnostics, but browser-specific frame-driver work stays in `BrowserGame`, renderer command recording stays in `Renderer`, resource loading stays in `Resources`, and player/camera behavior stays in their scene components.
