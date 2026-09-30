# Bootstrap a Slang RHI terrain laboratory for native and browser use

This ExecPlan follows [PLANS.md](../../PLANS.md) and [AGENTS.md](../../AGENTS.md). Status: native dependency integration and a minimal console application's D3D12 startup are verified. The user authorized a main.cpp linked to RHI, clarification of the native build environment, and a native-build skill; browser work remains deferred. Windowing, rendering, UI and terrain milestones remain future work. Keep Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective current.

## Purpose / Big Picture

Establish a small C++ terrain laboratory using Slang RHI in both a native Windows D3D12 window and a browser WebGPU canvas. The observable result is the same diagnostic terrain scene, ImGui controls and numerical output on both targets, with measured startup, resource use and streaming behavior. Native D3D12 is the daily development path; the browser is proved immediately and checked at relevant changes and milestones. This establishes the foundation for the terrain implementation described in [the restart research](../research/terrain-first-restart.md).

## Progress

- [x] (2026-09-30) Reviewed backup terrain, architecture and plans through three independent source investigations.
- [x] (2026-09-30) Checked current Slang RHI/SlangPy sources and recorded concrete browser limitations.
- [x] (2026-09-30) Adapted the ExecPlan template for the clean repository and wrote the terrain-first direction.
- [x] (2026-09-30) Recorded the user's selection of Slang RHI, native-first development with browser support, thin SGL-inspired helpers, ImGui and an early character/follow-camera terrain probe. Added AGENTS.md and a home for project skill workflows.
- [x] (2026-09-30) Expanded AGENTS.md with the user's simplicity, human-readability, function-comment, documentation and testing requirements. Copied .clang-format and .editorconfig unchanged from the inspected RHI revision and verified that their contents match upstream. Checked local documentation links; no application code or runtime tests exist yet.
- [x] (2026-09-30) Added external/slang-rhi as a Git submodule pinned to the reviewed revision, with root CMakeLists.txt including external/CMakeLists.txt and the external file including Slang RHI. Selected native D3D12 only and disabled unused backends, upstream tests/examples and optional vendor integrations.
- [x] (2026-09-30) Native configure and library build passed using CMake 3.26.3, Ninja 1.13.2 and MSVC 19.51. All 79 build steps completed, producing build/native/external/slang-rhi/slang-rhi.lib. Generated configuration confirms only D3D12 is enabled. Browser setup remains deferred.
- [x] (2026-10-01) Added src/main.cpp and target ofg, linked directly to Slang RHI. It exports the Agility SDK selection, creates a D3D12 device with RHI validation, reports the adapter and exits. Native configure/build, direct startup in an ordinary PowerShell, and the GPU-labeled ofg-startup CTest check passed on an NVIDIA GeForce RTX 3050 Ti Laptop GPU.
- [x] (2026-10-01) Added the build-native skill and documented Visual Studio discovery/x64 environment initialization in DEVELOPING.md. Skill validation and clang-format 22.1.3 checks passed. No browser or windowing code was added.
- [ ] Pin and build a compatible native/browser dependency set.
- [ ] Establish independently buildable doctest/CTest CPU tests and a shared ImGui renderer.
- [ ] Run matching terrain rendering and compute scenarios on DX12 and browser WebGPU.
- [ ] Exercise asynchronous uploads, failure handling, retirement and bounded residency.
- [ ] Record reproducible commands, measurements and visual evidence, and prepare the first large-terrain/character milestone.

## Surprises & Discoveries

The backup only renders a 160 m square of synchronous, single-resolution terrain. Its resource registry retains evicted chunk meshes. Its native graphics route is offscreen Vulkan/Dawn rather than a native application. These findings justify a fresh terrain-focused vertical slice.

Slang RHI has an Emscripten integration, including prebuilt WASM Slang compiler libraries, but the inspected upstream example only checks device/queue and buffer behavior. Its Emscripten CI builds without running tests. Indirect draws are disabled in the current RHI WASM command path. None of this proves that ordinary browser graphics or compute fail; those are precisely what this spike must establish.

