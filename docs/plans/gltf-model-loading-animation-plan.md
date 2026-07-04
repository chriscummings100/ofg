# Load glTF Models, PBR Materials, Skeletons, Skinning, And Player Animations

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

Maintain this document in accordance with `C:\dev\ofg\PLANS.md`.

## Purpose / Big Picture

After this work, OFG can load glTF 2.0 and GLB files into reusable model resources, instantiate those model resources into the C++ scene graph, create renderer resources for their meshes, textures, and PBR metallic-roughness materials, represent model nodes and skeleton joints as scene entities, play animation clips, CPU-skin skinned meshes through mesh-renderer-owned skin bindings, blend locomotion animations, and use a Quaternius player model as the visible player instead of the temporary cyan box.

The end user-visible result is a browser scene where the player entity renders as the imported Quaternius character. In third-person mode the model idles while stopped, walks while moving normally, and blends toward a sprint animation while fast movement is held. The model remains driven by the existing C++ `Player` component and scene/camera ownership. TypeScript may fetch model bytes, but C++ owns parsing, reusable model resources, scene instantiation, entity transforms, animation state, mesh-renderer skin bindings, skinning, and renderer submission.

## Progress

- [x] (2026-07-02 05:28Z) Read `C:\dev\ofg\PLANS.md`, `C:\dev\ofg\docs\GUIDES.md`, `C:\dev\ofg\docs\API_CONTRACTS.md`, `C:\dev\ofg\docs\SYSTEMS.md`, and the completed player/camera plan for current ownership and validation constraints.
- [x] (2026-07-02 05:28Z) Inspected the scene, resource, renderer, shader, player, and fixture layout relevant to glTF loading.
- [x] (2026-07-02 05:28Z) Pulled tinygltf into `C:\dev\ofg\cpp\third_party\tinygltf` from upstream commit `a434ee02066c2d9b62a3504876aed38e6e399fe0` and added `SOURCE.md`.
- [x] (2026-07-02 05:28Z) Drafted this ExecPlan with staged milestones from dependency integration through player animation binding.
- [x] (2026-07-02 06:03Z) Reviewed the Khronos glTF 2.0 node, skin, and animation documentation and clarified that OFG should use the imported scene entity tree as the skeleton transform hierarchy.
- [x] (2026-07-02 06:04Z) Clarified that `SkinBinding` is owned by `MeshRenderer`, not by standalone joint/bone components, and that loaded glTF assets should become reusable model-resource templates that can be instantiated many times into a scene.
- [x] (2026-07-02 06:05Z) Made multi-instance static import an early milestone requirement and clarified that OFG should start with explicit model instantiation/copy context rather than a full scene serialization system.
- [x] (2026-07-02 06:06Z) Defined the model template as an OFG-owned prefab graph that is normalized from glTF, references shared loaded resources, and is not a raw glTF document or live `Scene`.
- [x] (2026-07-02 06:08Z) Added Milestone 0 for `Object`/`Ptr<T>` so referenceable scene/resource objects can invalidate non-owning references before glTF model instancing increases pointer complexity.
- [x] (2026-07-02 06:16Z) Applied plan-review decisions: `AnimationPlayer` is a component, scene update order is explicit, importer resource loading needs deduplication, lighting starts with one main directional light plus ambient, CPU skinning must be allocation-conscious, and `Ptr<T>` is avoided in hot loops and owner containers.
- [x] (2026-07-02 06:49Z) Milestone 0 completed: added `Object`/`Ptr<T>`, made core scene/resource types referenceable and non-movable, converted the first high-risk stored observers, updated `OFG-BOOT-006`, added pointer invalidation tests, ran local milestone review, and validated with `npm run format:cpp`, `npm run format:cpp:check`, `npm run test:cpp`, and `git -c safe.directory=C:/dev/ofg diff --check`.
- [x] (2026-07-02 07:02Z) Milestone 1 completed: integrated tinygltf through a private `ofg_tinygltf` CMake target, added the OFG-owned `GltfDocument` parse/accessor layer and tests for GLB, embedded glTF, external resources, decoded images, and clear accessor/resource errors, ran local milestone review, fixed callback cache copy churn and WASM tinygltf exception flags, and validated with `npm run format:cpp`, `npm run format:cpp:check`, `npm run test:cpp`, `git -c safe.directory=C:/dev/ofg diff --check`, and `npm run build:wasm`.
- [x] (2026-07-02 07:11Z) Milestone 1A completed: expanded `GltfDocument` with OFG-owned node, mesh, primitive, material, texture, skin, animation, and extension metadata, added native player asset audit tests, documented the selected player mesh and animation library in `docs\player-model-asset-audit.md`, ran local milestone review, and validated with `npm run format:cpp`, `npm run format:cpp:check`, `npm run test:cpp`, `git -c safe.directory=C:/dev/ofg diff --check`, and `npm run build:wasm`.
- [x] (2026-07-02 07:28Z) Milestone 2 completed: added `ModelResource`, `ModelResourceImportContext`, and glTF-to-model import/instantiation APIs; converted `static-box.glb` into shared mesh/material resources; instantiated five independent copies into one `Scene`; added deduplication and stale-resource lifetime tests; ran local milestone review; fixed the destroyed cached mesh diagnostic; and validated with `npm run format:cpp`, `npm run format:cpp:check`, `npm run test:cpp`, `git -c safe.directory=C:/dev/ofg diff --check`, and `npm run build:wasm`.
- [x] (2026-07-02 08:03Z) Milestone 3 completed: replaced the opaque demo shader/material path with metallic-roughness PBR, added scene main directional and ambient lighting, imported glTF PBR textures/factors/tangents, generated tangents when normal maps require them, centralized the opaque PBR shader layout, ran local milestone review, fixed contract drift and normal-matrix handling, and validated with `npm run format:cpp`, `npm run format:cpp:check`, `npm run test:cpp`, `git -c safe.directory=C:/dev/ofg diff --check`, `npm run build:wasm`, `npm run smoke:render`, and `npm run smoke:browser`.
- [x] (2026-07-02 08:13Z) Milestone 4 completed: added `SkinTemplate` to `ModelResource`, added optional `SkinBinding` metadata owned by `MeshRenderer`, imported glTF skins, inverse bind matrices, and optional `skin.skeleton`, resolved skin joints to instantiated scene entities, generated normals for supported triangle fixtures without `NORMAL`, ran local milestone review, fixed contract drift and importer split pressure, and validated with `npm run format:cpp`, `npm run format:cpp:check`, `npm run test:cpp`, `git -c safe.directory=C:/dev/ofg diff --check`, and `npm run build:wasm`.
- [x] (2026-07-02 08:39Z) Milestone 5 completed: added `AnimationClip` and scene-owned `AnimationPlayer` components, imported glTF animation channels for translation/rotation/scale with `LINEAR` and `STEP` interpolation, rejected `CUBICSPLINE` clearly, bound same-file clips to instantiated node entities, proved ordinary node and joint animation updates plus post-animation joint overrides, ran local milestone review, fixed contract/test/split findings, and validated with `npm run format:cpp`, `npm run format:cpp:check`, `npm run test:cpp`, `git -c safe.directory=C:/dev/ofg diff --check`, and `npm run build:wasm`.
- [x] (2026-07-02 09:16Z) Milestone 6 completed: added JOINTS_0/WEIGHTS_0 import, per-instance `MeshRenderer::SkinBinding` dynamic meshes, fixed-capacity `Mesh::update_vertices_in_place`, scene world-transform caching before CPU skinning, multiple-instance and GPU buffer-churn tests, ran local milestone review, fixed the mesh update commit-order finding, and validated with `npm run format:cpp`, `npm run format:cpp:check`, `npm run test:cpp`, `git -c safe.directory=C:/dev/ofg diff --check`, and `npm run build:wasm`.
- [x] (2026-07-02 09:42Z) Milestone 7 completed: extended `AnimationPlayer` to weighted clip states with normalized per-target blending, added scene-owned `PlayerAnimationController`, exposed `Player::current_speed()`/`fast_speed()`, updated scene order to players, animation controllers, animation players, skinning, cameras, added focused blend/controller tests, ran local milestone review, fixed review cleanups, and validated with `npm run format:cpp`, `npm run format:cpp:check`, `npm run test:cpp`, `git -c safe.directory=C:/dev/ofg diff --check`, and `npm run build:wasm`.
- [x] (2026-07-02 10:10Z) Milestone 8 implementation completed and is ready for milestone review: added browser `Uint8Array` byte transport for the selected player and animation GLBs, imported and cached Quaternius model resources in `Game`, remapped UAL1 idle/walk/sprint clips by node name to the player model, attached the skinned model under the unscaled player entity, hid the fallback cube through `MeshRenderer::visible`, raised WASM memory to 256 MiB initial with growth enabled, packaged selected model GLBs, updated debug status with `modelLoadingState`/`playerModelLoaded`, and captured idle/walk/sprint screenshots under `C:\dev\ofg\artifacts\player-model`.
- [x] (2026-07-02 10:48Z) Milestone 8 completed: local milestone review found one required coverage gap for invisible renderer extraction; added `renderer skips invisible scene mesh renderers`, reran validation, verified `.deploy` contains `quaternius-superhero-male.glb` and `quaternius-ual1-standard.glb`, and confirmed browser smoke status `modelLoadingState: "loaded"`, `playerModelLoaded: true`, `lastError: null`.
- [x] (2026-07-02 11:18Z) Milestone 9 completed: refreshed coverage docs and summary JSON, expanded the C++ coverage gate to include `cpp/src/animation` and `opaque_pbr_shader.cpp`, documented the glTF importer coverage exception, added focused material/animation/controller coverage tests, reran final validation, and confirmed browser smoke still reports `modelLoadingState: "loaded"`, `playerModelLoaded: true`, and `lastError: null`.
- [x] (2026-07-02 16:49Z) Follow-up architecture cleanup: removed the separate `PlayerAnimationController` component, moved locomotion clip weighting and default player model resource ownership into `Player`, reduced `Game::load_player_model` to lifecycle/status delegation, queued browser player bytes until `Game::prepare()` has created the player scene, and moved renderer transform helpers into shared math.
- [x] (2026-07-03) Follow-up resource-loading cleanup: superseded the early `ModelResourceImportContext` ownership model. `Resources` now owns durable imported mesh/material/texture/shader/model resources, schedules loading through a generic `Resource::update_loading()` queue, and `ModelResource` owns a temporary `ModelResourceLoader` for glTF root/dependency/import state.

## Surprises & Discoveries

- Observation: `assets\models\tests\animated-cube.gltf` references external buffer URI `AnimatedCube.bin`, but the checked-in sibling file is `animated-cube.bin`.
  Evidence: The JSON `buffers[0].uri` value is `AnimatedCube.bin`; `Get-ChildItem assets\models\tests` shows `animated-cube.bin`. The first implementation milestone must either correct the fixture filename/URI or make the test resource provider explicitly map this known fixture alias with a recorded rationale.

- Observation: `assets\models\tests\animated-cube.gltf` also references a base-color image that is not checked in.
  Evidence: The JSON image URI is `AnimatedCube_BaseColor.png`, and the file is absent from `assets\models\tests`. Milestone 1 keeps the source fixture unchanged and uses a focused test provider that supplies a valid 1x1 transparent PNG so image decode behavior remains covered.

- Observation: The current renderer vertex layout is too small for PBR and normal maps.
  Evidence: `MeshVertex` contains position, normal, and UV only, while `opaque_uber.wgsl` treats the normal attribute as vertex color. PBR requires real normals, tangents, texture coordinates, material factors, and texture bindings.

- Observation: Per-frame CPU skinning cannot use the current `Mesh::replace_vertices` path as-is.
  Evidence: `Mesh::replace_vertices` creates a new WebGPU vertex buffer when GPU state exists. CPU skinning needs same-size vertex updates through queue writes to avoid recreating durable resources every animation frame.

- Observation: The Quaternius character GLBs are much larger than the focused test fixtures.
  Evidence: player model GLBs are roughly 8 MB to 16 MB, while `static-box.glb`, `box-animated.glb`, `simple-skin.gltf`, and `rigged-simple.glb` are tiny. Implementation should prove behavior on small assets before turning on the runtime player model.

- Observation: Browser asset loading needs a byte transport boundary that does not move scene ownership into TypeScript.
  Evidence: C++/WASM currently receives only resize, controls, frames, and debug status through `BrowserGame`; there is no model asset API yet. TypeScript may fetch bytes from `assets/models`, but C++ must own parsing and scene/resource mutation.

- Observation: glTF joint hierarchy is the node hierarchy; a skin marks which nodes are joints.
  Evidence: The Khronos glTF 2.0 spec states that the joint hierarchy is the node hierarchy, that `skin.joints` designates nodes as joints, and that a node object does not itself say whether it is a joint. This means OFG should not create a separate hidden bone transform tree. Preserve imported nodes as entities and store skin metadata only on mesh renderers that need it.

- Observation: glTF animation channels target node TRS properties, and CPU skinning should consume the resulting entity transforms.
  Evidence: The glTF 2.0 animation section describes animation as keyframe animation of node transforms. Animation channels target a node plus a path such as translation, rotation, scale, or morph weights. This fits an OFG update order where animation writes entity local transforms, later procedural systems may adjust those entities, and skinning reads current joint entity world transforms.

- Observation: For skinned meshes, OFG must avoid double-applying the skinned mesh node transform.
  Evidence: The glTF 2.0 skinning section says only joint transforms are applied to the skinned mesh and the skinned mesh node transform is ignored for the skinning result. For OFG CPU skinning, the local skinning matrix should be based on `inverse(world_from_mesh_node) * world_from_joint_entity * inverse_bind_matrix`; the renderer can still draw the skinned mesh with the mesh entity's world transform, and the mesh-node transform cancels correctly.

- Observation: Skinning metadata belongs to the mesh renderer that needs it.
  Evidence: In OFG, `MeshRenderer` is already the scene component that bridges an entity, a mesh resource, material overrides, draw properties, and render extraction. Adding a separate joint/bone component only to hold data needed by a mesh renderer would split one rendering concern across unrelated components and make ownership harder to reason about.

