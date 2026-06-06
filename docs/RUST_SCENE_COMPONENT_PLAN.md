# Rust Scene Component Model

This ExecPlan is a living document. The sections Progress, Surprises &
Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as
work proceeds.

Once this plan is started, proceed independently for as long as possible. Return
to the user only for critical input that cannot be safely inferred, or when the
plan is complete.

If `PLANS.md` is present in the repo, maintain this document in accordance with
it and link back to it by path.

## Purpose / Big Picture

Add a simple Rust-owned scene/component model so OFG has a practical place for
future game objects while the browser runtime remains Rust-owned. After this
plan is complete, the engine has one scene tree made of entities. Each entity
has a transform and may have typed components. The scene has convenient global
handles for the root, terrain object, player entity, and active camera entity.

This is not a full ECS framework. The immediate user-visible behavior should be
unchanged except that the existing debug player marker is rendered through the
new mesh renderer component path. The new architecture enables later work to
load an animated GLTF player, attach objects to player hand/socket entities, and
place world decorations as ordinary entities with mesh renderer components. This
plan deliberately does not implement GLTF loading, skeletal animation, sockets,
or decoration placement.

## Progress

- [x] (2026-06-06) Discussed the desired model: one Rust scene, an array of
  `Entity` slots, stable `EntityId` handles, typed components on entities, and
  scene globals for terrain, player, and active camera.
- [x] (2026-06-06) Read `PLANS.md`, `docs/API_CONTRACTS.md`,
  `docs/ARCHITECTURE.md`, and the archived Rust conversion plan context.
- [x] (2026-06-06) Updated active docs and API contracts so future agents
  understand that a
  tiny Rust scene/component model is allowed while TypeScript scene ownership
  remains forbidden.
- [x] (2026-06-06) Ran milestone 1 validation with
  `git -c safe.directory=C:/dev/ofg diff --check`; it passed with line-ending
  warnings only. Ran local milestone-review passes for contract, code quality,
  legacy, correctness, and validation; no required findings. Sub-agent review
  was not used because this session's sub-agent tool policy requires an
  explicit user request for delegation.
- [x] (2026-06-06) Renamed the current Rust `World` graph to `Scene`, renamed
  `WorldError` to `SceneError`, updated `Engine` to expose `scene()` and
  `scene_mut()`, moved `crates/engine_core/src/world.rs` to
  `crates/engine_core/src/scene.rs`, and preserved existing tree, transform,
  facade, and player behavior.
- [x] (2026-06-06) Validated the rename with `cargo test -p engine_core`; all
  21 tests passed. Ran local milestone-review passes for contract, code quality,
  legacy, correctness, and validation; no required findings. `rg` found no
  remaining Rust `World` owner references, only unrelated TypeScript "World
  descriptor" strings.
- [x] (2026-06-06) Added typed entity components and short-lived entity access
  wrappers. `Scene` now owns a root entity, root/player/terrain/active-camera
  global handles, `Entity` records with `Components`, `EntityRef`, `EntityMut`,
  and typed camera, player, mesh renderer, and terrain component structs.
- [x] (2026-06-06) Validated the component/accessor slice with
  `cargo test -p engine_core`; all 24 tests passed. Ran local milestone-review
  passes for contract, code quality, legacy, correctness, and validation. A
  required code-quality finding found `scene.rs` over 600 lines, so component
  and access-wrapper definitions were split into `scene_components.rs` and
  `scene_access.rs`; `scene.rs` is now 483 lines. Re-ran
  `cargo test -p engine_core` and `git -c safe.directory=C:/dev/ofg diff
  --check`; both passed, with Git line-ending warnings only.
- [x] (2026-06-06) Moved player and camera state onto scene entities.
  `Engine::create_player` now attaches `PlayerComponent`, `CameraComponent`,
  and a hidden `MeshRendererComponent` to the player/camera rig. Player
  movement, debug-fly movement, mode toggling, camera sync, and render snapshot
  generation now read/write `PlayerComponent` instead of a separate
  `PlayerControllerState`.
