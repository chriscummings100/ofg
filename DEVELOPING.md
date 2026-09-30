# Developing OFG

The current application is a small native Windows console executable linked to Slang RHI. It creates a D3D12 device, prints the selected adapter and exits. It does not yet create a window or render. Browser configuration is deferred to a later step.

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
cmake --build build/native --target ofg --parallel
```

The root `CMakeLists.txt` includes `external/CMakeLists.txt`, which selects the D3D12-only dependency configuration and adds `external/slang-rhi`. Git records the exact RHI revision in the submodule entry; do not update it implicitly during configuration.

The first configuration downloads the prebuilt Slang compiler and the DirectX dependencies selected by the pinned upstream build. Generated files and downloads stay under `build/native`. Subsequent builds reuse that directory.

Check each command's exit status before continuing. Keep using the same build directory for incremental builds. There is no custom dependency manager or build-wrapper script. The executable lives at `build/native/ofg.exe`, beside the Slang DLLs and the `D3D12` directory copied by upstream CMake. Keep those runtime files with the executable.

## Run and check startup

These commands work from an ordinary PowerShell; launching the built application does not require the developer environment:

```powershell
.\build\native\ofg.exe
ctest --test-dir build/native -R '^ofg-startup$' --output-on-failure
```

Successful startup returns zero and prints `OFG initialized D3D12 on <adapter name>.` Device initialization failure prints an error and returns nonzero. The CTest startup check runs the same executable and is labeled `native` and `gpu`; it requires a working D3D12 adapter/runtime. There are no CPU/doctest targets yet.

The native entry point exports the Agility SDK version/path using Slang RHI's helper. This selects the copied D3D12 runtime without requiring a system-wide installation or a custom PATH for the application.

## Formatting

Use the checked-in `.clang-format`. The verified formatter is clang-format 22.1.3 from the Visual Studio C++ Clang tools. After the discovery block above, check the current source with:

```powershell
& (Join-Path $vsRoot 'VC\Tools\Llvm\x64\bin\clang-format.exe') --dry-run --Werror src/main.cpp
```

Those optional Clang tools are needed for that formatter command, not for the MSVC build. Formatting must not modify the RHI submodule.

## Verified baseline

The application configure/build, direct startup, CTest startup check and formatting check passed on 2026-10-01 with CMake 3.26.3, Ninja 1.13.2 and MSVC 19.51 in an x64 Visual Studio 2026 developer environment. Startup ran on an NVIDIA GeForce RTX 3050 Ti Laptop GPU. RHI is pinned to `16324a68af477baaede620e713644f5e9613b1a2`, using its prebuilt Slang 2026.17.1 dependency.

The generated RHI configuration confirms D3D12 is enabled and all other graphics backends are disabled. The verified application startup establishes linking, runtime dependency discovery and device creation; it does not establish rendering or browser behavior.

The repository [build-native skill](.agents/skills/build-native/SKILL.md) guides agents through this workflow and its environment requirements.
