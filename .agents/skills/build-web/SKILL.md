---
name: build-web
description: Configure and build OFG's Emscripten/WebGPU target, serve it locally, and verify browser rendering with Playwright screenshots and diagnostics. Use for browser builds and browser smoke failures; native C++ tests remain a separate workflow.
---

# Build and verify OFG in the browser

Use [DEVELOPING.md](../../../DEVELOPING.md) for exact environment, build, serving and smoke commands. The [architecture note](../../../docs/architecture.md) explains the shared renderer and browser host. Work from the repository root and preserve existing edits.

## Keep the toolchains separate

Use the `web` CMake configure/build preset and target `ofg-web`, with output in `build/web`. The tested SDK is Emscripten 6.0.0; `EMSDK` identifies its root. CMake and Ninja must be available when configuring. Visual Studio's Ninja can be used without compiling browser code with MSVC. Environment changes in one command shell do not prepare a later independent shell.

Reuse the existing build tree. Do not replace the SDK, update the RHI submodule pin or remove build directories merely to make a build pass. Report missing prerequisites or a toolchain mismatch with the relevant diagnostic. Native builds remain in `build/native` and must not acquire an Emscripten or Node dependency.

The browser configuration enables RHI WebGPU through `emdawnwebgpu` and excludes GLFW, DirectX, desktop Dawn and the native C++ tests. The first configure fetches the pinned Slang WASM libraries; linking still optimizes the bundled compiler and can take substantially longer than native linking. A generated HTML file alone is not evidence of a successful build: check the command's exit status and all three HTML/JS/WASM outputs.

## Serve and capture

Install browser tooling from the checked-in lockfile with the documented npm command. The smoke script uses `playwright-core` with installed Google Chrome; it does not download a browser. Use `npm run smoke:web` after the build. It starts its own server on a free loopback port and closes its browser/server on completion. The full C++ test suite stays native.

Inspect the fresh `artifacts/browser-smoke/report.json` and screenshots. Success requires a successful script exit, rendered checkerboard pixels, resize/reload checks, missing-WebGPU messaging and no recorded browser errors. Report an unavailable GPU/browser as a failed or unverified check, never as a pass. Console warnings are retained separately; the documented Windows power-preference warning is not a rendering failure. Do not infer the browser's selected GPU from the native adapter.

For interactive investigation, use `npm run serve:web` and the printed localhost URL, or the smoke script's `--headed` option. Use localhost or HTTPS rather than opening generated HTML with `file://`. Keep a useful development server available during visual work and identify it in the handoff; do not terminate unrelated servers or publish the site.

## Known integration details

The Slang source is embedded by CMake, so shader edits require a rebuild. Keep JavaScript logic in `web/shell.html` and inline C++ `EM_ASM` blocks simple: clang-format can corrupt JavaScript-only operators such as strict equality. The current build is single-threaded and needs no cross-origin isolation headers.

The current WASM includes the Slang compiler and is about 26 MB uncompressed. Asyncify/WASM-exception settings follow the pinned RHI integration and emit a known Emscripten warning. Do not suppress it or assume all asynchronous exception paths work because the checkerboard passes. Record failures and revisit the settings when those paths change.

Report build and browser-runtime results separately, with screenshot/report locations and any limitations. For changes to shared rendering or build selection, also use the native workflow to verify its affected checks. Documentation-only changes need link/skill validation, not a fresh graphics build.
