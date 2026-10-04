# Resource loading foundation

[Resources](../src/resources/resources.h) is a static, application-thread service in `ofg-core`. It returns `std::shared_ptr<T>` immediately from `loadResourceAsync<T>(key)`. `T` derives from [Resource](../src/resources/resource.h) and takes a string key in its constructor. Mesh, Material, Shader and Sampler have ready procedural factories. Texture accepts typed procedural pixels and on-demand PNG/JPEG loading.

## One handle throughout loading

Keep the returned pointer in the state or component that needs the asset. `isLoaded()` means data is ready; `isFailed()` exposes a terminal error, available through `error()`. `isFinished()` includes both success and failure. Pending resources start in `ResourceState::Loading` and finish in `Loaded` or `Failed`. Do not poll only success without also handling failure.

Call `Resources::update()` once per application frame to advance pending resources. `Game::frame` calls this before State and Scene updates. The [resource tests](../tests/resources-test.cpp) contain an executable example of a State requesting one resource in `onEnterStart()`, polling it in `onEnter()`, and retaining the same pointer in Main.

Concrete resources implement a small `loadStep()`: return false while waiting, true when usable, or throw a diagnostic on failure. The service records exceptions on that resource rather than aborting every other load. Constructors establish identity and initial fields only; begin loading and request dependencies from `loadStep()`. Keep each step bounded: async means cooperative progress over frames here, not a hidden worker thread. The public API has no callbacks, futures or generic file-format registry. Texture privately owns its native read/browser fetch state. Image decoding is one synchronous CPU stage, not a hard frame-time guarantee.

Dependencies are ordinary shared pointers owned by the concrete resource. Its loading step requests them once, checks failure, then waits for readiness. Avoid owning back-references and cyclic load dependencies. New dependencies begin on the next update; ordering among already-pending resources is unspecified, so parents must poll rather than assume a particular processing order.

## Weak lookup and lifetime

The dictionary holds weak pointers. Repeated requests for the same exact key return the same live object, including pending or failed resources. Requesting an incompatible type for a live key throws `EngineError`; empty keys are rejected. Keys are case-sensitive opaque strings in this first slice, with no filesystem canonicalization or format-specific options.

The final shared owner controls destruction. The cache and scheduler do not retain abandoned pending assets. An update temporarily owns a resource only while advancing it. Expired dictionary entries are pruned during updates or replaced by a new request. A failed resource remains failed while retained; release all owners and request again for a fresh attempt. There is no explicit unload API that invalidates another caller's ownership.

All access, loading and final-owner release should stay on the application thread. Shared-pointer reference counting does not make the asset data or static dictionary thread-safe. Resource uses standard shared ownership, independently of the scene's Object/Ptr observer mechanism. Mesh/Shader GPU handles honor in-flight lifetime through RHI command/backend ownership; the weak lookup alone does not establish that safety.

## Native blocking convenience

`loadResource<T>(key)` is compiled only for native builds. It obtains the same shared identity and pumps `Resources::update()` until that resource is finished, returning it on success or failure; inspect its status. It is intended for tools/debugging and must not be invoked from a loading step. Recursive scheduler updates are also rejected.

This initial blocking loop services only the cooperative resource scheduler. A concrete loader must make progress through that scheduler; it must not wait for an external host event loop that this function does not pump. Native Texture loading reads at most 256 KiB per step and decodes after reading; it makes progress entirely through this scheduler. Browser application code uses async loading and per-frame polling exclusively.

## Verification and origin

The design retains the old backup's pollable Resource idea, with new implementation for shared ownership, weak lookup and a small scheduler. It does not copy the old owning singleton registry, GPU context, resource subclasses or model/blob state machines.

The native CPU suite covers status transitions, duplicate requests, weak lifetimes, failure/retry, shared dependencies created during update, invalid keys/types, native blocking completion, reentry rejection and state-entry polling. These fixtures perform deterministic staged work without a disk, GPU or network. Build commands are in [DEVELOPING.md](../DEVELOPING.md); the [completed plan](archived/resource-foundation.md) records validation scope.