- [x] (2026-06-06) Moved terrain identity onto one root-level scene entity.
  `BrowserGameState::reset_game` creates a terrain entity with
  `TerrainComponent { seed, preset }`, sets `scene.terrain`, and keeps terrain
  chunks out of the scene tree.
- [x] (2026-06-06) Added logical mesh/material resources and render extraction.
  `SceneResources` owns logical `MeshResource` and `MaterialResource` labels
  addressed by typed generational `MeshId`/`MaterialId` handles. `Engine`
  extracts visible `RenderMeshItemPacket` values with entity id, logical
  resource ids, and world matrices.
- [x] (2026-06-06) Rendered the debug player marker through scene mesh
  extraction. The marker is now a player `MeshRendererComponent`; first-person
  mode hides it and debug-fly mode emits it as a visible scene mesh item.
  `engine_web` resolves the marker's logical mesh/material labels to its
  renderer-owned GPU resources and draw arrays. The old marker-specific render
  snapshot fields and `build_player_marker_world_matrix` helper were removed.
- [x] (2026-06-06) Validated milestones 4 and 5 with
  `cargo test -p engine_core` and `cargo test -p engine_web`; all 28
  `engine_core` tests and 21 `engine_web` tests passed. Ran local
  milestone-review passes for contract, code quality, legacy, correctness, and
  validation. Required findings were fixed: the oversized
  `engine_core/src/tests.rs` file was split with scene tests moved under
  `crates/engine_core/src/tests/scene_tests.rs`, and replacing a debug-fly
  player now hides the previous player marker before creating the new rig.
  Re-ran `cargo test -p engine_core`, `cargo test -p engine_web`, and
  `git -c safe.directory=C:/dev/ofg diff --check`; tests passed and diff-check
  reported only Git line-ending warnings.
- [x] (2026-06-06) Completed final validation. `npm test` passed with 55
  TypeScript/mocha tests after a full build. `npm run check:wasm` passed for
  terrain and engine-web wasm artifacts. `npm run smoke:browser` passed and
  produced artifacts under
  `artifacts/browser-smoke/2026-06-06T18-16-31-163Z`. The smoke report showed
  `FIRST -> FLY`, `hasWebGpu: true`, Rust runtime sentinels, nonblank pixel
  stats, and the debug-fly screenshot visibly contained the yellow player
  marker.
- [x] (2026-06-06) Added explicit public API coverage for the new scene and
  browser-state surface. The tests now directly cover `EntityId` raw
  conversion, `Scene::entity_ids`, invalid global handles, missing global
  accessors, `root_mut`, `terrain(_mut)`, `player(_mut)`,
  `active_camera(_mut)`, all `EntityRef` and `EntityMut` component accessors,
  all component removers, `SceneResources::new`, mesh/material lookup miss
  paths, `PlayerComponent::new`, direct `RenderSnapshot::from_player_view`,
  engine missing-player results, direct `Engine::debug_snapshot`, facade
  preview X/Y and toggle calls, and `BrowserGameState` convenience methods.
  Re-ran `cargo test -p engine_core` and `cargo test -p engine_web`; all 36
  `engine_core` tests and 22 `engine_web` tests passed. Re-ran
  `npm test`; all 55 TypeScript/mocha tests passed after a full build. Re-ran
  `git -c safe.directory=C:/dev/ofg diff --check`; it passed with line-ending
  warnings only.

## Surprises & Discoveries

- Observation: The active Rust conversion plan is archived, not active.
  Evidence: `docs/API_CONTRACTS.md` says the completed Rust conversion plan is
  archived at `docs/archived/RUST_CONVERSION_PLAN.md`; current boundary
  decisions live in `docs/API_CONTRACTS.md` and `docs/ARCHITECTURE.md`.
- Observation: The current Rust `World` already is the scene graph foundation.
  Evidence: `crates/engine_core/src/world.rs` owns generational `EntityId`
  values, parent/child links, local transforms, world transforms, recursive
  destroy, and world-transform propagation.
- Observation: The repo-local milestone-review skill exists, but sub-agent
  spawning has an additional session policy gate.
  Evidence: `.agents/skills/milestone-review/SKILL.md` asks for sub-agent
  reviewers when available, while the discovered sub-agent tool states it may be
  used only when the user explicitly asks for sub-agents, delegation, or
  parallel agent work.
