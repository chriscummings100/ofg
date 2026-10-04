# State and scene foundations

The GPU-independent `ofg-core` library contains the imported state machine, entity hierarchy, component base, observer pointers and CPU math. Both application targets link it. The separate [resource foundation](resources.md) uses shared asset ownership alongside the scene. The default procedural scene exercises these systems on both hosts; the native [core tests](../tests/scene-test.cpp) and [rendering contracts](../tests/scene-rendering-test.cpp) cover their behavior.

## State flow

[State](../src/state/state.h) owns one primary child and any indexed substates through `std::unique_ptr`. Subclass it and implement the protected enter/main/leave hooks. Drive the root with `update()` from application code. A hook returns false to continue on a later update; returning true completes that phase. Enter-start/end and leave-start/end are single-shot hooks.

Descendants begin after the parent's entry completes. Active children update before the parent main hook; substates run in ascending index order. Replacing a child requests its departure, waits for its leave phase, then activates the replacement. If several replacements are requested while waiting, the newest pending state wins. `leave()` discards pending states and drains active descendants before running the parent's leave hooks. A leave request during entry waits for entry to complete and skips main.

`setInhibitControlOnChild(true)` suppresses the parent's main hook while its primary child is active. Indexed substates do not trigger this inhibition. `spawnSibling()` is valid only on the active primary child. Null children, negative substate indices, spawning during departure, recursive updates of the same state and runaway immediate transitions report `EngineError`. The per-update transition budget is 1024 per state.

Pass actual unique ownership when spawning. Returned pointers/references are borrowed and may expire when pending replacements are overwritten or descendants finish. State is not an Object and does not support Ptr observation. Destruction releases storage without calling leave hooks: request leave and continue updating when orderly application cleanup is required. Hooks may capture application context; there is no event bus, built-in clock or resource-loading dependency. The full lifecycle examples are in [state-machine-test.cpp](../tests/state-machine-test.cpp).

## Scene ownership and transforms

[Scene](../src/scene/scene.h) owns individually allocated [Entity](../src/scene/entity.h) objects, beginning with root ID zero. `createEntity(parent)` appends to its parent's child/sibling chain. Allocations remain at stable addresses as the scene grows. IDs are lookup indices within that scene generation, not globally unique or deletion-safe handles.

Each entity holds position, quaternion rotation and scale. `parentFromLocal` composes translation * rotation * scale. `worldTransform()` lazily caches every ancestor, including the root transform; `worldFromLocal` delegates to that cache. Local transform access is read-only. Use `setLocalTransform`, `setLocalPosition`, `setLocalRotation`, or `setLocalScale` to invalidate the entity and descendants. Setters reject non-finite position/scale and normalize a nonzero finite quaternion. Zero geometry scale is permitted. `Scene::update()` refreshes dirty matrices; reads are also correct before update. Math retains the backup's float, column-vector, Y-up, left-handed, +Z-forward convention. Projection helpers use depth [0, 1]. These local transforms do not solve large-world precision; global terrain addressing remains future work. Use `packMat4` for an explicit column-major array rather than assuming named vector members form a flat C++ array.

`clear()` destroys entities, increments the generation counter and creates a new root with reset IDs. Moves preserve transferred entity addresses and rebind their owning scene; move assignment destroys the destination's previous entities. A moved-from scene has no root until cleared. Generation counters are local reset counters and are copied by moves, not universal lifetime tokens.

There is no individual deletion or reparenting. Components remain passive; State drives behavior and Game calls the resource/state/scene/render phases. Raw entity pointers passed to APIs must still be live; ownership checks do not make dangling raw pointers safe.

## Observation and components

[Object](../src/core/object.h) and [Ptr](../src/core/ptr.h) retain the old small intrusive observer list. A Ptr registers with its target, becomes null on target destruction, and throws `EngineError` if dereferenced while null. It never owns or extends the target's lifetime. Use unique ownership for storage, raw pointers/references for short-lived access, and Ptr when a stored reference must notice destruction. These facilities are single-threaded; they are not shared/weak reference counting or GPU resource handles.

```cpp
ofg::Scene scene;
ofg::Ptr<ofg::Entity> probe{scene.createEntity(scene.getRoot())};
probe->setLocalPosition({0.0f, 2.0f, 0.0f});
scene.clear();
// probe.get() is now null, even though the new scene reuses entity IDs.
```

[Component](../src/scene/component.h) observes its entity through Ptr. Scene owns typed vectors of unique Camera, MeshRenderer and Animator allocations; Entity provides borrowed typed accessors. `createCamera(entity)`, `createMeshRenderer(entity)` and `createAnimator(entity)` reject null/foreign entities and duplicates. Storage growth and scene moves preserve component addresses. Clear and move assignment destroy displaced components before entities, invalidating component observers. `Entity::scene()` borrows its current owner and follows Scene moves. The existing standalone TestComponent remains a lifetime fixture.

`setActiveCamera(camera)` selects a camera belonging to the scene; null deselects it. Selection uses Ptr and survives scene moves. Camera defaults to a 60-degree vertical FOV, near 0.1 and far 1000 scene units. `setPerspective` validates radians/distances, and the viewport supplies aspect ratio. Draw extraction inverts the full camera world matrix and rejects a singular view.

MeshRenderer retains a shared Mesh and nullable overrides. `material(index)` resolves an override or mesh default. `makeMaterialUnique(index)` explicitly clones and installs the effective material. Assigning the same mesh preserves overrides; changing/clearing the mesh resets them. Invalid slots throw. See [resources](resources.md) for geometry, uniforms and GPU readiness.

