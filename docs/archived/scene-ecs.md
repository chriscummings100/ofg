# Build the First ECS Scene System

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

This plan follows `C:\dev\ofg\PLANS.md`.

## Purpose / Big Picture

OFG currently renders the demo through a small render-submission `Scene` that stores a main view plus a list of `RenderObject` values. This plan replaces that ad hoc demo scene authoring with the first real scene system. For this v1, "ECS" means an entity tree plus scene-owned component containers: a `Scene` owns a root `Entity`, child entities, local transforms, and `MeshRenderer` components that can be iterated by index. This is not yet a full query/systems framework.

After this work, the player-visible result should be unchanged: the browser and native smoke scenes still show the checker floor and four animated colored cubes. The internal behavior changes so the floor and each cube are entities with `MeshRenderer` components. `Game` owns a current scene pointer, as requested, and `Renderer` builds its transient draw list from all mesh renderers in that scene.

This is intentionally a first version. It does not add entity deletion, reparenting, serialization, scripting, culling, cameras as entities, prefab loading, `Character`, or TypeScript scene mutation. The next natural step is frustum culling using camera data, mesh bounds, and each renderer entity's `world_from_local` transform. Because current mesh resources do not expose cached bounds, the first culling prerequisite should be a local mesh bounds API computed during `Mesh::init` and refreshed by later mesh mutation paths.

## Progress

- [x] (2026-06-29 21:18 +01:00) Drafted the initial plan from the current renderer/demo scene code and user API preferences.
- [x] (2026-06-29 21:35 +01:00) Folded in the `review-plan` feedback: tightened component ownership, root/clear semantics, mesh-renderer iteration, demo lifecycle, docs, coverage, and visual validation.
- [x] (2026-06-29 22:25 +01:00) Implemented core scene/entity/component types and quaternion transform math with focused C++ tests.
- [x] (2026-06-29 22:25 +01:00) Migrated the existing demo to stable floor and cube entities while preserving the intended visual output.
- [x] (2026-06-29 22:25 +01:00) Replaced render-object scene authoring with mesh-renderer draw-list extraction and borrowed draw data.
- [x] (2026-06-29 22:25 +01:00) Moved `Game` to a current scene pointer and updated active API contracts/docs.
- [x] (2026-06-29 22:55 +01:00) Validated with formatting, C++ tests, native/browser/focused browser smoke, full coverage, refreshed coverage docs, and visual artifacts.

## Surprises & Discoveries

- Observation: `C:\dev\ofg\cpp\include\ofg\scene\scene.hpp` already has a type named `Scene`, but it is a render-submission container rather than an entity/component scene graph.
  Evidence: it stores `RenderView m_main_view` and `std::vector<RenderObject> m_render_objects`, and `Renderer` converts those render objects into `DrawCommand` values.
- Observation: `Renderer` already owns the transient `DrawList` conversion step.
  Evidence: `C:\dev\ofg\cpp\src\render\renderer.cpp` has `build_draw_list_from_scene(const Scene& scene, DrawList& draw_list)`, which is the right place to switch from `RenderObject` iteration to `MeshRenderer` iteration without changing `OpaquePass`'s WebGPU command flow.
- Observation: active docs still describe old scene ownership.
  Evidence: `docs\API_CONTRACTS.md` says "`Game` owns demo-scene state" and "The current `Scene` stores the main render view plus renderable objects." `docs\SYSTEMS.md` describes `Scene` as minimal render-object storage under both `CppRuntime` and `CppRenderer`.
- Observation: `tools\cpp-coverage.mjs` does not currently include `cpp\src\scene` in its checked-file filter.
  Evidence: `collectCheckedFiles` checks `core`, `gpu`, `math`, `resources`, selected `game`, `runtime`, and selected `render` paths, but no `scene` path.
- Observation: The milestone-review skill references `docs\ARCHITECTURE.md`, but this repository currently does not have that file.
  Evidence: `Get-Content -Raw docs/ARCHITECTURE.md` failed with "Cannot find path"; the review used `docs\API_CONTRACTS.md`, `docs\SYSTEMS.md`, `AGENTS.md`, `PLANS.md`, and this ExecPlan instead.
- Observation: The first post-cleanup native C++ test run rebuilt the native Dawn dependency and emitted third-party/toolchain warnings, but the OFG test executable passed.
  Evidence: `npm run test:cpp` completed at 2026-06-29 22:25 +01:00 with "100% tests passed, 0 tests failed out of 1"; warnings were from Dawn/clang-cl command-line compatibility.
