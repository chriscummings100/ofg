# Add Camera Components and Debug Fly Camera

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

This plan follows `C:\dev\ofg\PLANS.md`.

## Purpose / Big Picture

OFG currently renders the demo scene through a `RenderView` value that is stored directly on `Scene`. `RenderView` is only a packed view-projection matrix, so it hides the real model: the camera should be an entity in the scene, and the entity transform should provide the camera position and rotation. The renderer still needs matrices, but those matrices should be derived from the active camera, not stored as ad hoc scene state.

After this work, the user-visible result is that the existing checker floor and colored cube demo can be explored with a classic debug fly camera in the browser. Clicking the canvas should activate pointer-lock look controls, keyboard movement should fly the camera through the scene, and releasing pointer lock should stop mouse look. With no input, the scene should start from the same perspective as the current demo so existing browser and native smoke expectations remain stable.

Internally, this plan replaces the current `RenderView` naming and ownership with two clearer concepts:

Camera component: a scene-owned component attached to an entity. The entity's `LocalTransform` stores camera position and rotation. The component stores projection settings such as perspective field of view, near clip, and far clip.

CameraProperties: a renderer-facing value resolved from a camera and an aspect ratio. It includes a pointer to the source `Camera`, the calculated world/view/projection matrices, near/far clip distances, aspect ratio, and other camera facts that later render passes, culling, picking, lighting, and debug overlays can use from one shared camera snapshot.

This plan does not add a general input/action binding system, production player movement, camera collision, editor UI, camera serialization, multiple viewports, split-screen, orthographic rendering, or frustum culling. It creates the first camera component and the first camera mode: debug fly camera.

## Progress

- [x] (2026-06-30 21:39 +01:00) Drafted this ExecPlan from the current `RenderView`, scene component, demo scene, browser runtime, and TypeScript host code.
- [x] (2026-06-30 21:55 +01:00) Reviewed the plan with correctness, completeness, clarity, efficiency, and performance sub-agents; folded accepted feedback and user additions into this plan.
- [x] (2026-06-30 22:09 +01:00) Milestone 1 complete: added `CameraProperties`, a look-at adapter, CMake/test registration, coverage-filter registration, and doctests proving parity with the old `projection * view` path.
- [x] (2026-06-30 22:16 +01:00) Milestone 2 complete: added scene-owned `Camera` components, main-camera selection/fallback, scale-ignored camera transform resolution, current-state docs, and focused doctest/coverage validation.
- [x] (2026-06-30 22:26 +01:00) Milestone 3 complete: migrated demo setup and renderer pass submission to scene cameras and `CameraProperties`, added look-at quaternion math, captured the default screenshot, and passed browser/native render smokes.
- [x] (2026-06-30 22:43 +01:00) Milestone 4 complete: added the C++ debug fly camera controller, Game/BrowserGame debug-input facade, TypeScript raw input collector, moved-camera screenshot, docs updates, and coverage-backed tests.
- [x] (2026-06-30 22:51 +01:00) Milestone 5 complete: removed `RenderView`/`main_view` compatibility, refreshed active docs and coverage summaries, reran full validation, refreshed default/moved screenshots, and prepared the plan for archive.

## Surprises & Discoveries

- Observation: `RenderView` is currently only a single `math::Mat4 m_view_projection`.
  Evidence: `C:\dev\ofg\cpp\include\ofg\render\camera.hpp` defines `struct RenderView` and `render_view_from_matrix`.

- Observation: `Scene` currently stores the main render view directly rather than owning a camera component.
  Evidence: `C:\dev\ofg\cpp\include\ofg\scene\scene.hpp` contains `RenderView m_main_view`, `main_view()`, and `set_main_view(RenderView)`.

- Observation: The demo scene currently recomputes camera view and projection matrices in `update_demo_scene`.
  Evidence: `C:\dev\ofg\cpp\src\render\demo_scene.cpp` calls `math::look_at_rh`, `math::perspective_rh`, then `scene.set_main_view(render_view_from_matrix(math::mul(*projection, *view)))`.

- Observation: The browser host currently forwards only resize, animation-frame time, debug-status reads, and disposal to C++.
  Evidence: `C:\dev\ofg\src\app\wasmRuntime.ts` exposes `resize`, `frame`, `debugStatus`, and `dispose`; `C:\dev\ofg\cpp\src\web\embind_module.cpp` binds the same surface on `BrowserGame`.

- Observation: The repo instructions reference `C:\dev\ofg\GUIDES.md`, but the active guide file is currently `C:\dev\ofg\docs\GUIDES.md`.
  Evidence: `Get-Content -Path GUIDES.md` failed while preparing the plan, while `Get-Content -Path docs\GUIDES.md` succeeded. Implementation should read `docs\GUIDES.md`, `AGENTS.md`, `PLANS.md`, and active contracts before changing code.

- Observation: The milestone-review skill references `docs\ARCHITECTURE.md`, but this repository currently does not have that file.
  Evidence: `Test-Path docs\ARCHITECTURE.md` returned `False` during Milestone 1 review. The review used `AGENTS.md`, `PLANS.md`, `docs\GUIDES.md`, `docs\API_CONTRACTS.md`, `docs\SYSTEMS.md`, this ExecPlan, and the touched source/test files.

## Decision Log

- Decision: Implement `Camera` as a scene component attached to an entity, not as renderer-owned state.
  Rationale: The user explicitly wants entity transforms to be camera position and rotation. This keeps cameras in the same ownership model as `MeshRenderer` and leaves renderer code consuming resolved frame data.
  Date/Author: 2026-06-30 / Codex

- Decision: Replace `RenderView` with `CameraProperties` as the renderer-facing data contract.
  Rationale: `RenderView` is confusing because it sounds like a high-level view object but only carries a packed matrix. `CameraProperties` names the actual payload and can carry the source `Camera*`, calculated matrices, clip distances, aspect ratio, and future render-system camera facts without making the renderer own camera behavior.
  Date/Author: 2026-06-30 / Codex and user

