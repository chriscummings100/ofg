# Developing OFG

This document describes the intended developer machine setup for OFG. The project is now a C++/WASM runtime with a TypeScript browser host. Development is currently Windows-first, so the recommended setup assumes a Windows PC and avoids making normal builds download compiler toolchains.

The repository does not install compiler toolchains, build tools, or Dawn source checkouts. Install the tools once on the machine, keep command-line tools available through `PATH` or the documented environment variables, and let builds use those installed tools. CI should eventually use either a prepared runner or a pinned builder image, not a job that installs compilers on every run.

## Tool Versions

Current source-controlled pins:

- Node.js: `24` from `.node-version`
- Emscripten: `6.0.0` from `emscripten-version.txt`
- LLVM/Clang: `22.1.8` from `llvm-version.txt`
- Ninja: `1.13.2` from `ninja-version.txt`
- Dawn native smoke revision: `31e25af254ab572c77054edec4946d2244e184dd` from `dawn-version.txt`

Prefer these versions when installing tools manually. If an exact installer is unavailable, use the nearest compatible version and record the difference before relying on it for CI.

## Windows Install Checklist

Install these once on the development machine.

1. Git for Windows

   Download from:

       https://git-scm.com/download/win

   Required for normal source control, manual Emscripten SDK setup, and Dawn source checkout.

2. Node.js 24

   Download from:

       https://nodejs.org/en/download

   The repo currently accepts Node 20 or newer in `package.json`, but `.node-version` pins development to Node 24. Install npm with Node. After install:

       node --version
       npm --version

3. Visual Studio Build Tools with Desktop C++

   Download Build Tools for Visual Studio from:

       https://visualstudio.microsoft.com/downloads/

   In the Visual Studio Installer, install the "Desktop development with C++" workload. Make sure these components are selected:

   - MSVC build tools
   - A modern Windows 11 SDK, preferably 10.0.26100 or newer
   - C++ CMake tools for Windows, optional if using standalone CMake
   - C++ Clang tools for Windows, optional if using standalone LLVM below

   OFG still uses Clang as the compiler. Visual Studio Build Tools are needed because Windows C++ builds also need SDK headers, import libraries, `rc.exe`, `mt.exe`, and linker support. This is the piece that removes a lot of Windows build friction.

4. LLVM/Clang 22.1.8

   Download from the official LLVM releases:

       https://github.com/llvm/llvm-project/releases

   Use the Windows x64 archive or installer for `llvmorg-22.1.8`, then add its `bin` directory to `PATH`. Required tools:

       clang
       clang++
       clang-cl
       llvm-cov
       llvm-profdata

   Verify:

       clang++ --version
       llvm-cov --version
       llvm-profdata --version

5. CMake

   Download from:

       https://cmake.org/download/

   Add CMake to `PATH` during install. Verify:

       cmake --version

6. Ninja

   Download from:

       https://github.com/ninja-build/ninja/releases

   Put `ninja.exe` in a stable tools directory and add that directory to `PATH`. The repo pin is `1.13.2`. Verify:

       ninja --version

7. Emscripten SDK 6.0.0

   Official install documentation:

       https://emscripten.org/docs/getting_started/downloads.html
       https://emscripten.org/docs/tools_reference/emsdk.html

   Recommended install location on Windows:

       C:\tools\emsdk

   Install and activate:

       git clone https://github.com/emscripten-core/emsdk.git C:\tools\emsdk
       cd C:\tools\emsdk
       git checkout 6.0.0
       emsdk.bat install 6.0.0
       emsdk.bat activate 6.0.0

   For each terminal where you build OFG, activate the SDK environment:

       C:\tools\emsdk\emsdk_env.bat

   Then verify:

       emcmake --version
       em++ --version

   Set `EMSDK=C:\tools\emsdk` in the environment when the Emscripten activation script does not do so for the shell. The repository build wrappers use the installed SDK and do not fall back to a repository-local SDK.

8. Chromium-based browser for WebGPU smoke

   Install Chrome or Edge. If the smoke script cannot find it automatically, set:

       OFG_BROWSER_PATH=C:\Program Files\Google\Chrome\Application\chrome.exe

9. Wrangler for Cloudflare Pages upload

   Official documentation:

       https://developers.cloudflare.com/workers/wrangler/install-and-update/
       https://developers.cloudflare.com/pages/get-started/direct-upload/

   Cloudflare recommends running Wrangler through npm tooling. This repository uses the Wrangler version installed from `package-lock.json`. For local manual deployment:

       npm run deploy -- --project-name=<project-name>

## Dawn and Native Render Smoke

Dawn is used only for native/offline render smoke. It is not needed for ordinary browser development or Cloudflare deployment.

Official Dawn build documentation:

    https://dawn.googlesource.com/dawn/+/HEAD/docs/building.md

Dawn is not a simple one-file SDK install in the same way Node, CMake, Ninja, or LLVM are. Prepare a Dawn checkout outside the repository generated artifacts and set `OFG_DAWN_SOURCE_DIR` to the checkout root before running native render smoke:

    set OFG_DAWN_SOURCE_DIR=C:\tools\dawn

The checkout should be compatible with `dawn-version.txt`; a newer installed checkout is acceptable when `npm run smoke:render` validates it successfully. For a no-download CI environment, pre-provision that checkout in the runner or builder image instead of cloning it during the job.

## First-Time Repo Setup

After installing the tools above:

    npm install
    npm run build
    npm test
    npm run smoke:browser

For full local validation, including native Dawn smoke and coverage:

    set OFG_DAWN_SOURCE_DIR=C:\tools\dawn
    npm run smoke:render
    npm run coverage

## Common Commands

Browser build:

    npm run build

C++ native tests:

    npm run test:cpp

TypeScript tests:

    npm run test:ts

All tests:

    npm test

Browser smoke:

    npm run smoke:browser

Native Dawn smoke:

    npm run smoke:render

Coverage:

    npm run coverage

Package Cloudflare Pages output:

    npm run package:site

Deploy prebuilt output to Cloudflare Pages:

    npm run deploy -- --project-name=<project-name>

## Deployment Model

Cloudflare should receive prebuilt static assets. It should not build the C++/WASM toolchain itself.

The deployable output is:

    .deploy
    .deploy/_headers
    .deploy/index.html
    .deploy/dist/app/*.js
    .deploy/src/app/styles.css
    .deploy/assets/wasm/ofg_cpp/ofg_cpp.js
    .deploy/assets/wasm/ofg_cpp/ofg_cpp.wasm

Recommended flow:

    npm run deploy -- --project-name=<project-name>

Native Dawn smoke, LLVM coverage, and local C++ test binaries are validation concerns only. They should not be part of Cloudflare deployment.

## CI Direction

The desired CI model is:

1. Build and validate on GitHub Actions.
2. Use a prepared environment with tools already installed.
3. Upload `.deploy` to Cloudflare Pages through Wrangler Direct Upload.

Good options:

- A self-hosted Windows runner that mirrors the primary developer machine.
- A pinned Docker builder image for Linux browser-WASM builds.
- A split pipeline where a heavy validation runner performs native smoke/coverage and a smaller Emscripten builder packages/deploys browser assets.

Avoid:

- Installing Emscripten, LLVM, Ninja, or Dawn during every CI job.
- Building C++ inside Cloudflare Pages.
- Making deployment depend on native Dawn smoke.

## Current Follow-Ups

Future CI work should capture these manually installed dependencies in a prepared runner or Docker builder image. Do not reintroduce repository commands that download Emscripten, LLVM, Ninja, CMake, Visual Studio Build Tools, or Dawn into `artifacts`.