- Observation: `MeshRenderer::m_sort_origin_offset` is a local-space offset, not a world placement.
  Evidence: renderer extraction computes `command.m_sort_origin = transform_point(world_from_local_value, mesh_renderer.m_sort_origin_offset)`, so cube placement belongs in the entity local transform and the demo cube offset remains zero.
- Observation: Adding `cpp\src\scene` to the C++ coverage gate exposed under-covered first-version scene/move/const paths plus quaternion invalid-input paths.
  Evidence: the first `npm run coverage` attempt failed for `cpp\src\math\quat.cpp` at 88.00%, `cpp\src\render\renderer.cpp` at 89.53%, and `cpp\src\scene\scene.cpp` at 75.90%; targeted tests brought the final run to 100.00%, 90.70%, and 98.97% respectively.

## Decision Log

- Decision: `MeshRenderer` stores non-owning raw pointers to resource objects such as `Mesh*`, not `std::shared_ptr<Mesh>`.
  Rationale: `Resources` already owns meshes, materials, shaders, and textures for the active WebGPU lifetime. Shared ownership would blur resource lifetime and make unloading harder to reason about. A later hot-reload/unload system should prefer handles with generation checks, not shared ownership.
  Date/Author: 2026-06-29 / Codex and user.
- Decision: Entity-returning APIs return `Entity*` consistently, not a mix of `Entity&` and `Entity*`.
  Rationale: This matches the desired public style, supports nullable lookup results for invalid ids, and keeps creation/getter APIs visually consistent.
  Date/Author: 2026-06-29 / Codex and user.
- Decision: The local transform rotation field is `m_rotation`, represented by a quaternion.
  Rationale: Radians are assumed unless otherwise stated, so names like `m_rotation_radians` are unnecessary. A quaternion avoids baking early Euler-angle assumptions into the scene API.
  Date/Author: 2026-06-29 / Codex and user.
- Decision: Transform matrices are named by direction, especially `world_from_local`.
  Rationale: The naming makes multiplication order clear: `world_pos = world_from_local * local_pos`.
  Date/Author: 2026-06-29 / Codex and user.
- Decision: New C++ float literals use lowercase `f`, such as `1.0f`.
  Rationale: This matches the user's preferred style and keeps new code consistent within this feature.
  Date/Author: 2026-06-29 / Codex and user.
- Decision: Scene-owned entity and component storage must be pointer-stable in v1, but it should be described as pointer-stable non-dense storage rather than cache-friendly dense ECS storage.
  Rationale: Entities store direct pointers to components and callers traverse raw `Entity*` links. `std::vector<std::unique_ptr<T>>` keeps pointed-to objects stable and simple for v1, but it still has per-object heap allocation and pointer chasing. Later performance work can add handles, arenas, or dense component pages.
  Date/Author: 2026-06-29 / Codex and user.
- Decision: Defer `Character` from this milestone.
  Rationale: The current behavior only needs `MeshRenderer`; adding `Character` now creates API, tests, docs, and coverage work without observable value. A later gameplay milestone can introduce it with real behavior.
  Date/Author: 2026-06-29 / Codex and user.
- Decision: Keep `Game`'s current scene as `std::unique_ptr<Scene>` for v1.
  Rationale: The user explicitly wants `Game` to contain a current scene pointer so future scene swaps are a direct extension. The plan must therefore include null checks and pre-prepare resize behavior rather than silently changing this to a value member.
  Date/Author: 2026-06-29 / Codex and user.

## Outcomes & Retrospective

Implemented the first scene ECS and preserved the demo visual. `Scene` now owns a root entity, pointer-stable entity storage, scene-owned `MeshRenderer` components, indexed mesh-renderer iteration, root/entity lookup, generation invalidation, and local-to-world transform helpers. `Entity` exposes a local transform with `m_position`, quaternion `m_rotation`, and `m_scale`, plus parent/child/sibling traversal and `Entity*`-returning APIs. `Game` now owns `std::unique_ptr<Scene> m_current_scene`, and the renderer builds its transient draw list by borrowing immutable mesh-renderer draw data.

The demo scene is now split into resource build, stable entity/component setup, and per-frame transform/view mutation. Native and browser artifacts still show the checker floor and four colored cubes:

- `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png`
- `C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png`
- `C:\dev\ofg\artifacts\browser-smoke-cpp\scene.png`

Validation passed with `npm run format:cpp:check`, `npm run test:cpp`, `npm run smoke:render`, `npm run smoke:browser`, `npm run smoke:browser:cpp`, and `npm run coverage`. Coverage docs were refreshed under `docs\coverage`; the final C++ coverage gate includes `cpp\src\scene\scene.cpp` at 98.97%, `cpp\src\math\quat.cpp` at 100.00%, and `cpp\src\render\renderer.cpp` at 90.70%.