## Procedural scene assets

`Shader::create(name, source, vertexEntry, fragmentEntry)`, `Material::create(shader)` and `Mesh::create(vertices, indices, subMeshes)` return ready, uncached resources with empty keys. Resource has a protected ready constructor; the keyed constructor remains pending, with a default loading step that reports unsupported keyed loading. The procedural types do not expose a string-key constructor, so use their factories rather than `loadResourceAsync`.

Readiness means a validated CPU description exists. Graphics lazily uploads Mesh buffers and compiles Shader source on first rendering use. GPU errors propagate with operation/compiler diagnostics; readiness does not promise shader compilation succeeded. Graphics shutdown releases prepared GPU handles even when CPU resources survive, so later initialization can reprepare them.

Mesh data is immutable: interleaved position/normal/UV0/tangent/UV1/color vertices, uint32 triangle indices, and nonempty triangle-aligned submesh ranges with shared default Materials. Creation rejects non-finite attributes, invalid indices/ranges and missing materials, and computes local bounds. It retains CPU geometry for re-preparation. Mesh and Shader own private GPU state without importing RHI into core headers.

Material stores a shared Shader and a map of direct field names to `UniformValue`: float, int32, uint32, Vec2/3/4 or Mat4. `setUniform` edits the shared object deliberately; `clone()` copies values into a new uncached Material sharing the Shader. No implicit copy-on-write occurs. MeshRenderer's `makeMaterialUnique(index)` installs such a clone as an override. Other submeshes/instances remain unchanged.

Shaders must expose `material` as a struct or constant/parameter block of supported direct fields, and `draw.clipFromLocal` as float4x4. At binding, Graphics rejects missing, extra, unsupported or incorrectly typed material fields. Arrays, nested data, buffers and boolean uniforms are excluded. Named top-level sampled textures and samplers are supported separately from the uniform block. The material block's field names need not match the fixture's tint field. RHI ShaderCursor handles reflected offsets; explicit array packing and reflected matrix layout handle the CPU/shader ABI. Every draw gets independent shader-object storage.

Draw extraction skips pending dependencies and reports failed ones. DrawLists retain shared assets and snapshot world matrices; keep materials unchanged between extraction and submission. Graphics caches pipelines by weak Shader ownership identity and target color format, not Material values. A material clone therefore reuses its shader's program and pipelines. RHI retains submitted GPU resources; releasing the final CPU asset owner after submission is tested natively. Slang's compiler session can retain module internals until device destruction, independently of asset/cache ownership.


## Sampled textures, views and samplers

`Texture::create(TextureDesc, TextureData)` copies base pixels with an explicit format and optional padded row pitch (zero means tightly packed). Supported formats are R8/RG8/RGBA8 UNORM, RGBA8 sRGB, R16/RG16/RGBA16 float and R32/RG32/RGBA32 float. Bytes are in the declared format; fp16 uses IEEE binary16 bits and fp32 uses binary32. No implicit format conversion occurs. Rows are top-to-bottom: UV (0,0) addresses the top-left image boundary. Shader loads of R/RG textures use the API's absent-component defaults; use the desired channels explicitly in the shader.

`TextureMipmaps::None` is the procedural default. `Generate` allocates the full chain down to 1x1 on first rendering use and generates it with GPU render passes. Only base pixels are retained on CPU. Loaded metadata is available through `desc()` and `mipCount()`; querying a pending/failed texture throws. Samplers are immutable `Sampler::create(SamplerDesc)` resources, defaulting to linear min/mag/mip filtering and repeat U/V addressing. Nearest and clamp-to-edge are also supported.

```cpp
auto texture = Resources::loadResourceAsync<Texture>(assetPath);
auto sampler = Sampler::create();
material->setTexture("baseColor", texture);
material->setSampler("baseColorSampler", sampler);
```