## Decision Log

2026-09-30, user decision: use C++/Slang RHI, with native Windows D3D12 for most development and Emscripten/WebGPU for the browser. Build and run both early, then test the browser periodically and at compatibility-sensitive changes. This supersedes the earlier open framework comparison; browser removal would require a new product decision.

2026-09-30, proposed scope: use direct or instanced patch draws and explicit compute entry points. Avoid depending on indirect rendering, mesh shaders, bindless resources, CUDA, automatic differentiation or Python-generated bindings for the portable runtime. Offline experiments may use native capabilities independently.

2026-09-30, user direction: establish thin wrappers informed by SGL, not a dependency on the entire SGL/SlangPy runtime. Use Dear ImGui, adapting the relevant SGL integration ideas. Keep a narrow platform/render boundary and a GPU-independent terrain-data target. Bring in a walking character and follow camera soon after basic terrain as a laboratory inspection tool, without importing the old engine wholesale.

2026-09-30, historical alternative: Falcor2 and plain SlangPy were researched as native-first runtimes. The subsequent Slang RHI decision closes that comparison for the current bootstrap. SlangPy remains useful for optional offline terrain experiments.

2026-09-30, proposed tooling: CMake presets with Ninja, an exact-revision RHI Git submodule, doctest plus CTest, separate native/browser binary directories and explicit backend selection. Favor the compatible upstream prebuilt Slang packages to avoid rebuilding the compiler unnecessarily. Do not add a Makefile or npm wrapper merely to invoke CMake.

2026-09-30, user scope update: implement only the Slang RHI submodule and a simple native build chain for now. Root CMake includes external CMake, which includes RHI. Defer browser configuration and application targets. Use ordinary CMake/Ninja commands for this initial library build, with presets and test targets added alongside the corresponding implementation.

2026-10-01, user scope update: add a minimal native main.cpp linked to RHI and a skill explaining how to build it. A console device-startup check proves linking and runtime dependencies without introducing a window system. Use the existing CMake/Ninja setup and discover Visual Studio through vswhere; keep shell initialization in the same process as configuration/compilation.

## Outcomes & Retrospective

The pinned submodule and native CMake integration now build ofg.exe. Direct startup and the CTest integration check create a D3D12 device successfully without a developer-shell runtime dependency. Compilation does need the Visual Studio x64 environment, now documented with installation discovery and the warning that separate tool invocations use separate shells. The build-native skill is validated. The submodule remains unmodified. Rendering, browser execution, terrain tests, memory measurements and performance benchmarks remain unimplemented or unverified; the overall bootstrap is incomplete.

## Contract and Quality Baseline

Both hosts consume the same versioned diagnostic data and explicit shader interfaces. The native host owns its window and presentation; the web host owns canvas, input, resize, asynchronous fetching and the browser callback loop. Shared C++ owns portable terrain data and rendering logic. Rendering APIs must not enter the GPU-independent addressing/data tests.

Global addresses use integer sample/tile coordinates. Compute fixtures initially use a small integer-friendly input and float32 output with a declared comparison tolerance. General GPU erosion is not assumed bit-identical across APIs. Baked artifacts used by clients must have a version/hash and an authoritative producer.

Every allocation has an identifiable owner and eviction path. Resources referenced by queued GPU work are retired only after completion. Requests carry generation identity so a late result cannot populate a newly reused tile slot. Queues, staging data and retired resources count toward memory budgets.

Use doctest for C++ tests and register them with CTest. Establish independently configurable CPU tests, separate native GPU integration checks and browser smoke, plus a practical coverage policy when adding code. CPU-only configuration must avoid adding the RHI target or fetching graphics dependencies. Pin doctest directly for OFG; upstream only creates its own doctest target when its test suite is enabled, so enabling all upstream tests merely to acquire that target is unnecessary. Addressing, stale-request, ownership and failure branches need behavioral tests. GPU/platform exclusions need explicit rationale. A percentage cannot substitute for executing both target paths.