Follow-up design work remains: entity handles/deletion/reparenting, cached world transforms with dirty propagation, camera components, mesh bounds generated during mesh initialization, and frustum culling from camera data plus `world_from_local` transforms.

## Contract and Quality Baseline

This plan preserves `OFG-BOOT-001 TypeScript Host Ownership`: TypeScript remains responsible for DOM, canvas, WASM loading, and smoke helpers only. It must not own scene graph state, gameplay simulation, renderer internals, or game-world data structures.

This plan intentionally updates `OFG-BOOT-002 C++ Runtime Ownership`: C++ still owns frame state, renderer resources, draw-list construction, WebGPU resource creation, browser WebGPU runtime behavior, and native Dawn offscreen rendering. The contract should change from "`Game` owns demo-scene state" to "`Game` owns the current ECS scene and the demo binding data needed to populate and animate that scene." The contract should also change from "`Scene` stores renderable objects" to "`Scene` stores entities and scene-owned component containers; `Renderer` builds a private transient `DrawList` from `MeshRenderer` components."

This plan preserves `OFG-BOOT-003 WASM Facade`: the browser facade stays narrow and does not expose raw scene mutation to TypeScript.

This plan preserves `OFG-BOOT-004 Renderer Compatibility`: browser and native smoke must keep equivalent visual output, including clear color, checker ground plane, saturated cube colors, resource layer, opaque pass shader path, and smoke thresholds.

This plan preserves `OFG-BOOT-005 WebGPU Baseline`: no optional GPU features, no manual limits above adapter defaults, and the same opaque textured material path.

This plan preserves `OFG-BOOT-006 Resource Lifetime`: mesh, material, shader, and texture resources are created during preparation and reused across ordinary frames. Per-frame animation may update entity local transforms and build transient draw commands, but it must not recreate resource objects every frame.

This plan preserves `OFG-BOOT-007 Generated Artifacts`, `OFG-BOOT-008 Deployment`, and `OFG-BOOT-009 Coverage`. Modified implementation files must meet the coverage gate or the plan must record an explicit exception with rationale. Because this plan adds `cpp\src\scene\scene.cpp`, it must also update the C++ coverage filter so scene implementation files are checked.

All new and modified C++ functions should have comments or doc strings describing their purpose. Files should retain detailed top comments. New C++ code must follow repo naming conventions: classes and structs use `CamelCase`, functions use `lowercase_with_underscores`, member variables use `m_name_with_underscores`, and locals use `name_with_underscores`.

## Context and Orientation

Current scene code lives in `C:\dev\ofg\cpp\include\ofg\scene\scene.hpp`. It is header-only today and defines:

    struct RenderObject {
        Mesh* m_mesh{nullptr};
        math::Mat4 m_model{math::mat4_identity()};
        PropertyBag m_properties;
        std::vector<MaterialOverride> m_material_overrides;
        math::Vec3 m_sort_origin;
    };

    class Scene {
    public:
        const RenderView& main_view() const noexcept;
        void set_main_view(RenderView main_view) noexcept;
        void add_render_object(RenderObject object);
        void clear() noexcept;
        std::span<const RenderObject> render_objects() const noexcept;
        std::size_t size() const noexcept;
    };

Current demo resource and object creation lives in `C:\dev\ofg\cpp\src\render\demo_scene.cpp`. `build_demo_scene` creates shader, textures, materials, and meshes. `update_demo_scene` creates a temporary `Scene`, fills it with one ground `RenderObject` and four cube `RenderObject` values, then moves it into the `Game` scene.

Current draw-list extraction lives in `C:\dev\ofg\cpp\src\render\renderer.cpp`. `build_draw_list_from_scene` loops over `scene.render_objects()` and copies each `RenderObject` into a `DrawCommand`.

Current `Game` scene ownership lives in `C:\dev\ofg\cpp\include\ofg\game\game.hpp` and `C:\dev\ofg\cpp\src\game\game.cpp`. `Game` currently stores:

    DemoScene m_demo_scene;
    Scene m_scene;

`Game::prepare_impl` builds resources and the initial scene. `Game::resize_impl` and `Game::update_impl` call `update_demo_scene` to rebuild render objects. `Game::resize` is valid before `Game::prepare`; this plan must preserve that behavior by recording aspect during early resize and only touching scene data once `m_current_scene` exists.

The existing renderer pass should not need large WebGPU changes for this work. `C:\dev\ofg\cpp\src\render\opaque_pass.cpp` consumes `DrawList` and writes `DrawCommand::m_model` into the draw uniform buffer.

## Plan of Work