- Decision: A scene without any camera is an error at render time, while a scene without an explicit main-camera selection uses the first camera in creation order.
  Rationale: This keeps authoring ergonomic for the common case while still failing clearly when no camera exists at all.
  Date/Author: 2026-06-30 / User and Codex

- Decision: Keep the current shader uniform name `view_projection` unless a shader refactor becomes necessary.
  Rationale: The misleading public C++ type is the problem. The WGSL uniform name is conventional and does not need to churn while the opaque pass still consumes one clip-from-world matrix.
  Date/Author: 2026-06-30 / Codex

- Decision: Camera local forward is negative Z, local up is positive Y, and local right is positive X.
  Rationale: This matches common right-handed camera convention and the existing `math::look_at_rh`/perspective path, where the demo camera looks into the scene along a right-handed view direction.
  Date/Author: 2026-06-30 / Codex

- Decision: Camera scale is ignored when resolving camera properties.
  Rationale: Camera scale does not have a meaningful perspective-view interpretation. For v1, the camera resolver should accumulate camera and ancestor translation/rotation for placement, but ignore local scale values in the camera ancestry. Projection settings live on the `Camera` component.
  Date/Author: 2026-06-30 / Codex and user

- Decision: TypeScript collects raw input only; C++ owns camera mode behavior and mutates the camera entity transform.
  Rationale: `OFG-BOOT-001` and `OFG-BOOT-003` require TypeScript to remain a narrow host. Keyboard/mouse state can cross the WASM facade, but scene graph state and gameplay/control behavior stay in C++.
  Date/Author: 2026-06-30 / Codex

- Decision: `BrowserGame` stores the latest setup-phase debug camera input and forwards it once `Game` becomes active.
  Rationale: `BrowserGame::create` returns before asynchronous browser WebGPU setup has created the static `Game` singleton. Buffering the latest sanitized snapshot lets the TypeScript host send one input snapshot per animation frame immediately without seeing lifecycle errors during startup.
  Date/Author: 2026-06-30 / Codex

- Decision: The TypeScript runtime wrapper validates finite debug-input numbers before forwarding scalar values through Embind.
  Rationale: C++ still validates the boundary, but rejecting non-finite JavaScript values before crossing Embind keeps wrapper tests precise and avoids depending on generated binding behavior for obviously invalid input.
  Date/Author: 2026-06-30 / Codex

- Decision: The first camera mode is always-available debug fly camera, with no production player/controller abstraction yet.
  Rationale: The user asked for the first camera mode to be a classic debug fly cam. A broader action binding or character-controller system would add scope before OFG has basic scene exploration.
  Date/Author: 2026-06-30 / Codex

- Decision: New public struct fields should not use the `m_` member prefix.
  Rationale: Small value structs such as `CameraProperties` and `DebugCameraInput` are public data contracts. Plain field names such as `clip_from_world` and `move_x` are easier to read at call sites. Private class members and non-public implementation state can continue to use the repo's `m_` convention.
  Date/Author: 2026-06-30 / User and Codex

- Decision: The camera transform resolver composes parent transforms iteratively by pre-multiplying translation-and-rotation matrices while walking from the camera entity to the root.
  Rationale: This preserves root-to-camera transform order, ignores scale as requested, avoids recursion, and avoids allocating a temporary ancestry list during render-facing camera resolution.
  Date/Author: 2026-06-30 / Codex

- Decision: Add `math::quat_look_at_rh` as the reusable bridge from eye/target/up camera descriptions into entity rotations.
  Rationale: The demo camera migration needed to preserve the old look-at view while moving camera position and rotation into `LocalTransform`. Keeping the conversion in math with branch coverage avoids a hidden demo-only formula and gives the debug camera controller a tested quaternion path to build on.
  Date/Author: 2026-06-30 / Codex

## Outcomes & Retrospective

Milestone 1 introduced the CPU-side `CameraProperties` snapshot while preserving the active `RenderView` path for later removal. New files:

- `C:\dev\ofg\cpp\include\ofg\render\camera_properties.hpp`
- `C:\dev\ofg\cpp\src\render\camera_properties.cpp`
- `C:\dev\ofg\cpp\tests\camera_properties_test.cpp`

Validation passed with `npm run format:cpp`, `npm run test:cpp`, `npm run coverage:cpp`, and `git -c safe.directory=C:/dev/ofg diff --check`. C++ coverage reported `cpp\src\render\camera_properties.cpp line coverage 95.74%`.

Milestone review:
- Scope: Milestone 1 `CameraProperties` adapter, CMake/test registration, and coverage filter update.
- Reviewers: local contract, code-quality, legacy, correctness, and validation passes. Sub-agents were not spawned because this turn did not have an explicit user request for delegated review agents.
- Required findings fixed: cleaned up the adapter validation flow so distinct-eye/target, parallel-up, and invalid-projection paths are directly covered.
- Follow-ups recorded: none for this milestone.
- Rejected findings: none.
- Validation rerun: `npm run format:cpp`, `npm run test:cpp`, `npm run coverage:cpp`, and `git -c safe.directory=C:/dev/ofg diff --check`.
- Remaining risk: `CameraProperties` is not yet consumed by renderer pass submission; this is intentional until Milestones 2 and 3 add scene camera components and migrate renderer resolution.

Milestone 2 introduced the scene-owned `Camera` component and active-camera selection while preserving the temporary `RenderView` path for renderer migration. New files:

- `C:\dev\ofg\cpp\include\ofg\scene\camera.hpp`
- `C:\dev\ofg\cpp\src\scene\camera.cpp`

Updated public scene APIs:

- `Entity::camera()`
- `Scene::camera_count()`
- `Scene::get_camera(std::size_t)`
- `Scene::main_camera()`
- `Scene::set_main_camera(Camera*)`

Validation passed with `npm run format:cpp`, `npm run test:cpp`, `npm run coverage:cpp`, and `git -c safe.directory=C:/dev/ofg diff --check`. C++ coverage reported `cpp\src\scene\camera.cpp line coverage 100.00%` and `cpp\src\scene\scene.cpp line coverage 95.92%`.