The direct Texture binding selects its default view and can be assigned while loading. `texture->createView({firstMip, mipCount})` selects a range after loading; zero count means all remaining levels. `defaultView()` selects the entire chain. TextureView is a copyable retaining value, not a separately loaded resource. It shares storage and never reinterprets format. Binding a view makes its first level shader mip zero. Material clones copy binding maps while sharing textures and immutable samplers; replacing a cloned binding does not alter its source.

Texture/sampler names refer to direct top-level `Texture2D<float...>` and `SamplerState` shader fields. `draw` and `material` remain reserved blocks. Reflection rejects missing/extra/wrong-kind bindings and unsupported resource shapes. Draw extraction omits pending texture dependencies and throws with the resource key on failure. A manually submitted DrawList must have ready textures. Materials must remain unchanged between extraction and submission.

### Image loading and limits

`Resources::loadResourceAsync<Texture>(path)` decodes PNG/JPEG into RGBA8 sRGB, straight alpha, with generated mips. Images are treated as sRGB color; this loader does not interpret embedded color profiles, JPEG EXIF orientation, or import float files. Linear data textures currently enter through the procedural API. The independently vendored stb_image revision/license is recorded in [external/stb](../external/stb/README.md).

Native paths use the filesystem; the fixture host resolves an absolute path beside its executable. Browser keys are URLs relative to the page and use asynchronous Emscripten Fetch, with a 30-second timeout. Requests retain no asset owner. Final-owner release cancels pending I/O; completion callbacks record status only, and decode/publish happens in Resources::update. Failures remain terminal while retained, and releasing all owners permits a fresh attempt.

Accepted encoded data is limited to 64 MiB; dimensions to 8192 per axis; base pixels to 256 MiB. These bounds reject oversized inputs before decoder allocation, but browser transport buffering itself is managed by the browser. Decoding temporarily retains encoded bytes, decoder output and the final pixel copy. GPU preparation checks actual device limits too. Generated 2D mip storage adds up to roughly one third for square textures (up to nearly the base size for 1D tails), plus upload staging; there is no global residency budget or performance claim in this milestone.

### GPU generation and portability

Graphics owns a TextureRenderer helper; assets own private GPU handles and the helper tracks them weakly for shutdown. Default bindings use RHI's default view; selected mip views are cached within the texture allocation. Each destination mip is cleared and rendered from a disjoint source-level view. Explicit texel loads integrate area overlap, preserving odd borders and 1xN/Nx1 tails. Linear formats average channels independently. sRGB reads decode RGB, reduction weights RGB by alpha and unpremultiplies, and sRGB attachment writes encode RGB. Alpha remains linear. The mip chain is generated once per GPU allocation, before scene passes, without CPU readback or per-level waits.

Browser fp32 use requires `float32-filterable` because the pinned RHI declares float sampled bindings as filterable even for explicit loads. `cmake/rhi-webgpu-formats.cmake` adjusts a generated copy of the pinned WebGPU source to withhold ShaderSample for fp32 when the actual device lacks that feature. The submodule and pin remain unchanged. Unsupported fp32 produces a useful error before allocation/binding; there is no silent downconversion or CPU mip fallback. Float16 storage uses float shader arithmetic and does not require shader-f16.

Shutdown invalidates surviving texture/view/sampler GPU handles. Reinitialization uploads retained base bytes and regenerates mips. Native tests release the final CPU owners after submission and verify readback; RHI/backend command ownership supplies in-flight retention. Public writable textures, compute-write bindings, cubemaps, arrays, compression, blending and specialized normal/height reduction remain outside this slice.

## PBR resources

See [PBR contracts](pbr.md) for typed material construction, surface slots/UV transforms, render classification and
binding limits. `Material::clone` also copies alpha/cull/PBR state. `Vertex` includes optional tangent handedness, UV1 and
color; zero tangent W selects derivative evaluation. `Environment::fromBytes` creates a ready immutable baked resource
and validates its entire cube/LUT payload before publication. `Scene::lighting` and extracted `DrawList` retain shared
environment ownership. Graphics uploads cube faces/mips privately, tracks allocations weakly for shutdown, and lets RHI
retain submitted references. This synchronous embedded bake path does not add an asynchronous environment-file loader.