## Context and Orientation

The repository initially contained only `README.md`. It now contains project guidance, the research note, this plan, a Slang RHI submodule, root/external CMake files, src/main.cpp, a native-build skill and native build instructions in `DEVELOPING.md`. Target ofg builds a console application and ofg-startup is a GPU-dependent CTest integration check. There are no build presets, CPU/doctest suites or windowed application yet. `C:\dev\ofg-old2` is a read-only reference, not an implementation dependency.

A render hardware interface, or RHI, wraps different GPU APIs. Slang compiles shader code for those APIs. Emscripten compiles C++ to WebAssembly for the browser. The native SlangPy package adds Python bindings and convenience APIs; it is not an established browser host.

Research snapshots are RHI `16324a68af477baaede620e713644f5e9613b1a2` and SlangPy `bdbc9f3f809c5f6eda7e61e6eecae787db3f9620`. At that RHI revision the build fetches Slang 2026.17.1 and upstream CI uses emsdk 5.0.6. Verify their complete build configuration before selecting the actual OFG lock; do not combine arbitrary latest packages.

## Plan of Work

### Milestone 1: reproducible builds, tests and device bring-up

Create root `CMakeLists.txt`, `CMakePresets.json`, a small `cmake` dependency/toolchain directory, `.gitignore`, `DEVELOPING.md` and minimal native/web hosts. Add `external/slang-rhi` as a Git submodule pinned to the tested revision. Suggested implementation directories are `src/core`, `src/render`, `src/ui`, `src/lab`, `src/terrain`, `src/platform/native`, `src/platform/web`, `shaders`, and `tests`. Create directories as actual content lands rather than manufacturing empty engine subsystems. Keep test fixtures and large/generated data distinct; put transient outputs in `build` and `artifacts`.

Define GPU-independent `ofg_core`, `ofg_render`, a shared laboratory target and separate native/web entry points, with exact names settled during implementation. A CPU-only preset should configure and run doctest without Emscripten, a window, a GPU or RHI downloads. Use a separate pinned doctest dependency so OFG tests do not require the entire upstream RHI test suite.

For native, enable only RHI D3D12 and the SDK pieces required for that path. Explicitly disable unused CPU, D3D11, Vulkan, Metal, CUDA, OptiX and WebGPU backends, optional vendor tooling, upstream examples and upstream tests. The browser preset enables WebGPU and uses Emscripten's port rather than a desktop Dawn build. Audit fetched dependencies: do not promise zero third-party downloads or zero native SDK work. Preserve incremental build trees and record cold versus incremental build time separately.

Start with a small native window/event host (GLFW is the first candidate, consistent with RHI examples) and a browser canvas/event host. Shared application code must not depend on a GLFW window handle. Use the same frame/update interface, but native owns a normal loop while web schedules callbacks. No local graphics build may require the browser toolchain to be installed.

Build both targets from pinned inputs. Record graphics adapter, browser version, backend, enabled features and limits. Bring up a native window and browser canvas, compile the same simple Slang shaders, and present a triangle in both. Verify asynchronous initialization, resize, errors and device teardown. Measure compressed transfer bytes and cold startup. If compiler distribution is too costly, investigate supported precompilation with reflection as a separate experiment; do not claim raw WGSL is a drop-in RHI shader program.

Success is passing CPU-only tests, two inspectable screenshots, logs with correct backend identities, and reproducible clean-build instructions. Upstream buffer-only WASM sample success is insufficient.

### Milestone 2: shared ImGui, terrain and compute on both targets