Milestone review:
- Scope: Milestone 2 `Camera` component, scene camera storage/selection, entity accessors, scale-ignored camera property resolution, CMake/test registration, coverage, and active current-state docs.
- Reviewers: local contract, code-quality, legacy, correctness, and validation passes. Sub-agents were not spawned because this turn did not have an explicit user request for delegated review agents.
- Required findings fixed: updated `docs\API_CONTRACTS.md` and `docs\SYSTEMS.md` so active docs mention live scene-owned cameras and the temporary `RenderView` compatibility path; replaced temporary camera ancestry allocation with iterative pre-multiplication.
- Follow-ups recorded: `RenderView`, `Scene::main_view`, and `Scene::set_main_view` remain intentionally temporary until renderer migration and final cleanup milestones.
- Rejected findings: none.
- Validation rerun: `npm run format:cpp`, `npm run test:cpp`, `npm run coverage:cpp`, and `git -c safe.directory=C:/dev/ofg diff --check`.
- Remaining risk: Renderer pass submission still uses the old stored render view. This is the next milestone's explicit migration target.

Milestone 3 migrated active rendering from stored `RenderView` data to scene cameras. `setup_demo_scene` now creates a camera entity with the same eye/target/up/FOV/near/far as the legacy demo view, `update_demo_scene` only updates animated entity transforms, `Renderer::render_impl` resolves `Scene::main_camera()` into `CameraProperties`, and `OpaquePass` uploads `CameraProperties::clip_from_world` to the existing `view_projection` uniform.

Validation passed with `npm run format:cpp`, `npm run format:cpp:check`, `npm run test:cpp`, `npm run coverage:cpp`, `npm run smoke:browser`, `npm run smoke:render`, and `git -c safe.directory=C:/dev/ofg diff --check`. C++ coverage reported `cpp\src\math\quat.cpp line coverage 100.00%`, `cpp\src\render\demo_scene.cpp line coverage 94.78%`, `cpp\src\render\opaque_pass.cpp line coverage 91.24%`, and `cpp\src\render\renderer.cpp line coverage 90.27%`.

Visual artifacts:
- `C:\dev\ofg\artifacts\camera-debug-flycam\default-view.png`
- `C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png`
- `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png`

Milestone review:
- Scope: Milestone 3 look-at quaternion helper, demo camera entity setup, removal of per-frame demo camera matrices, renderer resolution to `CameraProperties`, opaque pass camera snapshot submission, docs, screenshot, and smoke validation.
- Reviewers: local contract, code-quality, legacy, correctness, and validation passes. Sub-agents were not spawned because this turn did not have an explicit user request for delegated review agents.
- Required findings fixed: updated `docs\API_CONTRACTS.md` and `docs\SYSTEMS.md` to describe the active `CameraProperties` renderer path and legacy `RenderView` compatibility; added half-turn tests so the new quaternion matrix conversion branches meet coverage.
- Follow-ups recorded: remove `RenderView`, `Scene::main_view`, `Scene::set_main_view`, and the remaining scene-test coverage for that compatibility path during the final cleanup milestone.
- Rejected findings: none.
- Validation rerun: `npm run format:cpp:check`, `npm run test:cpp`, `npm run coverage:cpp`, `npm run smoke:browser`, `npm run smoke:render`, and `git -c safe.directory=C:/dev/ofg diff --check`.
- Remaining risk: Debug input is not wired yet, so the camera is active but still stationary without code-driven controls. This is the next milestone's target.

Milestone 4 introduced debug fly camera controls while preserving the ownership boundary that TypeScript only collects raw DOM input. New files:

- `C:\dev\ofg\cpp\include\ofg\game\debug_camera_controller.hpp`
- `C:\dev\ofg\cpp\src\game\debug_camera_controller.cpp`
- `C:\dev\ofg\cpp\tests\debug_camera_controller_test.cpp`
- `C:\dev\ofg\src\app\debugInput.ts`
- `C:\dev\ofg\tests\ts\debugInput.test.ts`

`Game` now owns a durable `DebugCameraController` and latest `DebugCameraInput` snapshot. `BrowserGame::set_debug_camera_input(...)` accepts scalar raw input, buffers setup-phase snapshots until async WebGPU setup creates `Game`, and then forwards each live snapshot. `src\app\main.ts` consumes one DOM input snapshot per animation frame before `runtime.frame(timeMs)`. The controller derives yaw/pitch from the active scene camera, clamps pitch and large frame deltas, normalizes diagonal movement, ignores camera scale through the existing camera resolver, and resets its internal timing/orientation if a scene temporarily has no main camera.

Validation passed with `npm run format:cpp`, `npm run format:cpp:check`, `npm run test:cpp`, `npm run coverage:cpp`, `npm run test:ts`, `npm run coverage:ts`, moved-camera Playwright verification, and `git -c safe.directory=C:/dev/ofg diff --check`. C++ coverage reported `cpp\src\game\debug_camera_controller.cpp line coverage 91.45%`. TypeScript coverage passed for checked files at the documented threshold; `src/app/main.ts` remains covered by browser smoke as the existing browser-entrypoint exception.

Visual artifacts:
- `C:\dev\ofg\artifacts\camera-debug-flycam\flycam-moved.png`
- `C:\dev\ofg\artifacts\camera-debug-flycam\flycam-moved-report.json`