- Observation: `ResourceId<T>` cannot use a derived `Copy` implementation when
  the resource payload contains `String`.
  Evidence: `cargo test -p engine_core` reported that the derived `Copy` impl
  for `ResourceId<MeshResource>` required `MeshResource: Copy`; the fix was a
  manual `Copy`/`Clone`/`Eq`/`Hash` implementation for the handle fields only.
- Observation: Replacing the player while debug-fly mode was active could leave
  the old player entity's marker renderer visible.
  Evidence: Local correctness review of `Engine::create_player` found the old
  visible mesh renderer was not cleared when `scene.player` moved to a new
  entity. The fix hides the previous player's mesh renderer and adds
  `replacing_player_hides_previous_player_marker`.

## Decision Log

- Decision: Implement the model in Rust, not TypeScript.
  Rationale: `OFG-API-009` forbids TypeScript scene graph or ECS ownership.
  `engine_core` is the correct browser-free home for long-lived scene state.
  Date/Author: 2026-06-06 / Codex.
- Decision: Treat node and entity as the same concept.
  Rationale: The requested model is a tree of entities. A separate `Node` type
  would add vocabulary without new behavior.
  Date/Author: 2026-06-06 / Codex.
- Decision: Store `Entity` records in a Vec-like arena and refer to them with
  stable generational `EntityId` handles.
  Rationale: Long-lived Rust references into a mutable tree create borrowing and
  self-reference problems. Handles are the standard Rust arena/slot-map pattern
  and match the existing code.
  Date/Author: 2026-06-06 / Codex.
- Decision: Rename the current `World` graph to `Scene` instead of adding a
  separate `Scene { world: World }` wrapper.
  Rationale: In the current code, `World` and scene would do the same job.
  Keeping both would make ownership and naming less clear.
  Date/Author: 2026-06-06 / Codex.
- Decision: Use fixed typed component fields at first, not trait objects,
  archetypes, reflection, or a scheduler.
  Rationale: The first use cases need camera, player, terrain, and mesh
  renderer components. A small typed store is simpler, easier to test, and
  sufficient for now.
  Date/Author: 2026-06-06 / Codex.
- Decision: Run milestone reviews locally unless the user explicitly asks for
  sub-agent delegation.
  Rationale: This satisfies the repo-local milestone-review checklist while
  respecting the session's stricter sub-agent tool policy.
  Date/Author: 2026-06-06 / Codex.
- Decision: Keep camera/light data in `RenderSnapshot` and move scene objects to
  separate render mesh item extraction.
  Rationale: The debug player marker should behave like future decorations and
  attachments: an entity with a transform and `MeshRendererComponent`. Removing
  marker-specific snapshot fields avoids a parallel marker rendering path.
  Date/Author: 2026-06-06 / Codex.
- Decision: Resolve built-in marker mesh/material resources in `engine_web` by
  logical labels, not WebGPU handles stored in `engine_core`.
  Rationale: `engine_core` must stay browser-free and WebGPU-handle-free while
  still giving the renderer enough information to bind the correct GPU assets.
  Labels are sufficient for this initial built-in resource and can be replaced
  by a richer asset registry when GLTF/decorations land.
  Date/Author: 2026-06-06 / Codex.

## Outcomes & Retrospective

The implementation is complete. The API stayed small: a `Scene` owns entities,
typed components, globals for terrain/player/active camera, logical resources,
and short-lived entity access wrappers. Player/camera behavior remains covered
by existing and new Rust tests, including explicit coverage for the public
scene/accessor/resource/render/browser-state methods added by this plan.
Terrain is represented by one scene entity, not chunk entities. The debug player
marker now travels through the same mesh renderer extraction path intended for
future GLTF player meshes, hand attachments, and decorations. The full
validation set passed:

- `cargo test -p engine_core`
- `cargo test -p engine_web`
- `npm test`
- `npm run check:wasm`
- `npm run smoke:browser`

The remaining architecture gaps are intentional future work: GLTF loading,
skeletal animation, named sockets/hand attachments, and decoration placement.

