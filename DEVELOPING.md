# Developing OFG

The default application opens the [dockable laboratory workspace](docs/imgui.md), with a PBR Scene viewport, Scene Hierarchy and Render Settings. Native Windows D3D12 and browser WebGPU share the UI and scene renderer. --scene / ?demo=scene retain the checker-cube fixture. Select the original full-screen checkerboard using `--checkerboard` natively or `?demo=checkerboard` in the browser.

See the [architecture note](docs/architecture.md) for source responsibilities and the checkerboard reference flow. Agent workflows are documented in the [native build skill](.agents/skills/build-native/SKILL.md) and [browser build skill](.agents/skills/build-web/SKILL.md).

## Prerequisites

Use an x64 Visual Studio Developer PowerShell with the C++ tools and Windows SDK installed. Git, CMake 3.24 or newer, and Ninja must be on PATH. Emscripten, Python and a native Dawn checkout are not required.

## Starting from ordinary PowerShell

The compiler needs more than `cl.exe` on PATH: Visual Studio also supplies SDK include/library paths, the linker and resource tools. Initialize the x64 developer environment in the shell that will configure and build:

```powershell
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Visual Studio C++ tools were not found.' }

& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
```

This discovers the installed edition rather than hard-coding its path. It prepares the current PowerShell process; it does not change the machine-wide environment or open a separate window. `-SkipAutomaticLocation` preserves the working directory.

Agent command tools usually start a fresh shell for each invocation. Run this initialization and the build commands in the same invocation, or initialize again in each new shell. Opening a developer environment in an earlier command does not prepare later commands. A normal x86 command prompt is not the intended configuration.

## Configure and build

From the repository root:

```powershell
git submodule update --init --recursive
cmake --preset native-debug
cmake --build --preset native-debug --parallel
```

The root `CMakeLists.txt` includes `external/CMakeLists.txt`, which selects D3D12 for native builds or WebGPU for Emscripten, and adds `external/slang-rhi`. Git records the exact RHI revision in the submodule entry; do not update it implicitly during configuration.

The first configuration downloads the prebuilt Slang compiler, DirectX dependencies and GLFW. GLFW is pinned to `b00e6a8a88ad1b60c0a045e696301deb92c9a13e`, matching RHI's native examples. With `BUILD_TESTING=OFF`, build only target `ofg`. Generated files and downloads stay under `build/native`. Subsequent builds reuse that directory.

Check each command's exit status before continuing. Keep using the same build directory for incremental builds. There is no custom dependency manager or build-wrapper script. The executable lives at `build/native/ofg.exe`, beside the Slang DLLs and the `D3D12` directory copied by upstream CMake. Keep those runtime files with the executable.

## Run and test

These commands work from an ordinary PowerShell; launching the built application does not require the developer environment:

```powershell
.\build\native\ofg.exe
.\build\native\ofg.exe --check-device
ctest --test-dir build/native --output-on-failure
```

Normal launch prints the selected adapter and opens a 960-by-640 framebuffer showing the stationary procedural scene. `ofg.exe --checkerboard` selects alternating dark/light 64-pixel squares. Resize or maximize the window: the scene projection tracks aspect ratio; the checkerboard diagnostic retains 64-pixel squares. Minimize/restore should resume rendering. In the workspace, Escape releases camera capture; the close button exits. The --no-ui diagnostic retains its earlier Escape-to-close behavior. The shader is embedded at build time from `shaders/checkerboard.slang` and `shaders/mesh.slang`, so launch does not depend on the current working directory. Editing the shader triggers CMake regeneration and recompilation on the next build.

`--check-device` creates the device, prints `OFG initialized D3D12 on <adapter name>.` and exits without a window. Failure returns nonzero. CTest runs four finite checks with a 30-second timeout. The core suite is labeled `cpu`; the others are labeled `native` and `gpu`:

- `ofg-core`: GPU-independent doctest suite for state lifecycle, scene hierarchy/transforms, observer lifetime, resource loading/lifetime and CPU math.
- `ofg-startup`: device creation through `ofg --check-device`.
- `ofg-checkerboard`: a doctest GPU integration test using the same pipeline and draw as the application. It verifies every RGBA8 UNORM pixel at 1-by-1, 129-by-97 and 259-by-193, including alpha, partial cells and row pitch. RHI validation errors fail the test. Expected gray levels are exactly 32 and 224; no tolerance is needed for these constant UNORM values.