Milestone review:
- Scope: Milestone 4 C++ debug camera input/controller, Game and BrowserGame lifecycle bridge, Embind/TypeScript runtime surface, DOM input collector, docs, C++/TypeScript tests, coverage, and moved-camera screenshot.
- Reviewers: local contract, code-quality, legacy, correctness, and validation passes. Sub-agents were not spawned because this turn did not have an explicit user request for delegated review agents.
- Required findings fixed: reset controller timing/orientation when a scene has no main camera so a later camera does not inherit stale no-camera frame time; added focused test coverage for that edge.
- Follow-ups recorded: final cleanup must still remove `RenderView`, `Scene::main_view`, `Scene::set_main_view`, and active-doc temporary compatibility wording.
- Rejected findings: none.
- Validation rerun: `npm run format:cpp`, `npm run format:cpp:check`, `npm run test:cpp`, `npm run coverage:cpp`, `npm run test:ts`, `npm run coverage:ts`, moved-camera Playwright verification, and `git -c safe.directory=C:/dev/ofg diff --check`.
- Remaining risk: The first fly camera mode is intentionally always-on for the demo scene; production action binding, camera collision, editor UI, and player-controller modes remain out of scope.

Milestone 5 removed the temporary render-view compatibility layer. Deleted files:

- `C:\dev\ofg\cpp\include\ofg\render\camera.hpp`
- `C:\dev\ofg\cpp\src\render\camera.cpp`

`Scene` no longer exposes `main_view()` or `set_main_view(...)`, no active C++/TypeScript/test code references `RenderView`, and active contracts now describe scene-owned cameras plus renderer-resolved `CameraProperties` as the current model. Coverage summaries in `docs\coverage` were refreshed from the final `npm run coverage` output, and stale active coverage docs for the retired `bootstrap_renderer.cpp` exception were removed.

Final validation passed with `npm run format:cpp:check`, `npm test`, `npm run smoke:browser`, `npm run smoke:browser:cpp`, `npm run smoke:render`, `npm run coverage`, and `git -c safe.directory=C:/dev/ofg diff --check`. The final cleanup search `rg -n "RenderView|render view|main_view|set_main_view" cpp src tests docs/API_CONTRACTS.md docs/SYSTEMS.md -S` returned no matches. The review server remains available at `http://127.0.0.1:5173`.

Final visual artifacts:
- `C:\dev\ofg\artifacts\camera-debug-flycam\default-view.png`
- `C:\dev\ofg\artifacts\camera-debug-flycam\flycam-moved.png`
- `C:\dev\ofg\artifacts\camera-debug-flycam\flycam-final-report.json`
- `C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png`
- `C:\dev\ofg\artifacts\browser-smoke-cpp\scene.png`
- `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png`

Milestone review:
- Scope: Milestone 5 `RenderView` removal, active docs/contracts cleanup, coverage refresh, final screenshots, browser/native smoke artifacts, and full validation.
- Reviewers: local contract, code-quality, legacy, correctness, and validation passes. Sub-agents were not spawned because this turn did not have an explicit user request for delegated review agents.
- Required findings fixed: removed the stale active coverage exception for retired `cpp/src/render/bootstrap_renderer.cpp`; refreshed `docs\coverage\latest.md` so it lists `camera_properties.cpp`, `scene\camera.cpp`, and `debug_camera_controller.cpp` while dropping deleted `camera.cpp`.
- Follow-ups recorded: none for this plan.
- Rejected findings: none.
- Validation rerun: `npm run format:cpp:check`, `npm test`, `npm run smoke:browser`, `npm run smoke:browser:cpp`, `npm run smoke:render`, `npm run coverage`, screenshot refresh, active legacy-reference searches, and `git -c safe.directory=C:/dev/ofg diff --check`.
- Remaining risk: The debug fly camera is intentionally a first always-on developer camera mode. Camera serialization, editor UI, collision, production player controls, action binding, multiple viewports, and frustum culling remain outside this plan.

Final outcome: OFG now has scene-owned `Camera` components, renderer-facing `CameraProperties`, a browser-controlled C++ debug fly camera, and no active `RenderView` API. With no input, the demo scene starts from the preserved default perspective; with keyboard input, the active camera entity moves through the scene while renderer resource counters remain stable.

## Contract and Quality Baseline

This plan preserves `OFG-BOOT-001 TypeScript Host Ownership`: TypeScript may own DOM boot, canvas lookup/creation, WASM loading, raw input event collection, local dev ergonomics, and smoke helpers. TypeScript must not own gameplay simulation, camera motion behavior, scene graph state, renderer internals, resource objects, draw commands, or GPU handles.

This plan intentionally updates `OFG-BOOT-002 C++ Runtime Ownership`: C++ still owns frame state, debug status, the current scene graph, demo-scene binding data, renderer resources, draw-list construction, browser WebGPU runtime behavior, and native Dawn offscreen rendering. The contract must change from "Scene stores the main render view" to "Scene stores camera components, optionally tracks an explicitly selected main camera, and resolves the selected-or-first camera into renderer-facing `CameraProperties` for each render."

This plan intentionally extends `OFG-BOOT-003 WASM Facade`: the browser facade remains narrow, but it gains a debug input method. That method should pass raw movement/look inputs and modifier state only. It must not expose raw scene handles, camera pointers, renderer internals, GPU handles, or arbitrary mutation APIs to TypeScript.

This plan preserves `OFG-BOOT-004 Renderer Compatibility`: with no debug input, browser and native smoke should still validate equivalent plane-and-cubes rendering. Browser fly-camera input may produce additional screenshots for review, but it should not destabilize the default smoke visual contract.

This plan preserves `OFG-BOOT-005 WebGPU Baseline`: no optional GPU features, no manual limits above adapter defaults, and the same opaque textured material path. The visual still uses a perspective camera, depth buffering, generated checker texture, generated white texture, a ground plane, and four animated cubes.

This plan preserves `OFG-BOOT-006 Resource Lifetime`: adding cameras and controls must not create texture, shader, material, mesh, pass, or pipeline resources every ordinary frame. Per-frame work may update camera and cube entity transforms and rebuild transient draw commands.

This plan preserves `OFG-BOOT-007 Generated Artifacts`, `OFG-BOOT-008 Deployment`, and `OFG-BOOT-009 Coverage`. Modified implementation files must meet the default coverage attention gate, currently about 90% line coverage, unless this plan records an explicit exception with rationale.

