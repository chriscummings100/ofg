# Resource loading foundation

[Resources](../src/resources/resources.h) is a static, application-thread service in `ofg-core`. It returns `std::shared_ptr<T>` immediately from `loadResourceAsync<T>(key)`. `T` derives from [Resource](../src/resources/resource.h) and takes a string key in its constructor. This is the loading/lifetime foundation only: no file, network, texture, mesh or GPU loader has been added yet.

## One handle throughout loading

Keep the returned pointer in the state or component that needs the asset. `isLoaded()` means data is ready; `isFailed()` exposes a terminal error, available through `error()`. `isFinished()` includes both success and failure. Pending resources start in `ResourceState::Loading` and finish in `Loaded` or `Failed`. Do not poll only success without also handling failure.

Call `Resources::update()` once per application frame to advance pending resources. The current checkerboard does not request assets or call this update yet. The [resource tests](../tests/resources-test.cpp) contain an executable example of a State requesting one resource in `onEnterStart()`, polling it in `onEnter()`, and retaining the same pointer in Main.

Concrete resources implement a small `loadStep()`: return false while waiting, true when usable, or throw a diagnostic on failure. The service records exceptions on that resource rather than aborting every other load. Constructors establish identity and initial fields only; begin loading and request dependencies from `loadStep()`. Keep each step bounded: async means cooperative progress over frames here, not a hidden worker thread. No callbacks, futures, separate request objects or generic file-format registry are included.

Dependencies are ordinary shared pointers owned by the concrete resource. Its loading step requests them once, checks failure, then waits for readiness. Avoid owning back-references and cyclic load dependencies. New dependencies begin on the next update; ordering among already-pending resources is unspecified, so parents must poll rather than assume a particular processing order.

## Weak lookup and lifetime

The dictionary holds weak pointers. Repeated requests for the same exact key return the same live object, including pending or failed resources. Requesting an incompatible type for a live key throws `EngineError`; empty keys are rejected. Keys are case-sensitive opaque strings in this first slice, with no filesystem canonicalization or format-specific options.

The final shared owner controls destruction. The cache and scheduler do not retain abandoned pending assets. An update temporarily owns a resource only while advancing it. Expired dictionary entries are pruned during updates or replaced by a new request. A failed resource remains failed while retained; release all owners and request again for a fresh attempt. There is no explicit unload API that invalidates another caller's ownership.

All access, loading and final-owner release should stay on the application thread. Shared-pointer reference counting does not make the asset data or static dictionary thread-safe. Resource uses standard shared ownership, independently of the scene's Object/Ptr observer mechanism. Future GPU-backed assets must also honor in-flight GPU lifetime through RHI ownership/retirement; the weak cache alone does not establish that safety.

## Native blocking convenience

`loadResource<T>(key)` is compiled only for native builds. It obtains the same shared identity and pumps `Resources::update()` until that resource is finished, returning it on success or failure; inspect its status. It is intended for tools/debugging and must not be invoked from a loading step. Recursive scheduler updates are also rejected.

This initial blocking loop services only the cooperative resource scheduler. A concrete loader must make progress through that scheduler; it must not wait for an external host event loop that this function does not pump. Actual I/O and its waiting strategy will be defined alongside the first real asset loader. Browser application code uses async loading and per-frame polling exclusively.

## Verification and origin

The design retains the old backup's pollable Resource idea, with new implementation for shared ownership, weak lookup and a small scheduler. It does not copy the old owning singleton registry, GPU context, resource subclasses or model/blob state machines.

The native CPU suite covers status transitions, duplicate requests, weak lifetimes, failure/retry, shared dependencies created during update, invalid keys/types, native blocking completion, reentry rejection and state-entry polling. These fixtures perform deterministic staged work without a disk, GPU or network. Build commands are in [DEVELOPING.md](../DEVELOPING.md); the [completed plan](archived/resource-foundation.md) records validation scope.
