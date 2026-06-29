# Clean Up C++ Static Singleton Facades

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

If PLANS.md is present in the repo, maintain this document in accordance with it and link back to it by path.

## Purpose / Big Picture

The previous static singleton lifecycle refactor established the broad shape of `Game`, `Resources`, and `Renderer`, but it left too much lifecycle logic in public static methods and kept migration-era helper classes that now obscure ownership. This plan tightens the architecture so the public static APIs are thin facades over instance methods, while the instance objects own all real state and lifecycle behavior.

After this cleanup, public calls should be simple and predictable:

    Game::create(...);
    Game::prepare();
    Resources::create_texture("checker");
    Renderer::render(encoder, target, scene);

The lifecycle vocabulary is `create`, `prepare`, `release`, and `destroy`. `create` is a single-shot call that creates and initializes the singleton instance and returns `void`, throwing on failure. `prepare` is a multi-call function that returns `true` when preparation is complete. `release` is a multi-call function that returns `true` when release is complete. `destroy` is a single-shot call that destroys the already released singleton instance.

Each public static method should do little more than fetch the singleton and forward to an instance `_impl` method, except for static lifecycle edges where the singleton pointer itself must be created or destroyed. Tiny accessors may remain static-only when forwarding would duplicate code, but lifecycle decisions, validation, state transitions, frame/debug state, resource storage, and render work should live in non-static instance code. There should be no duplicated static and non-static lifecycle state for the same live singleton. In the normal case, the only static data member on each facade should be its singleton pointer, such as `s_game`, `s_resources`, or `s_renderer`; any additional static member is a problem indicator and needs explicit justification.

`GameRuntime` and `ResourceArena` were useful as migration scaffolding, but they no longer appear to earn their architectural split. This plan removes them unless implementation uncovers a concrete reason to keep a smaller, clearly named helper. The expected end state is that `Game` directly owns frame/debug/runtime status and `Resources` directly owns stable vectors of textures, shaders, materials, and meshes.

## Progress

- [x] (2026-06-28 21:45Z) Reviewed the user's correction: static public methods should forward to `_impl`; `GameRuntime` and `ResourceArena` should be justified or removed; prepare/release should be explicit instance state machines; repeated lifecycle checks in resource methods should be centralized.
- [x] (2026-06-28 21:50Z) Inspected current `Game`, `Renderer`, `Resources`, `GameRuntime`, and `ResourceArena` code and confirmed the deviations are present.
- [x] (2026-06-28 21:55Z) Created this follow-up ExecPlan at `C:\dev\ofg\docs\plans\cpp-singleton-facade-cleanup-plan.md`.
- [x] (2026-06-28 22:05Z) Clarified release/destroy ordering: top-level browser `dispose()` drains `Game::release()` first; `Game::release_impl()` drains renderer then resources; `Game::destroy_impl()` destroys renderer then resources after release is complete.
- [x] (2026-06-28 22:15Z) Clarified lifecycle naming: public singleton `init` methods should become single-shot throwing `create` methods, paired with single-shot `destroy`.
- [x] (2026-06-28 23:34Z) Milestone 1: cleaned up `Renderer` as the smallest model for thin static facade plus instance-owned state machines.
- [x] (2026-06-28 23:34Z) Milestone 2: cleaned up `Resources`, centralized resource lifecycle guards, and removed the `ResourceArena` split.
- [x] (2026-06-28 23:34Z) Milestone 3: merged `GameRuntime` into `Game` and converted runtime helpers from bool/error-string helpers to throwing private `Game` methods.
- [x] (2026-06-28 23:34Z) Milestone 4: made `Game` static wrappers thin, removed duplicated static lifecycle state, and made `Game::prepare_impl` / `Game::release_impl` explicit switch-based state machines.
- [x] (2026-06-28 23:47Z) Milestone 5: updated docs/contracts/tests/coverage, ran final validation, inspected browser/native screenshots, and rebuilt Cloudflare packaging.

## Surprises & Discoveries

- Observation: The previous implementation stores lifecycle state twice for each singleton.
  Evidence: `cpp/include/ofg/game/game.hpp`, `cpp/include/ofg/render/renderer.hpp`, and `cpp/include/ofg/resources/resources.hpp` each declare both a static lifecycle state such as `s_state` and an instance lifecycle state such as `m_state`.