The GPU tests require a working D3D12 adapter/runtime. The window uses the surface's preferred format, which may apply sRGB encoding to the shader's linear gray levels; screenshots need not have the same byte values as the UNORM offscreen test. All C++ tests use the independently vendored [doctest 2.4.11 header](external/doctest/README.md). None of the automated C++ tests opens a window, so presentation and resize still need visual inspection.

The native entry point exports the Agility SDK version/path using Slang RHI's helper. This selects the copied D3D12 runtime without requiring a system-wide installation or a custom PATH for the application.

## CPU-only build and tests

Use the same Visual Studio x64 environment setup above, then:

```powershell
cmake --preset cpu-tests
cmake --build --preset cpu-tests --parallel
ctest --preset cpu-tests
```

This separate `build/cpu-tests` tree sets `OFG_BUILD_APP=OFF`. It builds only `ofg-core` and `ofg-core-test`, without adding Slang RHI, GLFW or graphics downloads. The vendored doctest header is independent of the RHI submodule, so submodule initialization is unnecessary for this preset. Native-debug includes the same core suite; use `ctest --preset native-debug -L cpu` or `-L gpu` for an already-built subset. An application build with tests disabled uses `cmake --build build/native --target ofg` instead of the test-inclusive build preset.

See [state and scene contracts](docs/state-and-scene.md) and [resource loading](docs/resources.md) for usage, ownership, provenance and limits. Core tests are native; browser smoke remains focused on rendering. The initial state/scene import passed 40 cases and 826 assertions, including all 16 legacy state and 11 math cases, on 2026-10-01. The resource foundation expands that suite to 48 cases and 878 assertions, all passing on the same date. No numerical coverage percentage has been measured or required for this import; behavioral acceptance is recorded in its [plan](docs/archived/import-state-and-scene.md).

## glTF resource foundation checks

The first [glTF milestone](docs/plans/gltf-model-loading.md) established Skin/Animation resources, Mesh deformation
data, MeshRenderer instance bindings and passive Animators. That milestone preceded file import and instantiation;
the Model laboratory section below describes their current implementation. Playback and deformation remain future work. Run the CPU presets above; to isolate the new contracts after building:

```powershell
.\build\cpu-tests\ofg-core-test.exe --source-file="*model-resources-test.cpp" --no-colors
```

Verified on 2026-10-04 in the `gltf-model-loading` worktree: CPU configure/build/CTest pass, with 77 cases and 1807
assertions in the full suite. The focused file contributes 12 cases and 196 assertions covering factory validation,
independent instance bindings, failure atomicity, passive updates, resource retention, and scene growth/moves/clear.
The native-debug configure/build and all four CTest targets also pass. No new draw layout or deformation shader is
introduced. Focused output is saved as `artifacts/models/resource-tests.txt`; the plan records portability evidence.

The web configure/build, `npm.cmd run smoke:web` and `node tools/pbr-smoke.mjs` also pass in this worktree, in Chrome
154.0.8037.95. Both browser reports have no recorded errors. The existing Asyncify/WASM-exception build warning and
Windows powerPreference browser warning remain. Native offscreen and browser PBR overview captures were inspected;
these are regression evidence for existing rendering, not a demonstration of glTF import or animation.

## Animation bring-up

The [completed animation ExecPlan](docs/archived/gltf-animation.md) records the character milestone. The preserved
CC0 superhero/UAL1 assets live in `assets/models/character`, with byte hashes and original provenance. Model
instantiation preserves imported node names. TRS playback, all-influence compute skinning, character launch modes and
the Animation panel are implemented. The [character guide](docs/animation.md) describes controls and the direct-mapping
limitation. Run the documented `cpu-tests`, `native-debug` and `web` configure/build/test workflows above, then:

```powershell
./build/native/ofg.exe --character
./build/native/ofg.exe --character-pair
./build/native/ofg-render-test.exe '--test-case=Compute skinning*,Animated superhero*' --no-colors
node node_modules/playwright-core/cli.js install ffmpeg
node tools/animation-smoke.mjs
```