- Observation: Loaded glTF assets need a reusable model-resource representation separate from live `Scene` ownership.
  Evidence: A live `Scene` owns pointer-stable entities and component storage for one scene generation. Loading one tree model and placing five copies in the world requires shared meshes/materials/textures/animation clips, but five distinct entity trees and mesh-renderer instances. A compact model template or prefab can be copied into a `Scene`; a live `Scene` object is the wrong resource type to copy directly.

- Observation: Model instantiation has the same hard parts as serialization, but does not yet require a full scene serialization system.
  Evidence: Copying imported model data into a scene needs stable entity remapping, pointer/reference deduplication, and copy policies that duplicate entities/components while sharing loaded texture/material/mesh resources. A focused instantiation context can solve this for model resources now, while keeping the data layout friendly to a future scene serializer.

- Observation: A model template should be engine-normalized, not a mirror of tinygltf.
  Evidence: The live scene needs OFG `Entity` transforms, `MeshRenderer` component data, resource references, and animation bindings. The raw glTF document contains format details such as accessors, buffer views, node indices, materials, skins, and scenes. Those are useful during import, but keeping them as the runtime prefab would leak parser concepts and make instantiation repeat decode work.

- Observation: glTF import, model instancing, animation, and skinning will create more cross-owner references than the current demo scene.
  Evidence: `MeshRenderer` already stores non-owning mesh/material references, future `SkinBinding` will store entity references, animation binding will target entity references, and `ModelInstance` will return live entity/component references. Raw pointers make entity/resource deletion likely to become a crash instead of a clear engine error.

- Observation: Referenceable objects should be non-copyable and non-movable if they own an intrusive reference list.
  Evidence: If an `Object` can move after `Ptr<T>` has registered against its address, the stored object pointer and linked-list owner become ambiguous. This affects resource classes such as `Mesh`, `Material`, `Texture`, and `Shader`, which currently have move constructors because `Resources` creates temporary objects and moves them into `unique_ptr` storage. Milestone 0 must construct referenceable resources directly in place or otherwise eliminate moves before deriving them from `Object`.

- Observation: Resource loading needs deduplication before model instancing is useful.
  Evidence: The static cube copy test and later tree/character instances should duplicate entities and mesh renderers, but not rebuild identical textures, materials, shaders, or meshes. A first importer cache can deduplicate by canonical asset URI or generated label, then evolve into an asset-id system later.

- Observation: `Ptr<T>` registration must stay out of hot loops.
  Evidence: `Ptr<T>` keeps an intrusive reference list correct by registering, unregistering, and relinking references. That is useful for stored observers, but doing it while building draw lists, sampling animations, or skinning every frame would add avoidable pointer-churn to the renderer and animation hot paths.

- Observation: CPU skinning must be treated as a temporary but performance-conscious implementation.
  Evidence: OFG will eventually need GPU skinning, but this plan only needs the player model initially. Even for one player, CPU skinning should preallocate scratch buffers, reuse dynamic GPU buffer capacity, avoid steady-state heap allocation, and report enough counters to catch accidental per-frame resource churn.

- Observation: Browser model loading likely needs more WASM memory than the current bootstrap build.
  Evidence: The current WASM link options use fixed memory with `ALLOW_MEMORY_GROWTH=0` and a 32 MB initial memory. The player GLBs are roughly 8 MB to 16 MB each before tinygltf decode buffers, image decode buffers, texture uploads, animation data, and model-resource state.

- Observation: The repo-local `milestone-review` skill references `docs\ARCHITECTURE.md`, but this repository does not currently have that file.
  Evidence: `Get-Content docs/ARCHITECTURE.md` failed with "Cannot find path". The Milestone 0 review used `AGENTS.md`, `PLANS.md`, `docs/API_CONTRACTS.md`, this ExecPlan, the local diff, and touched tests instead.

- Observation: The tinygltf parse layer compiles for both native tests and the current Emscripten/WASM target.
  Evidence: After Milestone 1, `npm run test:cpp` and `npm run build:wasm` both passed. This proves the dependency and `GltfDocument` code build under the browser toolchain, but it does not yet prove runtime browser asset loading or the larger player memory budget.

- Observation: The selected Quaternius player mesh and UAL animation libraries share the same skin joint names in the same order.
  Evidence: `player_asset_audit_test.cpp` compares `quaternius-superhero-male.glb`, `quaternius-ual1-standard.glb`, and `quaternius-ual2-standard.glb`; each has one skin with 65 joints and matching ordered joint names. This supports name/order-based animation binding for the first player integration.

- Observation: The Quaternius player mesh uses normal maps but does not provide tangent attributes.
  Evidence: `quaternius-superhero-male.glb` has three normal textures and zero `TANGENT` attributes across its three triangle-list primitives. Milestone 3 must generate tangents from positions, normals, and UVs or disable/reject normal mapping when tangents cannot be generated.

- Observation: The Quaternius skins omit `skin.skeleton`.
  Evidence: The selected player and UAL assets expose `skin.skeleton == -1` while still listing all 65 joints. `SkinBinding` should keep the explicit skeleton root optional and derive a practical root/pivot from the imported node tree when the field is absent.

- Observation: The first browser player load will exceed the current fixed WASM memory budget by a wide margin.
  Evidence: `docs\player-model-asset-audit.md` records that `quaternius-superhero-male.glb` plus `quaternius-ual1-standard.glb` are 23,593,976 source bytes and about 129,580,200 bytes for source plus decoded buffers/images before importer, renderer, upload-staging, or GPU memory. The current browser build still uses 32 MB fixed memory.

- Observation: `static-box.glb` already exercises glTF node `matrix` import.
  Evidence: The Milestone 2 `model_resource_test.cpp` checks that the imported root local transform maps local Y to world negative Z and local Z to world positive Y, proving the importer decomposes the fixture's matrix rather than relying only on TRS data.

- Observation: The first `ModelResource` importer currently derives roots from all nodes with no parent instead of the glTF default scene's root-node list.
  Evidence: `GltfDocument` exposes `default_scene_index()` and `scene_count()`, but does not yet expose per-scene root nodes, so `gltf_importer.cpp` uses parent indices to choose roots. This is correct for the tested fixtures, but multi-scene or partial-scene assets need explicit scene-root metadata before broad model support.

- Observation: The Milestone 2 material bridge is render-path-compatible but not a complete glTF material import.
  Evidence: Imported materials use the current opaque shader, a base-color factor property, and a default white texture so static meshes can be represented with valid `Mesh`/`Material` resources. Real glTF base-color textures, metallic-roughness textures, normal textures, and texture color spaces remain Milestone 3 work.

- Observation: Import-context-owned resources were an early milestone compromise and have now been replaced.
  Evidence: The 2026-07-03 resource-loading cleanup removed `ModelResourceImportContext`; `ModelResourceLoader` now holds temporary `Ptr<T>` caches and imported durable resources are owned by `Resources`.

- Observation: Milestone 3 PBR import would have pushed the structural glTF importer past the review split-pressure threshold if left in one file.
  Evidence: `cpp\src\assets\gltf_importer.cpp` is 562 lines after the tangent/import changes, while PBR material, texture, fallback, and cache helpers live in `cpp\src\assets\gltf_importer_resources.cpp` at 366 lines.

- Observation: Dawn's current WGSL frontend rejected shader-side `inverse()` in the native smoke path.
  Evidence: `npm run smoke:render` failed with "unresolved call target 'inverse'" when the vertex shader attempted `transpose(inverse(draw.model))`. The fix moved the inverse-transpose normal matrix to CPU-side draw uniforms in `OpaquePass`, keeping the shader simple and smoke-compatible.

- Observation: The current PBR demo still creates one pipeline per material bind-group layout handle, not one pipeline for the whole shader.
  Evidence: Browser smoke reports `pipelineCreateCount: 6` for the PBR demo scene. This remains durable initialization/first-use work and `OFG-BOOT-005` explicitly avoids an exact one-pipeline assumption, but a later renderer optimization should share identical material bind-group layouts.

- Observation: `simple-skin.gltf` is a useful skin fixture but omits normals.
  Evidence: Its primitive has `POSITION`, `JOINTS_0`, and `WEIGHTS_0` attributes but no `NORMAL`. Milestone 4 now generates normals for supported triangle-list primitives without source normals, which keeps the skin fixture usable without weakening texture/UV requirements.

- Observation: Milestone 4 skin support initially pushed `cpp\src\assets\gltf_importer.cpp` over the review split-pressure threshold.
  Evidence: The file reached 701 lines before review cleanup. Moving generated normal/tangent helpers into `cpp\src\assets\gltf_importer_geometry.cpp` left the structural importer at 574 lines, with the geometry helper at 148 lines and resource helper at 366 lines.

- Observation: Milestone 5 animation import again created importer split pressure before review cleanup.
  Evidence: Adding animation sampler/channel decoding pushed `cpp\src\assets\gltf_importer.cpp` to 721 lines. Moving animation import helpers into `cpp\src\assets\gltf_importer_animation.cpp` left the structural importer at 579 lines and the animation helper at 220 lines.

- Observation: `simple-skin.gltf` exercises both the skin hierarchy and a joint-targeted rotation animation.
  Evidence: Its single animation channel targets node 2, which is also the second joint in `skin.joints`. Milestone 5 tests now play that clip on the instantiated joint entity and then manually override the joint transform after animation evaluation, preserving the future IK/procedural-control hook before Milestone 6 skinning reads the pose.

- Observation: CPU skinning benefits from a scene-owned world-transform cache at the animation-to-skinning boundary.
  Evidence: Milestone 6 now has `Scene::update` fill `m_world_transform_cache` after animation players run and before mesh renderers skin. Cached scene updates compute one world matrix per entity id; direct `MeshRenderer::update_skinning()` calls still fall back to recursive `world_from_local()` for focused tests and explicit post-animation overrides.

- Observation: Dynamic GPU vertex-buffer reuse is testable with the native Dawn fixture.
  Evidence: `model_skinning_test.cpp` imports `simple-skin.gltf` with a test `GpuContext`, advances two ordinary scene updates, and checks that the dynamic skinned mesh upload bytes increase while `vertex_buffer_create_count()` stays flat. `mesh_resource_test.cpp` independently proves `Mesh::update_vertices_in_place()` updates a dynamic mesh without recreating its vertex buffer and rejects mismatched capacities.

- Observation: Locomotion clip weights must be set after player movement and before animation sampling.
  Evidence: `PlayerAnimationController` reads `Player::current_speed()` after `Player::update` has processed the latest controls, then calls `AnimationPlayer::set_clip_state()` before `AnimationPlayer::update` samples weighted clips. `animation_blending_test.cpp` proves a zero-speed update samples idle, normal movement samples walk, and fast movement samples sprint in one `Scene::update`.

- Observation: The first blending path can stay generic without cross-file animation binding yet.
  Evidence: Milestone 7 tests use synthetic clips to prove translation and rotation blending, and the controller accepts explicit `AnimationClip` references named like the audited Quaternius locomotion clips. The actual player mesh plus separate animation-library binding remains Milestone 8 work, where model resources and clip names are loaded together in the browser/runtime path.

- Observation: The selected player GLB has degenerate UV triangles in geometry that still needs generated tangents.
  Evidence: The first browser import of `quaternius-superhero-male.glb` failed with `glTF primitive cannot generate tangents from degenerate texture coordinates.` Milestone 8 changed generated tangent handling to skip degenerate-UV triangles and provide a stable fallback tangent for vertices without a valid accumulated tangent, with a regression test in `model_resource_test.cpp`.

- Observation: Hiding the fallback player cube through a tiny scale breaks the renderer's normal-matrix validation.
  Evidence: After the player model first loaded successfully, browser status reported `Opaque draw model matrix is not invertible for normal transformation.` The hidden fallback cube used a `0.001` scale, whose determinant was below the normal-matrix inversion tolerance. Milestone 8 added `MeshRenderer::visible()` and render extraction now skips invisible renderers.

- Observation: Renderer visibility needed direct native coverage because it now gates hidden fallback behavior.
  Evidence: The Milestone 8 review found that `MeshRenderer::visible()` was only covered indirectly by browser smoke and demo-scene default visibility. `renderer_test.cpp` now has `renderer skips invisible scene mesh renderers`, which renders a scene where every mesh renderer points at non-GPU-ready resources but is hidden; the render path succeeds only if extraction skips those renderers before draw-list resource validation.

- Observation: Partial player-model scene attachment is not yet transactional.
  Evidence: The player now self-loads model resources through `Resources`, but once `instantiate_model_resource()` starts mutating a live `Scene`, OFG currently has only `Scene::clear()` and no targeted entity/component deletion or model-instance transaction rollback. Normal parse, clip-remap, and queued-before-scene failures remain recoverable, but an exception after partial live-scene attachment would require a future scene deletion/transaction primitive to retry without restarting the scene.

- Observation: Final coverage initially exposed narrow native-line gaps in new runtime files.
  Evidence: The first Milestone 9 coverage run reported attention items in `material.cpp`, `animation_player.cpp`, `mesh_renderer.cpp`, and `player_animation_controller.cpp`. Focused tests now cover incomplete material GPU mutation, animation clip validation, invalid animation playback inputs, stale clip references, mesh-renderer skinning failures, invisible renderer extraction, and locomotion edge weights; the final C++ coverage gate reports `animation_clip.cpp` 100.00%, `material.cpp` 90.71%, `animation_player.cpp` 91.26%, `mesh_renderer.cpp` 94.76%, and `player_animation_controller.cpp` 100.00%.

- Observation: glTF importer files remain better validated by fixture matrices than by the current per-file native line gate.
  Evidence: The raw LLVM summary includes many malformed-format and unsupported-feature branches under `cpp\src\assets`. Those branches are covered through focused glTF document/import/model/skinning tests, the Quaternius player audit, `npm run build:wasm`, browser smoke, and native render smoke. `COVERAGE.md`, `tools\cpp-coverage.mjs`, and this plan now record `cpp\src\assets` as a deliberate coverage exception until the importer feature matrix stabilizes enough for a meaningful 90% per-file gate.