- Observation: Public static lifecycle methods still contain substantial lifecycle control flow.
  Evidence: `cpp/src/render/renderer.cpp::Renderer::prepare`, `cpp/src/resources/resources.cpp::Resources::prepare`, and `cpp/src/game/game.cpp::Game::prepare` switch on instance state, set states, call implementation functions, and handle failed/releasing cases directly.

- Observation: `GameRuntime` still carries old bool plus caller-filled error-string APIs.
  Evidence: `cpp/include/ofg/game/game_runtime.hpp` exposes methods such as `resize(..., std::string& error)`, `tick(..., std::string& error)`, and `mark_surface_configured(std::string& error)`, while `Game` immediately adapts those errors into exceptions.

- Observation: `ResourceArena` is now only stable storage behind `Resources`.
  Evidence: `cpp/include/ofg/resources/resources.hpp` stores `ResourceArena m_arena`, forwards `Resources::create_*` into `m_arena.add_*`, and exposes `Resources::arena()` as a narrow compatibility/diagnostic accessor.

- Observation: The milestone-review skill asks for `docs/ARCHITECTURE.md`, but this repository currently has no such file.
  Evidence: `Test-Path docs\ARCHITECTURE.md` returned false during the local review pass; review used `AGENTS.md`, `PLANS.md`, `docs/API_CONTRACTS.md`, `docs/SYSTEMS.md`, and this ExecPlan instead.

- Observation: The native C++ test and coverage gates rebuilt the local Dawn dependency from scratch, so short command timeouts can leave an active Ninja build holding `artifacts\build\cpp-native`.
  Evidence: early `npm run test:cpp` invocations timed out while `cmake`, `ninja`, and `clang-cl` were still active; rerunning with a 600 second timeout captured the passing CTest result.

## Decision Log

- Decision: Treat `GameRuntime` and `ResourceArena` as migration scaffolding to remove, not permanent architecture.
  Rationale: The accepted ownership model says `Game` owns game/runtime state and `Resources` owns resource allocation/storage. Keeping separate owner-like classes adds indirection and preserves old error-string patterns without buying clear reuse.
  Date/Author: 2026-06-28 / User and Codex

- Decision: Public static facade methods should be forwarding shells.
  Rationale: Singleton ergonomics are the public API, not the implementation model. Keeping logic in instance methods makes state ownership legible, reduces duplicated static state, and makes future non-blocking prepare/release stages easier to reason about.
  Date/Author: 2026-06-28 / User and Codex

- Decision: Release drains work; destroy only tears down already released systems.
  Rationale: Browser `dispose()` drains `Game::release()` first. `Game::release_impl()` drains `Renderer::release()` to completion, then `Resources::release()` to completion. Browser `dispose()` then calls `Game::destroy()`, and `Game::destroy_impl()` calls `Renderer::destroy()` followed by `Resources::destroy()`. `Renderer::destroy()` and `Resources::destroy()` should not be responsible for draining release in the normal shutdown path.
  Date/Author: 2026-06-28 / User and Codex

- Decision: Use `create` rather than `init` for singleton lifecycle creation.
  Rationale: The lifecycle bookends read more cleanly as `create` and `destroy`. `create` is single-shot, returns `void`, and throws on error; `prepare` and `release` remain resumable bool-returning operations; `destroy` is single-shot teardown after release.
  Date/Author: 2026-06-28 / User and Codex

- Decision: Singleton pointers should be the only normal static members on `Game`, `Renderer`, and `Resources`.
  Rationale: Lifecycle, status, error, resource, and render state belong to the instance. Extra static state, especially another lifecycle state, is a smell; if `Game` has more static members than `s_game`, or the equivalent is true for `Renderer`/`Resources`, the implementation should be fixed or the exception must be recorded in this plan.
  Date/Author: 2026-06-28 / User and Codex

- Decision: `prepare_impl` and `release_impl` should use explicit switch-based state machines.
  Rationale: The future non-blocking architecture needs visible stages that can return `false` and be resumed. The current "static wrapper sets state, calls void impl, sets ready" pattern hides the state machine in the wrong layer.
  Date/Author: 2026-06-28 / User and Codex

- Decision: Prefer private guard helpers over repeated lifecycle condition blocks for resource operations.
  Rationale: Repeated three-branch checks in each resource method are noisy and easy to drift. A single helper such as `require_live_for_create` or `require_ready_for_mutation` keeps policy explicit in one place. Use a macro only if call-site function names cannot be captured cleanly otherwise.
  Date/Author: 2026-06-28 / Codex