All new and modified C++ files must keep detailed top comments, and all new functions must have comments or doc strings defining their purpose. New C++ code must follow the repo conventions: classes and structs use `CamelCase`, functions use `lowercase_with_underscores`, private class member variables use `m_name_with_underscores`, public struct fields use plain `name_with_underscores` without `m_`, locals use `name_with_underscores`, and new C++ source/header formatting must pass `npm run format:cpp:check`.

Because this work affects browser UI/input and rendering, implementation must keep a local dev server available with `npm run dev`, take screenshots at the default-camera checkpoint after Milestone 3 and the moved-camera checkpoint after Milestone 4, and present them in chat with durable artifact paths. Store durable screenshots under `C:\dev\ofg\artifacts\camera-debug-flycam\`.

## Context and Orientation

Current renderer camera data lives in `C:\dev\ofg\cpp\include\ofg\render\camera.hpp` and `C:\dev\ofg\cpp\src\render\camera.cpp`. These files define `RenderView` as a single view-projection matrix and provide `render_view_from_matrix(math::Mat4)`.

Current scene ownership lives in `C:\dev\ofg\cpp\include\ofg\scene\scene.hpp` and `C:\dev\ofg\cpp\src\scene\scene.cpp`. `Scene` owns a root `Entity`, child entities, flat `MeshRenderer` component storage, a current generation token, and a `RenderView m_main_view`. `Entity` owns `LocalTransform`, parent/child/sibling links, and a typed `MeshRenderer*` pointer. `ComponentType` currently has only `MeshRenderer`.

Current transform helpers live in `C:\dev\ofg\cpp\src\scene\scene.cpp` and `C:\dev\ofg\cpp\include\ofg\math\transform.hpp`. `world_from_local(const Entity&)` composes local entity transforms into root/world space. `math::look_at_rh` and `math::perspective_rh` already build the current view and projection matrices. `math::mat4_from_quat` already converts an entity quaternion into a matrix.

Current render submission lives in `C:\dev\ofg\cpp\src\render\renderer.cpp`. `Renderer::render_impl` builds a transient `DrawList` from scene mesh renderers, then calls each `OpaquePass` with `scene.main_view()` and the draw list. `C:\dev\ofg\cpp\src\render\opaque_pass.cpp` writes `view.m_view_projection` to the frame uniform buffer.

Current demo scene setup and animation live in `C:\dev\ofg\cpp\src\render\demo_scene.cpp`. `setup_demo_scene` creates the ground and cube entities. `update_demo_scene` validates inputs, recomputes camera matrices from a fixed eye/target/up/FOV, stores the resulting render view on the scene, resets the ground transform, and animates cube transforms.

Current browser runtime code lives in `C:\dev\ofg\cpp\include\ofg\web\browser_game.hpp`, `C:\dev\ofg\cpp\src\web\browser_game.cpp`, and `C:\dev\ofg\cpp\src\web\embind_module.cpp`. It owns browser WebGPU setup and exposes the Embind `BrowserGame` facade.

Current TypeScript host code lives in `C:\dev\ofg\src\app\main.ts`, `C:\dev\ofg\src\app\canvasHost.ts`, and `C:\dev\ofg\src\app\wasmRuntime.ts`. It creates the canvas and runtime, forwards resize and animation frames, reads debug status, and updates status text.

## Plan of Work

Milestone 1 introduces the renderer-facing camera snapshot without removing the existing scene camera path yet. Add `CameraProperties` in `C:\dev\ofg\cpp\include\ofg\render\camera_properties.hpp` and `C:\dev\ofg\cpp\src\render\camera_properties.cpp`, then register the new source file and tests in `C:\dev\ofg\cpp\CMakeLists.txt`. Keep temporary compatibility with the current `Scene::main_view()` and hard-coded demo camera until the camera component path is active. The value should be CPU-side data for now; the opaque pass should still upload only the existing clip-from-world matrix until another pass consumes more camera data.

    const Camera* camera;
    math::Mat4 world_from_camera;
    math::Mat4 camera_from_world;
    math::Mat4 clip_from_camera;
    math::Mat4 clip_from_world;
    float vertical_fov_radians;
    float aspect;
    float near_z;
    float far_z;

Add a temporary adapter such as `camera_properties_from_look_at` that accepts `Camera*` or `nullptr`, eye, target, up, vertical FOV, aspect, near Z, and far Z. During this milestone the hard-coded demo camera can pass `nullptr` as the source camera and compute `world_from_camera`, `camera_from_world`, `clip_from_camera`, and `clip_from_world` from the same values currently sent to `math::look_at_rh` and `math::perspective_rh`. After Milestone 3, camera-derived properties should carry a non-null source camera pointer. Add tests that compare the adapter's `clip_from_world` against the old `projection * view` path within a small epsilon.

Milestone 2 adds the `Camera` component. Add `C:\dev\ofg\cpp\include\ofg\scene\camera.hpp` and `C:\dev\ofg\cpp\src\scene\camera.cpp`, then register the new source and doctests in `C:\dev\ofg\cpp\CMakeLists.txt`. Extend `ComponentType` with `Camera`. Extend `Entity` with `camera()` accessors, and extend `Scene` with scene-owned camera storage, `camera_count()`, `get_camera(std::size_t)`, `main_camera()`, and `set_main_camera(Camera*)`.

`Scene::main_camera()` should return the explicitly selected camera when one has been set. If no explicit selection exists, it should return the first camera in creation order. If the scene has no cameras, it should return `nullptr`; renderer code should treat that as a clear render-time `EngineError`. `Scene::set_main_camera(nullptr)` should clear the explicit selection and restore first-camera fallback. `Scene::set_main_camera(camera)` should accept non-null cameras only when they belong to the same scene generation. Foreign-scene, stale-after-clear, or otherwise invalid camera pointers must throw a clear `EngineError`. `Scene::clear()` must clear camera storage and explicit camera selection. Scene moves and move assignment must preserve a valid selected camera pointer when the selected camera object moved with the scene, and tests must cover this behavior.

The `Camera` component should start with perspective settings:

    float m_vertical_fov_radians;
    float m_near_z;
    float m_far_z;

Use defaults equivalent to the current demo unless another existing constant fits better: vertical FOV 55 degrees, near Z 0.1f, and far Z 80.0f. Accessors/mutators must validate that FOV, aspect, near Z, and far Z are finite; FOV is greater than 0 and less than pi radians; aspect is greater than 0; near Z is greater than 0; and far Z is greater than near Z. Invalid inputs should throw `EngineError` with an actionable message.

Add a resolver such as `Camera::camera_properties(float aspect) const` or `camera_properties_from_camera(const Camera&, float aspect)`. The resolver should use a new camera-specific transform helper that walks the camera entity ancestry iteratively, accumulates translation and rotation, and ignores all local scale values in that ancestry. This avoids using the current recursive `world_from_local` helper, which includes scale and can recurse every render. Add tests for an unparented camera, a nested camera, and a camera under scaled transforms to prove scale is ignored and parent rotation/translation still affect the camera.

Milestone 3 migrates the demo scene. Add a camera entity during `setup_demo_scene`, create the `Camera` component, and rely on the first-camera fallback unless explicit selection is useful. Do not cache camera `Entity*` or `Camera*` in `DemoScene` unless implementation proves it is needed; after setup, renderer/controller code should query `scene.main_camera()`. Convert the old hard-coded camera eye/target/up values into the camera entity's transform. Add a tested math helper, such as `quat_look_at_rh` or `quat_from_forward_up`, or document and test an equivalent plan-local formula for converting the old look-at basis into an entity quaternion.

Preserve the current initial view with a measurable check:

    eye = math::vec3(6.2f, 4.4f, 7.6f)
    target = math::vec3(0.0f, 0.55f, 0.0f)
    up = math::vec3(0.0f, 1.0f, 0.0f)
    vertical_fov = 55 degrees
    near_z = 0.1f
    far_z = 80.0f

Add a focused test that compares the default demo camera's `CameraProperties::clip_from_world` against the old `math::perspective_rh(...) * math::look_at_rh(...)` path within an epsilon. After this milestone, `update_demo_scene` should animate ground/cube entity transforms but should not compute or store render-view matrices. `Renderer::render_impl` should compute the aspect ratio from `RenderTarget` and resolve `scene.main_camera()` into `CameraProperties` immediately before pass submission. If `scene.main_camera()` returns `nullptr`, render should throw a clear `EngineError` before WebGPU pass submission.

Milestone 4 adds debug fly camera input and behavior. Add a small C++ input snapshot type, for example `DebugCameraInput`, with movement axes, mouse-look delta, pointer-lock or look-active state, and fast/slow modifiers. Add a debug camera controller in a native-checkable file such as `C:\dev\ofg\cpp\include\ofg\game\debug_camera_controller.hpp` and `C:\dev\ofg\cpp\src\game\debug_camera_controller.cpp`, then register those files and tests in `C:\dev\ofg\cpp\CMakeLists.txt`. `Game` should own one durable controller member and one latest input snapshot; it should not allocate/recreate controller state inside `update_impl`.

`Game::set_debug_camera_input` should follow the existing static-facade pattern: call `require_game(...)`, validate finite input values, store the input on the live `Game` instance, and clear it during release/destroy. It should throw clearly before `Game::create`, after release, or while failed. `BrowserGame` must not call `Game::set_debug_camera_input` before `m_game_active` is true, because `BrowserGame::create` returns before asynchronous WebGPU setup creates `Game`. During setup, `BrowserGame` should store the latest sanitized debug input snapshot locally and forward it once `m_game_active` becomes true, or ignore setup-phase input safely with no error; choose one behavior and record it in the Decision Log during implementation.

`Game::update_impl` should call `tick_runtime`, update the demo cube transforms, then run the debug camera controller against `m_current_scene` so camera input wins for the current frame. If there is no current scene or no main camera, the controller should do nothing or report a clear recoverable error consistent with surrounding `Game` behavior; renderer submission remains responsible for throwing when no camera exists.

The controller should compute frame delta from accepted frame timestamps, treat the first update as zero delta, and clamp large deltas to avoid huge camera jumps after tab suspension or startup. Suggested initial movement behavior:

    move_x: +1 moves camera right, -1 moves camera left
    move_y: +1 moves camera up in world space, -1 moves camera down in world space
    move_z: +1 moves camera forward along camera local -Z, -1 moves backward
    W/S: set positive/negative move_z
    A/D: set negative/positive move_x
    Space/C: set positive/negative move_y
    Shift: fast movement
    Ctrl: slow precision movement
    mouse delta X: positive yaw turns right
    mouse delta Y: positive pitch looks down

Normalize the movement vector when its length is greater than 1 so diagonal movement is not faster than axis-aligned movement. Mouse deltas are CSS pixel deltas accumulated while pointer lock is active. Clamp pitch to an explicit range, such as +/- 89 degrees, to avoid flipping through vertical. Store yaw/pitch in the controller or derive them from the initial demo camera transform. Convert yaw/pitch back to the camera entity quaternion each frame. Keep no-input behavior deterministic, including first-frame zero delta and after pointer-lock loss.

Extend `BrowserGame`, `embind_module.cpp`, `src/app/wasmRuntime.ts`, and `src/app/main.ts` with a narrow debug input call. Prefer a scalar raw Embind method to avoid extra binding complexity:

    BrowserGame::set_debug_camera_input(
        double move_x,
        double move_y,
        double move_z,
        double look_delta_x,
        double look_delta_y,
        bool look_active,
        bool fast,
        bool slow);

The TypeScript `RawBrowserGame` interface should expose the scalar method, while the app-facing `BrowserGameRuntime` wrapper may expose an object-shaped `setDebugCameraInput(input: DebugCameraInput)` method. The wrapper should validate finite number inputs before forwarding or rely on C++ to report a recoverable error; choose one and cover it with tests.

Add a TypeScript input collector, likely `C:\dev\ofg\src\app\debugInput.ts`, that listens for keyboard events, pointer lock changes, canvas clicks, and mouse movement. DOM events should only mutate fixed collector state. Pointer deltas should accumulate until consumed, and `main.ts` should send exactly one snapshot per `requestAnimationFrame` before `runtime.frame(timeMs)`, then reset consumed mouse deltas. The collector must remove listeners on disposal/restart. The app may request pointer lock when the canvas is clicked. Do not add visible in-app instructional text unless the user explicitly asks for UI controls; the dev status text can remain focused on runtime status.

Milestone 5 performs cleanup, validation, docs, screenshots, and coverage. Remove temporary `RenderView`, `main_view`, and `set_main_view` compatibility after the camera component path is active. Search active code and active docs without matching this plan, archived plans, or coverage summaries. For example:

    rg -n "RenderView|render view|main_view|set_main_view" cpp src tests docs/API_CONTRACTS.md docs/SYSTEMS.md -S

Update `docs/API_CONTRACTS.md`, `docs/SYSTEMS.md`, and any other active docs touched by this behavior. Update `tools/cpp-coverage.mjs` so new implementation files are checked by the C++ coverage gate, then refresh `docs/coverage/latest.md` according to `C:\dev\ofg\COVERAGE.md`. Run the full validation suite, capture default and moved-camera screenshots, record artifacts, and archive this plan to `C:\dev\ofg\docs\archived\` only after all work is complete and no temporary compatibility alias remains in active code.

## Concrete Steps

Run all commands from `C:\dev\ofg` unless otherwise stated.

Before coding, confirm current state:

    git status --short
    rg -n "RenderView|main_view|set_main_view|ComponentType|BrowserGame|debugStatus" cpp src tests docs/API_CONTRACTS.md docs/SYSTEMS.md -S
    Get-Content -Path docs\GUIDES.md

During implementation, format C++ after C++ edits:

    npm run format:cpp
    npm run format:cpp:check

Run focused C++ tests after each C++ milestone:

    npm run test:cpp

Run TypeScript tests after TypeScript input/runtime changes:

    npm run test:ts

For browser/rendering work, keep a review server running:

    npm run dev

Report the printed local URL in chat. If port 5173 is busy, the dev server should choose another port and print it.

Before final acceptance, run:

    npm run format:cpp:check
    npm test
    npm run smoke:browser
    npm run smoke:browser:cpp
    npm run smoke:render
    npm run coverage

Expected outcome: all commands pass. Coverage output should not list modified implementation files in the default filtered attention report unless this plan records an exception with rationale.

When adding C++ implementation or test files, update the explicit lists in `C:\dev\ofg\cpp\CMakeLists.txt`. When adding new implementation directories or basenames, update `C:\dev\ofg\tools\cpp-coverage.mjs` so the new implementation files are included in `npm run coverage:cpp`.

## Milestone Review

After each milestone:

1. Update any changed API contracts or active docs.
2. Run the repo-local `milestone-review` skill against the milestone diff and this ExecPlan.
3. Apply required findings before marking that milestone complete, or record a rejected finding with rationale in the Decision Log.
4. Re-run relevant validation commands.
5. Record the review summary, commands, artifacts, and remaining risks in Progress or Outcomes & Retrospective.

Do not mark a milestone complete until this review is done and any required fixes have landed.

## Validation and Acceptance

Code acceptance:

- `RenderView` is removed from active C++ code and active docs by final acceptance; any temporary compatibility alias before Milestone 5 is documented with a removal step in Progress.
- `Scene` owns `Camera` components in pointer-stable scene storage.
- An entity can have at most one `Camera` component, matching the existing `MeshRenderer` component style.
- Pointer-stable scene storage means `Camera*` and `Entity*` values remain valid while their owning `Scene` generation is live, including after vector growth, and become invalid after `Scene::clear()`.
- `Scene::clear()` resets camera storage, explicit active-camera selection, entity ids, component storage, and generation.
- `Scene::main_camera()` returns the explicit camera when set, otherwise the first camera in creation order, otherwise `nullptr`.
- `Scene::set_main_camera(nullptr)` clears explicit selection and restores first-camera fallback; `Scene::set_main_camera(camera)` rejects foreign or stale non-null camera pointers.
- Scene moves and move assignment preserve valid entity/component owner pointers and preserve/reset explicit main-camera selection according to where the camera object moved.
- Renderer pass submission uses `CameraProperties`, with the current shader still receiving the correct clip-from-world matrix.
- Rendering with no input starts from a view visually matching the current demo.
- Rendering a scene with no cameras fails clearly before WebGPU pass submission.
- Debug fly camera movement mutates the active camera entity transform in C++.
- TypeScript forwards only raw input state once per animation frame and does not own scene/camera objects.
- Debug input does not create GPU resources or reconfigure the surface during ordinary no-resize frames.

Test acceptance:

- Add or update C++ doctests for camera component creation, duplicate camera rejection, scene move/clear behavior, active camera selection/fallback, stale/foreign camera rejection, invalid projection validation, `CameraProperties` resolution, scale-ignored camera ancestry, and debug controller movement.
- Add or update math tests for look-at-to-quaternion or forward/up-to-quaternion behavior, including comparison against the old `perspective_rh * look_at_rh` default demo path.
- Add or update renderer tests for no-camera failure and successful pass submission with resolved camera properties.
- Add or update demo-scene tests so setup creates a camera entity and update no longer depends on `Scene::set_main_view`.
- Add C++ debug-controller tests for first-frame zero delta, large-delta clamp, pitch clamp, movement basis, diagonal normalization, finite-input validation, reset/no-input determinism, and no-main-camera behavior.
- Add or update TypeScript Mocha tests for the runtime wrapper debug-input method and input collector behavior: keydown/keyup mapping, pointer-lock change/loss, canvas click requesting pointer lock, mouse delta accumulation and per-frame consumption, many mouse events producing one runtime call per frame, and listener cleanup/dispose.
- Existing smoke tests pass with no input.

Visual acceptance:

- Capture a default no-input screenshot under `C:\dev\ofg\artifacts\camera-debug-flycam\default-view.png`.
- Capture at least one moved-camera screenshot under `C:\dev\ofg\artifacts\camera-debug-flycam\flycam-moved.png`.
- Provide a reproducible moved-camera path, either as a small Playwright script or a documented manual sequence that clicks the canvas, sends keyboard/mouse input, captures the screenshot, and records the artifact path.
- Present screenshots in chat during implementation for human review.
- Browser smoke screenshots under existing artifact directories continue to satisfy pixel thresholds.
- Runtime debug counters show `pipelineCreateCount` and `bufferCreateCount` are stable after warm-up across no-input and moved-camera frames, and `surfaceConfigureCount` changes only on resize.

Coverage acceptance:

- `npm run coverage` passes.
- Modified implementation files meet the default line-coverage attention threshold, currently about 90%, or this plan records an explicit exception with rationale.
- If the coverage filter does not check a newly added implementation directory, update the coverage tooling so the new implementation files are included.
- Refresh `C:\dev\ofg\docs\coverage\latest.md` after the final successful coverage run, following `C:\dev\ofg\COVERAGE.md`.

## Idempotence and Recovery

Most changes are additive until the final `RenderView` removal pass. If a milestone fails, keep the last passing state and fix forward rather than reverting unrelated user changes. Do not use `git reset --hard` or checkout-based destructive commands.

If the renderer rename causes broad breakage, keep a temporary helper that builds `CameraProperties` from the old hard-coded camera values, then remove `RenderView`/`main_view` compatibility after the camera resolver is active.

If pointer lock or browser input behaves differently across Chromium variants, keep the C++ debug input API intact and adjust only the TypeScript collector. The C++ tests should continue to validate camera behavior without browser input.

If visual smoke fails after the demo camera migration, first compare the old hard-coded `look_at_rh` output to the new resolved `CameraProperties` output in a focused test. The desired recovery is to fix camera basis/quaternion conversion, not to reintroduce a stored `RenderView`.

If coverage fails, add focused tests for the changed behavior rather than excluding files. Record any unavoidable browser-only exception in this plan with the exact file and reason.

## Artifacts and Notes

Durable implementation screenshots should be stored under:

    C:\dev\ofg\artifacts\camera-debug-flycam\

Useful final artifacts should include:

    C:\dev\ofg\artifacts\camera-debug-flycam\default-view.png
    C:\dev\ofg\artifacts\camera-debug-flycam\flycam-moved.png
    C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png
    C:\dev\ofg\artifacts\browser-smoke-cpp\scene.png
    C:\dev\ofg\artifacts\render-smoke\opaque-demo.png

Record short validation transcripts in Progress or Outcomes when commands pass or fail. Avoid pasting full build logs into the plan unless a specific failure message is needed to explain a decision.

## Interfaces and Dependencies

New or changed C++ interfaces expected by completion:

    class Camera;

    struct CameraProperties {
        const Camera* camera{nullptr};
        math::Mat4 world_from_camera;
        math::Mat4 camera_from_world;
        math::Mat4 clip_from_camera;
        math::Mat4 clip_from_world;
        float vertical_fov_radians{0.0f};
        float aspect{1.0f};
        float near_z{0.1f};
        float far_z{80.0f};
    };

    class Camera : public Component {
    public:
        explicit Camera(Entity* entity) noexcept;
        float vertical_fov_radians() const noexcept;
        float near_z() const noexcept;
        float far_z() const noexcept;
        void set_perspective(float vertical_fov_radians, float near_z, float far_z);
        CameraProperties camera_properties(float aspect) const;
    };

    enum class ComponentType {
        MeshRenderer,
        Camera,
    };

    class Entity {
    public:
        Camera* camera() noexcept;
        const Camera* camera() const noexcept;
    };

    class Scene {
    public:
        std::size_t camera_count() const noexcept;
        Camera* get_camera(std::size_t index) noexcept;
        const Camera* get_camera(std::size_t index) const noexcept;
        Camera* main_camera() noexcept;
        const Camera* main_camera() const noexcept;
        void set_main_camera(Camera* camera);
    };

    struct DebugCameraInput {
        float move_x;
        float move_y;
        float move_z;
        float look_delta_x;
        float look_delta_y;
        bool look_active;
        bool fast;
        bool slow;
    };

    class Game {
    public:
        static void set_debug_camera_input(DebugCameraInput input);
    };

    class BrowserGame {
    public:
        void set_debug_camera_input(double move_x,
            double move_y,
            double move_z,
            double look_delta_x,
            double look_delta_y,
            bool look_active,
            bool fast,
            bool slow);
    };

The exact names may change during implementation if the codebase points to a better local pattern, but the final API must preserve the ownership boundaries: scene owns camera components, renderer consumes resolved `CameraProperties`, C++ owns camera behavior, and TypeScript forwards raw input only.

New or changed TypeScript interfaces expected by completion:

    export interface DebugCameraInput {
      readonly moveX: number;
      readonly moveY: number;
      readonly moveZ: number;
      readonly lookDeltaX: number;
      readonly lookDeltaY: number;
      readonly lookActive: boolean;
      readonly fast: boolean;
      readonly slow: boolean;
    }

    export interface BrowserGameRuntime {
      setDebugCameraInput(input: DebugCameraInput): void;
    }

    export interface RawBrowserGame {
      set_debug_camera_input(
        moveX: number,
        moveY: number,
        moveZ: number,
        lookDeltaX: number,
        lookDeltaY: number,
        lookActive: boolean,
        fast: boolean,
        slow: boolean
      ): void;
    }

No new third-party runtime dependency is planned. Existing C++ math helpers plus one small tested quaternion/basis helper, doctest, TypeScript, Mocha, Playwright, and WebGPU tooling should be enough.