The ffmpeg helper is needed only for Playwright motion recording (v1011 from the pinned Playwright CLI); it does not
replace the installed browser or SDK. Use `npm.cmd run serve:web`, then `http://127.0.0.1:8080/?demo=character`;
append `&instances=2` for independent instances or `&ui=0` for full-canvas viewing. Native asset lookup is relative
to the executable; the browser server explicitly allowlists both GLBs. Build/test logs, pose sequences, PNG captures
and browser motion video are saved under `artifacts/animation`. Re-run model/UI/PBR/texture smokes after shared changes.

The first portable experiment translates a 65-vertex fixture using 64-thread compute groups, then consumes the
72-byte output directly as a vertex stream. Its native test is included in `ofg-scene-rendering`; run it alone after
the native build with:

```powershell
./build/native/ofg-render-test.exe '--test-case=Compute vertex*' --no-colors
```

After configuring the normal `web` preset, build the separate small RHI browser proof and inspect it with:

```powershell
cmake --build build/web --target ofg-compute-proof-web --parallel 6
node tools/compute-vertex-smoke.mjs
```

The browser tool serves only the three `compute-proof` build outputs on a temporary loopback server and records
left/right/left-again captures and diagnostics under `artifacts/animation/browser`. It uses the same C++ helper and
Slang shader as native, through the pinned RHI WebGPU backend. Native captures are under `artifacts/animation/native`.
This separate experiment tests translation/packing/queue order. Actual skinning uses `render/deformation.cpp` and
`shaders/deformation.slang`. Test-only proof code remains in `tests/compute-vertex-*`.

Verified on 2026-10-04 in the gltf-animation worktree: CPU CTest passes 104 cases/11,135 assertions and all four native
CTest targets pass on NVIDIA GeForce RTX 3050 Ti Laptop GPU. Focused tests also pass after the final lifetime, scene
replacement and layout fixes. Native captures cover idle, walk, sprint, a one-shot, stable pause and independent instances.
The Emscripten build and Chrome 154.0.8037.95 animation smoke pass on the actual RHI Intel gen-12lp WebGPU adapter,
including panel controls, scrubbing, pause/resume, independent instances, hidden viewport updates, resize/reload,
full-canvas fly/reset and loading cancellation. Model, UI, PBR and texture/checkerboard smokes pass too. The separate
translation proof passes on both backends. Captures and motion transitions were inspected; browser reports contain
no errors and only the known Windows powerPreference warning. See `artifacts/animation/browser/animation-report.json`
for checks and the recorded video path. No performance improvement is claimed.

## Formatting

Use the checked-in `.clang-format`. The verified formatter is clang-format 22.1.3 from the Visual Studio C++ Clang tools. After the discovery block above, check the current source with:

```powershell
$projectSources = @(rg --files src tests -g '*.cpp' -g '*.h')
& (Join-Path $vsRoot 'VC\Tools\Llvm\x64\bin\clang-format.exe') --dry-run --Werror $projectSources
```

Those optional Clang tools are needed for that formatter command, not for the MSVC build. Formatting must not modify the RHI submodule.

## Verified baseline

The application configure/build, direct startup, CTest startup check and formatting check passed on 2026-10-01 with CMake 3.26.3, Ninja 1.13.2 and MSVC 19.51 in an x64 Visual Studio 2026 developer environment. Startup ran on an NVIDIA GeForce RTX 3050 Ti Laptop GPU. RHI is pinned to `16324a68af477baaede620e713644f5e9613b1a2`, using its prebuilt Slang 2026.17.1 dependency.

The generated RHI configuration confirms D3D12 is enabled and all other graphics backends are disabled. The native checkerboard build and both tests passed on the same hardware on 2026-10-01. Live window inspection verified rendering, maximize/resize, minimize/restore and clean shutdown. Screenshots are saved locally under `artifacts/checkerboard/native.png`, `native-maximized.png` and `native-restored.png`. That checkerboard milestone did not verify texture sampling, compute or ImGui; later texture and workspace evidence is recorded below.