## Decision Log

- Decision: Vendor tinygltf as a pinned source snapshot instead of a submodule.
  Rationale: The repo already vendors small C++ dependencies under `cpp/third_party`, and a source snapshot keeps CMake, CI, and offline development straightforward. The snapshot provenance is recorded in `cpp/third_party/tinygltf/SOURCE.md`.
  Date/Author: 2026-07-02 / Codex

- Decision: Keep tinygltf types private to a glTF import module.
  Rationale: OFG resource, scene, and animation contracts should not leak third-party data structures. This makes future parser replacement or callback changes local to the importer.
  Date/Author: 2026-07-02 / Codex

- Decision: Make `GltfDocument` an OFG-owned parse snapshot that copies decoded tinygltf buffer, accessor, buffer-view, image, and count data.
  Rationale: Later importer stages need stable spans and diagnostics without exposing tinygltf classes or keeping a parser object alive. The extra copy is acceptable for this parse boundary because Milestone 8 will release source bytes and decoded `GltfDocument` data after conversion to durable OFG resources.
  Date/Author: 2026-07-02 / Codex

- Decision: Use a focused test resource provider to document known `animated-cube.gltf` fixture resource mismatches instead of mutating the fixture during Milestone 1.
  Rationale: The parser must support provider-backed URI resolution and clear missing-resource errors. Keeping the broken filesystem case tested proves real missing external files fail with a diagnostic, while the fixture-provider path proves the rest of the glTF can parse when those known resources are supplied.
  Date/Author: 2026-07-02 / Codex

- Decision: Use `quaternius-superhero-male.glb` as the default player mesh and `quaternius-ual1-standard.glb` as the default locomotion animation library.
  Rationale: The male superhero mesh is compatible with both UAL animation skeletons and includes the target skinned PBR character. UAL1 contains the first controller clips `Idle_Loop`, `Walk_Loop`, `Jog_Fwd_Loop`, and `Sprint_Loop`; UAL2 is skeleton-compatible but lacks `Sprint_Loop`.
  Date/Author: 2026-07-02 / Codex

- Decision: Treat `skin.skeleton` as optional in OFG skin bindings.
  Rationale: The audited Quaternius assets omit the explicit skeleton root while still supplying complete joint lists. OFG should preserve a `Ptr<Entity>` or optional node index when `skin.skeleton` exists, but later skinning/attachment code must also work when it is absent by using the joint hierarchy and model root.
  Date/Author: 2026-07-02 / Codex

- Decision: TypeScript may fetch model files, but C++ owns model interpretation and scene mutation.
  Rationale: `OFG-BOOT-001` allows the browser host to own DOM and loading ergonomics. It must not own gameplay, scene graph state, renderer resources, animation, or draw submission.
  Date/Author: 2026-07-02 / Codex

- Decision: Import all glTF nodes as scene entities, not as a separate hidden model hierarchy.
  Rationale: The existing renderer consumes `Scene` entity transforms and `MeshRenderer` components. Representing model nodes and joints as entities lets animation, skinning, camera/player attachment, and later debugging use one transform model.
  Date/Author: 2026-07-02 / Codex

- Decision: Treat the imported node/entity hierarchy as the skeleton transform hierarchy.
  Rationale: glTF skinning designates ordinary nodes as joints through `skin.joints`; there is no separate transform hierarchy for bones in the format. OFG should preserve every glTF node as an entity and avoid adding standalone joint or bone components unless a later feature needs actual behavior there.
  Date/Author: 2026-07-02 / Codex

- Decision: Store skinning metadata on `MeshRenderer`.
  Rationale: `SkinBinding` describes how one mesh-renderer instance is skinned: which instantiated node entities act as joints, which inverse bind matrices apply, which bind-pose data is used, and which dynamic mesh receives CPU-skinned output. That data belongs with the renderer instance using it, not in separate components attached to joint entities.
  Date/Author: 2026-07-02 / Codex

- Decision: Animation writes entity local transforms, while skinning reads the current post-animation joint entity transforms.
  Rationale: This keeps animation, later IK/procedural controls, and rigid attachments on one transform graph. Systems that need to attach an object to a hand can parent the object under the hand joint entity; systems that need to override an arm can modify the same entity before skinning runs.
  Date/Author: 2026-07-02 / Codex

- Decision: Load glTF into a reusable `ModelResource`, then instantiate that resource into live scenes.
  Rationale: A model resource is conceptually a small scene, but it should not be an actual live `Scene` because live scenes own entity/component storage and generation-limited pointers. `ModelResource` should be format-neutral so future model importers can produce the same resource type. It should store node templates, shared mesh/material/texture resources, skin templates, and animation clips. Instantiation copies the node tree into a target `Scene`, creates `MeshRenderer` components, attaches per-instance skin bindings where needed, and returns a `ModelInstance`.
  Date/Author: 2026-07-02 / Codex

- Decision: Keep `GltfDocument`, `ModelResource`, and `ModelInstance` as distinct layers.
  Rationale: `GltfDocument` is the parse/decode layer and may retain source indices for diagnostics. `ModelResource` is the reusable engine asset: a format-neutral, scene-shaped prefab graph with node/component templates and references to loaded OFG resources. `ModelInstance` is live scene state with actual `Entity*`, `MeshRenderer*`, animation players, and per-instance skin bindings.
  Date/Author: 2026-07-02 / Codex

- Decision: Introduce `Object` and `Ptr<T>` before glTF import work.
  Rationale: `Object` will be the base for referenceable scene/resource/runtime objects. `Ptr<T>` will be a nullable non-owning pointer that registers itself in the target object's intrusive reference list. Destroying an `Object` nulls every registered `Ptr`; dereferencing a null or invalidated `Ptr` throws a clear `EngineError`. This preserves existing ownership by `Scene` and `Resources` while making stale references fail predictably.
  Date/Author: 2026-07-02 / Codex

- Decision: Use `Ptr<T>` for stored observing references, not for owning storage.
  Rationale: `Scene` should continue to own entities/components in pointer-stable owning containers, and `Resources` should continue to own resource objects in pointer-stable owning containers. Those core owner lists should not become `Ptr<T>` lists because `Ptr<T>` does not own its target. `Ptr<T>` should replace fields such as mesh renderer resource references, skin-binding joint entity references, animation target references, returned model-instance references, caches, and bindings that persistently observe an `Object` owned elsewhere. Function parameters, immediate stack-local borrows, ownership containers, and tight internal loops may still use references or raw pointers where lifetime is locally obvious.
  Date/Author: 2026-07-02 / Codex

- Decision: Make `Object` non-copyable and non-movable.
  Rationale: Intrusive reference tracking depends on stable object addresses. Referenceable objects should be allocated in pointer-stable owner storage such as `unique_ptr` vectors and constructed in place. Resource move tests and construction helpers should be revised accordingly.
  Date/Author: 2026-07-02 / Codex

- Decision: Keep Milestone 0's pointer migration focused on durable stored observers, and explicitly defer owner-internal tree links and render extraction borrows.
  Rationale: `Ptr<T>` registration is useful for references that persist across owner boundaries, but it is avoidable churn in hot paths and unnecessary for owner-internal structures that are destroyed together. Scene tree raw links remain under `Scene` ownership for now, and `DrawCommand` raw pointers remain transient one-render borrows. `DemoScene` cached bindings are also deferred because `Game::release` clears `DemoScene` before resource release and scene bindings are generation-checked, but they are recorded as a future candidate once real model instances replace the demo scene.
  Date/Author: 2026-07-02 / Codex

- Decision: Do not introduce full scene serialization before the first model-instancing path.
  Rationale: The first need is narrower than saving/loading arbitrary scenes. Use a serialization-shaped, data-only `ModelResource` plus an explicit `ModelInstantiationContext` that owns remap tables and copy policies. This proves cross-scene copying semantics early without freezing a whole-scene file format before the scene/component model settles.
  Date/Author: 2026-07-02 / Codex

- Decision: Use a resource import context with URI-based deduplication for the first model loader.
  Rationale: The glTF importer needs a place to create and reuse meshes, materials, textures, shaders, and fallback textures. The first version can deduplicate by canonical model URI plus glTF index or generated resource label; later asset IDs can replace this without changing `ModelResource` semantics.
  Date/Author: 2026-07-02 / Codex

- Decision: Make `AnimationPlayer` a scene component.
  Rationale: Animation playback mutates scene entity transforms during `Scene::update`, so its lifetime and update ordering should live with the scene. `Scene` should own `AnimationPlayer` components in the same pointer-stable component storage pattern as `MeshRenderer`, `Player`, and `Camera`; `ModelInstance` may return a `Ptr<AnimationPlayer>` for convenience but does not own it.
  Date/Author: 2026-07-02 / Codex

- Decision: Start PBR lighting with one main directional light plus ambient light.
  Rationale: This is enough to validate PBR material response without building a full lighting system. `Scene` should store an explicit main-light selection for a directional light so future dynamic sky/atmosphere work can identify which light represents the sun.
  Date/Author: 2026-07-02 / Codex

- Decision: Keep GPU instancing out of scope for this plan.
  Rationale: The static cube copy milestone proves model-resource copying and shared resource references. Renderer batching or GPU instancing will matter later, but adding it here would distract from glTF import, model resources, animation, and player skinning.
  Date/Author: 2026-07-02 / Codex

- Decision: Native smoke may load model assets directly from the filesystem.
  Rationale: Browser smoke exercises TypeScript fetch and WASM transport. Native smoke should be allowed to use a filesystem resource provider so the native renderer can validate real textures and future terrain/material assets without a browser fetch layer.
  Date/Author: 2026-07-02 / Codex

- Decision: Implement CPU skinning first, but add a dynamic vertex update API before doing per-frame skinning.
  Rationale: CPU skinning satisfies the requested scope and is simpler to validate than GPU skinning. Updating an existing vertex buffer avoids violating the resource lifetime contract by recreating buffers every frame.
  Date/Author: 2026-07-02 / Codex

- Decision: Bind animation clips to instances by stable node or joint names as well as by imported node indices.
  Rationale: The player mesh and Quaternius animation library are separate GLBs. Name-based binding gives a path to reuse animation clips across compatible skeletons without requiring both clips and mesh to come from the same file.
  Date/Author: 2026-07-02 / Codex

- Decision: Support core glTF metallic-roughness PBR first; reject required unsupported material extensions with clear errors.
  Rationale: The player target uses ordinary GLB/PBR assets, while `material-specular-glossiness-13.glb` exists as an extension fixture. A clear unsupported-extension diagnostic is safer than silently rendering required `KHR_materials_pbrSpecularGlossiness` incorrectly.
  Date/Author: 2026-07-02 / Codex

- Decision: Make `ModelResource` non-movable and return it as `std::unique_ptr<ModelResource>`.
  Rationale: `ModelResource` derives from `Object` so other systems can hold safe references to durable imported model templates later. The intrusive pointer list requires pointer-stable storage, so import/build APIs transfer ownership by `unique_ptr` instead of returning movable values.
  Date/Author: 2026-07-02 / Codex

- Decision: Let the first `ModelResourceImportContext` own CPU/GPU resource caches directly. Superseded on 2026-07-03 by `Resources`-owned durable resources plus temporary `ModelResourceLoader` caches.
  Rationale: Milestone 2 needed deduplicated mesh/material/texture/shader resources before the global asset system existed. The later resource-loading cleanup replaced context ownership with `Resources::create_*` ownership while preserving `ModelResource` references and instantiation semantics.
  Date/Author: 2026-07-02 / Codex

- Decision: Use the existing opaque shader and a white fallback texture as the Milestone 2 material bridge.
  Rationale: The static model-resource milestone is about parsing mesh/node data and copying instances into scenes. PBR material correctness, glTF texture import, texture color spaces, and normal maps are explicitly handled in Milestone 3, so the importer only needs valid material resources that can pass through the current renderer path.
  Date/Author: 2026-07-02 / Codex

- Decision: Use one shared opaque PBR shader layout helper for demo, importer, and renderer tests.
  Rationale: The frame/draw/material binding contract is easy to duplicate incorrectly. `opaque_pbr_shader_layout()` keeps the demo shader, imported model materials, and renderer tests on the same layout while leaving the older `opaque_demo_shader_layout()` wrapper available for demo-scene callers.
  Date/Author: 2026-07-02 / Codex

- Decision: Represent first-pass PBR factors as `base_color_factor` plus packed `pbr_factors`.
  Rationale: `base_color_factor` maps directly to glTF, while `pbr_factors` stores metallic factor, roughness factor, normal scale, and a normal-enabled flag in one aligned vec4. Base-color textures are imported as sRGB; metallic-roughness and normal textures are imported as linear; missing textures use white, neutral metallic-roughness, and flat-normal fallbacks.
  Date/Author: 2026-07-02 / Codex

- Decision: Generate tangents only when normal mapping needs them.
  Rationale: glTF assets may omit `TANGENT` while still providing normal maps, including the selected Quaternius player. Import explicit `TANGENT` when present; otherwise generate tangents from positions, normals, and UVs for normal-mapped triangle primitives. Missing UV attributes still fail clearly when a normal map requires tangent space; degenerate-UV triangles are skipped and vertices without any valid accumulated tangent receive a stable normal-based fallback tangent so otherwise valid real-world meshes can load.
  Date/Author: 2026-07-02 / Codex

- Decision: Use browser `loadPlayerModel(playerBytes, animationBytes)` as the first model byte-transport API. Superseded on 2026-07-03 by generic blob requests and `Resources::load_model_resource`.
  Rationale: The Milestone 8 target had exactly one player mesh GLB and one compatible animation-library GLB, so a narrow pair of `Uint8Array` arguments was enough to prove C++ ownership. The later resource-loading cleanup moved asset choice and dependency loading fully into C++ while TypeScript only services opaque blob ids.
  Date/Author: 2026-07-02 / Codex

