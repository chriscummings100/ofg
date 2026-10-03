# Resource loading foundation

[Resources](../src/resources/resources.h) is a static, application-thread service in `ofg-core`. It returns `std::shared_ptr<T>` immediately from `loadResourceAsync<T>(key)`. `T` derives from [Resource](../src/resources/resource.h) and takes a string key in its constructor. Mesh, Material and Shader are now ready procedural resource types. File/network loading and textures remain future work.

## One handle throughout loading

Keep the returned pointer in the state or component that needs the asset. `isLoaded()` means data is ready; `isFailed()` exposes a terminal error, available through `error()`. `isFinished()` includes both success and failure. Pending resources start in `ResourceState::Loading` and finish in `Loaded` or `Failed`. Do not poll only success without also handling failure.

Call `Resources::update()` once per application frame to advance pending resources. `Game::frame` calls this before State and Scene updates. The [resource tests](../tests/resources-test.cpp) contain an executable example of a State requesting one resource in `onEnterStart()`, polling it in `onEnter()`, and retaining the same pointer in Main.

Concrete resources implement a small `loadStep()`: return false while waiting, true when usable, or throw a diagnostic on failure. The service records exceptions on that resource rather than aborting every other load. Constructors establish identity and initial fields only; begin loading and request dependencies from `loadStep()`. Keep each step bounded: async means cooperative progress over frames here, not a hidden worker thread. No callbacks, futures, separate request objects or generic file-format registry are included.

Dependencies are ordinary shared pointers owned by the concrete resource. Its loading step requests them once, checks failure, then waits for readiness. Avoid owning back-references and cyclic load dependencies. New dependencies begin on the next update; ordering among already-pending resources is unspecified, so parents must poll rather than assume a particular processing order.

## Weak lookup and lifetime

The dictionary holds weak pointers. Repeated requests for the same exact key return the same live object, including pending or failed resources. Requesting an incompatible type for a live key throws `EngineError`; empty keys are rejected. Keys are case-sensitive opaque strings in this first slice, with no filesystem canonicalization or format-specific options.

The final shared owner controls destruction. The cache and scheduler do not retain abandoned pending assets. An update temporarily owns a resource only while advancing it. Expired dictionary entries are pruned during updates or replaced by a new request. A failed resource remains failed while retained; release all owners and request again for a fresh attempt. There is no explicit unload API that invalidates another caller's ownership.

All access, loading and final-owner release should stay on the application thread. Shared-pointer reference counting does not make the asset data or static dictionary thread-safe. Resource uses standard shared ownership, independently of the scene's Object/Ptr observer mechanism. Mesh/Shader GPU handles honor in-flight lifetime through RHI command/backend ownership; the weak lookup alone does not establish that safety.

## Native blocking convenience

`loadResource<T>(key)` is compiled only for native builds. It obtains the same shared identity and pumps `Resources::update()` until that resource is finished, returning it on success or failure; inspect its status. It is intended for tools/debugging and must not be invoked from a loading step. Recursive scheduler updates are also rejected.

This initial blocking loop services only the cooperative resource scheduler. A concrete loader must make progress through that scheduler; it must not wait for an external host event loop that this function does not pump. Actual I/O and its waiting strategy will be defined alongside the first real asset loader. Browser application code uses async loading and per-frame polling exclusively.

## Verification and origin

The design retains the old backup's pollable Resource idea, with new implementation for shared ownership, weak lookup and a small scheduler. It does not copy the old owning singleton registry, GPU context, resource subclasses or model/blob state machines.

The native CPU suite covers status transitions, duplicate requests, weak lifetimes, failure/retry, shared dependencies created during update, invalid keys/types, native blocking completion, reentry rejection and state-entry polling. These fixtures perform deterministic staged work without a disk, GPU or network. Build commands are in [DEVELOPING.md](../DEVELOPING.md); the [completed plan](archived/resource-foundation.md) records validation scope.

## Procedural scene assets

`Shader::create(name, source, vertexEntry, fragmentEntry)`, `Material::create(shader)` and `Mesh::create(vertices, indices, subMeshes)` return ready, uncached resources with empty keys. Resource has a protected ready constructor; the keyed constructor remains pending, with a default loading step that reports unsupported keyed loading. The procedural types do not expose a string-key constructor, so use their factories rather than `loadResourceAsync`.

Readiness means a validated CPU description exists. Graphics lazily uploads Mesh buffers and compiles Shader source on first rendering use. GPU errors propagate with operation/compiler diagnostics; readiness does not promise shader compilation succeeded. Graphics shutdown releases prepared GPU handles even when CPU resources survive, so later initialization can reprepare them.

Mesh data is immutable: interleaved position/normal/UV vertices, uint32 triangle indices, and nonempty triangle-aligned submesh ranges with shared default Materials. Creation rejects non-finite attributes, invalid indices/ranges and missing materials, and computes local bounds. It retains CPU geometry for re-preparation. Mesh and Shader own private GPU state without importing RHI into core headers.

Material stores a shared Shader and a map of direct field names to `UniformValue`: float, int32, uint32, Vec2/3/4 or Mat4. `setUniform` edits the shared object deliberately; `clone()` copies values into a new uncached Material sharing the Shader. No implicit copy-on-write occurs. MeshRenderer's `makeMaterialUnique(index)` installs such a clone as an override. Other submeshes/instances remain unchanged.

Shaders must expose `material` as a struct or constant/parameter block of supported direct fields, and `draw.clipFromLocal` as float4x4. At binding, Graphics rejects missing, extra, unsupported or incorrectly typed material fields. Arrays, nested data, textures, buffers and boolean uniforms are excluded. The material block's field names need not match the fixture's tint field. RHI ShaderCursor handles reflected offsets; explicit array packing and reflected matrix layout handle the CPU/shader ABI. Every draw gets independent shader-object storage.

Draw extraction skips pending dependencies and reports failed ones. DrawLists retain shared assets and snapshot world matrices; keep materials unchanged between extraction and submission. Graphics caches pipelines by weak Shader ownership identity and target color format, not Material values. A material clone therefore reuses its shader's program and pipelines. RHI retains submitted GPU resources; releasing the final CPU asset owner after submission is tested natively. Slang's compiler session can retain module internals until device destruction, independently of asset/cache ownership.