Implement one small ImGui draw-data renderer using RHI, with native and browser event adapters. SGL's `src/sgl/ui/ui.cpp` and `imgui.slang` are references; they rely on SGL resource wrappers and include software rasterization that is unnecessary here. Adapt only the hardware draw path and retain notices/provenance for any reused code. Use portable upload staging and safe per-frame buffer reuse rather than copying native mapped-buffer assumptions blindly. Handle fonts/textures, alpha blending, scissor rectangles, index/vertex offsets, resize/DPI, and keyboard/mouse capture. ImGui input capture must prevent the camera/character from responding while editing a control. Multi-window platform viewports are outside the bootstrap.

Show backend, frame timings, seed and a simple terrain/compute parameter in the same ImGui panel on both targets. A UI change must visibly affect the shared scene. Reuse RHI's reference-counted handles and shader cursor before adding custom resource wrappers or reflection APIs.

Replace the triangle with a small set of regular heightfield patches. Upload a float32 height field, obtain normals from one shared convention, and render via direct/instanced draws. Use a portable compute kernel that modifies a diagnostic height field with a known expected result. Validate compute output by asynchronous readback and consume it in rendering without a blocking per-frame CPU readback.

Test buffer and texture paths actually needed by the proposed terrain implementation, sample/texture format support, shader reflection/binding layout, compute-to-render ordering and repeated frame submission. Use integer global patch addresses and camera-relative float transforms. Capture a simple far-from-origin fixture. This is a capability probe, not the final terrain LOD system.

Success requires numerical output within declared tolerances, visibly matching terrain on both platforms, no validation errors, and documented unsupported operations. CPU frame timing is always available; use GPU timestamps only if the pinned RHI/backend actually supports them. Label CPU timings accurately.

### Milestone 3: small streaming and lifetime stress case

Fetch a small static tile dataset asynchronously through the web host and the corresponding native loader. Introduce variable delay, missing/corrupt data, reordered completions and teleports. Keep a resident coarse diagnostic surface while finer data are unavailable. Bound resident tiles, in-flight jobs, decoded bytes, staging bytes and GPU allocations. Discard stale completions and safely retire resources.

Run a ten-minute repeated route and teleport cycle on both hosts. Live allocated bytes and live resource counts, including pending retirement, must reach a stable plateau; cumulative allocation counters may keep increasing. Queue depth must recover after load ends. Record CPU time distributions, uploads per frame, memory estimates and queue latency. A process RSS measurement alone does not establish GPU residency; account for owned resource sizes and pending retirement explicitly.

Do not introduce browser worker threads as a hidden dependency. If needed, add and test deployment requirements deliberately. A single-threaded cooperative baseline should remain observable while the threading model is evaluated.

### Milestone 4: record the baseline and hand off to terrain implementation

Summarize results, integration changes and outstanding gaps for the selected RHI stack. If small upstream repairs are needed, isolate and document them. If a required capability fails, report the specific issue and viable fixes before changing the selected platform/framework direction. Do not silently remove browser support.

Write the next ExecPlan around multiresolution terrain, the first hydrology bake and early human-scale traversal, using the research note's continuity and residency contracts. Once there is a stable sampleable surface, add a simple character controller and follow camera, then one small glTF character with idle/walk animation. A placeholder body can establish terrain contact before asset import. Use the terrain's canonical height/query data, not GPU readback or a separate visual-only height approximation. A capsule/heightfield walking model is enough initially; define slope, grounding and behavior when detailed terrain is unavailable. Walking and fly-camera modes should make seams, scale, slopes and load delays easy to inspect. Do not expand this into inventories, multiplayer or a general physics/animation engine.

Create the initial repository skills around verified workflows. `review-plan` should inspect scope and acceptance; `milestone-review` should inspect the actual diff and evidence; `graphics-smoke` should automate the proven native/browser scenario. Keep ordinary rules in AGENTS.md and exact commands in DEVELOPING.md. Use the skill-creator workflow when authoring skills.

## Concrete Steps

