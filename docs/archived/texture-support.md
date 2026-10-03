# Add sampled textures and GPU mipmaps

This living ExecPlan follows [PLANS.md](../../PLANS.md). Status: complete, verified on 2026-10-03. Implementation was authorized on 2026-10-03. Existing PBR research edits belong to other work and must be preserved.

## Purpose and contract

Add GPU-independent Texture, TextureView and Sampler resources, named material bindings, PNG/JPEG loading on demand, and GPU mip generation on native D3D12 and browser WebGPU. Formats are R/RG/RGBA UNORM8, RGBA sRGB8 and R/RG/RGBA float16/float32. Browser fp32 is explicitly gated on actual device support. Public writable textures, compute, compression, arrays and cubemaps are out of scope.

Texture owns retained base-level bytes; views retain textures without cycles; immutable samplers are shared. Loaded means CPU-ready. Graphics prepares and retires RHI resources. Mips use separate fullscreen render passes and area-weighted reduction, linear-light alpha-weighted sRGB colors, and independent linear-data channels. No CPU mip generation or per-level GPU waits. File decoding remains synchronous CPU work.

## Progress

- [x] (2026-10-03) Inspected source and pinned RHI; recorded approved interfaces and backend constraints.
- [x] (2026-10-03) Implement CPU resources, asynchronous image loading and material ownership/tests.
- [x] (2026-10-03) Implement GPU upload, views, samplers, capability reporting and mip generation/tests.
- [x] (2026-10-03) Integrate fixture assets and browser loading/smoke scenarios.
- [x] (2026-10-03) Ran native/browser checks, inspected screenshots, reviewed interfaces/ownership and updated documentation.

## Surprises & Discoveries

WebGPU rejected LoadOp::DontCare because the pinned mapping returns Undefined; mip passes now explicitly clear. Initial browser smoke's fixed white-area threshold assumed an analytic checker with hard edges; bilinear/mip filtering reduced resized white pixels to 0.95% of the canvas. Replaced the canvas threshold with 20% of visible geometry for each white/gray/cyan region after inspecting images and measuring counts. Native capture needed DPI awareness and restoring the app window after a hidden launcher start; fresh captures now show the scene.

Pinned RHI 16324a68af477baaede620e713644f5e9613b1a2 has no mip-generation command. Its WebGPU float bindings require filterable textures; its format report does not distinguish unavailable fp32 sampling. Storage binding format/access handling is unfinished. Use render passes and a narrow generated-source capability patch, preserving the submodule checkout and pin.

## Decision Log

2026-10-03: User chose on-demand files, filtering/mips, sampled textures first, and capability-gated browser fp32. Use CPU-independent descriptions with private asset-owned GPU data. Vendor stb_image from the pinned RHI with provenance. Explicit mip views use the same format as their texture. Samplers default to linear/repeat. File images decode to RGBA8 sRGB; procedural uploads accept declared raw pixel formats and retain only base pixels.

## Plan of Work and interfaces

Texture::create(desc, data) copies and validates base pixels; Texture(path) integrates with Resources. Texture::defaultView/createView return retaining values. Material::setTexture accepts shared Texture or TextureView; setSampler accepts shared Sampler. Pending dependencies skip draws; failed dependencies and invalid bindings report errors. GPU preparation occurs before scene pass encoding and is recreated after Graphics shutdown. Native file paths are host-resolved; browser fetches use Emscripten Fetch with cancellation-safe request state.

Add an asymmetric authored image fixture and native/browser packaging. Run existing CPU/native/web workflows; extend native offscreen and browser smoke checks without porting all C++ tests to web.

## Validation and acceptance

Use the build-native/build-web skills and DEVELOPING.md environment setup. Commands: cmake --preset cpu-tests; cmake --build --preset cpu-tests; ctest --preset cpu-tests; corresponding native-debug commands; cmake --preset web; cmake --build --preset web; npm.cmd run smoke:web. Run clang-format 22.1.3 checks and git diff --check. No numerical coverage percentage is introduced; behavior and failure paths are the acceptance criteria.