- Decision: Raise browser WASM memory to `INITIAL_MEMORY=268435456` and enable `ALLOW_MEMORY_GROWTH=1`.
  Rationale: The selected player and animation-library GLBs are about 23.6 MB on disk and about 129.6 MB for source plus decoded buffers/images before importer conversion, CPU/GPU resources, and dynamic skinning state. A 256 MiB initial heap reduces early growth pressure while growth remains enabled for game-scale assets and future terrain/material work.
  Date/Author: 2026-07-02 / Codex

- Decision: Hide loaded-model fallbacks through `MeshRenderer::visible` rather than zero or tiny transforms.
  Rationale: Renderer normal-matrix generation requires invertible model matrices. A hidden renderer should not enter the draw list at all; using tiny scales to hide fallback visuals creates invalid or near-singular transforms and makes render diagnostics misleading.
  Date/Author: 2026-07-02 / Codex

- Decision: Write a CPU-computed normal matrix in draw uniforms instead of calling `inverse()` in WGSL.
  Rationale: Correct PBR lighting under non-uniform model scale needs inverse-transpose normal/tangent transforms, but the native Dawn smoke validator rejected shader-side `inverse()`. One extra mat4 fits inside the existing 256-byte dynamic draw-uniform stride and keeps the shader portable for the current browser/native toolchains.
  Date/Author: 2026-07-02 / Codex

- Decision: Store reusable skin data in `ModelResource::SkinTemplate` and per-instance skin data in `MeshRenderer::SkinBinding`.
  Rationale: The reusable template should contain source skin index/name, joint node indices, inverse bind matrices, and optional skeleton root node index. Instantiation should resolve those node indices into `Ptr<Entity>` values owned by the target scene and store them on the mesh renderer that needs them. Joint entities remain ordinary scene nodes, with no bone or joint component introduced for metadata.
  Date/Author: 2026-07-02 / Codex

- Decision: Preserve `skin.skeleton` as optional metadata, not a required skeleton owner.
  Rationale: glTF skins can omit `skin.skeleton`, and the selected Quaternius assets do. When present, OFG records it as the optional skeleton root/pivot in both the template and the mesh-renderer binding; when absent, joint ordering and the scene node hierarchy remain sufficient for animation, attachments, and later skinning.
  Date/Author: 2026-07-02 / Codex

- Decision: Generate normals for supported untextured triangle primitives that omit `NORMAL`.
  Rationale: `simple-skin.gltf` is intentionally small and useful for skin metadata tests, but it omits normals. Generating smooth normals for valid triangle lists lets the fixture import through the PBR mesh path while still rejecting degenerate triangles and still requiring UVs when a material texture or normal map needs them.
  Date/Author: 2026-07-02 / Codex

- Decision: Store CPU-skinned output as a per-instance dynamic mesh owned by `MeshRenderer::SkinBinding`.
  Rationale: The shared bind-pose mesh belongs to the reusable `ModelResource`, while each instantiated renderer needs distinct mutable vertex output. Keeping the dynamic mesh inside the renderer-owned skin binding avoids standalone bone/joint metadata components and keeps skinning resources next to the renderer that consumes them.
  Date/Author: 2026-07-02 / Codex

- Decision: Cache scene world transforms after animation and before CPU skinning.
  Rationale: Skinning needs current joint entity world transforms, but recomputing the parent chain for every joint is avoidable even for the first player-only CPU path. `Scene::update` now computes one world matrix per entity id after animation players run, before future procedural overrides/skinning, and passes that cache into mesh renderers; explicit renderer skinning calls keep a recursive fallback for manual tests and direct debugging.
  Date/Author: 2026-07-02 / Codex

- Decision: Keep generic clip blending inside `AnimationPlayer`.
  Rationale: `AnimationPlayer` already owns bound source-node targets, rest transforms, local playback time, and transform writes. Extending it to multiple weighted clip states keeps pose blending local to the animation component and avoids introducing a second system that also writes animated entity transforms.
  Date/Author: 2026-07-02 / Codex

- Decision: Keep player locomotion animation ownership inside the `Player` component.
  Rationale: The `Player` component owns player movement and the first hardcoded player model binding. Its movement speed is the direct source of idle/walk/sprint weights, so a separate scene component would only hold metadata for the player and split one behavior across two ownership sites. `Player` updates clip weights on its bound `AnimationPlayer` before animation sampling and still avoids root motion.
  Date/Author: 2026-07-02 / Codex

- Decision: Run player updates before animation players.
  Rationale: `Player` needs to process movement and write locomotion clip weights from the latest speed before `AnimationPlayer` samples weighted clips. The canonical scene order is now players, animation players, procedural overrides, CPU skinning, and cameras.
  Date/Author: 2026-07-02 / Codex

- Decision: Bind Milestone 5 same-file animation clips by imported source-node index, and defer cross-file name binding to the player animation-library milestones.
  Rationale: glTF channels target source node indices, and `ModelResource` instantiation already has an exact source-node-index to live-entity table for animations imported from the same file. The Quaternius player mesh and animation library are separate GLBs, so name-based skeleton/clip binding remains necessary, but belongs with Milestone 7 or 8 when external animation clips are attached to a different model resource.
  Date/Author: 2026-07-02 / Codex

- Decision: Expose locomotion weight computation as a small pure helper.
  Rationale: `Player` applies the computed weights to its bound animation player, but `compute_locomotion_animation_weights()` makes idle/walk/sprint edge cases directly testable without manufacturing impossible `Player` speeds. This keeps the runtime update path simple and gives coverage a stable target for invalid speed diagnostics.
  Date/Author: 2026-07-02 / Codex

- Decision: Keep `cpp\src\assets` outside the default 90% native line gate for this milestone.
  Rationale: The glTF parser/importer is format-matrix code with many malformed input branches. The current useful confidence comes from fixture-driven document/import/model/skinning tests, selected player asset audits, WASM build validation, and browser/native smoke tests. The exception is documented in `COVERAGE.md`, `tools\cpp-coverage.mjs`, and this plan, and should be revisited when broader asset-format support settles.
  Date/Author: 2026-07-02 / Codex

## Outcomes & Retrospective

Milestone 0 is implemented. `Object` and `Ptr<T>` now provide lifetime-aware stored observers for referenceable scene/resource objects. `Entity`, `Component` and current concrete components, `Mesh`, `Material`, `Texture`, and `Shader` are `Object`-derived. Resource objects are constructed directly in `Resources`-owned `std::unique_ptr` storage and are non-copyable/non-movable. The first migrated stored observers are `Component::m_entity`, `Scene::m_main_camera`, `MeshRenderer::m_mesh`, `Material::m_shader`, `SubMesh::m_default_material`, `MaterialOverride::m_material`, and texture-valued `PropertyBag` entries. `DrawCommand` and scene tree links remain raw by documented policy. Validation passed with `npm run format:cpp`, `npm run format:cpp:check`, `npm run test:cpp`, and `git -c safe.directory=C:/dev/ofg diff --check`.

Milestone 0 review was run locally because the available multi-agent tool requires an explicit user request for sub-agent delegation. The local contract, code-quality, legacy, correctness, and validation passes found two required cleanup items: document the new `Object`/`Ptr<T>` lifetime contract in `docs/API_CONTRACTS.md`, and include a complete `Material` type where public `Ptr<Material>` values are copied. Both were fixed and validation was rerun. Remaining risk: `DemoScene` still has raw cached bindings guarded by lifecycle/generation checks; this is acceptable for the temporary demo scene but should not be copied into `ModelResource`/`ModelInstance` work.

Milestone 1 is implemented. `ofg_tinygltf` now builds the vendored tinygltf snapshot privately, `GltfDocument` exposes an OFG-owned parse/accessor layer with no public tinygltf types, and tests cover `static-box.glb`, `simple-skin.gltf`, and the known-broken `animated-cube.gltf` resource cases. The provider callback cache now stores loaded external resources once and returns cache pointers to avoid an extra `AssetFile` copy before tinygltf copies callback output bytes. The tinygltf target also receives the same WASM exception compile option used by the rest of the browser C++ code. Validation passed with `npm run format:cpp`, `npm run format:cpp:check`, `npm run test:cpp`, `git -c safe.directory=C:/dev/ofg diff --check`, and the extra browser-toolchain check `npm run build:wasm`.

Milestone 1 review was run locally using the contract, code-quality, legacy, correctness, and validation passes from the repo-local milestone-review skill. No sub-agents were used because delegated reviewers require an explicit user request in this environment. Required findings fixed: avoid provider callback cache copy churn and compile tinygltf with the current WASM exception mode. Follow-ups recorded: runtime browser asset loading and WASM memory budget remain Milestone 8 work, and `docs\ARCHITECTURE.md` is still missing from the repo-local review checklist.

Milestone 1A is implemented. `GltfDocument` now exposes OFG-owned metadata for extensions, nodes, meshes, primitives, attributes, materials, textures, skins, and animations while keeping tinygltf private. `player_asset_audit_test.cpp` parses the selected player and animation assets, proves skeleton compatibility, locks down locomotion clip names, verifies triangle-list skinned mesh attributes, records the missing tangent requirement, and confirms the player-load memory pressure exceeds 120 MiB before renderer/GPU costs. `docs\player-model-asset-audit.md` records the selected assets and audit facts for later milestones.

Milestone 1A review was run locally with contract, code-quality, legacy, correctness, and validation passes. Required finding fixed: guard player audit helper accessor lookups before indexing. Follow-ups recorded: `cpp\src\assets\gltf_document.cpp` is 588 lines, just under the review split-pressure threshold, so Milestone 2 should move importer/model-resource work into new files instead of growing the parse source further. Validation rerun passed with `npm run format:cpp`, `npm run format:cpp:check`, `npm run test:cpp`, and `git -c safe.directory=C:/dev/ofg diff --check`; `npm run build:wasm` also passed after the parser metadata expansion.

Milestone 2 is implemented. `ModelResource` is now the format-neutral imported prefab graph with node templates, root-node indices, mesh-renderer templates, and shared resource references. Historically this milestone used `ModelResourceImportContext` to create and deduplicate initial mesh, material, shader, and fallback texture resources; the 2026-07-03 resource-loading cleanup superseded that with `Resources` ownership and temporary `ModelResourceLoader` caches. `gltf_importer.cpp` imports triangle-list static meshes from `static-box.glb`, decodes accessor strides and unsigned index component types, rejects required unsupported extensions and unsupported primitive features clearly, handles TRS and decomposable node matrices, and creates one `MeshRenderer` per mesh-bearing node when the model is instantiated. `model_resource_test.cpp` proves one loaded model can be copied five times into a scene with distinct entities and renderers while sharing the underlying mesh resource, and proves `Scene::clear` invalidates returned model instance pointers.

Milestone 2 review was run locally with contract, code-quality, legacy, correctness, and validation passes. Required finding fixed: instantiating a model after context-owned mesh resources are destroyed now throws a clear `EngineError`, covered by `ModelResource instantiation fails clearly after cached mesh destruction`. Follow-ups recorded: the importer should expose and honor glTF default-scene root nodes instead of deriving roots from all parentless nodes; real glTF material/texture import is deferred to Milestone 3; `cpp\src\assets\gltf_importer.cpp` is 536 lines and should not become the dumping ground for PBR, skinning, and animation work. Validation rerun passed with `npm run format:cpp`, `npm run format:cpp:check`, `npm run test:cpp`, `git -c safe.directory=C:/dev/ofg diff --check`, and `npm run build:wasm`.

Milestone 3 is implemented. `MeshVertex` now carries position, normal, tangent, and UV data; `PipelineCache` exposes those attributes to the opaque shader; the opaque WGSL path now implements core metallic-roughness PBR with base-color, metallic-roughness, and normal texture slots; and `Scene` stores a main directional light plus ambient term. The demo scene creates sRGB base-color textures, linear neutral metallic-roughness and flat-normal fallbacks, real normals/tangents for the generated cube/ground meshes, and a lit PBR version of the existing smoke scene. The glTF importer now reads material factors, base-color/metallic-roughness/normal textures, image color-space roles, explicit `TANGENT` attributes, and generated tangents when a normal map requires tangent space. Tests cover PBR material properties, texture color spaces, imported tangents, fallback texture counts, and scene light validation.

Milestone 3 review was run locally using the repo-local milestone-review contract, code-quality, legacy, correctness, and validation passes. No sub-agents were used because delegated reviewers require an explicit user request in this environment, and `docs\ARCHITECTURE.md` is still absent from the repo-local checklist. Required findings fixed: `docs\API_CONTRACTS.md` now describes the PBR/main-light renderer contract; duplicated PBR shader layout data was centralized in `cpp\include\ofg\render\opaque_pbr_shader.hpp`; and non-uniform-scale normal/tangent transforms now use a CPU-computed inverse-transpose normal matrix after native smoke rejected shader-side `inverse()`. Remaining follow-ups: material bind-group layout sharing could reduce the current six PBR demo pipelines, and glTF default-scene root-node metadata remains deferred from Milestone 2. Validation rerun passed with `npm run format:cpp`, `npm run format:cpp:check`, `npm run test:cpp`, `git -c safe.directory=C:/dev/ofg diff --check`, `npm run build:wasm`, `npm run smoke:render`, and `npm run smoke:browser`. Latest visual artifacts are `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png`, `C:\dev\ofg\artifacts\render-smoke\report.json`, `C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png`, and `C:\dev\ofg\artifacts\browser-smoke\report.json`; native smoke reported `sceneRatio: 0.502097` and browser smoke reported `sceneRatio: 0.535230961298377` with `lastError: null`.

Milestone 4 is implemented. `ModelResource` now stores skin templates with source skin metadata, ordered joint node indices, inverse bind matrices, and optional skeleton-root node indices. `MeshRenderer` owns optional `SkinBinding` metadata with ordered joint `Ptr<Entity>` values, copied inverse bind matrices, and optional skeleton-root entity reference. The glTF importer reads skin tables, validates joint and skeleton indices, reads `FLOAT MAT4` inverse bind accessors or defaults to identity bind matrices when omitted, and attaches skin template references to mesh-renderer templates. Model instantiation preserves the imported node hierarchy and resolves skin joints to the same scene entities that animation and later procedural controls will modify. No separate bone/joint components were added.

