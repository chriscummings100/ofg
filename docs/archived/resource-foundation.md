# Add a small pollable resource foundation

This ExecPlan follows [PLANS.md](../../PLANS.md). Complete. The user authorized the initial resource layer; concrete meshes, textures and procedural geometry follow separately.

## Purpose / Big Picture

A state can keep one shared resource pointer from its entry phase onward, poll readiness, and retain the loaded asset without a separate request object. Static Resources access remains; the service only weakly indexes assets.

## Progress

- [x] (2026-10-01) Confirmed static access, shared resource/dependency ownership, weak lookup, polling, and native-only blocking convenience.
- [x] (2026-10-01) Implement Resource and Resources in the GPU-independent core.
- [x] (2026-10-01) Verify lifetime, deduplication, loading/failure and state-entry behavior; document the contract.

## Surprises & Discoveries

The old Resource already exposed loading state, but its global owner retained all assets until teardown. This slice replaces ownership and scheduling without importing its GPU-specific resource subclasses.

## Decision Log

Use three resource states: Loading, Loaded and Failed. A small virtual load step reports completion; Resources advances it on the application thread. The static typed load functions construct a concrete Resource subclass from its string key. Cache keys are exact strings; requesting an incompatible type under a live key is an error. No file normalization or format registry is needed yet.

Store weak pointers in the dictionary and use a weak snapshot during update, so loading a dependency cannot invalidate iteration or keep abandoned work alive. Polling is the only notification mechanism in this slice. Native blocking load pumps the same cooperative scheduler; browser builds omit it. Actual file/network loading and a worker system are not part of this foundation.

## Outcomes & Retrospective

Implemented the static typed APIs, shared pollable resource, weak dictionary and cooperative update. Eight new resource cases pass; the full suite now passes 48 cases/878 assertions. Native application/GPU checks pass, and ofg-core compiles with Emscripten. Concrete I/O and asset types remain deliberately outside this slice.

## Contract and Quality Baseline

Call all APIs on one application thread. Resources are owned by callers and dependencies; no owning back-reference cycles. A failed resource is finished but not loaded. Repeated loads return the same live resource, including a failed one; dropping all owners permits a fresh attempt. Derived load steps must do bounded work and return false while pending, true once ready, or throw a diagnostic on failure. Data must only be consumed after readiness. GPU retirement is a responsibility of future concrete GPU assets.

## Context and Orientation

The uncommitted state/scene foundation is already verified. New files live in src/resources, linked into ofg-core; native doctest cases join ofg-core-test. The preserved backup is reference-only. Existing build-native/build-web workflows apply.

## Plan of Work

Add the resource base and static weak dictionary without a separate request object or registry. Test using tiny staged resources with shared dependencies, including a state waiting during entry. Document cooperative update, failed loads, native blocking semantics, weak cache cleanup and browser limits.

## Concrete Steps

After the Visual Studio x64 setup in DEVELOPING.md: cmake --build --preset cpu-tests --parallel; ctest --preset cpu-tests. Rebuild native-debug and run its CTest suite after CMake integration. Compile ofg-core in build/web to check Emscripten compatibility. This CPU-only slice changes no graphics or host behavior.

## Milestone Review

Reviewed cache identity, weak lifetime during loading and after completion, error/terminal status, dependency insertion during iteration, native-only blocking API, and application-thread boundaries. The scheduler uses a weak snapshot to tolerate dictionary rehashing; no task system, callback registry or owning singleton storage was added. All public contracts and nontrivial helpers are commented. Documentation explicitly states that no concrete I/O loader exists and native blocking only pumps the cooperative scheduler. The first test compile exposed missing <ostream> for doctest shared_ptr diagnostics; adding that standard include resolved it. Formatting and local documentation links pass. Earlier state/scene changes and the RHI pin remain preserved.

## Validation and Acceptance

Test pending-to-loaded transitions, deduplication, weak lifetime during pending/success/failure, retry after expiry, dependency lifetime, dependency creation during update, error propagation, wrong-type/empty-key rejection, blocking native completion and reentrant-update rejection. Preserve the existing CPU and native GPU tests. Behavioral coverage is required; no unmeasured percentage is claimed. No browser test-suite port or new visual capture is required for this CPU-only slice.

## Idempotence and Recovery

Preserve earlier uncommitted work and dependency pins. Builds reuse their separate output directories; do not modify the backup.

## Artifacts and Notes

Verified on 2026-10-01 with the existing Visual Studio x64 environment: cmake --build --preset cpu-tests --parallel; ctest --preset cpu-tests; direct ofg-core-test.exe reports 48 cases and 878 assertions. cmake --build --preset native-debug --parallel and ctest --preset native-debug pass all three tests. cmake --build build/web --target ofg-core --parallel passes with Emscripten 6.0.0. clang-format 22.1.3 and git diff --check pass. No fresh browser runtime test was needed for these CPU-only sources; the prior checkerboard smoke is not evidence of actual resource fetching. See docs/resources.md for scope and usage.

## Interfaces and Dependencies

Resource exposes pollable state/key/error. Resources exposes static loadResourceAsync<T>, native-only loadResource<T>, and update. Only C++20/standard library and the existing EngineError are needed. Concrete test resources supply staged work; no disk/network or GPU loader is claimed.