First, add a real scene ECS model under `C:\dev\ofg\cpp\include\ofg\scene\scene.hpp` and a new implementation file `C:\dev\ofg\cpp\src\scene\scene.cpp`. Move non-trivial scene behavior out of the header. Add this new `.cpp` file and any math `.cpp` file to `C:\dev\ofg\cpp\CMakeLists.txt`.

The public scene API should include these concepts:

    using EntityId = std::uint32_t;

    enum class ComponentType {
        MeshRenderer,
    };

    struct LocalTransform {
        math::Vec3 m_position{0.0f, 0.0f, 0.0f};
        math::Quat m_rotation{math::quat_identity()};
        math::Vec3 m_scale{1.0f, 1.0f, 1.0f};
    };

    class Component {
    public:
        ComponentType type() const noexcept;
        Entity* entity() noexcept;
        const Entity* entity() const noexcept;
    };

    class MeshRenderer final : public Component {
    public:
        Mesh* m_mesh{nullptr};
        PropertyBag m_properties;
        std::vector<MaterialOverride> m_material_overrides;
        math::Vec3 m_sort_origin_offset{0.0f, 0.0f, 0.0f};
    };

    class Entity {
    public:
        EntityId id() const noexcept;
        LocalTransform& local_transform() noexcept;
        const LocalTransform& local_transform() const noexcept;

        Entity* parent() noexcept;
        const Entity* parent() const noexcept;
        Entity* first_child() noexcept;
        const Entity* first_child() const noexcept;
        Entity* next_sibling() noexcept;
        const Entity* next_sibling() const noexcept;

        Component* create_component(ComponentType type);
        MeshRenderer* mesh_renderer() noexcept;
        const MeshRenderer* mesh_renderer() const noexcept;
    };

    class Scene {
    public:
        Entity* get_root() noexcept;
        const Entity* get_root() const noexcept;
        Entity* get_entity(EntityId id) noexcept;
        const Entity* get_entity(EntityId id) const noexcept;
        Entity* create_entity(Entity* parent);

        std::size_t entity_count() const noexcept;
        std::size_t mesh_renderer_count() const noexcept;
        MeshRenderer* get_mesh_renderer(std::size_t index) noexcept;
        const MeshRenderer* get_mesh_renderer(std::size_t index) const noexcept;

        const RenderView& main_view() const noexcept;
        void set_main_view(RenderView main_view) noexcept;
        void clear() noexcept;
    };

`Scene` constructs a root entity immediately. The root has id `0`, parent `nullptr`, and identity local transform. `Scene::create_entity` requires a non-null parent from the same scene; passing `nullptr` or an entity from a different scene throws `EngineError`. New child entities receive monotonically assigned ids starting at `1` until `clear()`. `Scene::get_entity` returns `nullptr` for invalid ids. `Scene::clear()` invalidates all existing entity/component pointers, clears component containers and lookup data, resets the main view to identity, resets ids, and creates a fresh root id `0`.

`Entity::create_component(ComponentType::MeshRenderer)` allocates a scene-owned `MeshRenderer`, sets its component type and owning `Entity*`, stores its pointer on the entity, appends it to the scene's mesh-renderer container, and returns it as `Component*`. Creating a second mesh renderer on the same entity throws `EngineError`. `MeshRenderer` iteration order is creation order and is exposed by `mesh_renderer_count()` plus `get_mesh_renderer(index)`. Out-of-range mesh-renderer indexes return `nullptr`.

Second, add quaternion math in the math layer if it does not already exist. Prefer folding tiny helpers into the existing math files unless the implementation is large enough to justify `C:\dev\ofg\cpp\include\ofg\math\quat.hpp` and `C:\dev\ofg\cpp\src\math\quat.cpp`. The minimal API should be:

    struct Quat {
        float x{0.0f};
        float y{0.0f};
        float z{0.0f};
        float w{1.0f};
    };

    Quat quat_identity() noexcept;
    std::optional<Quat> quat_from_axis_angle(Vec3 axis, float radians, std::string& error);
    std::optional<Quat> normalize(Quat value, std::string& error);
    Mat4 mat4_from_quat(Quat rotation) noexcept;

The fallible functions return `std::nullopt` and set `error` when inputs are non-finite or zero length. On success, they clear `error`. `mat4_from_quat` expects a finite normalized quaternion. The demo cube animation should call `quat_from_axis_angle(math::vec3(0.0f, 1.0f, 0.0f), angle, error)` and throw `EngineError` if it returns `std::nullopt`.

Third, add transform composition helpers in the scene layer because they depend on `LocalTransform` and `Entity`:

    math::Mat4 parent_from_local(const LocalTransform& transform) noexcept;
    math::Mat4 world_from_local(const Entity& entity) noexcept;