- Decision: Keep explicit resource data initialization method names such as `Texture::init_from_rgba8_pixels`, `Material::init_with_shader`, and `Mesh::init_with_geometry` outside the singleton lifecycle rename.
  Rationale: The `init` to `create` rename applies to the top-level singleton lifecycle entry points on `Game`, `Resources`, and `Renderer`. Resource objects are still allocated by `Resources::create_*`, then explicitly initialized with GPU or asset data by resource-specific methods; those methods are not singleton creation APIs.
  Date/Author: 2026-06-28 / Codex

## Outcomes & Retrospective

Completed. `Game`, `Resources`, and `Renderer` now expose singleton lifecycle APIs as `create`, `prepare`, `release`, and `destroy`, with `create` throwing on failure and `prepare`/`release` returning progress booleans. The old `GameRuntime` and `ResourceArena` owner-like splits were removed; `Game` directly owns frame/debug/runtime status and `Resources` directly owns stable vectors of high-level resources.

The public static methods are now thin facade calls except for the singleton creation/destruction edges and tiny accessors. Lifecycle state is instance-owned while the singleton is live, and the only normal static data members on the facades are `s_game`, `s_resources`, and `s_renderer`. Browser disposal drains `Game::release()` and then calls `Game::destroy()`, with `Game::release_impl()` releasing renderer before resources and `Game::destroy_impl()` destroying renderer before resources.

The main lesson was that the cleanup only became simple once the duplicated static state was removed. A smaller but useful correction was keeping resource object `init_*` data-upload methods distinct from singleton lifecycle `create`; that preserved the resource API shape while still cleaning the global lifecycle vocabulary. Remaining follow-up: `cpp/src/web/browser_game.cpp` is now 616 lines and should be considered for a focused browser-lifecycle split before much more code is added there.

## Contract and Quality Baseline

This plan preserves the static public API direction recorded in `docs/API_CONTRACTS.md`: `Game`, `Resources`, and `Renderer` remain public static lifecycle facades for a single active WebGPU device lifetime. It intentionally refines the internal ownership model so those facades delegate to instance methods instead of embedding lifecycle behavior in public static functions.

`OFG-BOOT-001 TypeScript Host Ownership` remains unchanged. TypeScript owns DOM boot, canvas sizing, runtime loading, and smoke helpers; it must not own renderer resources, scene state, or draw submission.

`OFG-BOOT-002 C++ Runtime Ownership` remains unchanged at the public contract level. C++ owns frame state, debug status, resource creation/storage, scene state, renderer pass setup, and WebGPU draw submission. Internally, this plan strengthens that contract by moving `GameRuntime` into `Game` and `ResourceArena` into `Resources`.

`OFG-BOOT-003 WASM Facade` remains unchanged. Browser `dispose()` drains `Game::release()` synchronously, calls `Game::destroy()`, then releases browser WebGPU handles and the Embind wrapper. Internally, `Game::release_impl()` drains renderer release before resources release, and `Game::destroy_impl()` destroys renderer before resources after release has already completed. Browser/native boundaries must continue to catch C++ exceptions and preserve useful debug status.

`OFG-BOOT-004 Renderer Compatibility` and `OFG-BOOT-005 WebGPU Baseline` remain visually unchanged. Browser and native smoke must still show the plane-and-cubes scene with the same WebGPU-only renderer path.

`OFG-BOOT-006 Resource Lifetime` is refined internally. `Resources` should directly own resource storage; creation still allocates high-level resource objects and explicit resource methods still create GPU data. Mip generation remains required by the texture contract and must not be dropped.

`OFG-BOOT-009 Coverage` applies. Modified implementation files must meet the coverage gate unless this plan records an explicit exception.

Quality constraints from `AGENTS.md` apply: C++ source/header files need maintained top-of-file purpose comments, public functions need useful comments, and large files should be watched. `cpp/src/native/render_smoke.cpp` and `cpp/src/web/browser_game.cpp` are already in the 500-1000 line concern range, so this plan should avoid growing them unless a split is included.

## Context and Orientation

The repository root is `C:\dev\ofg`. C++ code lives in `C:\dev\ofg\cpp`. Browser TypeScript lives in `C:\dev\ofg\src`. The previous static singleton plan is archived at `C:\dev\ofg\docs\archived\cpp-static-singleton-lifecycle-plan.md`.

The current static facades are:

    C:\dev\ofg\cpp\include\ofg\game\game.hpp
    C:\dev\ofg\cpp\src\game\game.cpp
    C:\dev\ofg\cpp\include\ofg\resources\resources.hpp
    C:\dev\ofg\cpp\src\resources\resources.cpp
    C:\dev\ofg\cpp\include\ofg\render\renderer.hpp
    C:\dev\ofg\cpp\src\render\renderer.cpp

