# Developing OFG

The current application opens a resizable native Windows D3D12 window and renders a checkerboard through Slang RHI. Browser configuration comes next, using this image as a visual baseline.

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
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native --target ofg ofg-render-test --parallel
```

The root `CMakeLists.txt` includes `external/CMakeLists.txt`, which selects the D3D12-only dependency configuration and adds `external/slang-rhi`. Git records the exact RHI revision in the submodule entry; do not update it implicitly during configuration.

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

`--check-device` creates the device, prints `OFG initialized D3D12 on <adapter name>.` and exits without a window. Failure returns nonzero. CTest runs two finite checks, both labeled `native` and `gpu`, with a 30-second timeout:

- `ofg-startup`: device creation through `ofg --check-device`.
- `ofg-checkerboard`: a doctest GPU integration test using the same pipeline and draw as the application. It verifies every RGBA8 UNORM pixel at 1-by-1, 129-by-97 and 259-by-193, including alpha, partial cells and row pitch. RHI validation errors fail the test. Expected gray levels are exactly 32 and 224; no tolerance is needed for these constant UNORM values.

Both tests require a working D3D12 adapter/runtime. The window uses the surface's preferred format, which may apply sRGB encoding to the shader's linear gray levels; screenshots need not have the same byte values as the UNORM offscreen test. The integration test uses doctest 2.4.11 already pinned inside the RHI submodule; there are no GPU-independent CPU tests yet. Neither automated test opens a window, so presentation and resize still need visual inspection.

The native entry point exports the Agility SDK version/path using Slang RHI's helper. This selects the copied D3D12 runtime without requiring a system-wide installation or a custom PATH for the application.

## Formatting

Use the checked-in `.clang-format`. The verified formatter is clang-format 22.1.3 from the Visual Studio C++ Clang tools. After the discovery block above, check the current source with:

```powershell
& (Join-Path $vsRoot 'VC\Tools\Llvm\x64\bin\clang-format.exe') --dry-run --Werror src/main.cpp src/checkerboard.cpp src/checkerboard.h tests/checkerboard-test.cpp
```

Those optional Clang tools are needed for that formatter command, not for the MSVC build. Formatting must not modify the RHI submodule.

## Verified baseline

The application configure/build, direct startup, CTest startup check and formatting check passed on 2026-10-01 with CMake 3.26.3, Ninja 1.13.2 and MSVC 19.51 in an x64 Visual Studio 2026 developer environment. Startup ran on an NVIDIA GeForce RTX 3050 Ti Laptop GPU. RHI is pinned to `16324a68af477baaede620e713644f5e9613b1a2`, using its prebuilt Slang 2026.17.1 dependency.

The generated RHI configuration confirms D3D12 is enabled and all other graphics backends are disabled. The native checkerboard build and both tests passed on the same hardware on 2026-10-01. Live window inspection verified rendering, maximize/resize, minimize/restore and clean shutdown. Screenshots are saved locally under `artifacts/checkerboard/native.png`, `native-maximized.png` and `native-restored.png`. Browser execution, texture sampling, compute and ImGui remain unverified.

The repository [build-native skill](.agents/skills/build-native/SKILL.md) guides agents through this workflow and its environment requirements.
