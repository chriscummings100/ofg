# Developing OFG

The current application opens a resizable native Windows D3D12 window and renders a checkerboard through Slang RHI. A separate Emscripten/WebGPU build renders the same shader in a browser.

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

Normal launch prints the selected adapter and opens a 960-by-640 framebuffer with alternating dark/light 64-pixel squares. Resize or maximize the window: squares retain their pixel size and new squares fill the exposed area. Minimize/restore should resume rendering. Escape or the close button exits cleanly. The shader is embedded at build time from `shaders/checkerboard.slang`, so launch does not depend on the current working directory. Editing the shader triggers CMake regeneration and recompilation on the next build.

`--check-device` creates the device, prints `OFG initialized D3D12 on <adapter name>.` and exits without a window. Failure returns nonzero. CTest runs three finite checks with a 30-second timeout. The core suite is labeled `cpu`; the other two are labeled `native` and `gpu`:

- `ofg-core`: GPU-independent doctest suite for state lifecycle, scene hierarchy/transforms, observer lifetime, resource loading/lifetime and CPU math.
- `ofg-startup`: device creation through `ofg --check-device`.
- `ofg-checkerboard`: a doctest GPU integration test using the same pipeline and draw as the application. It verifies every RGBA8 UNORM pixel at 1-by-1, 129-by-97 and 259-by-193, including alpha, partial cells and row pitch. RHI validation errors fail the test. Expected gray levels are exactly 32 and 224; no tolerance is needed for these constant UNORM values.

The two GPU tests require a working D3D12 adapter/runtime. The window uses the surface's preferred format, which may apply sRGB encoding to the shader's linear gray levels; screenshots need not have the same byte values as the UNORM offscreen test. All C++ tests use the independently vendored [doctest 2.4.11 header](external/doctest/README.md). None of the automated C++ tests opens a window, so presentation and resize still need visual inspection.

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

## Formatting

Use the checked-in `.clang-format`. The verified formatter is clang-format 22.1.3 from the Visual Studio C++ Clang tools. After the discovery block above, check the current source with:

```powershell
$projectSources = @(rg --files src tests -g '*.cpp' -g '*.h')
& (Join-Path $vsRoot 'VC\Tools\Llvm\x64\bin\clang-format.exe') --dry-run --Werror $projectSources
```

Those optional Clang tools are needed for that formatter command, not for the MSVC build. Formatting must not modify the RHI submodule.

## Verified baseline

The application configure/build, direct startup, CTest startup check and formatting check passed on 2026-10-01 with CMake 3.26.3, Ninja 1.13.2 and MSVC 19.51 in an x64 Visual Studio 2026 developer environment. Startup ran on an NVIDIA GeForce RTX 3050 Ti Laptop GPU. RHI is pinned to `16324a68af477baaede620e713644f5e9613b1a2`, using its prebuilt Slang 2026.17.1 dependency.

The generated RHI configuration confirms D3D12 is enabled and all other graphics backends are disabled. The native checkerboard build and both tests passed on the same hardware on 2026-10-01. Live window inspection verified rendering, maximize/resize, minimize/restore and clean shutdown. Screenshots are saved locally under `artifacts/checkerboard/native.png`, `native-maximized.png` and `native-restored.png`. Texture sampling, compute and ImGui remain unverified; browser checkerboard validation is described below.

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

The browser currently includes the runtime Slang compiler: the initial verified WASM output was 26,795,946 bytes (about 25.6 MiB), before HTTP compression. This is a bring-up baseline, not an optimized distribution-size target. Emscripten warns about combining Asyncify and WASM exceptions. These settings mirror the pinned RHI preset; the checkerboard path works, but arbitrary exception paths through suspended calls are not established by this smoke check. Revisit this with any async/lifecycle expansion.

## Serve and inspect the browser build

Node is used for browser tooling only. The checked-in lockfile pins `playwright-core` 1.61.0 and `pngjs` 7.0.0. Node 24.14.0 and installed Google Chrome were used for verification; the smoke script does not download a browser. From ordinary PowerShell:

```powershell
npm.cmd ci
npm.cmd run serve:web
```

Open `http://127.0.0.1:8080`. Stop the server with Ctrl+C. Serve the generated files over localhost or HTTPS, rather than opening the HTML with `file://`. This single-threaded build needs no cross-origin isolation headers. The small loopback server serves only the three generated files.

In another shell, run the focused browser check:

```powershell
npm.cmd run smoke:web
# Optional visible browser for debugging:
npm.cmd run smoke:web -- --headed
```

The script starts its own server on a free loopback port and closes it and Chrome afterward. It checks initial rendering, resize, reload, and the missing-WebGPU error message. Screenshots and `report.json` are saved under `artifacts/browser-smoke`. The report includes Chrome version, console/errors, image sizes and browser adapter diagnostics. The screenshots' checkerboard cells are checked against their pixel boundaries; native's full C++ test suite stays native. A successful browser check is not a claim that all RHI features work on WebGPU.

The native surface and browser surface may prefer different sRGB/UNORM formats. The smoke check accepts either consistent encoding of the shader's two gray levels; it still checks every cell boundary. Browser privacy may omit the RHI adapter name, so the app reports `browser-selected adapter` rather than guessing the native GPU. Chrome can warn that Windows ignores `powerPreference`; this is retained in the report rather than counted as a rendering error.

Build commands and test registration are in `CMakePresets.json` and `CMakeLists.txt`; automation lives in `tools/browser-smoke.mjs`. The [Playwright library documentation](https://playwright.dev/docs/library) covers the screenshot/console workflow used here.

Browser build and smoke passed on 2026-10-01 with Emscripten 6.0.0, Chrome 154.0.8037.59 and Playwright 1.61.0. Captured canvases were 960x641 initially and 773x478 after resize/reload. Console errors were empty; the missing-WebGPU message also passed. An independent browser adapter query reported Intel gen-12lp, while RHI omitted its description. Native preset configuration/build and both native CTest checks passed afterward.