`buildDrawList(scene, camera, aspectRatio)` rejects foreign cameras and conservatively culls whole-mesh bounds in homogeneous clip space. Entries retain assets and copy world matrices; they contain no scene pointers. The current [Game frame driver](../src/game.h) updates Resources, root State and the selected Scene, then extracts/renders. No generic ECS registry or component update lifecycle is introduced.

## Deformation and animation bindings

MeshRenderer retains an optional shared Skin, a `Ptr<Entity>` palette in Skin joint order, and instance morph weights.
`setSkin(skin, joints)` requires a ready Skin, a Mesh with influence sets, one unique live same-scene entity per joint
and palette indices within range (including zero-weight indices). Every skinned vertex must have positive total
joint weight; the setter does not normalize weights. `setSkin(nullptr, {})` clears the binding. Skin source-node
numbers and runtime EntityIds are not interchangeable: resolve source nodes into this instance's palette first.

`setMorphWeights(weights)` requires a Mesh, one finite value per target and permits extrapolation outside [0,1].
Invalid assignments leave the old state intact. Changing the Mesh clears Skin/joint bindings and material overrides
and copies the new Mesh defaults; setting the same Mesh preserves all instance state. Clearing Mesh clears its weights.
These setters do not change geometry, bounds or rendering yet.

Animator is a passive scene-owned component attached to the synthetic root of an instantiated Model. Use
`Scene::createAnimator(root)` and `Animator::setBindings(clips, nodes)`. Clips are shared Animation resources; the
source-indexed node map contains observed distinct entities within that root's subtree, including the root itself
when needed. Null entries intentionally represent nodes outside the selected model scene. All clip targets must fit
the mapping, and mapped weight tracks must match a MeshRenderer's morph count. The caller must provide clips and
bindings from the same source-node domain; this API does not infer names or retarget clips. Assignment validates
before replacing either vector. `setBindings({}, {})` clears the component.

```cpp
auto animator = scene.createAnimator(instanceRoot);
// Indices correspond to the Animation's source nodes, not runtime entity IDs.
animator->setBindings({clip}, {nullptr, jointA, jointB});
renderer->setSkin(skin, {jointB, jointA}); // Separately resolved Skin palette order.
renderer->setMorphWeights({0.25f, -0.5f});
```

Animations can affect ordinary nodes, joints or morph weights, so Animator belongs to the whole instance rather than
one renderer. Scene::update does not evaluate tracks or modify weights. Later mesh changes can invalidate a previously
configured morph track; callers must rebind compatible clips when changing such assets. No playback contract is
implied yet. Scene clear/destruction releases assets and invalidates observers. Scene moves preserve bindings and
rebind `Entity::scene()` to the destination owner.

## Provenance and deliberate adaptations

Imported on 2026-10-01 from the preserved filesystem snapshot `C:/dev/ofg-old2/cpp`; that backup has no Git metadata, so no source commit is claimed. Original paths are `include/ofg/state/state.hpp`, `src/state/state.cpp`, scene `scene/entity/component` headers and sources, core `object/ptr/engine_error`, and math `vec/mat/quat/transform`. Tests come from `state_machine_test.cpp`, `scene_test.cpp`, `object_ptr_test.cpp` and `math_test.cpp`.

State behavior and Object/Ptr ownership are preserved. Adaptations are repository filenames, include paths, camel-case names, formatting and comments; removal of concrete scene dependencies; scene IDs advancing only after successful allocation; and avoiding pointer arithmetic across named vector members. The old `Mat4::data()` was removed in favor of the existing explicit packing helper. All sixteen state tests and eleven math cases were retained; component-specific scene/resource fixtures were excluded and core lifetime cases added. The backup is unchanged.

## Verification

Use the `cpu-tests` configure/build/test presets after the native compiler environment setup in [DEVELOPING.md](../DEVELOPING.md). This path needs no RHI checkout, downloads, window or GPU. The native-debug preset also runs the core suite alongside startup, checkerboard and scene-rendering checks. Numerical tests retain their explicit doctest tolerances; lifetime and state assertions are exact. Coverage acceptance is behavioral for this import, with no measured percentage claimed. Browser support is checked by compiling the same core with Emscripten; the full C++ test suite remains native.

## Model instantiation

```cpp
#include "resources/model.h"
#include "resources/resources.h"
#include "scene/scene.h"

auto model = Resources::loadResourceAsync<Model>("assets/models/laboratory.gltf");
ModelInstance instance;
// The host keeps pumping Resources::update(), normally through Game::frame().
if (model->isLoaded() && !instance.root)
{
    instance = scene.instantiateModel(model, scene.getRoot());
    instance.root->setLocalPosition({10, 0, 0});
    // instance.nodes[sourceNodeIndex] observes this instance's node, or is null outside its scene.
}
```

`Scene::instantiateModel(model, parent, optionalSceneIndex)` selects the explicit scene, declared default, or scene
zero. A Model without scenes remains inspectable but cannot instantiate. Parent must be a live entity in the target
Scene. Instantiation stages a synthetic root, every entity, renderer, morph override, joint palette and optional
root Animator in a temporary Scene. Only after successful validation and destination reservation does it transfer
stable allocations, reassign IDs/owners and attach the root. Failure leaves the existing scene hierarchy and
components unchanged. Model node names remain in ModelData; instance entities currently retain their default display names.

`ModelInstance` retains the Model and observes the synthetic root, optional Animator and source-indexed node map.
Discarding it does not remove the scene objects. MeshRenderers retain their own shared assets; Animator retains all
clips and a separate node map, with null entries outside the selected scene. It is created only if at least one clip
targets an included node. Two instances share resources and have independent entities, joint palettes and morph
weights. Scene moves rebind owners; clear/destruction invalidates all instance observers. Rendering remains an
**undeformed preview**: no skinning, morph deformation or animation evaluation is implied.