From `C:\dev\ofg`, begin with `git status --short` and inspect available CMake, Ninja, native compiler and emsdk versions. Preserve unrelated changes. Create the build targets before attempting these proposed interfaces:

    cmake --preset native-debug
    cmake --build --preset native-debug
    ctest --preset native-debug --output-on-failure
    emcmake cmake --preset web-debug
    cmake --build --preset web-debug

Add a `cpu-tests` configure/build/test preset for GPU-independent work. The names above remain proposed until implemented. Pin compiler/toolchain choices during bring-up; MSVC and clang-cl are native options, while browser builds use Emscripten. Do not inherit the previous project's Clang-only requirement without a concrete reason.

These commands are specifications for targets to create, not currently runnable project commands. Milestone 1 must record tested commands for launching the native executable and serving the browser output, the actual localhost URL, and browser automation. Keep the server available during visual work. Add no public deployment in this spike.

## Milestone Review

Before completing each milestone, check source ownership, cross-platform behavior, numerical assumptions, failure paths, obsolete scaffolding, docs, and actual validation evidence. Fix actionable findings and rerun affected checks. No legacy review skill or historical API-contract document is installed in this fresh repository. Record the review directly unless an applicable skill is later added.

Research-stage review: an independent documentation review found one ambiguity in the residency acceptance criterion. It now requires bounded live bytes/resources, including pending retirement, rather than a plateau in cumulative allocation counts. The proposed boundaries address the backup's resource leak and GPU-coupled data code; platform limitations are explicitly gated; dependency revisions and provenance are recorded. Runtime acceptance is deliberately unclaimed.

Native dependency integration review: checked the root-to-external-to-RHI build chain, pinned Git submodule entry, unmodified dependency checkout, generated backend flags, ignored build outputs and documentation. Configure/build passed. No application behavior or test targets were added in this scoped step, so unit tests, coverage and screenshots are not applicable yet. Milestone 1 remains incomplete until its application and test deliverables are implemented.

Native application slice review, 2026-10-01: the entry point has documented purpose, direct RHI ownership through ComPtr, nonzero failure status, and the required Agility SDK exports. Its build/run/CTest integration and formatting checks pass. The build skill passes the skill-creator validator and describes the tested environment procedure. Coverage exception for this small startup slice: device initialization is integration-tested; no artificial unit mocks or coverage toolchain were introduced for the platform glue. The failure branch is inspected but not fault-injected. Establish coverage for actual application logic in later work. No screenshot is applicable to this console-only result, and Milestone 1 remains incomplete.

## Validation and Acceptance

Record native and browser hardware/driver/API identity and viewport for every comparison. Test graphics, compute, ImGui interaction/input capture/DPI, upload, readback, resize, camera movement, distant coordinates, request cancellation, failures and teardown. Unsupported-device startup must explain the failure rather than hang or show an empty canvas. During ongoing work run native tests routinely, browser smoke at every completed rendering milestone, and browser checks immediately after changes to shared shaders/bindings, GPU lifetimes, platform I/O, UI integration or dependencies.

The initial compute fixture should choose values exactly representable in float32 and compare against a CPU reference; a provisional absolute tolerance of 1e-5 applies to normalized outputs. Establish a separate physically meaningful tolerance for later erosion, not a universal pixel or height threshold. Compare images structurally, allowing documented backend raster differences.

Save screenshots after bring-up, terrain compute integration and the final stress scenario under `artifacts/portability`. Present representative native/browser images during implementation. Keep timing, resource counters, feature reports and run metadata alongside them. Performance goals are set against named hardware after the baseline; there is no established universal FPS or memory number yet.

Completion means both platform checks pass, long-run resource use is bounded, all required operations have demonstrated behavior, test commands and coverage exclusions are recorded, and the decision report explains costs and remaining limits. Passing a build alone does not satisfy this plan.

## Idempotence and Recovery