Milestone 4 review was run locally with contract, code-quality, legacy, correctness, and validation passes. Required findings fixed: `docs\API_CONTRACTS.md` now states that `ModelResource` templates and mesh-renderer `SkinBinding` metadata are C++ owned, and `cpp\src\assets\gltf_importer.cpp` was split after growing to 701 lines; generated normal/tangent helpers now live in `cpp\src\assets\gltf_importer_geometry.cpp`, leaving the importer at 574 lines. Tests now cover `simple-skin.gltf` with generated normals, missing `skin.skeleton`, inverse bind matrices, joint entity identity, and parent/child hierarchy, plus `rigged-simple.glb` with explicit `skin.skeleton` preserved as a mesh-renderer skeleton-root binding. Validation rerun passed with `npm run format:cpp`, `npm run format:cpp:check`, `npm run test:cpp`, `git -c safe.directory=C:/dev/ofg diff --check`, and `npm run build:wasm`. Remaining follow-ups: skinning still does not read `JOINTS_0`/`WEIGHTS_0` until Milestone 6, and default glTF scene-root metadata remains deferred.

Milestone 5 is implemented. `AnimationClip` stores imported source-node animation channels in C++-owned data, and `AnimationPlayer` is a `Scene` component that binds source node indices to instantiated `Entity` pointers, resets targets to imported rest transforms each update, advances local clip time, and samples one playing clip. `Scene::update` now validates controls, updates `Player` components, updates `AnimationPlayer` components, then updates cameras; future procedural override and CPU skinning hooks remain between animation and cameras as recorded in `OFG-BOOT-002`. The glTF importer decodes translation, rotation, and scale channels; supports `LINEAR` and `STEP`; normalizes rotation outputs; rejects `CUBICSPLINE` and morph weights clearly; and stores animation clips on `ModelResource`. Instantiation creates an `AnimationPlayer` for animated model resources and returns it through `ModelInstance`.

Milestone 5 review was run locally with contract, code-quality, legacy, correctness, and validation passes. No sub-agents were used because delegated reviewers require an explicit user request in this environment, and `docs\ARCHITECTURE.md` remains absent from the repo-local checklist. Required findings fixed: `docs\API_CONTRACTS.md` now documents animation clip/player ownership and the current scene update order; `cpp\src\assets\gltf_importer.cpp` was split after growing to 721 lines; scene animation tests were moved to `cpp\tests\scene_animation_test.cpp` after `scene_test.cpp` crossed the 600-line threshold; tests now prove update order, `Scene::clear` invalidates animation-player pointers, ordinary animated-cube playback changes entity transforms, `simple-skin.gltf` animates a joint entity and allows a manual post-animation joint override, `STEP` interpolation imports, and `CUBICSPLINE` fails clearly. Validation rerun passed with `npm run format:cpp`, `npm run format:cpp:check`, `npm run test:cpp`, `git -c safe.directory=C:/dev/ofg diff --check`, and `npm run build:wasm`. Remaining follow-ups: cross-file/name-based animation binding for the Quaternius animation library is deferred to Milestone 7 or 8, and CPU skinning still begins in Milestone 6.

Milestone 6 is implemented. The glTF importer decodes `JOINTS_0` and `WEIGHTS_0` attributes for skinned meshes, normalizes weights, validates influence counts, and stores skin influences on the model-resource mesh-renderer template. `MeshRenderer::SkinBinding` now owns the per-instance dynamic skinned mesh, scratch vertices, joint matrices, counters, and the existing joint entity references/inverse bind matrices; the reusable bind-pose mesh remains shared through the `ModelResource`. `Mesh::init_dynamic_vertices()` creates a fixed-capacity dynamic vertex mesh, and `Mesh::update_vertices_in_place()` updates CPU vertices plus existing WebGPU vertex-buffer contents without recreating buffers. `Scene::update` now runs players, animation players, builds a world-transform cache, CPU-skins mesh renderers from the cache, then updates cameras. Tests prove rest-pose skinning, animation-driven deformation, explicit post-animation joint override skinning, separate dynamic meshes for multiple instances, and flat GPU vertex-buffer creation during ordinary repeated updates.

Milestone 6 review was run locally with contract, code-quality, legacy, correctness, and validation passes. No sub-agents were used because delegated reviewers require an explicit user request in this environment, and `docs\ARCHITECTURE.md` remains absent from the repo-local checklist. Required finding fixed: `Mesh::update_vertices_in_place()` now validates GPU readiness before committing CPU vertex changes, preserving the existing prepare-then-commit mutation style. Previously fixed review items for this milestone are also in place: `docs\API_CONTRACTS.md` documents CPU-skinned dynamic mesh lifetime and scene update order, multiple skinned instances share bind-pose resources but own dynamic output, GPU-backed tests prove vertex-buffer creation counts stay flat, `MeshRenderer::set_mesh()` is no longer `noexcept`, and scene skinning uses a world-transform cache rather than recomputing every joint parent chain. Remaining risk: pose-dirty skipping is not implemented yet; for the current one-player CPU path the implementation is allocation-conscious and buffer-stable, and Milestone 7 or 8 should add dirtying only if profiling shows that repeated same-pose skinning matters. Validation rerun passed with `npm run format:cpp`, `npm run format:cpp:check`, `npm run test:cpp`, `git -c safe.directory=C:/dev/ofg diff --check`, and `npm run build:wasm`.

Milestone 7 is implemented. `AnimationPlayer` now stores multiple weighted `AnimationClipState` values, each with local time, playback speed, looping, weight, and playing state. It keeps the existing single-clip `play()` API by clearing to one weighted state, while the new `set_clip_state()` and `set_clip_weight()` APIs support controller-driven blends. Pose evaluation preallocates per-target accumulators when targets are bound, advances playing states, samples channels, normalizes translation/scale weights per target path, blends rotations through weighted normalized quaternions with sign coherence, and writes final transforms from the imported rest pose each update. `Player` now reports `current_speed()` and `fast_speed()`. `PlayerAnimationController` is a scene-owned component that binds a `Player`, an `AnimationPlayer`, and explicit idle/walk/sprint clips; it maps stopped, walking, and fast movement into clip weights without root motion. `Scene::update` now runs players, player animation controllers, animation players, CPU skinning, then cameras.

Milestone 7 review was run locally with contract, code-quality, legacy, correctness, and validation passes. No sub-agents were used because delegated reviewers require an explicit user request in this environment, and `docs\ARCHITECTURE.md` remains absent from the repo-local checklist. Required findings fixed: the pose accumulator was kept private to `AnimationPlayer` rather than exposed as a public scene type, `PlayerAnimationController` now uses `Player::fast_speed()` instead of duplicating the sprint multiplier, an unused const clip-state lookup was removed, and `AnimationPlayer::update()` now validates accumulator binding size before applying poses. `docs\API_CONTRACTS.md` documents `PlayerAnimationController`, weighted animation playback, and the new scene update order. Tests in `animation_blending_test.cpp` cover weighted translation blending, normalized rotation blending, idle/walk/sprint controller weights, scene update order from player speed to sampled pose, controller ownership, duplicate-component rejection, and `Scene::clear` pointer invalidation. Validation rerun passed with `npm run format:cpp`, `npm run format:cpp:check`, `npm run test:cpp`, `git -c safe.directory=C:/dev/ofg diff --check`, and `npm run build:wasm`. Remaining follow-up: binding the audited Quaternius animation-library clips to the separate player model by name is still Milestone 8 work.

Milestone 8 is implemented. TypeScript originally fetched `quaternius-superhero-male.glb` and `quaternius-ual1-standard.glb` as `Uint8Array` values and passed them through the narrow WASM facade; that byte-specific path was later superseded by generic blob requests and `Resources::load_model_resource`. `Player` now requests the player mesh and UAL1 animation library through `Resources`, polls `Ptr<ModelResource>` state, remaps `Idle_Loop`, `Walk_Loop`, and `Sprint_Loop` by source node name onto the player model nodes, instantiates the skinned model under the unscaled player entity, writes locomotion clip weights itself, and hides the fallback cube renderer through `MeshRenderer::visible`. The WASM build uses `INITIAL_MEMORY=268435456` and `ALLOW_MEMORY_GROWTH=1`. Deployment packaging includes the selected player GLBs and model cache headers. Browser smoke waits for `playerModelLoaded` before steady-state counter checks and passes. Dedicated screenshots are `C:\dev\ofg\artifacts\player-model\player-idle.png`, `C:\dev\ofg\artifacts\player-model\player-walk.png`, `C:\dev\ofg\artifacts\player-model\player-sprint.png`, and `C:\dev\ofg\artifacts\player-model\report.json`; the screenshot report records `modelLoadingState: "loaded"`, `playerModelLoaded: true`, and `lastError: null`.

Milestone 8 review was run locally with the repo-local milestone-review contract, code-quality, legacy, correctness, and validation passes. No sub-agents were used because delegated reviewers require an explicit user request in this environment, and `docs\ARCHITECTURE.md` remains absent from the repo-local checklist. Required finding fixed: invisible mesh-renderer extraction now has native coverage in `renderer skips invisible scene mesh renderers`, proving hidden fallback renderers are skipped before draw-list resource validation. Follow-ups recorded: `cpp\src\game\game.cpp` and `cpp\src\web\browser_game.cpp` are now 800-900 line orchestration files and should be split before they cross the hard threshold; partial player-model scene attachment rollback should wait for a targeted entity-deletion or model-instantiation transaction primitive. Validation rerun passed with `npm run format:cpp`, `npm run test:cpp`, `npm run format:cpp:check`, `npm run test:ts`, `git -c safe.directory=C:/dev/ofg diff --check`, `npm run package:site`, and `npm run smoke:browser`. `.deploy` contains `assets\models\player\quaternius-superhero-male.glb` at 15,479,612 bytes and `assets\models\player\quaternius-ual1-standard.glb` at 8,114,364 bytes; browser smoke reported `modelLoadingState: "loaded"`, `playerModelLoaded: true`, `lastError: null`, `sceneRatio: 0.5335830212234707`, and screenshots under `C:\dev\ofg\artifacts\browser-smoke`.

Milestone 9 is complete. `docs\API_CONTRACTS.md`, `docs\SYSTEMS.md`, `COVERAGE.md`, and `docs\coverage\latest.md` reflect the final ownership, update-order, model-loading, and coverage contracts. The coverage gate was expanded to include `cpp\src\animation` and `cpp\src\render\opaque_pbr_shader.cpp`; `cpp\src\assets` is explicitly recorded as the importer coverage exception. Focused tests were added for material incomplete-GPU mutation, animation clip validation, animation-player invalid/stale inputs, mesh-renderer skinning failures, renderer visibility, and locomotion edge weights. Final validation passed with `npm run format:cpp:check`, `npm test`, `npm run smoke:browser`, `npm run smoke:render`, `npm run package:site`, and `npm run coverage`. Refreshed coverage summaries are committed at `C:\dev\ofg\docs\coverage\cpp-summary.json` and `C:\dev\ofg\docs\coverage\ts-coverage-summary.json`; generated reports remain under `C:\dev\ofg\artifacts\coverage`. Browser smoke wrote `C:\dev\ofg\artifacts\browser-smoke\opaque-demo.png` and reported `modelLoadingState: "loaded"`, `playerModelLoaded: true`, `lastError: null`, `sceneRatio: 0.533458177278402`. Native render smoke wrote `C:\dev\ofg\artifacts\render-smoke\opaque-demo.png` and reported `passed: true`, `sceneRatio: 0.502097`.

Milestone 9 review was run locally with contract, code-quality, legacy, correctness, and validation passes. No sub-agents were used because delegated reviewers require an explicit user request in this environment, and `docs\ARCHITECTURE.md` remains absent from the repo-local checklist. No required findings were found. Follow-ups remain the previously recorded split pressure in `cpp\src\game\game.cpp` and `cpp\src\web\browser_game.cpp`, the targeted scene deletion/transaction primitive for partial model-instance rollback, and the future revisit of `cpp\src\assets` once importer coverage can be meaningfully brought under the default 90% line gate. Validation evidence inspected: final command logs, refreshed coverage summaries, browser smoke report, native render smoke report, and `git -c safe.directory=C:/dev/ofg diff --check`.

## Contract and Quality Baseline

This plan preserves and extends the active contracts in `C:\dev\ofg\docs\API_CONTRACTS.md`.

`OFG-BOOT-001 TypeScript Host Ownership` is preserved. TypeScript may fetch static asset bytes and pass them to the WASM facade, but it must not parse glTF, decide scene hierarchy, compute animations, skin meshes, own renderer resources, or choose gameplay state.

`OFG-BOOT-002 C++ Runtime Ownership` is extended. C++ will own glTF parsing, imported model resources/templates, model scene instantiation, entity transforms, mesh-renderer skin bindings, animation clips, animation players, player animation controllers, CPU skinning, and player animation selection. `Game` remains the high-level owner that decides which scene/model is active.

`OFG-BOOT-003 WASM Facade` is changed narrowly. The facade may gain byte-oriented model loading entry points and debug status fields for model-loading state. It must not expose raw scene pointers, mesh pointers, GPU handles, model-instance internals, skin bindings, or renderer pass internals to TypeScript.

`OFG-BOOT-004 Renderer Compatibility` and `OFG-BOOT-005 WebGPU Baseline` are intentionally updated by the PBR shader milestone. Browser/native smoke must stay visually aligned and must continue to request no optional WebGPU features. Smoke thresholds may change because the demo/player visuals change, but updates require screenshot evidence.

`OFG-BOOT-006 Resource Lifetime` is critical. Imported textures, materials, shaders, meshes, and pipelines must be durable resources. Animation may update transforms every frame. CPU skinning may update existing dynamic vertex-buffer contents every frame, but it must not recreate vertex/index buffers during ordinary steady-state frames.