For a position in entity-local space:

    world_pos = world_from_local * local_pos

The local matrix composes as translation, rotation, then scale for column vectors:

    parent_from_local = translation * rotation * scale

Ancestor composition is exact:

    world_from_local(root) = parent_from_local(root)
    world_from_local(child) = world_from_local(parent) * parent_from_local(child)

For v1, computing this by walking parents during extraction is acceptable for the demo. Extraction must compute each mesh renderer's `world_from_local` once per frame and reuse that value for the command model matrix and sort origin. Cached world transforms and dirty propagation are a required follow-up before larger scenes.

Fourth, migrate demo authoring through a stable setup/update split. Keep `build_demo_scene(DemoScene&)` resource-only: it creates shader, textures, materials, and meshes exactly once through `Resources`. Add `setup_demo_scene(DemoScene&, Scene&)`: it clears the scene, creates the floor entity and four cube entities as root children, creates mesh renderers, binds meshes/material overrides, and stores non-owning pointers to the demo entities or mesh renderers inside `DemoScene`. Keep `update_demo_scene(const DemoScene&, double time_ms, float aspect, Scene&)`: it validates that resources and cached scene pointers are present, updates the scene main view, and mutates local transforms only.

The scene should contain:

- one root entity created by `Scene`,
- one child floor entity with a `MeshRenderer` using the ground mesh and material defaults,
- four child cube entities with `MeshRenderer` components using the cube mesh and per-cube material overrides.

`DemoScene` cached scene pointers are valid only for the specific scene passed to `setup_demo_scene`. Whenever `Scene::clear()` runs or `Game` swaps/resets `m_current_scene`, `Game` must reset `m_demo_scene` or rerun setup before update. `Game::release_impl` must reset demo cached pointers and the current scene before `Resources::release()` invalidates resource pointers.

Fifth, replace render-object storage with component-driven extraction. `Renderer` should build its private transient `DrawList` from every `MeshRenderer` in `Scene` by index. Each renderer produces one command with the `world_from_local` computed once:

    command.m_mesh = mesh_renderer.m_mesh;
    command.m_model = world_from_local_value;
    command.m_properties = mesh_renderer.m_properties or a borrowed view of it;
    command.m_material_overrides = mesh_renderer.m_material_overrides or a borrowed span of it;
    command.m_sort_origin = transform_point(world_from_local_value, mesh_renderer.m_sort_origin_offset);

The preferred v1 implementation is to make `DrawCommand` borrow immutable draw properties and material overrides from `MeshRenderer` for the duration of `Renderer::render`, avoiding per-frame deep copies of `PropertyBag` and `std::vector<MaterialOverride>`. If implementation keeps owning copies temporarily, it must reuse storage so steady-state frames do not repeatedly allocate, and the plan's Outcomes must record this as a performance debt. `DrawList` remains a transient renderer queue; it must not be used after the render call whose scene it borrows from. Existing validation should continue to catch null meshes, bad material overrides, missing draw properties, and non-GPU-ready resources. Avoid double-validating the same command path if that becomes measurable, but do not remove validation without tests.

Sixth, update `Game` so it owns a swappable current scene pointer:

    std::unique_ptr<Scene> m_current_scene;

`Game::prepare_impl` creates `m_current_scene`, calls `build_demo_scene(m_demo_scene)`, calls `setup_demo_scene(m_demo_scene, *m_current_scene)`, then calls `update_demo_scene` with the latest recorded time/aspect before preparing the renderer. `Game::resize_impl` must remain valid before prepare: it records runtime size and aspect, and only updates the scene when the renderer is ready and `m_current_scene != nullptr`. `Game::update_impl` requires `m_current_scene != nullptr` and fails clearly if the scene is missing. `Game::render_impl` also fails clearly if `m_current_scene == nullptr`. Future scene swapping can replace this pointer, but this plan only needs one active scene.

Seventh, update tests, docs, and coverage tooling. Add `C:\dev\ofg\cpp\tests\scene_test.cpp` for scene graph, components, duplicate rejection, invalid parent rejection, root/clear behavior, same-scene validation, and transforms. Update `C:\dev\ofg\cpp\tests\demo_scene_test.cpp` to assert that the demo has one floor mesh renderer and four cube mesh renderers, that the same resource counts are created, that cached demo scene pointers are validated, and that cube transforms animate deterministically. Update renderer tests to build test scenes through `Entity` and `MeshRenderer` rather than `add_render_object`. Update `C:\dev\ofg\docs\API_CONTRACTS.md` and `C:\dev\ofg\docs\SYSTEMS.md` for the new ownership boundary. Update `tools\cpp-coverage.mjs` so `cpp\src\scene` implementation files are checked.