The repository [build-native skill](.agents/skills/build-native/SKILL.md) guides agents through this workflow and its environment requirements.

## Separate native and browser targets

CMake presets keep the toolchains and outputs separate. The native preset reuses the existing native build directory:

```powershell
cmake --preset native-debug
cmake --build --preset native-debug
ctest --preset native-debug
```

The browser target is `ofg-web`, selected by the `web` build preset. It produces `build/web/index.html`, `index.js` and `index.wasm`; it does not build GLFW, DirectX, desktop Dawn or the C++ test executables. The same `ofg-core` and `ofg-render` sources and embedded Slang shader are compiled by each toolchain. There is no Makefile or npm layer around compilation.

## Build for the browser

The tested SDK is Emscripten 6.0.0, already installed at `C:\tools\emsdk` on this machine. Activate the SDK using its normal setup so `EMSDK` points at its root and its compiler can find Node/Python. CMake reads the Emscripten toolchain from `EMSDK`; it does not download or switch SDK versions. Have CMake and Ninja on PATH. No MSVC compilation is involved in the browser build. If using the bundled Visual Studio Ninja, the discovery block above gives `$vsRoot`; add only its Ninja folder when needed:

```powershell
$env:PATH = (Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja') + ';' + $env:PATH
emcc --version
cmake --preset web
cmake --build --preset web --parallel 8
```

Stop if any command fails. The first build fetches the pinned RHI's Slang 2026.17.1 WASM libraries and uses Emscripten's `emdawnwebgpu` port. It does not build the Slang compiler from source. The final link still optimizes the bundled compiler and takes substantially longer than a native incremental link. Routine builds reuse `build/web`; leave `build/native` intact.

The browser currently includes the runtime Slang compiler: the initial verified WASM output was 26,795,946 bytes (about 25.6 MiB), before HTTP compression. This is a bring-up baseline, not an optimized distribution-size target. Emscripten warns about combining Asyncify and WASM exceptions. The startup settings originate in the pinned RHI preset. Scene rendering extends the Asyncify allowlist to OFG/RHI, browser frame calls and Emscripten callback thunks because buffer uploads and uniform staging maps can yield. Emscripten pauses/resumes its main loop across those yields. The browser renders into a persistent host-owned texture and acquires the canvas only for the final presentation pass, because canvas textures expire across event-loop turns. Arbitrary exception paths through suspended calls are not established by the smoke check. Revisit this with any async/lifecycle expansion.

## Serve and inspect the browser build

Node is used for browser tooling only. The checked-in lockfile pins `playwright-core` 1.61.0 and `pngjs` 7.0.0. Node 24.14.0 and installed Google Chrome were used for verification; the smoke script does not download a browser. From ordinary PowerShell:

```powershell
npm.cmd ci
npm.cmd run serve:web
```

Open `http://127.0.0.1:8080`. Stop the server with Ctrl+C. Serve the generated files over localhost or HTTPS, rather than opening the HTML with `file://`. This single-threaded build needs no cross-origin isolation headers. The small loopback server serves the generated HTML/JS/WASM and explicitly allowlisted fixture images from build/web/assets.

In another shell, run the focused browser check:

```powershell
npm.cmd run smoke:web
# Optional visible browser for debugging:
npm.cmd run smoke:web -- --headed
```

The script starts its own server on a free loopback port and closes it and Chrome afterward. It checks scene rendering, checker cells and isolated tint, resize, reload, the original checkerboard diagnostic, and the missing-WebGPU error message. Scene and checkerboard screenshots and `report.json` are saved under `artifacts/scene-rendering/browser`. The report includes Chrome version, console/errors, image sizes and browser adapter diagnostics. The screenshots' checkerboard cells are checked against their pixel boundaries; native's full C++ test suite stays native. A successful browser check is not a claim that all RHI features work on WebGPU.

The native surface and browser surface may prefer different sRGB/UNORM formats. The smoke check accepts either consistent encoding of the shader's two gray levels; it still checks every cell boundary. Browser privacy may omit the RHI adapter name, so the app reports `browser-selected adapter` rather than guessing the native GPU. Chrome can warn that Windows ignores `powerPreference`; this is retained in the report rather than counted as a rendering error.