Test byte layouts/pitch/overflow, view ranges and lifetimes, clone isolation, file decoding/failure/retry/cancellation, and pending draws. GPU tests cover all formats, values outside [0,1], orientation, filtering/addressing, mip correctness including odd/1D tails, sRGB/alpha, submitted lifetime and reinitialization. UNORM tolerance is two bytes; normalized fp16 2e-3 and fp32 1e-5 against a test-only CPU reference. Browser checks delayed/failed loads, fp16/fp32 or explicit gating, resize/reload and diagnostics. Save screenshots and reports in artifacts/textures and inspect native transitions.

## Recovery and ownership

Preserve unrelated edits and historical repository. Do not change dependency pins or erase build directories. Release pending I/O on final-owner destruction; terminal failures retry after all owners release. Retain CPU pixels so GPU lifecycle restart is recoverable. Keep generated dependency adaptations inside build outputs.

## Outcomes & Retrospective

CPU-only and native CTest checks have passed. Browser smoke passed PNG/JPEG, fp16/fp32, delayed loading, cancellation, missing/corrupt images, and actual-device fp32 feature masking with expected diagnostics. Final post-review CPU/native and browser checks pass. Native/browser images were inspected, including minified browser rendering and native restored presentation.

## Milestone Review and evidence

2026-10-03 review: CPU headers contain no RHI types. TextureView retains its image without a cycle; sampler settings are immutable. Resource loading cancels pending I/O without callbacks retaining assets. Graphics preparation precedes scene encoding; mip source/destination views are disjoint, with explicit clear/store and queued ordering. Submitted-resource retention and shutdown/reinitialization are tested. Replaced the analytic fixture with sampled PNG while retaining the original standalone checkerboard. Every project-defined function has a purpose/contract comment; formatting and documentation checks passed. PBR research edits were preserved. No unrequested dependency pin, public writable-texture API, CPU mip fallback, framework or performance claim was added.

Final validation: cpu-tests configure/build/CTest passed 61 cases / 1016 assertions. Native-debug configure/build passed and all four CTest checks passed: CPU suite, device startup, checkerboard (22 assertions), and scene/texture GPU tests (11 cases / 825 assertions). The native GPU is NVIDIA GeForce RTX 3050 Ti Laptop GPU. Tests cover all ten formats, exact float constants outside [0,1], fp16/fp32 nonconstant odd-size reduction, nearest/linear including fractional mip LOD, two simultaneous textures, reflection failures, orientation and lifetime.

Web configure/build and smoke passed in Chrome 154.0.8037.95. Successful paths have no errors/validation warnings; only the existing power-preference warning appears. The independent browser adapter query reports Intel gen-12lp; RHI does not disclose adapter identity. The actual RHI device supports float32-filterable; masking it during device creation verifies both ordinary color rendering and explicit fp32 rejection. Browser smoke covers PNG/JPEG, fp16/fp32, delayed response while frames continue, cancellation, missing/corrupt input, minification, resize/reload and missing WebGPU. Emscripten's existing Asyncify/WASM-exception warning remains; exercised success/failure/cancellation paths pass. WASM size is 27,257,116 bytes, not a throughput benchmark.

Evidence: artifacts/textures/browser/report.json and scene, scene-resized, scene-reloaded, scene-minified, jpeg, fp16, fp32, fp32-disabled-color and checkerboard screenshots. Native scene.png, scene-resized.png and scene-restored.png plus capture.ps1/stdout/stderr are under artifacts/textures/native. The native probe launched from the artifact directory and completed resize, minimize/restore and normal close. Initial invalid DPI captures were replaced with inspected evidence. CTest logs are copied under artifacts/textures.

clang-format 22.1.3 dry-run, JavaScript syntax checks, local documentation link checks and git diff --check passed. Vendored stb_image SHA-256 matches the pinned upstream copy (2D264EC3E6CE404FC2D0CFFF4476C982EB3EDF5A61122B03884FDEF7BACD9DEB); upstream checkout remains clean. No percentage coverage threshold or performance claim is introduced.