## Concrete Steps

Run all commands from `C:\dev\ofg` unless a command states otherwise.

1. Inspect the current scene, renderer, docs, and coverage files before editing:

       rg -n "RenderObject|render_objects|add_render_object|DemoScene|update_demo_scene|build_draw_list_from_scene|demo-scene|Scene" cpp docs tools -S

2. Add `math::Quat` and focused tests in `cpp\tests\math_test.cpp` or a new math test file.

3. Add `Entity`, `Component`, `MeshRenderer`, and `Scene` implementation files. Update `cpp\CMakeLists.txt` to compile any new `.cpp` and test files.

4. Add `cpp\tests\scene_test.cpp` and make the scene core pass before changing renderer extraction.

5. Split demo lifecycle into `build_demo_scene`, `setup_demo_scene`, and `update_demo_scene`; update `demo_scene_test.cpp` to cover resource counts, entity/component setup, cached pointer validation, and deterministic animation.

6. Update `Renderer` draw-list extraction to iterate mesh renderers, compute each `world_from_local` once, and preserve validation.

7. Update renderer tests to use scene entities/components.

8. Update `Game` ownership and lifecycle around `std::unique_ptr<Scene> m_current_scene`, preserving pre-prepare resize behavior.

9. Update `docs\API_CONTRACTS.md`, `docs\SYSTEMS.md`, and `tools\cpp-coverage.mjs`.

10. Format C++:

       npm run format:cpp
       npm run format:cpp:check

   Expected result: the check exits successfully with no formatting diffs.

11. Run focused native tests:

       npm run test:cpp

   Expected result: all doctest cases pass through CMake/CTest.

12. Run visual/native smoke:

       npm run smoke:render

   Expected result: `artifacts\render-smoke\opaque-demo.png` and `artifacts\render-smoke\report.json` are produced and sampled pixel thresholds pass. Record both paths in this plan and show the PNG in chat.

13. Run browser smoke and focused C++/WASM browser smoke because this touches rendering output:

       npm run smoke:browser
       npm run smoke:browser:cpp

   Expected result: browser artifacts are produced under `artifacts\browser-smoke` and `artifacts\browser-smoke-cpp`, including an inspectable opaque-demo image. Record and present the artifact paths in chat.

14. Run coverage:

       npm run coverage:cpp

   Expected result: modified native-checkable implementation files, including `cpp\src\scene\scene.cpp`, do not appear in the default filtered coverage attention output. If a modified implementation file appears, add tests or record an explicit exception with rationale.

15. Refresh committed coverage docs after a meaningful coverage run:

       npm run coverage

   Then copy or summarize generated results into `docs\coverage\cpp-summary.json` and `docs\coverage\latest.md` as described in `C:\dev\ofg\COVERAGE.md`. If only `npm run coverage:cpp` is run during iteration, the final implementation pass should still run `npm run coverage` unless this plan records why TypeScript coverage was intentionally skipped.

16. If TypeScript or WASM packaging behavior changes unexpectedly, also run:

       npm run build
       npm run test:ts

   Expected result: the app and TypeScript tests pass. This plan is not expected to require TypeScript source changes.

## Milestone Review

After each milestone:

1. Update any changed API contracts or active docs.
2. Run the repo-local `milestone-review` skill against the milestone diff and this ExecPlan.
3. Apply required findings before marking the milestone complete, or record a rejected finding with rationale in the Decision Log.
4. Re-run relevant validation commands.
5. Record the review summary, commands, artifacts, and remaining risks in Progress or Outcomes & Retrospective.

Milestone 1 is complete when quaternion math and scene/entity/component core tests pass.

Milestone 2 is complete when demo resources are still resource-only, `setup_demo_scene` creates the stable floor/cube entity scene, `update_demo_scene` mutates transforms/view only, and demo tests pass.

Milestone 3 is complete when renderer draw-list extraction uses `MeshRenderer` components and renderer tests pass.

Milestone 4 is complete when `Game` owns and renders through `m_current_scene`, preserves pre-prepare resize behavior, lifecycle tests pass, and active docs/contracts are updated.

Milestone 5 is complete when formatting, C++ tests, native smoke, browser smoke, focused C++ browser smoke, coverage gates, coverage docs, and visual artifact reporting have all passed or recorded explicit accepted exceptions.

## Validation and Acceptance

Acceptance criteria:

- `Scene::get_root()` returns the stable root entity with id `0`.
- `Scene::create_entity(Entity* parent)` returns an `Entity*`, rejects null or cross-scene parents, links the entity into the parent's first-child/next-sibling list, and assigns a stable id.
- `Scene::get_entity(EntityId id)` returns the matching `Entity*` or `nullptr` for an invalid id.
- `Scene::clear()` invalidates prior entity/component pointers, resets storage and ids, resets main view to identity, and creates a fresh root.
- `Entity::create_component(ComponentType::MeshRenderer)` creates one scene-owned mesh renderer, stores the pointer on the entity, sets the component's owning entity pointer, and exposes it through indexed scene iteration.
- Creating a duplicate component of the same type on one entity throws a clear `EngineError`.
- `MeshRenderer` stores non-owning `Mesh*` and material override data without owning resource lifetime.
- `LocalTransform` stores `m_position`, quaternion `m_rotation`, and `m_scale`.
- Quaternion fallible APIs use `std::optional` plus an error string and are tested for invalid/non-finite inputs.
- Transform helpers follow the naming and multiplication convention `world_pos = world_from_local * local_pos`.
- `world_from_local(child) = world_from_local(parent) * parent_from_local(child)` for non-root entities.
- `Renderer` builds draw commands from scene mesh renderers, computes each renderer world matrix once per extraction, preserves creation order, and keeps `DrawList` validation.
- The demo scene has one floor entity and four cube entities, each with a mesh renderer.
- `build_demo_scene` creates resources only, `setup_demo_scene` creates stable entities/components, and `update_demo_scene` mutates transforms/view only.
- The demo renders the same class of image as before: checker floor, four animated colored cubes, perspective camera, depth buffering, and opaque textured materials.
- `Game` owns a current scene pointer, handles null scene state clearly, preserves resize-before-prepare behavior, and renders through the current scene.
- `docs\API_CONTRACTS.md` and `docs\SYSTEMS.md` reflect the new scene ownership model.
- `tools\cpp-coverage.mjs` gates new native-checkable `cpp\src\scene` implementation files.

Required validation commands:

    npm run format:cpp:check
    npm run test:cpp
    npm run smoke:render
    npm run smoke:browser
    npm run smoke:browser:cpp
    npm run coverage

Coverage acceptance:

The plan is complete only when each modified implementation file either meets the default coverage attention gate or this plan records a specific exception. The current threshold is documented in `C:\dev\ofg\COVERAGE.md` and `OFG-BOOT-009` as about 90 percent line coverage. `cpp\src\game\game.cpp` already has a documented C++ coverage exception because device-bound render behavior is covered through WASM/native smoke; if this plan changes that exception's rationale, update `COVERAGE.md` and `docs\coverage\latest.md`.

Visual acceptance:

Because this work affects rendering output, capture and present durable artifacts before final acceptance. Required artifact paths to record in this plan and present in chat are:

- `artifacts\render-smoke\opaque-demo.png`
- `artifacts\render-smoke\report.json`
- `artifacts\browser-smoke\opaque-demo.png`, or the current browser-smoke image path if the tool writes a different filename
- the focused C++ browser smoke artifact path under `artifacts\browser-smoke-cpp`

## Idempotence and Recovery

The implementation should be additive and easy to retry. If a validation command fails, fix the code and rerun the same command. Do not delete generated build directories unless the failure is clearly stale generated output.

If the new scene API causes renderer tests to fail, keep `OpaquePass` WebGPU command encoding stable and debug the extraction layer first. This plan should not require changing shader code, material bind groups, mesh upload behavior, or WebGPU pass encoding.

If pointer stability problems appear, prefer pointer-stable scene-owned containers for v1. Do not switch resource ownership to `shared_ptr` to solve scene pointer issues.

If draw-command borrowing becomes too invasive, preserve behavior with a reusable storage fallback and record the performance debt in Outcomes. Do not leave repeated steady-state heap churn undocumented.

If quaternion math becomes larger than expected, keep the quaternion API minimal and test only the operations needed by this plan. Avoid adding a broad math library in this milestone.

If `m_current_scene` is reset or swapped, reset or rebuild `DemoScene` cached scene pointers before the next update. Update/render should throw a clear `EngineError` if they require a current scene and it is missing.

No `git reset --hard`, `git checkout --`, or broad cleanup should be used during this work. Existing unrelated user changes must be preserved.

## Artifacts and Notes

Implementation artifacts to record later:

- Native render-smoke PNG path: `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png`.
- Native render-smoke JSON report path: `C:\dev\ofg\artifacts\render-smoke\report.json`.
- Browser smoke screenshot path: `C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png`.
- Browser smoke report path: `C:\dev\ofg\artifacts\browser-smoke\report.json`.
- Focused C++ browser smoke artifact path: `C:\dev\ofg\artifacts\browser-smoke-cpp\scene.png`.
- Focused C++ browser smoke report path: `C:\dev\ofg\artifacts\browser-smoke-cpp\webgpu-init-report.json`.
- Milestone review summaries: local five-pass review for Milestones 1-4 completed at 2026-06-29 22:25 +01:00. Sub-agent tooling was available, but the current tool policy only allows spawning sub-agents when the user explicitly asks for sub-agents; the review was therefore performed locally across contract, code quality, legacy, correctness, and validation concerns. Required findings fixed during review: stale demo-scene header wording, active docs still describing render objects, coverage filter missing `cpp\src\scene`, doctest nodiscard warnings, uppercase `F` float suffixes in touched code, and cube sort-origin double-counting. Follow-ups recorded: cached world transforms, entity handles/deletion, camera components, mesh bounds, and frustum culling remain future work. Rejected findings: none. Validation rerun: `npm run format:cpp:check` and `npm run test:cpp` passed. Remaining risk at that time: full smoke, browser, and coverage gates were pending.
- Final validation review: Milestone 5 completed at 2026-06-29 22:55 +01:00 after `npm run smoke:render`, `npm run smoke:browser`, `npm run smoke:browser:cpp`, `npm run coverage`, and a final `npm run test:cpp` all passed. Required coverage finding fixed: the first full coverage run failed `quat.cpp`, `renderer.cpp`, and `scene.cpp`; targeted tests fixed all three before the final passing run. Remaining risk: no accepted validation exceptions beyond the existing documented `Game`/browser/native smoke coverage exceptions.
- Coverage summary path: `C:\dev\ofg\docs\coverage\cpp-summary.json`.
- TypeScript coverage summary path: `C:\dev\ofg\docs\coverage\ts-coverage-summary.json`.
- Coverage latest summary path: `C:\dev\ofg\docs\coverage\latest.md`.

Current source landmarks:

    C:\dev\ofg\cpp\include\ofg\scene\scene.hpp
    C:\dev\ofg\cpp\src\render\demo_scene.cpp
    C:\dev\ofg\cpp\src\render\renderer.cpp
    C:\dev\ofg\cpp\include\ofg\game\game.hpp
    C:\dev\ofg\cpp\src\game\game.cpp
    C:\dev\ofg\docs\API_CONTRACTS.md
    C:\dev\ofg\docs\SYSTEMS.md
    C:\dev\ofg\tools\cpp-coverage.mjs

Review-plan feedback applied on 2026-06-29:

- Added component owner/back-reference contract.
- Removed `Character` from v1.
- Replaced ambiguous mesh-renderer spans with indexed access.
- Defined root, id, clear, invalid-parent, and same-scene validation semantics.
- Made quaternion failure handling match existing `std::optional` math style.
- Made demo resource/setup/update lifecycle mandatory.
- Added docs and coverage tooling updates.
- Added browser and focused C++ browser visual artifact requirements.

## Interfaces and Dependencies

Final scene interfaces should include:

    namespace ofg {

    using EntityId = std::uint32_t;

    enum class ComponentType {
        MeshRenderer,
    };

    struct LocalTransform {
        math::Vec3 m_position{0.0f, 0.0f, 0.0f};
        math::Quat m_rotation{math::quat_identity()};
        math::Vec3 m_scale{1.0f, 1.0f, 1.0f};
    };

    class Component;
    class Entity;
    class MeshRenderer;
    class Scene;

    }

`Component` should include:

    ComponentType m_type;
    Entity* m_entity;

`MeshRenderer` should include:

    Mesh* m_mesh{nullptr};
    PropertyBag m_properties;
    std::vector<MaterialOverride> m_material_overrides;
    math::Vec3 m_sort_origin_offset{0.0f, 0.0f, 0.0f};

`Scene` should own pointer-stable, non-dense storage such as:

    RenderView m_main_view;
    std::vector<std::unique_ptr<Entity>> m_entities;
    std::vector<std::unique_ptr<MeshRenderer>> m_mesh_renderers;
    Entity* m_root;
    EntityId m_next_entity_id;

The exact private member layout can differ if implementation demands it, but ownership, pointer stability, root traversal, entity lookup, same-scene validation, and indexed mesh-renderer iteration must remain.

The renderer interface can remain:

    static void Renderer::render(WGPUCommandEncoder encoder, RenderTarget target, const Scene& scene);

`OpaquePass`, `Mesh`, `Material`, `Shader`, and `Texture` should remain conceptually unchanged by this plan. `DrawCommand` may change from owning copied draw data to borrowing immutable scene component data during `Renderer::render` to avoid per-frame heap churn.