`OFG-BOOT-009 Coverage` applies. Each modified implementation file must pass the default coverage attention gate, currently about 90% line coverage, unless this plan records a file-specific exception with rationale and compensating smoke evidence. This plan records `cpp\src\assets` as the current glTF importer exception, with fixture/audit/browser/native smoke coverage as compensation.

Milestone 0 adds a new lifetime-safety contract: stored non-owning references between `Object`-derived types should use `Ptr<T>` generously unless the plan records a narrower exception. `Ptr<T>` is not an ownership mechanism and must not replace `Scene` entity/component storage or `Resources` resource storage. Destroying an `Object` must invalidate registered pointers without throwing; dereferencing an invalidated pointer must throw `EngineError` with a message that names the pointer label or target type where possible.

Stored observer fields should use `Ptr<T>`; local borrows and hot loops should not. A system may resolve a stored `Ptr<T>` once at the edge of a frame/update step, validate it, then use raw references, raw pointers, or spans inside tight renderer, animation, and skinning loops. The plan must not introduce per-frame `Ptr<T>` copy/register/unregister churn in draw-list extraction, animation sampling, or CPU skinning.

Milestone 0 pointer migration table:

| Area | Milestone 0 treatment | Rationale |
| --- | --- | --- |
| `Object` base | Added for `Entity`, `Component`, `MeshRenderer`, `Camera`, `Player`, `Mesh`, `Material`, `Texture`, and `Shader`; future `AnimationPlayer` and `ModelResource` should derive when introduced. | These are the current referenceable scene/resource/runtime objects that model loading will store or return by reference. |
| Owner containers | `Scene` entity/component vectors and `Resources` resource vectors remain `std::unique_ptr` owner storage. | `Ptr<T>` is non-owning and must not replace ownership. |
| Resource construction | `Resources::create_*` constructs resources directly in owned `unique_ptr` slots; resource move constructors/assignments are deleted. | `Object` reference lists require stable target addresses. |
| Converted stored observers | `Component::m_entity`, `Scene::m_main_camera`, `MeshRenderer::m_mesh`, `Material::m_shader`, `SubMesh::m_default_material`, `MaterialOverride::m_material`, and texture-valued `PropertyBag` entries use `Ptr<T>`. | These references can persist outside one local call and should self-null when the target owner destroys the object. |
| Public getter/parameter borrows | Existing APIs still accept and return raw pointers such as `MeshRenderer::set_mesh(Mesh*)` and `mesh()`. | Callers get ergonomic immediate borrows while storage behind the API is lifetime-aware. |
| Draw-list extraction | `DrawCommand::m_mesh`, `DrawCommand::m_properties`, and spans remain raw transient borrows. | Draw commands are rebuilt and consumed within one render operation, and avoiding `Ptr<T>` copies keeps render extraction out of pointer-registration hot paths. |
| Entity tree links | `Entity` parent/child/sibling links and entity-owned typed component backlinks remain raw owner-internal links. | They are maintained inside one `Scene` generation and destroyed together; individual entity deletion can revisit this with a generation-aware deletion policy. |
| Temporary demo scene bindings | `DemoScene` cached resource/entity/component pointers remain raw for now. | The current demo scene is reset before resource release, scene bindings are generation-checked, and upcoming `ModelInstance` work should introduce `Ptr<T>`-based returned bindings rather than copying this temporary pattern. |

The repo readability rules in `C:\dev\ofg\AGENTS.md` and `C:\dev\ofg\docs\GUIDES.md` apply: files need purpose comments, functions need comments/docstrings, large functions over 50 lines need internal comments, and C++ uses the repo clang-format config.

## Context and Orientation

The repository root is `C:\dev\ofg`.

Current scene graph code lives under `C:\dev\ofg\cpp\include\ofg\scene` and `C:\dev\ofg\cpp\src\scene`. `Scene` owns a root entity tree and flat component vectors for `MeshRenderer`, `Camera`, `Player`, `PlayerAnimationController`, and `AnimationPlayer`, plus a main directional light and ambient light. `Entity` owns local transform data and child/sibling links. `Renderer::render` walks scene mesh renderers, resolves each entity world transform with `world_from_local`, and builds a transient `DrawList`.

Current resources live under `C:\dev\ofg\cpp\include\ofg\resources` and `C:\dev\ofg\cpp\src\resources`. `Resources` owns stable vectors of `Texture`, `Shader`, `Material`, and `Mesh`. `Mesh` stores CPU vertices/indices/submesh ranges and creates WebGPU vertex/index buffers. `Material` validates a `PropertyBag` against a `ShaderParameterLayout` and creates material bind groups dynamically.

Current rendering uses one opaque pass in `C:\dev\ofg\cpp\src\render\opaque_pass.cpp`. Frame and draw bind groups are fixed matrix uniforms. Materials bind a base color factor and base color texture. The current WGSL source is `C:\dev\ofg\cpp\src\render\shaders\opaque_uber.wgsl.hpp`.

The current visible player is a temporary scaled cube created in `C:\dev\ofg\cpp\src\render\demo_scene.cpp`. `Player` movement lives in `C:\dev\ofg\cpp\src\scene\player.cpp`; camera modes live in `C:\dev\ofg\cpp\src\scene\camera.cpp`.

The test assets are under `C:\dev\ofg\assets\models`. Small feature fixtures live under `assets\models\tests`; larger Quaternius player and animation library GLBs live under `assets\models\player`.

Definitions used in this plan:

glTF means the Khronos JSON-based glTF 2.0 format. GLB means the binary container form of glTF. A glTF node is a transform in a hierarchy; an OFG entity is the local scene equivalent.

`Object` means the OFG base class for pointer-stable, referenceable runtime objects. It owns an intrusive list of registered safe references so it can invalidate them during destruction.

`Ptr<T>` means a nullable, non-owning reference to an `Object`-derived type. It registers with the target object while live, unregisters on reassignment/destruction, becomes null when the target is destroyed, and throws `EngineError` when dereferenced while null or invalidated.

Initial `Object`-derived referenceable types should include `Entity`, `Component`, `MeshRenderer`, `Camera`, `Player`, `AnimationPlayer`, `Mesh`, `Material`, `Texture`, `Shader`, `ModelResource`, and any per-instance animation/controller objects stored by reference. Owner containers such as `Scene`'s component vectors and `Resources`' resource vectors remain owning `std::unique_ptr` storage.

PBR metallic-roughness means glTF core physically based material parameters: base color, metallic factor, roughness factor, optional base-color texture, optional metallic-roughness texture, optional normal texture, and related scalar factors.

Normal mapping means sampling a tangent-space normal texture in the fragment shader and transforming it by a tangent, bitangent, normal basis. This requires a tangent attribute or generated tangents.

Skinning means deforming mesh vertices by weighted joints. CPU skinning means computing skinned positions, normals, and tangents on the CPU and updating a dynamic vertex buffer before rendering.

In OFG, skeleton transform state means ordinary imported scene entities. A `SkinBinding` is optional metadata owned by a `MeshRenderer`. It points at a subset of those entities in `skin.joints` order, stores inverse bind matrices and bind-pose skinning data, and may reference a per-instance dynamic mesh for CPU-skinned output. It is not a second copy of bone transforms.

A `GltfDocument` means the parsed glTF/GLB source plus decoded buffers/images/accessors needed during import. It is allowed to know about glTF concepts and source indices. It should not be the runtime representation used for instancing.

A `ModelResource` means a reusable, compact model template created by a source-format importer such as the glTF importer. It is an OFG-owned prefab graph normalized into engine concepts: node templates, component templates, references to shared mesh/material/texture/shader resources, skin templates, animation clips, source-name/source-index maps for animation binding, and the default root node list. It should be data-only and serialization-friendly, but it is not an actual live `Scene`.

The `ModelResource` references loaded resources by stable OFG resource references, initially non-owning pointers to `Resources`-owned `Mesh`, `Material`, `Texture`, and `Shader` objects or a small wrapper around those pointers. It does not deep-copy texture pixels or mesh buffers per instance. If a later handle/asset-id system replaces raw pointers, this model resource is the layer that should adopt those handles.

An import resource context means the object passed to the glTF importer that can create or find OFG resources. It owns a small cache keyed by canonical model URI plus glTF object index, external URI, or generated fallback label. It creates meshes, materials, textures, shaders, and default fallback textures through `Resources` for runtime paths, and can create CPU-only resources for native unit tests that run without a WebGPU device.

A model instance means the result of copying that template into one live `Scene`: new entities, new mesh renderers, per-instance animation state, and per-instance skin bindings where needed. Instantiation needs an explicit remap table from model node indices to live `Entity*` values, and copy policies that duplicate scene-owned data while sharing durable resource pointers such as meshes, materials, textures, and shaders.

Animation blending means combining multiple animation clip poses for the same model instance, such as idle plus walk or walk plus sprint, using normalized clip weights.

A main light means the scene-selected directional light that PBR rendering treats as the sun-like key light. The first lighting model is one main directional light plus an ambient term.

The canonical scene update order after this plan is: player movement, player animation controllers, animation-player components, optional procedural entity/joint overrides, CPU skinning for skinned mesh renderers, camera components. `Scene::update` should own this order and tests should assert it.

## Plan of Work

Milestone 0 introduces safe non-owning references. Add `cpp/include/ofg/core/object.hpp` and `cpp/include/ofg/core/ptr.hpp`, with implementation files if needed. `Object` owns an intrusive linked list of `Ptr` reference nodes; `Ptr<T>` requires `T` to inherit `Object`, supports default/null construction, copy, move, assignment, reset, `get`, `operator bool`, `operator*`, and `operator->`; and throws `EngineError` with a clear message on invalid access. Destroying `Object` must walk its reference list and null every registered pointer without throwing. Convert the first referenceable classes to inherit `Object`: `Entity`, `Component` and concrete subclasses, `Mesh`, `Material`, `Texture`, `Shader`, and later `ModelResource`/animation objects that are returned or stored by reference. Because `Object` is non-copyable and non-movable, update `Resources` so resource objects are constructed directly in owned `unique_ptr` storage instead of constructed as temporaries and moved. Keep `Scene`'s entity/component vectors and `Resources`' resource vectors as owning `unique_ptr` storage; do not convert owner containers to `Ptr<T>`. Add a migration table before editing stored pointer fields. The initial conversion scope should include persistent observer fields used by model instancing, such as `MeshRenderer::m_mesh`, material overrides, submesh default materials, returned `ModelInstance` references, and future skin/animation bindings. Explicitly defer transient `DrawCommand` and draw-list extraction fields unless implementation finds a concrete stale-reference risk, because those are rebuilt and consumed immediately during one render. Keep short-lived local borrows as raw pointers or references when the lifetime is immediate and obvious. Add focused tests proving pointers copy/move/register/unregister correctly, object destruction nulls every reference, dereference throws after destruction or when null, and scene/resource deletion does not leave stale stored references.

Milestone 1 integrates tinygltf and builds a parse/accessor foundation without rendering anything new. Add a small CMake target or include path for `C:\dev\ofg\cpp\third_party\tinygltf`, preferably a private `ofg_tinygltf` target that compiles `tiny_gltf.cc` without OFG warning flags. Add `cpp/include/ofg/assets/gltf_document.hpp`, `cpp/src/assets/gltf_document.cpp`, and focused tests. The public OFG interface should expose only OFG-owned data and diagnostics, not tinygltf classes. Implement loading from bytes and from native filesystem paths for tests. The parser should support GLB memory, glTF JSON memory, embedded data URIs, and external sibling buffers/images through a resource-provider abstraction. Add a test helper for asset paths, likely a CMake compile definition such as `OFG_TEST_ASSET_DIR`. Tests should prove `static-box.glb`, `animated-cube.gltf`, `simple-skin.gltf`, and `rigged-simple.glb` can be parsed or intentionally rejected with clear diagnostics. Address both `animated-cube.gltf` fixture issues here: its buffer URI references `AnimatedCube.bin` while the checked-in file is `animated-cube.bin`, and its image URI references `AnimatedCube_BaseColor.png`, which is not checked in. Either correct the fixture, supply the missing files, or make the test provider intentionally map/skip these known fixture resources with a recorded rationale.

Milestone 1A audits target assets before relying on them. Use the parse layer to inspect the Quaternius player and animation-library GLBs without rendering them. Record required extensions, optional extensions, material texture types, missing attributes, whether tangents are present, animation interpolation modes, animation clip names, node and joint names, skin names, vertex/index counts, texture sizes, and rough decoded memory estimates. Compare the chosen player model's skeleton names against `quaternius-ual1-standard.glb` and `quaternius-ual2-standard.glb` animation skeleton names. This audit should decide the default player asset for Milestone 8, currently `quaternius-superhero-male.glb` unless the audit shows it is not compatible, and should list the exact fallback choice if needed.

Milestone 2 imports static model structure into OFG reusable model resources and scene instances, with the acceptance case "load a cube from glTF and copy it N times into one scene." Add `cpp/include/ofg/assets/gltf_importer.hpp` and `cpp/src/assets/gltf_importer.cpp`, or similarly named files. Add an import resource context/cache that creates and deduplicates meshes, materials, textures, shaders, and fallback textures through `Resources`, initially by canonical asset URI plus glTF index or generated label. Convert glTF scenes and nodes into a compact, format-neutral `ModelResource` that stores node and component templates rather than live entities. This template should not just echo tinygltf; it should contain OFG-ready local transforms, parent indices, root node indices, mesh-renderer templates, resource references, and source index/name maps needed later for animation. Node transforms must support both TRS and glTF `matrix`: decompose matrix values into OFG local transform when they are affine and decomposable, and reject unsupported shear/non-decomposable matrices with a clear test-covered error. Convert glTF mesh primitives into shared `Mesh` resources with one `SubMesh` per primitive and default materials assigned from imported material resources. Add an instantiation API that uses a `ModelInstantiationContext` to copy the model template under a supplied parent entity in a target `Scene`, creating one entity per node and one `MeshRenderer` per mesh-bearing node. The context should own remap tables from model node indices to live entities and should apply a clear copy policy: entities/components are duplicated into the destination scene, while durable resource pointers such as meshes, materials, textures, and shaders are shared. Decode accessors with byte offsets, buffer-view offsets, byte strides, scalar/vector types, and index component types `UNSIGNED_BYTE`, `UNSIGNED_SHORT`, and `UNSIGNED_INT`. Initially support triangle-list primitives only; reject points, lines, triangle strips, sparse accessors, morph targets, and required unsupported extensions with clear `EngineError` messages. Import base-color textures and factors enough to render `static-box.glb` through the existing material path before PBR lands. Add C++ tests that load one static cube model resource, instantiate it at least five times with different parent/local transforms, and prove entity transforms are per-instance while meshes/materials/textures are shared. GPU instancing and draw batching are explicitly out of scope.

