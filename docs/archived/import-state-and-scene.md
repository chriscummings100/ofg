# Bring forward the state machine and scene foundations

This ExecPlan follows [PLANS.md](../../PLANS.md). Complete; the user authorized adapting both systems fairly as-is from the preserved backup, without its concrete scene components.

## Purpose / Big Picture

Provide a tested, GPU-independent foundation for laboratory flow and scene hierarchy before adding terrain or character behavior. Preserve the backup's ownership and lifecycle semantics instead of designing a new ECS.

## Progress

- [x] (2026-10-01) Inspected the backup's state, scene, Object/Ptr, math and tests.
- [x] (2026-10-01) Adapt the state machine and component-free scene foundation.
- [x] (2026-10-01) Add an independent CPU build/test preset and run the native regression checks.
- [x] (2026-10-01) Document contracts and provenance, review the import, and record validation.

## Surprises & Discoveries

The old scene has explicit typed component containers, not a generic ECS registry. Removing all concrete types also removes the component factory, typed accessors and gameplay update loop. The reusable component base still binds an entity through a non-owning observer. Individual entity deletion and reparenting were never implemented.

## Decision Log

2026-10-01: preserve the State enter/main/leave lifecycle, primary child and indexed substates. Adapt filenames, naming and formatting to this repository. Retain the existing Object/Ptr observer implementation and standard unique ownership. Do not introduce shared ownership, generational handles, an ECS framework or new gameplay components.

2026-10-01: retain the small tested CPU math module supporting transforms. Keep the Component base but remove the now-empty concrete type enum/factory and per-type storage; add those concrete pieces when actual components are introduced. The checkerboard remains the graphics reference and does not need artificial scene/state behavior.

2026-10-01: vendor the unmodified doctest 2.4.11 header from the existing RHI revision with its MIT license and SHA-256 provenance, independently of the submodule. This avoids network access in the CPU preset. Pin doctest independently of RHI and configure a CPU-only preset which never adds graphics dependencies. Preserve separate CPU and GPU CTest labels. Coverage acceptance for this import is scenario-based: preserve all state tests and math tests, port scene-core tests, and exercise observer invalidation and scene moves. No numerical percentage is claimed or inherited from the backup; installing a coverage toolchain is outside this import.

## Outcomes & Retrospective

Imported the requested foundations with no concrete gameplay components or graphics dependencies. The CPU suite passes 40 cases and 826 assertions, all three native CTest checks pass, Emscripten compiles the core, and the rebuilt browser checkerboard passes its existing Chrome smoke. Resource loading remains the next separate investigation.

## Contract and Quality Baseline

Scene owns stable entity allocations until clear/destruction. IDs are indices within a scene generation, not globally safe handles. Ptr observes an Object, becomes null on destruction and never extends its lifetime; raw pointers require a live target. Scene moves retain entity addresses and rebind their scene owner. State owns its descendants and drains them before its leave hooks. All objects are single-threaded. Transforms retain the old float, Y-up, +Z-forward convention and are not an epic-world coordinate system.

## Context and Orientation

Source is under C:/dev/ofg-old2/cpp: include/ofg/{state,scene,core,math}, matching src folders and tests. The backup is read-only. Current OFG has the shared checkerboard renderer and native/web hosts. New code will live under src/{state,scene,core,math} in ofg-core, with native doctest tests under tests. Build prerequisites and Visual Studio x64 shell initialization are in DEVELOPING.md.

## Plan of Work

First adapt the independent state, object/observer and math code plus scene hierarchy and component base. Bring forward applicable tests, excluding gameplay/resource/GPU fixtures. Then integrate a small core library and test executable, independently pinned doctest, and a cpu-tests preset. Finally review public contracts, run CPU and existing native tests, check Emscripten compilation of the core, and update architecture/build guidance.

## Concrete Steps

From C:/dev/ofg, initialize the Visual Studio x64 environment described in DEVELOPING.md. Verified commands: cmake --preset cpu-tests; cmake --build --preset cpu-tests; ctest --preset cpu-tests. Also run the existing native-debug configure/build/CTest commands. For portability, compile ofg-core with the existing web toolchain. No browser test-suite port is intended.

## Milestone Review

Reviewed source against the backup, unique ownership and observer invalidation, scene moves/reset, state hooks, component exclusions, comments and native/web build boundaries. Preserved all sixteen state and eleven math cases; added scene lifetime/growth and observer list checks. Repaired named-member pointer arithmetic in imported math by explicit indexing and packing; removed unsafe flat Mat4::data(). Scene allocation now advances the ID only after insertion succeeds. No new ECS registry or shared ownership was introduced. Formatting, skill validation and documentation links pass; RHI remains untouched. CPU build.ninja contains no RHI, GLFW or DirectX targets. No percentage coverage or performance result is claimed.

## Validation and Acceptance

Preserve all sixteen state lifecycle tests and the existing math numerical fixtures. Verify root/ID lookup, parent rejection, creation-order traversal, transform composition, clear/reset, move construction/assignment, observer destruction/reset/copy/move, and component-to-entity observation. CPU configuration must not fetch or build Slang RHI/GLFW. Existing native device and pixel tests must remain green. No rendering changes are planned, so new visual captures are unnecessary unless the integration changes rendering behavior.

## Idempotence and Recovery

Use separate build/cpu-tests, build/native and build/web outputs. Do not remove or modify the backup or update dependency pins opportunistically. Preserve existing build trees and user edits.

## Artifacts and Notes

Record source provenance and any intentional semantic repairs alongside the architecture documentation. Verified on 2026-10-01: cmake --preset cpu-tests; cmake --build --preset cpu-tests --parallel; ctest --preset cpu-tests; direct ofg-core-test reports 40 cases/826 assertions. The corresponding native-debug commands pass all three checks (CPU, device startup, pixels). cmake --preset web and cmake --build --preset web pass; npm run smoke:web passes in Chrome 154.0.8037.59. The pre-existing Asyncify/WASM-exceptions warning remains. Browser report/screenshots are under artifacts/browser-smoke. Skills and build instructions were updated; clang-format 22.1.3 validation passes. This is a behavior-test acceptance checkpoint, not a measured coverage threshold.

## Interfaces and Dependencies

ofg-core exposes State, Scene, Entity, Component, Object, Ptr and CPU math using C++20 and the standard library. Tests use doctest 2.4.11. No graphics headers or platform APIs may enter this library.