The two migration-era helper splits to remove or explicitly rejustify are:

    C:\dev\ofg\cpp\include\ofg\game\game_runtime.hpp
    C:\dev\ofg\cpp\src\game\game_runtime.cpp
    C:\dev\ofg\cpp\include\ofg\resources\resource_arena.hpp
    C:\dev\ofg\cpp\src\resources\resource_arena.cpp

Current resources are high-level user-facing assets: `Texture`, `Shader`, `Material`, and `Mesh`. WebGPU handles such as texture views do not need full wrapper types by default.

Static facade means the public class has static methods such as `Renderer::create()` and `Renderer::prepare()` and private singleton storage such as `std::unique_ptr<Renderer> s_renderer`. The singleton pointer should normally be the only static data member on the facade; lifecycle state and debug/error/status data belong on the instance.

Forwarding shell means a public static method does almost no work beyond finding the singleton and calling an instance method:

    bool Renderer::prepare() {
        return require_renderer("Renderer::prepare").prepare_impl();
    }

Instance state machine means the instance method owns lifecycle transitions:

    bool Renderer::prepare_impl() {
        switch (m_state) {
        case RendererLifecycleState::Created:
            m_state = RendererLifecycleState::Preparing;
            [[fallthrough]];
        case RendererLifecycleState::Preparing:
            // do the current stage
            m_state = RendererLifecycleState::Ready;
            [[fallthrough]];
        case RendererLifecycleState::Ready:
            return true;
        default:
            throw EngineError(...);
        }
    }

## Plan of Work

Milestone 1 cleans up `Renderer` first because it is the smallest facade and can set the pattern for `Game` and `Resources`. Rename the singleton lifecycle entry from `Renderer::init` to `Renderer::create`; it should create the singleton, validate constructor inputs, return `void`, and throw on error. Remove `Renderer::s_state`; use only `m_state` while the singleton exists, and return `Uninitialized` when no singleton exists unless a live object was explicitly released and not yet destroyed. Rename lifecycle enum values that refer to the post-create state from `Initialized` to `Created`. Move the state switch from `Renderer::prepare()` into `Renderer::prepare_impl()` and make `prepare_impl()` return `bool`. Move release state transitions into `Renderer::release_impl()` and make it return `bool`. Keep `Renderer::destroy` as a tiny static lifecycle edge that destroys an already released renderer singleton and clears `s_renderer`; it must not be responsible for draining release in the normal shutdown path. Public static `resize`, `render`, `counters`, and `state` should remain tiny.

Milestone 2 cleans up `Resources` and removes `ResourceArena`. Rename the singleton lifecycle entry from `Resources::init` to `Resources::create`; it should create the singleton, validate constructor inputs, return `void`, and throw on error. Move the resource vectors from `ResourceArena` directly into `Resources`, along with `add_texture`, `add_shader`, `add_material`, `add_mesh`, `clear`, and diagnostic span accessors. Delete `Resources::arena()` unless a current caller truly requires it; if it remains temporarily, record it as a rejected cleanup with rationale. Remove `Resources::s_state`; use instance-owned state only. Rename lifecycle enum values that refer to the post-create state from `Initialized` to `Created`. Convert `Resources::prepare_impl()` and `Resources::release_impl()` to switch-based `bool` state machines. Keep `Resources::destroy` as a tiny static lifecycle edge that destroys an already released resources singleton and clears `s_resources`; it must not be responsible for draining release in the normal shutdown path. Replace repeated create/access lifecycle checks with one private helper, for example `require_live_for_create(const char* operation) const`, and have each static create method forward to an instance `create_texture_impl`, `create_shader_impl`, `create_material_impl`, or `create_mesh_impl`.

Milestone 3 merges `GameRuntime` into `Game`. Move `FrameState`, `RuntimeDebugStatus`, disposed state, GPU-ready state, surface-configured state, and runtime status helpers directly into `Game`. Convert `GameRuntime` methods that currently return `bool` plus `std::string& error` into private throwing `Game` methods, for example `resize_runtime`, `tick_runtime`, `mark_gpu_ready`, `mark_surface_configured`, and `mark_renderer_counters`. Remove `cpp/include/ofg/game/game_runtime.hpp` and `cpp/src/game/game_runtime.cpp` from CMake and tests, replacing tests with `Game`-level tests where the behavior remains externally relevant.