Milestone 3 expands the renderer and resources to PBR metallic-roughness in two steps. First add a PBR-compatible base material path with base color, metallic factor, roughness factor, fallback white texture, fallback neutral metallic-roughness texture, and one main directional light plus ambient term. Store the main directional light selection on `Scene`, even if the first implementation creates a single default light for every demo scene. Then add normal map support with a tangent policy informed by the Milestone 1A audit: import glTF `TANGENT` when present, generate tangents from positions/UVs/normals when a normal map exists and tangents are absent, or disable/reject normal mapping with a clear error when tangent generation is impossible. Change `MeshVertex` to carry real position, normal, tangent, and UV data, update `PipelineCache` vertex attributes, and update generated demo geometry to supply real normals and deterministic tangents. Missing normals should be generated for supported triangle meshes or rejected with a clear error; missing UVs should default to zero only when the material does not require texture coordinates. Base-color textures must use sRGB texture format; metallic-roughness and normal textures must use linear format. Keep generated demo materials working by providing default white, neutral metallic-roughness, and flat-normal textures. Validate through CPU tests for material property layouts plus browser/native smoke screenshots showing the existing scene still renders, then `static-box.glb` renders with a PBR material.

Milestone 4 loads skin metadata while using imported nodes as the skeleton. Preserve the glTF node hierarchy by instantiating entities for every node, including nodes with no mesh. Do not create a separate hidden bone transform tree, and do not add joint/bone components just to hold mesh-renderer metadata. For every mesh-bearing node that references a glTF skin, populate an optional `SkinBinding` member on that node's `MeshRenderer`. The binding stores ordered joint entity references in `skin.joints` order, inverse bind matrices, glTF `skin.skeleton` as the optional skeleton root, and any skin-template data needed by later CPU skinning. Define the final `SkinBinding` shape in this milestone so Milestone 6 fills in dynamic skinning resources rather than redesigning ownership. Tests using `simple-skin.gltf` and `rigged-simple.glb` should assert joint count, parent/child structure, joint entity identity, inverse bind matrix decode, attachment-friendly child behavior, mesh-renderer skin-binding ownership, and multi-instance independence. No vertex deformation is required in this milestone.

Milestone 5 imports animation data and plays one clip at a time through an `AnimationPlayer` component. Add animation data structures, likely under `cpp/include/ofg/animation` and `cpp/src/animation`: `AnimationClip`, `AnimationSampler`, `AnimationChannel`, `AnimationPlayer`, and interpolation helpers. Extend `ComponentType`, `Scene`, and `Entity` so `Scene` owns `AnimationPlayer` components in pointer-stable storage, and `Scene::update` runs animation players after `Player` components and before skinning/cameras. Decode glTF animation samplers for translation, rotation, and scale channels. Support `STEP` and `LINEAR` interpolation first; add `CUBICSPLINE` only if a current fixture or Quaternius clip requires it, otherwise reject it with a test. Name binding and source-node lookup happen at import/instantiation time; runtime updates use pre-bound target indices, sampler cursors, and preallocated pose buffers. Evaluate each pose from the imported base/rest local transform every frame rather than accumulating deltas. Rotation interpolation uses normalized quaternion interpolation with shortest-path sign handling. Targets not affected by a clip retain their rest pose unless another blended clip contributes. Tests should play `animated-cube.gltf` or `box-animated.glb` at known times and compare entity transforms. Tests should also prove a joint entity can be changed after animation evaluation and before skinning, establishing the future IK/control hook. Keep this milestone native C++ focused unless a minimal browser asset transport has already landed.

Milestone 6 implements CPU skinning through mesh-renderer-owned state. Extend `MeshRenderer` with optional `SkinBinding` data that keeps bind-pose vertices, joint indices, joint weights, the source shared bind-pose mesh, ordered joint entity references, inverse bind matrices, and a per-instance dynamic vertex buffer or dynamic mesh resource used by the renderer. Clarify memory ownership: bind-pose mesh/index/material resources are shared by `ModelResource`, while skinned vertex output and scratch buffers are per skinned instance. Extend `Mesh` with a fixed-capacity dynamic vertex update method that writes into an existing `COPY_DST | VERTEX` WebGPU vertex buffer with `wgpuQueueWriteBuffer` and updates CPU vertices without implicit resize or buffer recreation. Decode `JOINTS_0` and `WEIGHTS_0` for supported component types, normalize weights safely, and compute skin matrices from current joint entity world transforms plus inverse bind matrices. Add a per-frame world-transform cache or generation system so each entity world matrix is computed once per update and reused by all skinned mesh renderers. For a CPU-skinned mesh whose renderer still applies the mesh entity world matrix, compute local skin matrices as `inverse(world_from_mesh_node) * world_from_joint_entity * inverse_bind_matrix` so the skinned mesh node transform is not double-applied. Skin positions, normals, and tangents when present; when normals/tangents are absent, preserve the chosen Milestone 3 default/rejection policy. The skinning path must preallocate scratch buffers, avoid heap allocation after warm-up, avoid copying/registering `Ptr<T>` in the inner loop, skip work when no relevant pose changed, and report upload bytes plus dynamic-buffer create counters. Tests should evaluate `simple-skin.gltf` at rest, at a known animated time, and after a manual joint transform override, then compare skinned CPU vertices. Add a resource-lifetime test proving repeated skinning does not allocate replacement vertex buffers in ordinary frames, and a small multiple-skinned-instance test to expose per-instance memory growth. GPU skinning is explicitly future work.

Milestone 7 adds generic animation blending, then player locomotion wiring only after compatible player clips have been proven by the audit or a successful binding test. Extend `AnimationPlayer` to hold multiple active clip states with local time, playback speed, looping, and weight. Blend translation and scale linearly, blend rotation with normalized quaternion interpolation and shortest-path sign handling, and normalize final weights per target. Runtime blending uses pre-bound target arrays and preallocated pose buffers. Add tests with synthetic clips and, if practical, Quaternius clip samples. Add a `PlayerAnimationController` component or equivalent controller owned by the player model root only after idle/walk/sprint clip names and skeleton binding are known. The controller reads the `Player` component's current movement speed and maps it to idle, walk, and sprint clip weights. The controller should not move the player root through root motion in this milestone; the existing `Player` component continues to own world movement.

Milestone 8 integrates the Quaternius player model into the browser scene. Add browser byte transport APIs with an explicit state machine: TypeScript fetches model files or a manifest from `assets/models/player`, passes `Uint8Array` bytes to `BrowserGame`, `BrowserGame` queues bytes if `Game` is not ready, C++ imports/caches a `ModelResource`, and scene mutation happens on the C++ runtime side at a controlled preparation/update point. Prefer GLB files for the browser path so runtime player assets do not depend on multiple external fetches at first. Release TypeScript byte arrays, WASM source-byte copies, and decoded `GltfDocument` buffers/images once OFG resources are created and the `ModelResource` no longer needs them. Increase and validate WASM memory for game-scale assets; evaluate whether to raise `INITIAL_MEMORY`, enable controlled memory growth, or both, and record the chosen budget before loading multiple large GLBs. Package `assets/models` into `.deploy`, add appropriate `_headers` cache policy for model files, and validate with `npm run package:site` or `npm run build:cloudflare`. Load the audit-selected player GLB, defaulting to `quaternius-superhero-male.glb` if compatible. Load animation clips from the audit-selected animation library, bind them by skeleton/node names to the player model, and record a clear diagnostic if names do not match. The imported model should be parented under an unscaled visual child/model root of the existing player entity. Disable or remove the temporary cyan cube `MeshRenderer` when the model succeeds; keep the cube renderer as a fallback if model loading fails. The visual child owns offset, scale, and orientation correction so player movement/camera follow continue to use the existing `Player` entity. Capture screenshots of idle, walk, and sprint states.

Milestone 9 performs final consolidation, not deferred contract cleanup. Earlier milestones must update `C:\dev\ofg\docs\API_CONTRACTS.md` and `C:\dev\ofg\docs\SYSTEMS.md` when they change ownership or public interfaces. This final milestone audits those docs for drift, updates fixture source notes if the `AnimatedCube.bin` or missing image issues are corrected, runs milestone review, formatting, unit tests, smoke tests, packaging validation, and coverage. Refresh committed coverage summaries under `docs\coverage` only after coverage gates pass.

## Concrete Steps

Run all commands from `C:\dev\ofg` unless a command says otherwise.

Start each work session by checking the worktree and rereading this plan:

    git status --short
    Get-Content -Raw docs\plans\gltf-model-loading-animation-plan.md

After Milestone 0:

    npm run format:cpp
    npm run format:cpp:check
    npm run test:cpp

Expected result: `Object`/`Ptr<T>` unit tests pass, scene/resource tests pass after selected raw stored pointers are converted, and CTest reports no stale-reference crashes.

After dependency/CMake edits:

    npm run test:cpp

Expected result: CMake configures, builds, and CTest runs with tinygltf available but no behavior changed yet.

After Milestone 1A:

    npm run test:cpp

Expected result: metadata audit tests or reports can parse the player GLBs, list material/animation/skeleton requirements, estimate memory, and record the chosen player and animation-library assets before browser integration.

After each C++ source/header batch:

    npm run format:cpp
    npm run format:cpp:check
    npm run test:cpp

Expected result: clang-format is clean and doctest/CTest passes.

After TypeScript browser asset transport changes:

    npm run test:ts

Expected result: Mocha tests pass, including byte-fetch/facade tests that do not parse assets in TypeScript.

For visual milestones, keep a local server available:

    npm run dev

Expected result: the command prints a local URL, normally `http://127.0.0.1:5173`; if that port is busy, report the next printed URL in chat.

For visual verification during renderer/model milestones:

    npm run smoke:browser
    npm run smoke:render

Expected result: browser and native smoke write PNG/report artifacts under `artifacts\browser-smoke` and `artifacts\render-smoke`. Capture additional milestone screenshots under directories such as `artifacts\gltf-static`, `artifacts\gltf-animation`, `artifacts\gltf-skinning`, and `artifacts\player-model`.

Before final acceptance:

    npm run format:cpp:check
    npm test
    npm run smoke:browser
    npm run smoke:render
    npm run package:site
    npm run coverage

Expected result: all commands pass; modified implementation files do not appear in the default coverage attention output unless this plan records an explicit exception.

## Milestone Review

After each implementation milestone:

1. Update this ExecPlan's Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective.
2. Update `C:\dev\ofg\docs\API_CONTRACTS.md` or `C:\dev\ofg\docs\SYSTEMS.md` if the milestone changed ownership or public contracts.
3. Run the repo-local `milestone-review` skill against the milestone diff and this ExecPlan before marking that milestone complete.
4. Apply required findings before marking the milestone complete, or record a rejected finding with rationale in the Decision Log.
5. Re-run relevant validation commands after applying review findings.
6. Record the review summary, commands, screenshots, artifacts, and remaining risks in Progress or Outcomes & Retrospective.

## Validation and Acceptance

The plan is complete only when all of these behaviors are true.

Milestone 0 lifetime safety is in place before glTF import begins. `Ptr<T>` tests prove null access and post-destruction access throw `EngineError`, object destruction invalidates every registered pointer, copy/move assignment preserves the intrusive reference list, and converted scene/resource references no longer crash when their targets are destroyed.

Milestone 0 includes a pointer migration table. Persistent observers needed by model instancing use `Ptr<T>` unless explicitly deferred. Owner containers and transient draw-list/render-extraction pointers remain owning storage or raw borrows with documented rationale.

Native C++ tests parse and inspect the small glTF/GLB fixtures under `C:\dev\ofg\assets\models\tests`, including static mesh data, external-buffer glTF data, skeleton data, animation data, and skinned vertex results.

Milestone 1A produces an audit of the Quaternius player and animation-library files before Milestone 8 uses them. The audit names the selected player model, selected animation library, compatible clip names, skeleton/name mismatches if any, required tangent-generation/material features, and a WASM/native memory estimate.

Unsupported glTF features fail clearly. Required unsupported extensions, sparse accessors, unsupported primitive modes, unsupported animation interpolation, or malformed resources must throw `EngineError` with messages naming the unsupported feature.

The browser and native renderer still show the existing ground/cube scene through the updated PBR path, with browser/native smoke thresholds updated only from inspected screenshots.

The imported static glTF test model can be represented as scene entities with mesh renderers and can be rendered through the C++ renderer.

The static cube fixture can be loaded once into a reusable model resource and instantiated at least five times into one `Scene`. The resulting entities and mesh renderers are distinct per instance, while the underlying mesh/material/texture resources are shared. Moving or scaling one instance must not mutate the other instances.

The imported animated cube or box animation changes scene entity transforms over time in C++ without TypeScript owning animation state.

Animation playback is owned by `AnimationPlayer` scene components, and player locomotion weights are owned by the `Player` component. Tests prove `Scene::clear` destroys animation players, invalidates returned `Ptr` values, and `Scene::update` runs in the canonical order: players, animation players, procedural overrides, CPU skinning, cameras.

The imported simple skin or rigged fixture produces deterministic CPU-skinned vertices at rest, at a known animation time, and after a manual joint entity override applied after animation evaluation. Ordinary animation frames update existing dynamic vertex-buffer contents rather than recreating vertex buffers.