## Contract and Quality Baseline

This plan preserves `OFG-API-001`, the browser shell to Rust browser game API.
TypeScript should continue calling `RustBrowserGame.create(canvas,
assetLoader)`, `resize(viewport)`, `tick(frame)`, `command(command)`, and
`debugSnapshot()`. Do not add per-entity browser calls, raw wasm export calls,
or TypeScript-side scene mirrors for this plan.

This plan preserves `OFG-API-003`, the debug and smoke-test hook contract. If
scene information is needed for smoke tests, expose it through the Rust-assembled
`debugSnapshot()` and copy it through the existing TypeScript debug hook. Do not
derive scene/player/renderer state in TypeScript.

This plan preserves `OFG-API-004`, the terrain vertex and material layout.
Terrain mesh vertices, terrain material layer indices, and terrain shader
contracts are not part of this work.

This plan preserves `OFG-API-009`, forbidden TypeScript ownership. The forbidden
rule continues to mean no TypeScript scene graph, ECS, factory simulation, terrain
manager, render extraction owner, or WebGPU owner. The intentional change is
that `engine_core` gains a small Rust scene/component model.

Quality constraints:

- Keep new code browser-free in `crates/engine_core` where possible.
- Keep WebGPU handles out of `engine_core` scene resources.
- Keep terrain chunks out of the scene tree. Terrain remains one root-level
  terrain entity plus Rust terrain streaming/rendering systems.
- Keep `EntityId` as the long-lived handle. Use short-lived `EntityRef` and
  `EntityMut` access wrappers for readable mutation.
- Keep files under the repository style limits where practical, with top-of-file
  comments explaining what each new file does.
- Run the repo-local `milestone-review` skill after each milestone before
  marking it complete, as required by `PLANS.md`.

## Context and Orientation

Current relevant files:

- `crates/engine_core/src/scene.rs` contains the scene graph. It defines
  `EntityId`, `Entity`, `LocalTransform`, `WorldTransform`, `SceneError`, and
  `Scene`. `Scene` stores entity records, parent/child links, local and world
  transforms, lifecycle state, root/player/terrain/active-camera globals, and
  logical scene resources.
- `crates/engine_core/src/scene_components.rs` defines fixed typed components:
  camera, player, mesh renderer, terrain, and the aggregate `Components`.
- `crates/engine_core/src/scene_access.rs` defines short-lived `EntityRef` and
  `EntityMut` wrappers used to read and mutate entity transforms/components.
- `crates/engine_core/src/scene_resources.rs` defines logical mesh/material
  resources and typed generational `MeshId`/`MaterialId` handles. These are not
  WebGPU handles.
- `crates/engine_core/src/engine.rs` owns `Engine`, which has a `scene: Scene`
  field. It creates the player/camera rig as scene entities, stores player state
  in `PlayerComponent`, synchronizes the active camera entity, and extracts
  visible mesh renderer render items.
- `crates/engine_core/src/player.rs` defines `PlayerMode`,
  `PlayerMovementIntent`, `PlayerConfig`, `PlayerRig`, `EyeTransform`, and
  player movement helper functions.
- `crates/engine_core/src/render_packet.rs` builds the camera/light render
  snapshot and defines `RenderMeshItemPacket` for visible scene mesh renderers.
- `crates/engine_web/src/game_state.rs` composes `engine_core` and
  `terrain_core` for browser game state, including terrain-height grounding.
- `crates/engine_web/src/wgpu_renderer.rs` owns WebGPU resources and currently
  owns the debug player marker mesh/material as renderer-side state.
- `docs/API_CONTRACTS.md` is the active API boundary source of truth.
- `docs/ARCHITECTURE.md` is the active architecture overview.

Definitions for this plan:

- An `EntityId` is a stable generational handle containing an index and a
  generation. Code stores `EntityId` values across frames.
- An `Entity` is the actual stored scene record in `Scene.entities`. It contains
  parent/child links, transforms, alive/generation metadata, and typed component
  fields.
- A component is optional typed data attached to an entity. This plan starts
  with camera, player, mesh renderer, and terrain components.