Build commands and test registration are in `CMakePresets.json` and `CMakeLists.txt`; automation lives in `tools/browser-smoke.mjs`. The [Playwright library documentation](https://playwright.dev/docs/library) covers the screenshot/console workflow used here.

Browser build and smoke passed on 2026-10-01 with Emscripten 6.0.0, Chrome 154.0.8037.59 and Playwright 1.61.0. Captured canvases were 960x641 initially and 773x478 after resize/reload. Console errors were empty; the missing-WebGPU message also passed. An independent browser adapter query reported Intel gen-12lp, while RHI omitted its description. Native preset configuration/build and both native CTest checks passed afterward.

## Scene-object rendering checks

`ofg-scene-rendering` runs the scene GPU/Game tests in `ofg-render-test`; `ofg-checkerboard` selects the original exact pixel test. The CPU suite now also covers transform caches, typed components, meshes/materials and draw extraction. Run the existing cpu-tests and native-debug build/test presets and `npm.cmd run smoke:web`. Uniform tests allow one byte of UNORM quantization error for fractional values; flat primary colors remain exact. Browser geometry checks avoid cross-backend edge equality.

Use `.\build\native\ofg.exe --scene` for the retained scene, `.\build\native\ofg.exe --checkerboard` for the diagnostic, and `--check-device` for finite startup. Browser equivalents are `http://127.0.0.1:8080/?demo=scene` and `http://127.0.0.1:8080/?demo=checkerboard`. Game owns application updates; hosts retain event loops and presentation. There are no camera controls in this stationary fixture.

Native presentation evidence lives under `artifacts/scene-rendering/native`: `scene.png`, `scene-resized.png`, `scene-restored.png`, stdout/stderr logs and the local `capture.ps1` probe. The probe targets the rendering window (not its console), resizes and minimizes/restores it, then requests normal close. These screenshots supplement the offscreen integration tests.

In restricted agent shells, Emscripten needs write access to its installed SDK cache and temporary directory. A denied cache lock can look like an idle linker. Do not delete the SDK/cache or change toolchains: request the required build access, inspect only the task's processes, and stop a stalled task before retrying. `emsdk_env.ps1` also writes a generated script into the SDK. An already configured SDK can be selected with process-local `EMSDK=C:/tools/emsdk` and the documented Ninja path instead.

Scene-object implementation verified on 2026-10-03: cpu-tests passes 55 cases/959 assertions; native-debug passes all four CTest checks (7 render cases/178 assertions). The final web build and smoke pass in Chrome 154.0.8037.95 on the browser-selected adapter (independent query: Intel gen-12lp). Native uses NVIDIA GeForce RTX 3050 Ti Laptop GPU. All six browser captures were generated; the initial/resized/reloaded scene images were inspected. The report contains no errors or validation warnings, only the documented power-preference warning. The final WASM is 27,140,650 bytes before compression. This records build size, not a performance target.

The [completed scene-object plan](docs/archived/scene-object-rendering.md) records the checks, lifetime findings and limitations. The browser's final presentation uses `src/render/present.*` and `shaders/present.slang`: a resource-only image-load pass avoids both uniform staging yields and an unsupported surface copy-destination requirement. Native offscreen tests compare its output pixels. This internal presentation texture does not introduce material texture support.


## Texture checks and assets

The retained texture scene (`--scene` / `?demo=scene`) loads `assets/checker.png` on demand and samples it with GPU-generated mipmaps. CMake copies fixture assets to every build tree and watches them for regeneration. Native resolves assets relative to the executable, so launching from another directory remains supported. Browser assets are separate HTTP requests, not embedded WASM downloads. The original checkerboard remains selectable.

Public interfaces and examples are in [resource contracts](docs/resources.md#sampled-textures-views-and-samplers). PNG/JPEG import produces RGBA8 sRGB. Procedural uploads support UNORM8, float16 and float32, with default/ranged views and shared immutable samplers. Browser fp32 requires the actual device's optional `float32-filterable` feature; unsupported requests fail explicitly. The current RHI pin remains unchanged: a generated-source WebGPU format-report correction is applied during configuration.

Use the existing cpu-tests/native-debug configure/build/test presets and web build plus `npm.cmd run smoke:web`. CPU tests include real packaged PNG/JPEG loads. Native GPU tests include all ten formats, padded rows, float values outside [0,1], per-mip readback, odd-border area reduction, sRGB/alpha correctness, named texture/sampler validation and lifecycle restart. These tests execute within the existing CTest targets. Numeric tolerances are two UNORM bytes, normalized fp16 2e-3 and fp32 1e-5; exactly representable constant fixtures require exact results.

Browser diagnostic URLs include `?texture=assets/checker.jpg`, `?float=16`, and `?float=32`. The smoke suite holds an image response until frames prove pending loading, verifies cancellation, tests missing/corrupt assets, and removes float32-filterable from actual device creation to check the unsupported path. Expected failure diagnostics are recorded separately from successful-render errors. It compares checker/tint regions within visible geometry because filtering changes the count of flat white pixels at smaller viewports.

Current texture evidence is saved under `artifacts/textures/browser` and `artifacts/textures/native`. Native capture must account for Windows DPI scaling; the recorded probe captures the actual application window, resizes, minimizes/restores and closes it. The test process launches from the artifact directory to exercise asset-path independence. Browser images use the existing surface encoding; native/browser pixels are not asserted identical. These checks do not measure throughput or long-run residency.

Texture milestone verified on 2026-10-03: CPU-only 61 cases/1016 assertions; all four native CTest checks pass, including 11 scene/texture GPU cases/825 assertions and the checkerboard regression. Native window tests ran on NVIDIA GeForce RTX 3050 Ti Laptop GPU. Web build and extended smoke pass in Chrome 154.0.8037.95 (independent adapter query: Intel gen-12lp). Success paths report no validation errors; missing/corrupt assets and unsupported fp32 report their expected failures. The existing Emscripten Asyncify/WASM-exception warning remains. WASM is 27,257,116 bytes before compression. See the [completed texture plan](docs/archived/texture-support.md) for evidence and remaining limits.

## PBR laboratory

The default native/browser scene is the [PBR sphere grid](docs/pbr.md). Native launch is `build/native/ofg.exe`;
`--scene` retains the textured cubes and `--checkerboard` retains the original diagnostic. Browser equivalents are
`?demo=scene` and `?demo=checkerboard`; no query selects PBR. The grid legend, fixed lighting, camera controls,
material API and output policy are documented in `docs/pbr.md`.

After the normal native/web build commands, run:

```powershell
node tools/pbr-shader-check.mjs
node tools/pbr-smoke.mjs
npm.cmd run smoke:web
```

The fast shader check uses the native preset's pinned `build/native/_deps/slang-src/bin/slangc.exe`; an optional first
argument selects that same compiler from another build location. It checks actual Chrome WGSL diagnostics and numeric
vertex locations. The PBR runtime smoke checks the full browser path, including `?pbr=budget`. They use the existing
locked Playwright/Chrome tooling and serve only loopback. Reports are in `artifacts/pbr/shaders` and `artifacts/pbr/browser`.

Optional environment regeneration (not a build prerequisite): `python tools/bake-pbr.py`, with NumPy 1.26.4 used for the
committed bake. Rebuilding then embeds `assets/pbr-studio.bin` on both targets. Preserve shader/license notices under
`shaders/pbr`. The scene GPU CTest timeout is 120 seconds to cover cold compilation of surface/texture variants; startup
and checkerboard checks retain their 30-second timeouts. The native PBR test saves `artifacts/pbr/native/offscreen.ppm`
and reports first-use and ten warm submit-plus-queue-wait timings at 960x640; these are debug/validation measurements,
not GPU-only frame times or an FPS promise.


PBR verified on 2026-10-03: CPU-only 65 cases/1611 assertions; all four native CTest targets pass, including 19 scene/PBR/texture cases/1009 assertions and the 22-assertion checkerboard test. Web build, shader validation, PBR smoke and the retained texture smoke pass. Native captures cover overview/close-up, fly/reset, resize, minimize/restore and Escape close. Browser captures cover the same inspection poses, pointer lock, focus-loss clearing, debug normals, resize/reload and maximum bindings. See [the PBR plan](docs/plans/pbr-rendering.md#artifacts-and-notes) for named hardware, timings, payload sizes and the remaining reference-parity/residency validation gaps.

The property-naming revision was verified on 2026-10-04 with native/web incremental builds, all four native CTest targets, `node tools/pbr-shader-check.mjs` and `node tools/pbr-smoke.mjs`. Native window inspection and fixed-pose pixel comparisons passed; native and browser captures match their respective pre-change images exactly. Evidence is under `artifacts/pbr/naming`. The material interface now uses descriptive names documented in `docs/pbr.md`.

## Model laboratory

After a native build, run `build/native/ofg.exe --model assets/models/laboratory.gltf` from the repository root
(or pass an absolute model path from any directory). Relative model paths are relative to the launch directory;
dependencies are relative to the model. The default PBR mode is unchanged. Browser mode is
`?demo=model&asset=assets/models/laboratory.gltf`, or use `assets/models/laboratory.glb` for the embedded-image fixture.
The model mode also uses the ImGui workspace. Add native `--no-ui` or browser `&ui=0` for full-canvas rendering.
The initial camera is at (0,0,-5). Fly controls match PBR; R and F restore that model pose. Models are imported at their
authored scale and origin, without automatic framing. The window title/browser header and console report readiness,
resource counts and initially paused playback. Parser warnings are reported in the console.

`node tools/generate-model-fixture.mjs` reproducibly writes the original cube glTF, buffer and GLB. It uses the existing
OFG checker PNG. CMake configure copies assets to each build directory. `node tools/model-smoke.mjs` runs the browser
model check after a web build: external glTF, GLB embedded PNG, resize, frames during a held dependency response, and
cancellation without late publication. It saves screenshots and diagnostics under `artifacts/models/browser`.
Native `ofg-scene-rendering` includes imported-model culling and multiple-instance rendering and saves
`artifacts/models/native/instances.ppm`. CPU cases are in `tests/gltf-test.cpp` and `tests/model-resources-test.cpp`.

Model import/instantiation verified on 2026-10-04 in the same worktree: CPU suite 90 cases / 2115 assertions,
including 13 importer cases / 308 assertions; native-debug build and all four CTest targets pass. The native model
window loads GLB from a foreign working directory, resizes, minimizes/restores and closes cleanly. Inspected captures
and command output are under artifacts/models/native. Broader external asset coverage remains tracked in the plan.

The final web configure/build and model, texture/checkerboard and PBR smoke scripts pass in Chrome 154.0.8037.95.
Model smoke also checks malformed-model error reporting without partial publication. Capture helpers wait for fresh
submitted frames after resizing, avoiding a cleared-canvas screenshot race without weakening image checks.
Reports/captures are under artifacts/models/browser, artifacts/textures/browser and artifacts/pbr/browser.


## ImGui workspace

Initialize both pinned submodules with git submodule update --init --recursive. ImGui v1.92.9b-docking is pinned to b48d1afbe8ee8b238e2961dc363a949dd7304e23. The default application and browser page enable the workspace; --no-ui and ?ui=0 retain full-canvas rendering for image regression tests. The CPU-only preset does not configure or build ImGui.

Run the existing native and cpu-tests presets as above. The native GPU suite additionally checks UI texture switching, clipping, alpha, dynamic atlas updates, more than 65,535 vertices, odd 16-bit index counts, and linear/sRGB transfer. Browser validation after a completed web build uses:

```powershell
npm.cmd run smoke:ui
npm.cmd run smoke:pbr
npm.cmd run smoke:web
```

The UI smoke drives real canvas mouse/keyboard events and records screenshots/diagnostics under artifacts/imgui/browser. Native window captures are under artifacts/imgui/native. Do not serve or test intermediate JS/WASM files while a web link is replacing them. The final WASM optimization can take several minutes. The existing Asyncify/WASM-exceptions toolchain warning remains; UI frame uploads finish before canvas acquisition.

Workspace controls, layout persistence and ownership are documented in [docs/imgui.md](docs/imgui.md). Actual validation outcomes are recorded in the [workspace plan](docs/archived/imgui-workspace.md).