CPU skinning uses preallocated scratch memory after warm-up, reports upload-byte and dynamic-buffer creation counters, does not copy/register `Ptr<T>` in inner loops, and keeps dynamic vertex buffer creation counts flat during ordinary animation frames.

The browser scene can load a Quaternius player GLB as the player visual. In third-person mode, W/A/S/D movement drives the existing C++ `Player` component and the visible model blends between idle, walk, and sprint animations according to player movement speed.

Browser model loading has a debug-status state machine, retry/fallback behavior, and a memory budget recorded in docs or the plan. Deployment packaging includes the selected runtime model files under `assets/models/player`, and `npm run package:site` verifies model files are present in `.deploy`.

TypeScript remains a byte transport and host shell. It does not parse glTF JSON, create scene nodes, choose animation poses, skin meshes, or access renderer resources.

Final validation commands that must pass:

    npm run format:cpp:check
    npm test
    npm run smoke:browser
    npm run smoke:render
    npm run package:site
    npm run coverage

Screenshot acceptance: capture and present screenshots after the first rendered imported static model, after the first visible PBR/normal-map result, after animation playback, after CPU-skinned model playback, and after final player idle/walk/sprint behavior. Store durable screenshots under `C:\dev\ofg\artifacts` with clear subdirectory names.

## Idempotence and Recovery

tinygltf is vendored as copied source. If the dependency snapshot needs to be refreshed, replace only files under `C:\dev\ofg\cpp\third_party\tinygltf`, update `SOURCE.md` with the upstream commit, and run `npm run test:cpp`.

The importer should be additive and retryable. If a glTF feature is not supported yet, prefer a clear `EngineError` over partial imports that leave scene/resources half-mutated. Import into temporary OFG-owned model-resource structures first, then instantiate into a live scene only after validation succeeds where practical.

If model instantiation starts to look like general scene serialization, keep the early implementation narrow: template data plus explicit copy context and remap tables. Record any missing general serialization needs as a follow-up instead of freezing a broad scene save/load format during glTF import.

If `Object` adoption exposes too much code churn at once, keep the Milestone 0 rule focused on referenceable stored cross-owner relationships that glTF/model instancing will depend on. Do not convert math/value types, transient stack-only helpers, or owner containers merely for uniformity. Record any intentionally deferred raw pointer fields with rationale before moving to Milestone 1.

If browser asset loading fails, keep the temporary player box fallback and report the model-loading failure through debug status. Do not make the whole runtime unusable because an optional player model failed to fetch or parse.

If browser model loading exceeds the current WASM memory budget, stop before broad player integration and record the measured allocation pressure. Prefer a deliberate Emscripten memory setting change, such as a larger `INITIAL_MEMORY` and/or controlled `ALLOW_MEMORY_GROWTH`, over trial-and-error changes hidden inside Milestone 8.

If deployment packaging omits model assets or sets poor cache headers, fix packaging before marking browser player integration complete. Model assets should be included intentionally; do not rely on local dev server behavior as proof that Cloudflare Pages output is correct.

If PBR smoke thresholds fail, inspect the generated screenshots before changing `tools\smoke-contract.json`. Update thresholds only when the image shows the intended scene and the change is due to expected visual differences.

If the Quaternius animation library skeleton names do not match the chosen player model, add a diagnostic tool/test that lists missing and extra names, record the mismatch in Surprises & Discoveries, and either choose a matching model/clip pair or add a small explicit retarget map with tests.

If CPU skinning becomes too slow or large, keep the CPU implementation correct for the player target and record a future GPU-skinning plan. Do not switch to GPU skinning inside this plan unless the user explicitly changes scope.

If native smoke needs model assets, use a filesystem resource provider. Native smoke does not need to imitate the TypeScript fetch path; browser smoke covers the fetch and WASM transport boundary.

## Artifacts and Notes

Dependency snapshot:

    C:\dev\ofg\cpp\third_party\tinygltf\SOURCE.md
    Upstream commit: a434ee02066c2d9b62a3504876aed38e6e399fe0

Important fixture paths:

    C:\dev\ofg\assets\models\tests\static-box.glb
    C:\dev\ofg\assets\models\tests\animated-cube.gltf
    C:\dev\ofg\assets\models\tests\animated-cube.bin
    C:\dev\ofg\assets\models\tests\box-animated.glb
    C:\dev\ofg\assets\models\tests\simple-skin.gltf
    C:\dev\ofg\assets\models\tests\rigged-simple.glb
    C:\dev\ofg\assets\models\tests\material-specular-glossiness-13.glb
    C:\dev\ofg\assets\models\player\quaternius-superhero-male.glb
    C:\dev\ofg\assets\models\player\quaternius-superhero-female.glb
    C:\dev\ofg\assets\models\player\quaternius-ual1-standard.glb
    C:\dev\ofg\assets\models\player\quaternius-ual2-standard.glb

Planned screenshot artifact directories:

    C:\dev\ofg\artifacts\gltf-static
    C:\dev\ofg\artifacts\gltf-pbr
    C:\dev\ofg\artifacts\gltf-animation
    C:\dev\ofg\artifacts\gltf-skinning
    C:\dev\ofg\artifacts\player-model

## Interfaces and Dependencies

The exact names may evolve during implementation, but these concepts should exist at the end.

`C:\dev\ofg\cpp\include\ofg\core\object.hpp` and `C:\dev\ofg\cpp\include\ofg\core\ptr.hpp` should expose the safe reference foundation, conceptually:

    namespace ofg {
    class Object {
    public:
        Object(const Object&) = delete;
        Object& operator=(const Object&) = delete;
        Object(Object&&) = delete;
        Object& operator=(Object&&) = delete;
        virtual ~Object() noexcept;

    protected:
        Object() noexcept;
    };

    template <typename T>
    class Ptr {
    public:
        static_assert(std::is_base_of_v<Object, T>);

        Ptr() noexcept;
        Ptr(std::nullptr_t) noexcept;
        explicit Ptr(T* object);
        Ptr(const Ptr& other);
        Ptr(Ptr&& other) noexcept;
        Ptr& operator=(const Ptr& other);
        Ptr& operator=(Ptr&& other) noexcept;
        ~Ptr();

        void reset() noexcept;
        [[nodiscard]] T* get() const noexcept;
        [[nodiscard]] explicit operator bool() const noexcept;
        [[nodiscard]] T& operator*() const;
        [[nodiscard]] T* operator->() const;
    };
    }

`Object::~Object()` must invalidate every registered `Ptr` without throwing. `Ptr::operator*` and `Ptr::operator->` must throw `EngineError` when the pointer is null or invalidated.

`C:\dev\ofg\cpp\third_party\tinygltf` contains the vendored tinygltf snapshot. The implementation must define `TINYGLTF_IMPLEMENTATION`, `STB_IMAGE_IMPLEMENTATION`, and `STB_IMAGE_WRITE_IMPLEMENTATION` in exactly one translation unit, either the copied `tiny_gltf.cc` or one OFG wrapper source.

`C:\dev\ofg\cpp\include\ofg\assets\gltf_document.hpp` should expose an OFG-owned parse entry point, conceptually:

    namespace ofg {
    struct AssetFile {
        std::string m_path;
        std::vector<std::byte> m_bytes;
    };

    class GltfResourceProvider {
    public:
        virtual ~GltfResourceProvider() = default;
        virtual std::optional<AssetFile> load_relative(std::string_view uri) = 0;
    };

    struct GltfDocument {
        std::string m_label;
        // OFG-owned summary and import-ready data; no public tinygltf types.
    };

    GltfDocument load_gltf_document(std::string label,
        std::span<const std::byte> primary_bytes,
        GltfResourceProvider& resources);
    }

`C:\dev\ofg\cpp\include\ofg\assets\model_resource.hpp` should expose reusable, format-neutral model-resource data, conceptually:

    namespace ofg {
    struct ModelNodeTemplate {
        std::string m_name;
        std::uint32_t m_source_node_index{0};
        std::int32_t m_parent_index{-1};
        LocalTransform m_local_transform;
    };

    struct MeshRendererTemplate {
        std::uint32_t m_node_index{0};
        Ptr<Mesh> m_mesh;
        std::vector<MaterialOverride> m_material_overrides;
        std::optional<std::uint32_t> m_skin_template_index;
    };

    struct SkinTemplate {
        std::vector<std::uint32_t> m_joint_node_indices;
        std::vector<math::Mat4> m_inverse_bind_matrices;
        std::optional<std::uint32_t> m_skeleton_root_node_index;
    };

    class ModelResource {
    public:
        std::span<const std::uint32_t> root_node_indices() const noexcept;
        std::span<const ModelNodeTemplate> nodes() const noexcept;
        std::span<const MeshRendererTemplate> mesh_renderers() const noexcept;
        std::span<const SkinTemplate> skins() const noexcept;
        std::span<const AnimationClip> animation_clips() const noexcept;
    };
    }

`C:\dev\ofg\cpp\include\ofg\assets\gltf_importer.hpp` should expose model-resource creation with a temporary loader/import cache, conceptually:

    namespace ofg {
    class ModelResourceLoader {
    public:
        ModelResourceLoader();
        ModelResourceLoader(std::string source_uri, std::string model_name);
        void update(ModelResource& target);
        [[nodiscard]] Texture& get_or_create_texture(std::string cache_key, ...);
        [[nodiscard]] Material& get_or_create_material(std::string cache_key, ...);
        [[nodiscard]] Mesh& get_or_create_mesh(std::string cache_key, ...);
        [[nodiscard]] Shader& pbr_shader();
        [[nodiscard]] Texture& default_white_texture();
        [[nodiscard]] Texture& default_metallic_roughness_texture();
        [[nodiscard]] Texture& default_normal_texture();
    };

    struct GltfImportOptions {
        std::string m_model_name;
        std::string m_source_uri;
        bool m_allow_unsupported_optional_extensions{true};
    };

    struct ModelInstance {
        Ptr<Entity> m_root_entity;
        Ptr<AnimationPlayer> m_animation_player;
        std::vector<Ptr<Entity>> m_entities_by_node_index;
        std::vector<Ptr<MeshRenderer>> m_mesh_renderers;
    };

    std::unique_ptr<ModelResource> import_gltf_model_resource(const GltfDocument& document,
        const GltfImportOptions& options,
        ModelResourceLoader& loader);

    ModelInstance instantiate_model_resource(const ModelResource& resource,
        Scene& scene,
        Entity& parent);
    }

`MeshVertex` should carry attributes needed by the PBR shader:

    namespace ofg {
    struct MeshVertex {
        std::array<float, 3> m_position{};
        std::array<float, 3> m_normal{};
        std::array<float, 4> m_tangent{};
        std::array<float, 2> m_uv{};
    };
    }

`Mesh` should expose fixed-capacity dynamic vertex support for skinning:

    namespace ofg {
    class Mesh {
    public:
        void init_dynamic_vertices(std::vector<MeshVertex> vertices,
            std::vector<std::uint32_t> indices,
            std::vector<SubMesh> submeshes);
        void update_vertices_in_place(std::span<const MeshVertex> vertices);
        [[nodiscard]] bool is_dynamic_vertex_mesh() const noexcept;
    };
    }

`Scene` lighting should expose one main directional light plus ambient term for the first PBR path:

    namespace ofg {
    struct DirectionalLight {
        math::Vec3 m_direction{0.0f, -1.0f, 0.0f};
        math::Vec3 m_color{1.0f, 1.0f, 1.0f};
        float m_intensity{1.0f};
    };

    class Scene {
    public:
        void set_main_light(DirectionalLight light);
        [[nodiscard]] const DirectionalLight& main_light() const noexcept;
        void set_ambient_light(math::Vec3 color, float intensity);
    };
    }

Animation interfaces should remain C++ owned, and `AnimationPlayer` should be a scene component:

    namespace ofg {
    enum class AnimationTargetPath {
        Translation,
        Rotation,
        Scale,
    };

    class AnimationClip;

    class AnimationPlayer : public Component {
    public:
        explicit AnimationPlayer(Entity* entity) noexcept;
        void play(AnimationClip& clip, bool loop = true);
        void set_clip_state(AnimationClip& clip, float weight, bool loop = true, float playback_speed = 1.0f);
        void set_clip_weight(AnimationClip& clip, float weight);
        void update(const SceneUpdateContext& context);
    };

    class PlayerAnimationController : public Component {
    public:
        void bind(Player& player, AnimationPlayer& animation_player);
        void set_locomotion_clips(AnimationClip& idle_clip, AnimationClip& walk_clip, AnimationClip& sprint_clip);
        void update(const SceneUpdateContext& context);
    };
    }

`SkinBinding` should be optional metadata owned by `MeshRenderer`, not a separate component and not a separate bone transform owner:

    namespace ofg {
    struct SkinBinding {
        Ptr<Mesh> m_bind_pose_mesh;
        std::unique_ptr<Mesh> m_dynamic_skinned_mesh;
        std::vector<Ptr<Entity>> m_joints_in_skin_order;
        std::vector<math::Mat4> m_inverse_bind_matrices;
        Ptr<Entity> m_skeleton_root;
    };

    class MeshRenderer : public Component {
    public:
        SkinBinding* skin_binding() noexcept;
        const SkinBinding* skin_binding() const noexcept;
        void set_skin_binding(SkinBinding binding);
        void clear_skin_binding() noexcept;
    };
    }

Scene update order should be explicit and tested. The canonical order after this plan is:

    1. Player movement components.
    2. PlayerAnimationController components.
    3. AnimationPlayer components.
    4. Procedural entity/joint overrides.
    5. CPU skinning for skinned MeshRenderer components.
    6. Camera components.

This preserves same-frame camera follow, leaves a future IK hook, supports rigid attachments under joint entities, and ensures rendering sees current skinned vertices.

Browser-facing APIs should be blob-oriented and narrow. TypeScript should poll C++ for opaque blob ids and URIs, fetch those bytes, and complete or fail the blob id. C++ chooses model URIs, calls `Resources::load_model_resource`, resolves glTF dependencies, and owns importing. No TypeScript code should inspect glTF JSON, skeletons, animation channels, or material fields.