- A resource is shared scene-owned asset metadata such as a logical mesh or
  material. Renderer GPU handles are not scene resources.
- `EntityRef` and `EntityMut` are short-lived access wrappers returned from
  `Scene::entity(...)` and `Scene::entity_mut(...)`. They should not be stored
  long term.

## Plan of Work

First, update active documentation. Edit `docs/API_CONTRACTS.md` to clarify that
`OFG-API-009` forbids TypeScript scene ownership but allows this Rust-owned
scene/component model. Edit `docs/ARCHITECTURE.md` to say `engine_core` owns the
browser-free scene tree and typed components. Keep the archived Rust conversion
plan archived.

Second, rename the existing graph from `World` to `Scene`. Move or rename
`crates/engine_core/src/world.rs` to `crates/engine_core/src/scene.rs`. Rename
`WorldError` to `SceneError`. Rename `WorldTransform` only if the code reads
better; keeping `WorldTransform` is acceptable because it describes the resolved
world-space transform, not the owner type. Update `crates/engine_core/src/lib.rs`,
`engine.rs`, `tests.rs`, and facade tests to use `Scene`.

Third, change the stored slot type into the public conceptual `Entity` record.
The scene should have:

    pub struct Scene {
        entities: Vec<Entity>,
        free_indices: Vec<u32>,
        alive_count: usize,
        root: EntityId,
        terrain: Option<EntityId>,
        player: Option<EntityId>,
        active_camera: Option<EntityId>,
        resources: SceneResources,
    }

    pub struct Entity {
        generation: u32,
        alive: bool,
        parent: Option<EntityId>,
        children: Vec<EntityId>,
        local_transform: LocalTransform,
        world_transform: WorldTransform,
        components: Components,
    }

Root creation belongs to `Scene::new()`. `Scene::create_entity()` should parent
new entities under `root` by default. `Scene::create_child(parent)` should create
an entity under a specific parent. Destroying an entity must recursively destroy
descendants, clear components, and clear scene globals if the destroyed subtree
contained terrain, player, or active camera.

Fourth, add the component model. Add fixed typed components on each entity:

    #[derive(Default)]
    pub struct Components {
        pub camera: Option<CameraComponent>,
        pub player: Option<PlayerComponent>,
        pub mesh_renderer: Option<MeshRendererComponent>,
        pub terrain: Option<TerrainComponent>,
    }

    pub struct CameraComponent {
        pub fov_y_radians: f32,
        pub near_plane: f32,
        pub far_plane: f32,
    }

    pub struct PlayerComponent {
        pub mode: PlayerMode,
        pub yaw: f32,
        pub pitch: f32,
        pub debug_position: Vec3,
        pub debug_yaw: f32,
        pub debug_pitch: f32,
        pub intent: PlayerMovementIntent,
        pub config: PlayerConfig,
        pub camera_entity: EntityId,
    }

    pub struct MeshRendererComponent {
        pub mesh: MeshId,
        pub material: MaterialId,
        pub visible: bool,
    }

    pub struct TerrainComponent {
        pub seed: u32,
        pub preset: u32,
    }

Fifth, add entity access wrappers. `Scene::entity(id)` returns `EntityRef<'_>`.
`Scene::entity_mut(id)` returns `EntityMut<'_>`. Add convenience methods
`Scene::player_mut()`, `Scene::terrain_mut()`, and `Scene::active_camera_mut()`.
`EntityMut` should expose component mutators such as `player_mut()`,
`camera_mut()`, and `mesh_renderer_mut()`, plus `transform_mut()`.

Sixth, add logical resources. Add `SceneResources` and typed generational
resource IDs:

    pub struct ResourceId<T> {
        index: u32,
        generation: u32,
        marker: PhantomData<fn() -> T>,
    }

    pub type MeshId = ResourceId<MeshResource>;
    pub type MaterialId = ResourceId<MaterialResource>;

    pub struct MeshResource {
        pub label: String,
    }

    pub struct MaterialResource {
        pub label: String,
    }

`engine_core` resources are logical identifiers only. `engine_web` owns the GPU
mesh buffers, texture handles, material packets, and WebGPU draw submission.