Keep downloaded dependencies and generated outputs separate from source. Pin versions and artifact formats so retries reproduce inputs. Use generation epochs for requests and reject stale data after cancellation. Repeated startup/teardown must release resources safely. Preserve the backup and keep framework-specific code small enough that a failed experiment does not require abandoning terrain formats and numerical fixtures.

## Artifacts and Notes

Source evidence inspected on 2026-09-30:

| Claim | Pinned source |
| --- | --- |
| RHI maturity | [README](https://github.com/shader-slang/slang-rhi/blob/16324a68af477baaede620e713644f5e9613b1a2/README.md) |
| WebGPU selection and Slang WASM libraries | [CMakeLists.txt](https://github.com/shader-slang/slang-rhi/blob/16324a68af477baaede620e713644f5e9613b1a2/CMakeLists.txt#L401) |
| Asyncify/memory-growth configuration | [CMake presets](https://github.com/shader-slang/slang-rhi/blob/16324a68af477baaede620e713644f5e9613b1a2/CMakePresets.json#L83) |
| Emscripten build-only CI | [CI workflow](https://github.com/shader-slang/slang-rhi/blob/16324a68af477baaede620e713644f5e9613b1a2/.github/workflows/ci.yml) |
| Browser example scope | [wasm-test.cpp](https://github.com/shader-slang/slang-rhi/blob/16324a68af477baaede620e713644f5e9613b1a2/examples/wasm/wasm-test.cpp) |
| WASM indirect-draw omission | [wgpu-command.cpp](https://github.com/shader-slang/slang-rhi/blob/16324a68af477baaede620e713644f5e9613b1a2/src/wgpu/wgpu-command.cpp#L617) |
| Shader component/reflection interface | [slang-rhi.h](https://github.com/shader-slang/slang-rhi/blob/16324a68af477baaede620e713644f5e9613b1a2/include/slang-rhi.h) |
| SlangPy native platform selection | [SlangPy CMake](https://github.com/shader-slang/slangpy/blob/bdbc9f3f809c5f6eda7e61e6eecae787db3f9620/CMakeLists.txt) |
| SGL ImGui hardware draw path and input handling | [ui.cpp](https://github.com/shader-slang/slangpy/blob/bdbc9f3f809c5f6eda7e61e6eecae787db3f9620/src/sgl/ui/ui.cpp) |
| SGL ImGui shaders | [imgui.slang](https://github.com/shader-slang/slangpy/blob/bdbc9f3f809c5f6eda7e61e6eecae787db3f9620/src/sgl/ui/imgui.slang) |
| Existing RHI binding helper | [shader-cursor.h](https://github.com/shader-slang/slang-rhi/blob/16324a68af477baaede620e713644f5e9613b1a2/include/slang-rhi/shader-cursor.h) |

Browser filesystem and event-loop responsibilities follow [Emscripten runtime guidance](https://emscripten.org/docs/porting/emscripten-runtime-environment.html). If pthreads are adopted, cross-origin isolation and main-thread blocking restrictions follow [Emscripten threading guidance](https://emscripten.org/docs/porting/pthreads.html). Cross-origin isolation is a threading requirement here; do not copy the old README's claim that WebGPU itself universally requires those headers.

## Interfaces and Dependencies

The stable boundaries are diagnostic terrain data, explicit Slang kernel inputs/outputs, a narrow renderer, native/web hosts and asynchronous tile requests. Concrete class/function signatures should be chosen during implementation rather than importing old singleton APIs. Use a versioned manifest for units, coordinates, generator version, channel layouts and content hashes. Reserve Python/SlangPy tooling for producing or examining compatible artifacts without making it a runtime dependency of the browser application.

Revision note, 2026-09-30: revised from an open framework comparison into the user's selected Slang RHI terrain-laboratory bootstrap. The first authorized implementation step is now the minimal native dependency build; browser configuration is explicitly deferred. Application, testing, UI and terrain milestones remain future work.
