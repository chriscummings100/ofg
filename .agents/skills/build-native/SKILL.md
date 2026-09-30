---
name: build-native
description: Configure, compile, and smoke-test OFG's native Windows D3D12 application. Use for native builds, startup verification, and diagnosing the required MSVC x64 environment; browser builds are outside this workflow.
---

# Build OFG natively

Use the repository's [DEVELOPING.md](../../../DEVELOPING.md) as the source of exact commands, prerequisites and expected output. The application target is `ofg`, built by CMake/Ninja under `build/native`. Slang RHI is a pinned Git submodule, not an independently configured build.

## Build environment

Run from the repository root. Read the working-tree status and preserve existing edits.

Use an x64 Visual Studio Developer PowerShell, or initialize an ordinary PowerShell using the `vswhere` and `Launch-VsDevShell.ps1` procedure in DEVELOPING.md. Discover the installation rather than hard-coding the Visual Studio edition or year. Both host and target architecture must be amd64.

Shell environment changes do not persist across independent command-tool invocations. Initialize the developer environment in the same invocation that configures or compiles, or repeat initialization in each new shell. A successful previous build does not mean `cl`, SDK headers, the linker or Ninja are available in the next shell.

If the submodule is missing, use the documented initialization command. Do not use `git submodule update --remote`, move its pin, install toolchains or switch compilers merely to make a build pass. Explain missing prerequisites when the documented setup cannot supply them. Reuse the existing native build directory; changing generator, compiler or architecture requires a separate directory or deliberate reconfiguration, not an automatic recursive cleanup.

## Configure, compile and verify

Follow the documented native configure command, then build targets `ofg` and `ofg-render-test` (only `ofg` when `BUILD_TESTING=OFF`). Check the exit status at each stage before proceeding. The first configure fetches prebuilt Slang, DirectX dependencies and pinned GLFW; routine builds reuse them. D3D12 is the only enabled backend and no Emscripten or native Dawn setup is needed.

Run `ofg --check-device` for finite startup verification, then run both CTest checks as documented. Normal `ofg` launch opens an interactive checkerboard window and stays running until closed; do not use it as a finite command-line test. The render test checks every offscreen pixel through doctest. Both checks are GPU-dependent, so report unavailable hardware/runtime honestly. Do not enable the entire upstream RHI test suite to test OFG.

For rendering changes, also inspect the window, resize/maximize it, minimize/restore it, and verify clean close. Save screenshots under `artifacts/checkerboard`. The automated offscreen check does not prove presentation. The shared shader is embedded by CMake; editing it requires a rebuild, not runtime asset copying.

The Slang DLLs and the `D3D12` runtime directory belong beside `ofg.exe`; upstream CMake copies them into the build directory. The application exports the Agility SDK selection through RHI's helper. Investigate missing output files when startup fails outside a developer shell; do not mask packaging defects by adding dependency directories to the system PATH.

Report configure, compilation and startup/test outcomes separately. Include the relevant command or first actionable error and note any check that could not run. Keep the skill and DEVELOPING.md aligned when the build layout changes.