Seventh, migrate player and camera state into scene components. Replace
`Engine { world: World, player_controller: Option<PlayerControllerState> }` with
`Engine { scene: Scene, ... }`. `Engine::create_player(position)` creates a
player entity, a camera entity, attaches `PlayerComponent` and
`CameraComponent`, sets `scene.player`, sets `scene.active_camera`, and preserves
the existing player movement, debug-fly, and render snapshot behavior. Keep
high-level multi-entity logic as `Engine` or `Scene` methods; do not force code
to borrow player and camera mutably at the same time.

Eighth, represent terrain as one scene entity. `BrowserGameState::reset_game`
creates a root-level terrain entity with `TerrainComponent { seed, preset }` and
sets `scene.terrain`. Do not make terrain chunks into scene entities.

Ninth, make mesh renderer extraction functional by moving the existing debug
player marker to the scene mesh renderer path. Register a logical marker mesh
and material in scene resources, attach a `MeshRendererComponent` to the player
entity, and toggle its visibility based on player mode. Add render extraction in
`engine_core` that emits visible mesh renderer items with entity id, mesh id,
material id, and world transform. `engine_web` resolves those logical ids to its
GPU mesh/material resources and draws them with the existing object uniform path.
The visible behavior should match the current yellow marker in debug-fly mode.

Tenth, update tests and contracts. Keep behavior-focused test names. Add tests
near the changed Rust behavior, update generated/WASM boundary checks only when
needed, and update docs/API contracts if the render snapshot or debug snapshot
shape changes.

## Concrete Steps

Work from `C:\dev\ofg`.

Before editing:

    git -c safe.directory=C:/dev/ofg status --short
    rg -n "World|WorldError|EntitySlot|PlayerControllerState|player_marker|RenderSnapshot" crates/engine_core crates/engine_web docs src

Milestone 1, docs and contracts:

    Edit docs/API_CONTRACTS.md
    Edit docs/ARCHITECTURE.md
    git -c safe.directory=C:/dev/ofg diff --check

Milestone 2, scene rename:

    Rename crates/engine_core/src/world.rs to crates/engine_core/src/scene.rs
    Update mod/export/import names in crates/engine_core/src/lib.rs
    Update uses in crates/engine_core/src/engine.rs
    Update tests in crates/engine_core/src/tests.rs
    cargo test -p engine_core

Milestone 3, components and access wrappers:

    Add Components, CameraComponent, PlayerComponent, MeshRendererComponent, TerrainComponent
    Add EntityRef and EntityMut
    Add root/player/terrain/active_camera scene accessors
    cargo test -p engine_core

Milestone 4, player/camera/terrain migration:

    Move PlayerControllerState data into PlayerComponent
    Add CameraComponent to the camera entity
    Add TerrainComponent during BrowserGameState::reset_game
    cargo test -p engine_core
    cargo test -p engine_web

Milestone 5, logical resources and mesh renderer extraction:

    Add SceneResources and typed MeshId/MaterialId handles
    Add engine_core render extraction for visible mesh renderers
    Move debug player marker onto MeshRendererComponent path
    cargo test -p engine_core
    cargo test -p engine_web

Final validation:

    npm test
    npm run check:wasm
    npm run smoke:browser

After browser smoke, inspect the newest `artifacts/browser-smoke/<run-id>/`
screenshots and `report.json`. A black, blank, or solid frame after refresh is a
failure. Verify the camera toggle still reaches `FIRST -> FLY` and that the
yellow player marker still appears in debug-fly mode.

## Milestone Review

After each milestone:

1. Update this ExecPlan's Progress, Surprises & Discoveries, Decision Log, and
   Outcomes & Retrospective as needed.
2. Update any changed API contracts or active docs.
3. Run the repo-local `milestone-review` skill against the milestone diff and
   this ExecPlan.
4. Apply required findings before marking the milestone complete, or record a
   rejected finding with rationale in the Decision Log.
5. Re-run relevant validation commands after fixes.
6. Record the review summary, commands, artifacts, and remaining risks in this
   plan.

## Validation and Acceptance

This plan is accepted when all of the following are true:

- `engine_core` exports `Scene`, `Entity`, `EntityId`, `EntityRef`,
  `EntityMut`, `Components`, `CameraComponent`, `PlayerComponent`,
  `MeshRendererComponent`, `TerrainComponent`, `MeshId`, and `MaterialId`.
- The old `World` owner type is gone or fully renamed to `Scene`. Do not keep a
  parallel `Scene { world: World }` owner.
- `Scene` stores `entities: Vec<Entity>` and uses `EntityId` as the long-lived
  stable handle.
- New entities are part of a root-owned tree. Parent cycles are rejected.
  Destroying an entity destroys descendants and clears relevant globals.
- Every entity has a local transform and world transform.
- The player is an entity with `PlayerComponent`.
- The active camera is an entity with `CameraComponent`.
- Terrain is one root-level entity with `TerrainComponent` and is accessible
  through the scene's terrain global. Terrain chunks are not scene entities.
- Mesh renderers point at logical mesh/material resources, not WebGPU handles.
- Existing player movement, first-person grounding, debug-fly movement, camera
  mode toggling, and HUD/smoke debug behavior remain stable.
- The yellow debug player marker is rendered through the mesh renderer
  component path and remains visible in debug-fly mode.
- No TypeScript scene graph, ECS, render extractor, terrain manager, terrain
  chunk entity model, or WebGPU ownership is introduced.

Required command results:

    cargo test -p engine_core
    cargo test -p engine_web
    npm test
    npm run check:wasm
    npm run smoke:browser

All commands must pass. Browser smoke must include screenshot/report inspection.

## Idempotence and Recovery

The safest implementation path is milestone-by-milestone and test-first around
the renamed owner type. If a rename becomes noisy, keep the behavioral changes
out of that milestone and run `cargo test -p engine_core` before continuing.

Do not delete tests just because a type name changed. Rename tests and preserve
their behavioral assertions. Do not keep stale aliases such as `pub type World =
Scene` unless a later decision explicitly accepts temporary compatibility; this
project does not need backwards compatibility for retired internal names.

If render extraction breaks browser smoke, revert only the marker extraction
slice you changed, keep the scene/core milestones intact if their tests pass,
and record the rollback in Outcomes & Retrospective. Never use `git reset
--hard` or destructive checkout commands unless the user explicitly requests
them.

Use `rg` before removing any symbol:

    rg -n "World|WorldError|PlayerControllerState|player_marker|MeshRendererComponent" crates src docs

## Artifacts and Notes

The agreed API shape is:

    let mut player = scene.player_mut()?;
    player.player_mut()?.set_mode(PlayerMode::DebugFly);
    player.transform_mut().translation = Vec3::new(0.0, 12.0, 0.0);

    let mut entity = scene.entity_mut(object_id)?;
    let renderer = entity
        .mesh_renderer_mut()
        .ok_or(SceneError::MissingMeshRenderer(object_id))?;
    renderer.material = new_material;
    renderer.visible = true;

Long-lived fields store `EntityId`. Short-lived mutation uses `EntityMut`.
Operations involving multiple entities should remain `Scene` or `Engine` methods
to avoid awkward simultaneous mutable borrows.

The immediate future use cases shape the design but are not implemented here:

- Animated GLTF player: import creates a child subtree under the player entity
  with mesh renderer, animation, skin, and named bone/socket entities later.
- Hand attachments: attach child entities under future hand/socket entities,
  then render them through ordinary mesh renderer components.
- Decorations: create root-level or terrain-anchored entities with transforms and
  mesh renderer components, sharing mesh/material resources.

## Interfaces and Dependencies

