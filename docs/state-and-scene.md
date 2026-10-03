# State and scene foundations

The GPU-independent `ofg-core` library contains the imported state machine, entity hierarchy, component base, observer pointers and CPU math. Both application targets link it. The separate [resource foundation](resources.md) uses shared asset ownership alongside the scene. The checkerboard does not yet exercise these systems; the native [core tests](../tests/scene-test.cpp) are the executable examples.

## State flow

[State](../src/state/state.h) owns one primary child and any indexed substates through `std::unique_ptr`. Subclass it and implement the protected enter/main/leave hooks. Drive the root with `update()` from application code. A hook returns false to continue on a later update; returning true completes that phase. Enter-start/end and leave-start/end are single-shot hooks.

Descendants begin after the parent's entry completes. Active children update before the parent main hook; substates run in ascending index order. Replacing a child requests its departure, waits for its leave phase, then activates the replacement. If several replacements are requested while waiting, the newest pending state wins. `leave()` discards pending states and drains active descendants before running the parent's leave hooks. A leave request during entry waits for entry to complete and skips main.

`setInhibitControlOnChild(true)` suppresses the parent's main hook while its primary child is active. Indexed substates do not trigger this inhibition. `spawnSibling()` is valid only on the active primary child. Null children, negative substate indices, spawning during departure, recursive updates of the same state and runaway immediate transitions report `EngineError`. The per-update transition budget is 1024 per state.

Pass actual unique ownership when spawning. Returned pointers/references are borrowed and may expire when pending replacements are overwritten or descendants finish. State is not an Object and does not support Ptr observation. Destruction releases storage without calling leave hooks: request leave and continue updating when orderly application cleanup is required. Hooks may capture application context; there is no event bus, built-in clock or resource-loading dependency. The full lifecycle examples are in [state-machine-test.cpp](../tests/state-machine-test.cpp).

## Scene ownership and transforms

[Scene](../src/scene/scene.h) owns individually allocated [Entity](../src/scene/entity.h) objects, beginning with root ID zero. `createEntity(parent)` appends to its parent's child/sibling chain. Allocations remain at stable addresses as the scene grows. IDs are lookup indices within that scene generation, not globally unique or deletion-safe handles.

Each entity holds position, quaternion rotation and scale. `parentFromLocal` composes translation * rotation * scale. `worldFromLocal` recursively includes every ancestor, including the root transform. Math retains the backup's float, column-vector, Y-up, left-handed, +Z-forward convention. Projection helpers use depth [0, 1]. These local transforms do not solve large-world precision; global terrain addressing remains future work. Use `packMat4` for an explicit column-major array rather than assuming named vector members form a flat C++ array.

`clear()` destroys entities, increments the generation counter and creates a new root with reset IDs. Moves preserve transferred entity addresses and rebind their owning scene; move assignment destroys the destination's previous entities. A moved-from scene has no root until cleared. Generation counters are local reset counters and are copied by moves, not universal lifetime tokens.

There is deliberately no individual deletion, reparenting, transform cache or gameplay update loop in this import. Raw entity pointers passed to APIs must still be live; ownership checks do not make dangling raw pointers safe.

## Observation and components

[Object](../src/core/object.h) and [Ptr](../src/core/ptr.h) retain the old small intrusive observer list. A Ptr registers with its target, becomes null on target destruction, and throws `EngineError` if dereferenced while null. It never owns or extends the target's lifetime. Use unique ownership for storage, raw pointers/references for short-lived access, and Ptr when a stored reference must notice destruction. These facilities are single-threaded; they are not shared/weak reference counting or GPU resource handles.

```cpp
ofg::Scene scene;
ofg::Ptr<ofg::Entity> probe{scene.createEntity(scene.getRoot())};
probe->localTransform().position = {0.0f, 2.0f, 0.0f};
scene.clear();
// probe.get() is now null, even though the new scene reuses entity IDs.
```

[Component](../src/scene/component.h) retains the entity-binding base contract through Ptr. All concrete components have been excluded: mesh renderer, camera, light, player and animation player, plus scene environment/terrain behavior. Consequently there is no component type enum, creation switch or component container yet. Add explicit typed scene storage and entity accessors alongside the first real component, following the previous design; do not introduce a generic ECS framework just to fill that gap. TestComponent exists only in tests to verify the base's lifetime behavior.

## Provenance and deliberate adaptations

Imported on 2026-10-01 from the preserved filesystem snapshot `C:/dev/ofg-old2/cpp`; that backup has no Git metadata, so no source commit is claimed. Original paths are `include/ofg/state/state.hpp`, `src/state/state.cpp`, scene `scene/entity/component` headers and sources, core `object/ptr/engine_error`, and math `vec/mat/quat/transform`. Tests come from `state_machine_test.cpp`, `scene_test.cpp`, `object_ptr_test.cpp` and `math_test.cpp`.

State behavior and Object/Ptr ownership are preserved. Adaptations are repository filenames, include paths, camel-case names, formatting and comments; removal of concrete scene dependencies; scene IDs advancing only after successful allocation; and avoiding pointer arithmetic across named vector members. The old `Mat4::data()` was removed in favor of the existing explicit packing helper. All sixteen state tests and eleven math cases were retained; component-specific scene/resource fixtures were excluded and core lifetime cases added. The backup is unchanged.

## Verification

Use the `cpu-tests` configure/build/test presets after the native compiler environment setup in [DEVELOPING.md](../DEVELOPING.md). This path needs no RHI checkout, downloads, window or GPU. The native-debug preset also runs the core suite alongside the two GPU checks. Numerical tests retain their explicit doctest tolerances; lifetime and state assertions are exact. Coverage acceptance is behavioral for this import, with no measured percentage claimed. Browser support is checked by compiling the same core with Emscripten; the full C++ test suite remains native.