Milestone 4 makes `Game` follow the same thin-facade rule. Rename the singleton lifecycle entry from `Game::init` to `Game::create`; it should create the singleton, create owned systems as needed, return `void`, and throw on error. Public static `Game::prepare`, `resize`, `update`, `render`, and `release` should forward to instance `_impl` methods, with only minimal exception recording at the static boundary if needed for browser status. Remove duplicated live lifecycle storage such as `Game::s_state`; keep only the instance state while live. If debug status needs a last-known state after destruction, store that as debug-status data rather than as a second live lifecycle source. Rename lifecycle enum values that refer to the post-create state from `Initialized` to `Created`. Convert `Game::prepare_impl()` and `Game::release_impl()` into explicit switch-based bool state machines. `Game::prepare_impl()` should call `Resources::prepare()` and then `Renderer::prepare()` incrementally, returning `false` while either system is still preparing. `Game::release_impl()` should drain `Renderer::release()` to completion first, then drain `Resources::release()` to completion, returning `true` only after both are released. Browser `dispose()` drains `Game::release()` synchronously, then calls `Game::destroy()`. `Game::destroy_impl()` should call `Renderer::destroy()` and then `Resources::destroy()`, after release has already completed. Do not use prepare/release booleans to report failure; failures throw.

Milestone 5 updates contracts, tests, coverage docs, and final validation. Update `docs/API_CONTRACTS.md` and `docs/SYSTEMS.md` to remove `GameRuntime`, `ResourceArena`, public singleton `init` naming, and any implication that static public methods own lifecycle logic. Update tests to prove repeated prepare/release calls remain safe, second live create throws, release/destroy order is preserved, resource create methods use a single lifecycle policy, and renderer steady-state counters remain stable. Run formatting, tests, smoke, coverage, Cloudflare packaging, and final audits.

After each milestone, run the repo-local `milestone-review` skill before marking that milestone complete. Apply required findings or record a rejected finding with rationale in this plan's Decision Log.

## Concrete Steps

Work from `C:\dev\ofg`.

Milestone 1 likely touches:

    cpp/include/ofg/render/renderer.hpp
    cpp/src/render/renderer.cpp
    cpp/tests/renderer_test.cpp

Milestone 1 validation:

    npm run format:cpp:check
    npm run test:cpp
    npm run coverage:cpp
    rg -n "static RendererLifecycleState s_state|Renderer::init\\(|RendererLifecycleState::Initialized" cpp/include/ofg/render/renderer.hpp cpp/src/render/renderer.cpp
    git -c safe.directory=C:/dev/ofg diff --check