Final `engine_core` scene API:

    impl Scene {
        pub fn new() -> Self;

        pub fn root_id(&self) -> EntityId;
        pub fn terrain_id(&self) -> Option<EntityId>;
        pub fn player_id(&self) -> Option<EntityId>;
        pub fn active_camera_id(&self) -> Option<EntityId>;

        pub fn set_terrain(&mut self, entity: Option<EntityId>) -> Result<(), SceneError>;
        pub fn set_player(&mut self, entity: Option<EntityId>) -> Result<(), SceneError>;
        pub fn set_active_camera(&mut self, entity: Option<EntityId>) -> Result<(), SceneError>;

        pub fn create_entity(&mut self) -> EntityId;
        pub fn create_child(&mut self, parent: EntityId) -> Result<EntityId, SceneError>;
        pub fn destroy_entity(&mut self, entity: EntityId) -> Result<(), SceneError>;

        pub fn is_alive(&self, entity: EntityId) -> bool;
        pub fn entity_ids(&self) -> Vec<EntityId>;
        pub fn entity(&self, entity: EntityId) -> Result<EntityRef<'_>, SceneError>;
        pub fn entity_mut(&mut self, entity: EntityId) -> Result<EntityMut<'_>, SceneError>;

        pub fn root(&self) -> EntityRef<'_>;
        pub fn root_mut(&mut self) -> EntityMut<'_>;
        pub fn terrain(&self) -> Result<EntityRef<'_>, SceneError>;
        pub fn terrain_mut(&mut self) -> Result<EntityMut<'_>, SceneError>;
        pub fn player(&self) -> Result<EntityRef<'_>, SceneError>;
        pub fn player_mut(&mut self) -> Result<EntityMut<'_>, SceneError>;
        pub fn active_camera(&self) -> Result<EntityRef<'_>, SceneError>;
        pub fn active_camera_mut(&mut self) -> Result<EntityMut<'_>, SceneError>;

        pub fn set_parent(
            &mut self,
            child: EntityId,
            parent: Option<EntityId>,
        ) -> Result<(), SceneError>;

        pub fn update_world_transforms(&mut self);
        pub fn resources(&self) -> &SceneResources;
        pub fn resources_mut(&mut self) -> &mut SceneResources;
    }

Final entity access API:

    impl EntityRef<'_> {
        pub fn id(&self) -> EntityId;
        pub fn parent(&self) -> Option<EntityId>;
        pub fn children(&self) -> &[EntityId];
        pub fn local_transform(&self) -> LocalTransform;
        pub fn world_transform(&self) -> WorldTransform;
        pub fn camera(&self) -> Option<&CameraComponent>;
        pub fn player(&self) -> Option<&PlayerComponent>;
        pub fn mesh_renderer(&self) -> Option<&MeshRendererComponent>;
        pub fn terrain(&self) -> Option<&TerrainComponent>;
    }

    impl EntityMut<'_> {
        pub fn id(&self) -> EntityId;
        pub fn local_transform(&self) -> LocalTransform;
        pub fn set_local_transform(&mut self, transform: LocalTransform);
        pub fn transform_mut(&mut self) -> &mut LocalTransform;
        pub fn add_camera(&mut self, component: CameraComponent) -> &mut CameraComponent;
        pub fn camera_mut(&mut self) -> Option<&mut CameraComponent>;
        pub fn add_player(&mut self, component: PlayerComponent) -> &mut PlayerComponent;
        pub fn player_mut(&mut self) -> Option<&mut PlayerComponent>;
        pub fn add_mesh_renderer(
            &mut self,
            component: MeshRendererComponent,
        ) -> &mut MeshRendererComponent;
        pub fn mesh_renderer_mut(&mut self) -> Option<&mut MeshRendererComponent>;
        pub fn add_terrain(&mut self, component: TerrainComponent) -> &mut TerrainComponent;
        pub fn terrain_mut(&mut self) -> Option<&mut TerrainComponent>;
        pub fn remove_camera(&mut self) -> Option<CameraComponent>;
        pub fn remove_player(&mut self) -> Option<PlayerComponent>;
        pub fn remove_mesh_renderer(&mut self) -> Option<MeshRendererComponent>;
        pub fn remove_terrain(&mut self) -> Option<TerrainComponent>;
    }

The plan should not add external Rust ECS dependencies. The in-repo
generational arena pattern is sufficient for this milestone.

## Revision Note

2026-06-06: Initial ExecPlan created from the user-approved scene/component API
discussion. It records the Rust-owned exception to the no-ECS rule and anchors
the implementation on `EntityId`, `Entity`, `Scene`, typed components, and
logical mesh renderer resources.