Milestone 2 likely touches:

    cpp/include/ofg/resources/resources.hpp
    cpp/src/resources/resources.cpp
    cpp/include/ofg/resources/resource_arena.hpp
    cpp/src/resources/resource_arena.cpp
    cpp/CMakeLists.txt
    cpp/tests/resource_arena_test.cpp
    cpp/tests/resources_lifecycle_test.cpp
    cpp/tests/*resource*_test.cpp

Milestone 2 validation:

    npm run format:cpp:check
    npm run test:cpp
    npm run coverage:cpp
    rg -n "ResourceArena|Resources::arena|Resources::init\\(|ResourcesLifecycleState::Initialized|static ResourcesLifecycleState s_state" cpp/include cpp/src cpp/tests docs/API_CONTRACTS.md docs/SYSTEMS.md
    git -c safe.directory=C:/dev/ofg diff --check

Milestone 3 likely touches:

    cpp/include/ofg/game/game.hpp
    cpp/src/game/game.cpp
    cpp/include/ofg/game/game_runtime.hpp
    cpp/src/game/game_runtime.cpp
    cpp/CMakeLists.txt
    cpp/tests/game_runtime_test.cpp
    cpp/tests/runtime_debug_status_test.cpp
    src/app/wasmRuntime.ts if debug-status shape changes
    tests/ts/wasmRuntime.test.ts if debug-status shape changes

Milestone 3 validation:

    npm run format:cpp:check
    npm run test:cpp
    npm run test:ts
    npm run coverage:cpp
    rg -n "GameRuntime|game_runtime|std::string& error" cpp/include/ofg/game cpp/src/game cpp/tests docs/API_CONTRACTS.md docs/SYSTEMS.md
    git -c safe.directory=C:/dev/ofg diff --check

Milestone 4 likely touches:

    cpp/include/ofg/game/game.hpp
    cpp/src/game/game.cpp
    cpp/tests/game_runtime_test.cpp or replacement Game lifecycle tests
    cpp/tests/renderer_test.cpp
    cpp/tests/resources_lifecycle_test.cpp

Milestone 4 validation:

    npm run format:cpp:check
    npm run test:cpp
    npm run smoke:render
    npm run smoke:browser:cpp
    npm run coverage:cpp
    rg -n "static GameLifecycleState s_state|Game::init\\(|GameLifecycleState::Initialized" cpp/include/ofg/game/game.hpp cpp/src/game/game.cpp
    git -c safe.directory=C:/dev/ofg diff --check

Milestone 5 final validation:

    npm run format:cpp:check
    npm test
    npm run smoke
    npm run coverage
    npm run build:cloudflare
    rg -n "GameRuntime|ResourceArena|Resources::arena|Game::init\\(|Renderer::init\\(|Resources::init\\(|LifecycleState::Initialized|static .*LifecycleState s_state" cpp/include cpp/src cpp/tests docs/API_CONTRACTS.md docs/SYSTEMS.md
    rg -n "std::string& error" cpp/include/ofg/render cpp/src/render cpp/include/ofg/resources cpp/src/resources cpp/include/ofg/game cpp/src/game
    git -c safe.directory=C:/dev/ofg diff --check

## Milestone Review

After each milestone:

1. Update any changed API contracts or active docs.
2. Run the `milestone-review` skill against the milestone diff and this ExecPlan.
3. Apply required findings before marking the milestone complete, or record a rejected finding with rationale in the Decision Log.
4. Re-run relevant validation commands.
5. Record the review summary, commands, artifacts, screenshots, and remaining risks in Progress or Outcomes & Retrospective.

Milestone reviews for this plan must explicitly check:

- Public singleton lifecycle naming is `create`, `prepare`, `release`, and `destroy`; no public `Game::init`, `Renderer::init`, or `Resources::init` API remains.
- Public static facade methods are forwarding shells, except for documented tiny accessors or lifecycle edges where creation/destruction of the singleton itself must happen statically.
- The only normal static data members on `Game`, `Renderer`, and `Resources` are their singleton pointers; any extra static member has explicit Decision Log rationale.
- Lifecycle state for a live singleton is not duplicated in both static and non-static variables.
- `destroy` does not own release draining in the normal path; top-level browser disposal drains `Game::release()` and then calls `Game::destroy()`.
- `prepare_impl()` and `release_impl()` are explicit switch-based state machines that can return `false` for future multi-frame work.
- `prepare()` and `release()` booleans are never used to report fatal failure.
- `GameRuntime` is removed or any retained replacement is clearly not an owner-like split from `Game`.
- `ResourceArena` is removed or any retained replacement is clearly not an owner-like split from `Resources`.
- Repeated resource lifecycle checks are centralized in private helpers, not copy-pasted across create/access methods.
- Browser/native exception boundaries still preserve useful debug status and do not depend on C++ exceptions crossing into TypeScript.
- Browser and native smoke still render the plane-and-cubes scene.

## Validation and Acceptance

The plan is accepted when:

- Public singleton lifecycle APIs use `create`, `prepare`, `release`, and `destroy`. `create` returns `void` and throws on error; `prepare` and `release` return `bool`; `destroy` returns `void`.
- No public `Game::init`, `Renderer::init`, or `Resources::init` API remains, and post-create lifecycle enum values are named `Created` rather than `Initialized`.
- `Renderer::prepare`, `Renderer::release`, `Resources::prepare`, `Resources::release`, `Game::prepare`, and `Game::release` are thin public static forwarding methods.
- The instance methods `Renderer::prepare_impl`, `Renderer::release_impl`, `Resources::prepare_impl`, `Resources::release_impl`, `Game::prepare_impl`, and `Game::release_impl` contain clear lifecycle switch statements and return `bool` for incremental progress.
- No live singleton lifecycle state is stored twice as both static and instance state.
- The only normal static data members on `Game`, `Renderer`, and `Resources` are their singleton pointers, such as `s_game`, `s_resources`, and `s_renderer`. Any extra static state is removed or recorded as an explicit exception.
- Browser `dispose()` drains `Game::release()` synchronously before calling `Game::destroy()`.
- `Game::release_impl()` drains `Renderer::release()` before `Resources::release()`.
- `Game::destroy_impl()` calls `Renderer::destroy()` before `Resources::destroy()`, and destroy methods assume release has already been drained in the normal path.
- `GameRuntime` no longer exists as a separate class, unless a review-approved replacement is recorded in the Decision Log with a concrete reason.
- `ResourceArena` no longer exists as a separate class, unless a review-approved replacement is recorded in the Decision Log with a concrete reason.
- `Resources` directly owns stable storage for textures, shaders, materials, and meshes.
- `Game` directly owns frame/debug/runtime status.
- Public render/resource/game APIs use exceptions rather than caller-populated error strings. Remaining `std::string& error` matches, if any, are outside these public engine systems and are documented.
- Mip generation remains present in the texture path and covered by tests.
- Browser and native smoke still pass and produce the expected screenshots/reports under `C:\dev\ofg\artifacts`.
- Coverage passes for all modified implementation files.

Final validation must pass:

    npm run format:cpp:check
    npm test
    npm run smoke
    npm run coverage
    npm run build:cloudflare
    git -c safe.directory=C:/dev/ofg diff --check

## Idempotence and Recovery

Each milestone should preserve visual renderer output. If a milestone temporarily keeps a compatibility shim, record it explicitly and remove it by the next milestone unless the Decision Log records a concrete reason.

Keep changes small enough that a failed milestone can be diagnosed by its focused tests. Avoid touching browser/native smoke harness size unless required by the relevant milestone.

If merging `GameRuntime` or `ResourceArena` causes coverage to drop below the gate, add focused tests for the externally meaningful behavior rather than preserving the helper split only for test convenience.

Generated directories `C:\dev\ofg\dist`, `C:\dev\ofg\dist-test`, `C:\dev\ofg\.deploy`, `C:\dev\ofg\artifacts`, and `C:\dev\ofg\assets\wasm\ofg_cpp` can be regenerated by existing npm scripts.

## Artifacts and Notes

Expected durable source changes include:

    C:\dev\ofg\cpp\include\ofg\game\game.hpp
    C:\dev\ofg\cpp\src\game\game.cpp
    C:\dev\ofg\cpp\include\ofg\resources\resources.hpp
    C:\dev\ofg\cpp\src\resources\resources.cpp
    C:\dev\ofg\cpp\include\ofg\render\renderer.hpp
    C:\dev\ofg\cpp\src\render\renderer.cpp
    C:\dev\ofg\docs\API_CONTRACTS.md
    C:\dev\ofg\docs\SYSTEMS.md

Expected removed source files unless a later Decision Log entry rejects removal:

    C:\dev\ofg\cpp\include\ofg\game\game_runtime.hpp
    C:\dev\ofg\cpp\src\game\game_runtime.cpp
    C:\dev\ofg\cpp\include\ofg\resources\resource_arena.hpp
    C:\dev\ofg\cpp\src\resources\resource_arena.cpp

Expected visual artifacts after final validation:

    C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png
    C:\dev\ofg\artifacts\browser-smoke\report.json
    C:\dev\ofg\artifacts\render-smoke\opaque-demo.png
    C:\dev\ofg\artifacts\render-smoke\report.json

Milestone review for milestones 1-4:

    Scope: Renderer, Resources, and Game singleton facade cleanup plus browser disposal boundary updates.
    Reviewers: local contract, code quality, legacy, correctness, and validation passes. Sub-agent spawning was not used because the available multi-agent tool contract requires an explicit user request for sub-agents.
    Required findings fixed: `Renderer::release_impl` and `Resources::release_impl` were not explicit switch-based state machines; both were converted to lifecycle switch statements and C++ formatting/tests were rerun.
    Follow-ups recorded: `cpp/src/web/browser_game.cpp` is 616 lines, so it has split pressure under the repo review rules. This cleanup kept the file intact because the added code is browser lifecycle boundary handling and a split deserves its own ownership pass.
    Rejected findings: resource object `init_*` methods are intentionally retained; the lifecycle rename applies to singleton `Game`, `Renderer`, and `Resources` creation entry points only.
    Validation rerun: `npm run format:cpp:check` passed; `npm run test:cpp` passed with 1/1 CTest tests; `npm run coverage:cpp` passed with touched implementation files above the attention threshold.
    Remaining risk: browser/WASM, smoke, full coverage, and packaging gates still needed to run at that point; Milestone 5 resolved them.

Milestone review for milestone 5:

    Scope: docs/contracts/tests/coverage updates plus final validation artifacts.
    Reviewers: local contract, code quality, legacy, correctness, and validation passes. Sub-agent spawning was not used because the available multi-agent tool contract requires an explicit user request for sub-agents.
    Required findings fixed: none.
    Follow-ups recorded: `cpp/src/web/browser_game.cpp` remains the only file-size pressure item introduced by this cleanup.
    Rejected findings: none beyond the already recorded resource `init_*` naming decision.
    Validation rerun: `npm run format:cpp:check`, `npm test`, `npm run smoke`, `npm run coverage`, `npm run build:cloudflare`, both final `rg` audits, and `git -c safe.directory=C:/dev/ofg diff --check` all passed. The `rg` audits produced no matches; `diff --check` produced only line-ending normalization warnings.
    Visual artifacts inspected: `C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png` and `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png` both show the expected checker ground plane and colored cube scene.
    Remaining risk: none blocking this plan.

Concise command transcripts:

    npm run format:cpp:check
    Checked 79 C++ files.

    npm run test:cpp
    1/1 Test #1: ofg_cpp_tests ... Passed
    100% tests passed, 0 tests failed out of 1.

    npm run coverage:cpp
    cpp\src\game\render_target.cpp line coverage 100.00%
    cpp\src\render\renderer.cpp line coverage 90.12%
    cpp\src\resources\resources.cpp line coverage 93.49%
    C++ coverage summary written to artifacts\coverage\cpp\cpp-summary.json

    rg -n "GameRuntime|ResourceArena|Resources::arena|Game::init\(|Renderer::init\(|Resources::init\(|LifecycleState::Initialized|static .*LifecycleState s_state" cpp/include cpp/src cpp/tests docs/API_CONTRACTS.md docs/SYSTEMS.md
    No matches.

    rg -n "std::string& error" cpp/include/ofg/render cpp/src/render cpp/include/ofg/resources cpp/src/resources cpp/include/ofg/game cpp/src/game
    No matches.

    git -c safe.directory=C:/dev/ofg diff --check
    Passed with only existing line-ending normalization warnings.

    npm test
    1/1 Test #1: ofg_cpp_tests ... Passed
    19 TypeScript tests passing.

    npm run smoke
    Browser smoke passed and wrote C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png.
    Native render smoke passed and wrote C:\dev\ofg\artifacts\render-smoke\opaque-demo.png.

    npm run coverage
    C++ coverage gate passed.
    TypeScript coverage gate passed with the existing browser-entrypoint smoke exception.

    npm run build:cloudflare
    Packaged Cloudflare Pages site at C:\dev\ofg\.deploy.
    Generated WASM size: 284380 bytes (277.7 KiB).

## Interfaces and Dependencies

Final static facade shape:

    void Renderer::create(const RendererCreateInfo& create_info) {
        // Validate inputs, construct the singleton, and throw on failure.
        s_renderer = std::make_unique<Renderer>(create_info);
    }

    bool Renderer::prepare() {
        return require_renderer("Renderer::prepare").prepare_impl();
    }

    bool Resources::prepare() {
        return require_resources("Resources::prepare").prepare_impl();
    }

    bool Game::prepare() {
        return require_game("Game::prepare").prepare_impl();
    }

Final instance state-machine shape:

    bool Resources::prepare_impl() {
        switch (m_state) {
        case ResourcesLifecycleState::Created:
            m_state = ResourcesLifecycleState::Preparing;
            [[fallthrough]];
        case ResourcesLifecycleState::Preparing:
            // Do the current prepare stage.
            m_state = ResourcesLifecycleState::Ready;
            [[fallthrough]];
        case ResourcesLifecycleState::Ready:
            return true;
        default:
            throw EngineError("Resources::prepare cannot run in the current lifecycle state.");
        }
    }

Final `Resources` storage shape:

    std::vector<std::unique_ptr<Texture>> m_textures;
    std::vector<std::unique_ptr<Shader>> m_shaders;
    std::vector<std::unique_ptr<Material>> m_materials;
    std::vector<std::unique_ptr<Mesh>> m_meshes;

Final `Game` runtime ownership shape:

    FrameState m_frame_state;
    RuntimeDebugStatus m_status;
    bool m_disposed;
    bool m_gpu_ready;
    bool m_surface_configured;

`Game`, `Renderer`, and `Resources` may still use private static `std::unique_ptr` singleton storage. That singleton pointer should be the only normal static data member on each facade. If `Game` has more static members than `s_game`, or `Renderer`/`Resources` have more than their singleton pointer, treat it as a problem indicator: remove the extra static state or record a specific Decision Log exception. They should not use static lifecycle-state mirrors for live singleton state.
