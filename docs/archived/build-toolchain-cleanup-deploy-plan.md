# Remove repository-managed build tool downloads and add local Cloudflare deploy

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

This plan follows `C:\dev\ofg\PLANS.md`.

## Purpose / Big Picture

OFG should build, test, run smoke checks, and deploy using tools installed on the developer machine instead of repository scripts that download compilers, build tools, Emscripten SDKs, or Dawn source checkouts into `C:\dev\ofg\artifacts\toolchains`.

After this work, the repository no longer contains commands or scripts that auto-download build tools or Dawn. Old downloaded toolchains from earlier work are removed from `C:\dev\ofg\artifacts\toolchains`. The normal commands build and validate with the currently installed system tools. A local, authenticated Wrangler session can deploy the prebuilt static site to the Cloudflare Pages project named `ofg`.

The user-visible success path is:

    npm run build
    npm test
    npm run smoke:browser
    npm run smoke:render
    npm run package:site
    npm run deploy -- --project-name=ofg

## Progress

- [x] (2026-06-21 16:10Z) Confirmed the user-defined success criteria: no repository auto-download commands or scripts for build tools/Dawn, no leftover downloaded build tools, ability to build and run with installed system tools, and ability to deploy to Cloudflare.
- [x] (2026-06-21 16:10Z) Confirmed Cloudflare Pages project name is `ofg`.
- [x] (2026-06-21 16:10Z) Probed the current machine state: Emscripten 6.0.0 is visible from `C:\tools\emsdk`; Visual Studio LLVM 22.1.3 and Ninja 1.13.2 are installed under Visual Studio but not on `PATH`; Dawn exists at `C:\tools\dawn`; old repository toolchains still exist under `C:\dev\ofg\artifacts\toolchains`.
- [x] (2026-06-21 16:10Z) Confirmed local Wrangler login works per user report, and token-based CI deployment is out of scope for this plan.
- [x] (2026-06-21 16:10Z) Created this ExecPlan at `C:\dev\ofg\docs\plans\build-toolchain-cleanup-deploy-plan.md`.
- [x] (2026-06-21 16:18Z) Confirmed the existing public Cloudflare deployment is a Workers deployment at `https://ofg.chriscummings1024.workers.dev/`, not a Cloudflare Pages project.
- [x] (2026-06-21 16:23Z) Created the Cloudflare Pages project `ofg` with production branch `main`.
- [x] (2026-06-21 16:24Z) Packaged the current site and deployed it to Cloudflare Pages. Wrangler reported preview URL `https://4096a42c.ofg.pages.dev`.
- [x] (2026-06-21 16:25Z) Verified the Pages preview in a browser and stored visual evidence at `C:\dev\ofg\artifacts\deploy-smoke\ofg-pages-preview.png` with report `C:\dev\ofg\artifacts\deploy-smoke\ofg-pages-preview.json`.
- [x] (2026-06-21 16:26Z) Confirmed the canonical Pages URL `https://ofg.pages.dev/` returns HTTP 200 and Wrangler lists deployment `4096a42c-09b5-4585-a30b-a74f96eaa08a` as production on branch `main`.
- [x] (2026-06-21 16:35Z) Milestone 1 complete: removed `setup:*` scripts from `package.json`, deleted the four `tools/setup-*.mjs` downloader scripts, and updated active command/docs references in `README.md`, `DEVELOPING.md`, `docs/SYSTEMS.md`, `AGENTS.md`, and the active renderer plan.
- [x] (2026-06-21 16:36Z) Milestone 1 review complete. Scope: setup command removal and active docs cleanup. Reviewers: local contract, code quality, legacy, correctness, and validation passes because no sub-agent tools were available. Required findings fixed: none. Follow-ups recorded: wrapper refactor remains Milestone 2. Rejected findings: none. Validation rerun: `git diff --check` passed; active-doc search for setup command names, `artifacts/toolchains`, and `npx wrangler` returned no matches in edited active docs/package files. Remaining risk: tool wrappers still reference setup commands until Milestone 2.
- [x] (2026-06-21 17:45Z) Milestone 2 complete: refactored `tools/build-cpp-wasm.mjs`, `tools/test-cpp.mjs`, `tools/cpp-coverage.mjs`, and `tools/smoke-render-cpp.mjs` onto shared installed-tool discovery in `tools/lib/toolchain.mjs`; changed native Dawn smoke to require `OFG_DAWN_SOURCE_DIR`; disabled Dawn dependency fetching in the native smoke CMake path; removed wrapper mutation of LLVM/linker/profile-runtime files.
- [x] (2026-06-21 17:45Z) Milestone 2 validation passed: `node --check` passed for changed Node wrappers; `npm run build:wasm`, `npm run test:cpp`, `npm run coverage:cpp`, and `$env:OFG_DAWN_SOURCE_DIR='C:\tools\dawn'; npm run smoke:render` passed. Native render smoke wrote `C:\dev\ofg\artifacts\render-smoke\bootstrap.png` and `C:\dev\ofg\artifacts\render-smoke\report.json`, with `passed: true`.
- [x] (2026-06-21 17:45Z) Milestone 2 review complete. Scope: installed-tool wrapper refactor, Dawn smoke contract change, and active contract/docs updates. Reviewers: local contract, code quality, legacy, correctness, and validation passes; sub-agent tools were not used because the available sub-agent tool policy requires the user to explicitly request delegation. Required findings fixed: false-positive `artifacts/toolchains` and `download` comments in tool scripts. Follow-ups recorded: `docs/ARCHITECTURE.md` is referenced by the review template but does not exist in this repo. Rejected findings: none. Validation rerun: focused `package.json`/`tools` search for setup/downloader patterns returned no matches; `git diff --check` passed. Remaining risk: wrapper scripts are covered by command-level validation rather than unit tests, which is accepted for this cleanup milestone and must be revisited if their argument surface grows.
- [x] (2026-06-21 17:49Z) Milestone 3 complete: added `npm run deploy` through `tools/deploy-cloudflare-pages.mjs`, updated Cloudflare build packaging to avoid setup commands, documented the deploy command in `README.md`, `DEVELOPING.md`, `docs/SYSTEMS.md`, `docs/API_CONTRACTS.md`, and `AGENTS.md`, and verified local Wrangler is launched through the pinned package entrypoint rather than `npx` or a Windows `.cmd` shim.
- [x] (2026-06-21 17:49Z) Milestone 3 validation passed: `npm run build:cloudflare`, `npm run deploy -- --project-name=ofg --dry-run`, and `npm run deploy -- --project-name=ofg` passed. Wrangler reported deployed preview `https://1ff5528c.ofg.pages.dev`. A deployed preview smoke probe returned HTTP 200, confirmed cross-origin isolation headers, `navigator.gpu`, runtime initialization, and wrote screenshot `C:\dev\ofg\artifacts\deploy-smoke\ofg-pages-preview-1ff5528c.png` plus report `C:\dev\ofg\artifacts\deploy-smoke\ofg-pages-preview-1ff5528c.json`.
- [x] (2026-06-21 17:49Z) Milestone 3 review complete. Scope: npm deploy command, Cloudflare build wrapper, deployment docs/contracts, and deployed preview evidence. Reviewers: local contract, code quality, legacy, correctness, and validation passes; sub-agent tools were not used because the available sub-agent tool policy requires the user to explicitly request delegation. Required findings fixed: Windows `.cmd` spawn failure in `tools/cloudflare-build.mjs` and `tools/deploy-cloudflare-pages.mjs`; `OFG-BOOT-008` now names the npm Pages deploy path and pinned Wrangler dependency. Follow-ups recorded: none. Rejected findings: none. Validation rerun: `node --check` for both deployment wrappers, focused stale Wrangler/setup search, `git diff --check`, build, dry-run deploy, real deploy, and deployed preview smoke. Remaining risk: Wrangler warned about uncommitted changes during direct upload, which is expected for this local cleanup branch and does not affect package correctness.
- [x] (2026-06-21 17:52Z) Milestone 4 complete: resolved `C:\dev\ofg\artifacts\toolchains`, verified it matched the exact intended cleanup target, and removed it with guarded PowerShell deletion.
- [x] (2026-06-21 17:52Z) Milestone 4 validation passed: `Test-Path C:\dev\ofg\artifacts\toolchains` returned `False`; `artifacts` now contains only build, coverage, browser-smoke, browser-smoke-cpp, deploy-smoke, and render-smoke output directories; focused searches over `package.json`, `tools`, and active docs found no stale setup commands, repository toolchain paths, `npx wrangler`, or `wrangler.cmd pages deploy`; `npm run build:wasm` passed after deletion.
- [x] (2026-06-21 17:52Z) Milestone 4 review complete. Scope: generated toolchain deletion, search audit, and post-delete build proof. Reviewers: local contract, code quality, legacy, correctness, and validation passes; sub-agent tools were not used because the available sub-agent tool policy requires the user to explicitly request delegation. Required findings fixed: none. Follow-ups recorded: none. Rejected findings: none. Validation rerun: path absence check, focused stale-reference searches, and `npm run build:wasm`. Remaining risk: full build/test/smoke/coverage/deploy validation remains Milestone 5.
- [x] (2026-06-21 17:56Z) Milestone 5 complete: ran the final validation suite after deleting repository-local toolchains. Passing commands: `npm run build`, `npm test`, `npm run smoke:browser`, `$env:OFG_DAWN_SOURCE_DIR='C:\tools\dawn'; npm run smoke:render`, `npm run coverage`, `npm run package:site`, `npm run deploy -- --project-name=ofg --dry-run`, `npm run deploy -- --project-name=ofg`, and `npm run build:cloudflare`.
- [x] (2026-06-21 17:56Z) Final deployment and visual evidence recorded. Wrangler reported latest preview `https://3c752d6a.ofg.pages.dev`. Deployed preview smoke returned HTTP 200, confirmed Cloudflare isolation headers, `navigator.gpu`, initialized runtime, and wrote screenshot `C:\dev\ofg\artifacts\deploy-smoke\ofg-pages-preview-3c752d6a.png` plus report `C:\dev\ofg\artifacts\deploy-smoke\ofg-pages-preview-3c752d6a.json`. Local browser smoke wrote `C:\dev\ofg\artifacts\browser-smoke\bootstrap.png` and report `C:\dev\ofg\artifacts\browser-smoke\report.json`.
- [x] (2026-06-21 17:56Z) Final review complete. Scope: full cleanup/deploy plan, active docs/contracts, generated artifact cleanup, and final validation evidence. Reviewers: local contract, code quality, legacy, correctness, and validation passes; sub-agent tools were not used because the available sub-agent tool policy requires the user to explicitly request delegation. Required findings fixed: updated archived-plan wording so historical setup-script context reads as plan-start state; corrected Wrangler decision wording to local package entrypoint. Follow-ups recorded: future CI should use a prepared runner or Docker image rather than reintroducing repository downloader scripts. Rejected findings: none. Validation rerun: final stale-reference searches returned no matches, `Test-Path C:\dev\ofg\artifacts\toolchains` returned `False`, and `git diff --check` passed. Remaining risk: direct upload still warns about uncommitted local changes, which is expected until this cleanup branch is committed.

## Surprises & Discoveries

- Observation: The current shell sees Emscripten from `C:\tools\emsdk`, but does not see desktop Clang, LLVM coverage tools, or Ninja on `PATH`.
  Evidence: `Get-Command emcmake` and `Get-Command em++` find `C:\tools\emsdk\upstream\emscripten`; `Get-Command clang++`, `clang-cl`, `llvm-cov`, `llvm-profdata`, and `ninja` were missing from `PATH`.

- Observation: Visual Studio Community 18 has the native tools needed for C++ builds, even though they are not on `PATH`.
  Evidence: `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin` contains `clang.exe`, `clang++.exe`, `clang-cl.exe`, `llvm-cov.exe`, `llvm-profdata.exe`, and `lld-link.exe`; Visual Studio's bundled Ninja is at `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe`.

- Observation: Dawn is installed at `C:\tools\dawn`, but it is not on the source-controlled reference revision.
  Evidence: `git -C C:\tools\dawn rev-parse HEAD` returned `e247c6101ae17400e803eae1717e8500677e8cfc`; `C:\dev\ofg\dawn-version.txt` contains `31e25af254ab572c77054edec4946d2244e184dd`.

- Observation: Old repository-managed toolchains are still present and must be removed as part of acceptance.
  Evidence: `C:\dev\ofg\artifacts\toolchains` currently contains `dawn`, `emsdk`, `llvm`, and `ninja`.

- Observation: The existing public Cloudflare deployment is Workers, not Pages.
  Evidence: The user identified the deployed URL as `https://ofg.chriscummings1024.workers.dev/`; Wrangler Pages project checks found no Pages project named `ofg`.

- Observation: The first Pages deployment succeeded before build cleanup started.
  Evidence: `wrangler pages project create ofg --production-branch main` succeeded, `npm run package:site` produced `C:\dev\ofg\.deploy`, and `wrangler pages deploy .deploy --project-name=ofg` reported `https://4096a42c.ofg.pages.dev`. The canonical URL `https://ofg.pages.dev/` returns HTTP 200.

- Observation: Current `npm run package:site` can still trigger network work through Emscripten port caching even though it does not run repository setup scripts.
  Evidence: During packaging, Emscripten logged that it retrieved `emdawnwebgpu` from a GitHub Dawn release and cached the compiled port under `C:\tools\emsdk`. The cleanup plan must decide whether system Emscripten cache prewarming is acceptable or whether final validation should require a no-network build.

- Observation: The deployed Pages preview loads and renders the current WebGPU bootstrap.
  Evidence: A browser probe against `https://4096a42c.ofg.pages.dev` found `navigator.gpu` available, `crossOriginIsolated === true`, `window.__ofgDebugStatus().initialized === true`, and `frameCount` reached 11. Screenshot artifact: `C:\dev\ofg\artifacts\deploy-smoke\ofg-pages-preview.png`.

- Observation: Requiring exact Dawn revision drift was too strict for this cleanup goal.
  Evidence: The user clarified that latest installed Dawn is acceptable if it builds. The actual success criteria require no repository auto-downloads and successful build/run with installed system tools, not an exact Dawn checkout revision.

- Observation: The installed Dawn checkout works despite revision drift from `dawn-version.txt`.
  Evidence: `$env:OFG_DAWN_SOURCE_DIR='C:\tools\dawn'; npm run smoke:render` built Dawn with `DAWN_FETCH_DEPENDENCIES=OFF`, emitted a revision drift warning for `e247c6101ae17400e803eae1717e8500677e8cfc` versus `31e25af254ab572c77054edec4946d2244e184dd`, then produced a passing report at `C:\dev\ofg\artifacts\render-smoke\report.json`.

- Observation: The milestone review template names `docs/ARCHITECTURE.md`, but this repository does not currently contain that file.
  Evidence: `Get-Content -Raw docs\ARCHITECTURE.md` failed with path-not-found during Milestone 2 review. The review used `docs/API_CONTRACTS.md`, `docs/SYSTEMS.md`, `AGENTS.md`, `PLANS.md`, and this ExecPlan instead.

- Observation: Spawning Windows `.cmd` shims from Node wrappers failed in this shell.
  Evidence: `npm run build:cloudflare` initially failed with `spawnSync npm.cmd EINVAL`. The fixed wrappers run npm through `process.env.npm_execpath` when available and run Wrangler through `node_modules\wrangler\bin\wrangler.js` with `process.execPath`.

- Observation: Cloudflare Pages direct upload now works for project `ofg`.
  Evidence: `npm run deploy -- --project-name=ofg` completed and Wrangler reported `https://1ff5528c.ofg.pages.dev`; the deployed preview smoke report at `C:\dev\ofg\artifacts\deploy-smoke\ofg-pages-preview-1ff5528c.json` confirms headers, WebGPU availability, isolation, and initialized runtime.

- Observation: The old generated repository toolchains are no longer needed by normal build commands.
  Evidence: After deleting `C:\dev\ofg\artifacts\toolchains`, `npm run build:wasm` still configured through `C:\Program Files\CMake\bin\cmake.exe`, Visual Studio Ninja, and `C:\tools\emsdk`, then generated `assets\wasm\ofg_cpp\ofg_cpp.js` and `.wasm`.

## Decision Log

- Decision: Remove repository setup scripts instead of keeping them as optional bootstrap helpers.
  Rationale: The user explicitly defined success as no commands or scripts in the repository that auto-download build tools or Dawn.
  Date/Author: 2026-06-21 / User and Codex

- Decision: Keep source-controlled version pin files unless implementation proves they are misleading.
  Rationale: Files such as `C:\dev\ofg\emscripten-version.txt`, `C:\dev\ofg\llvm-version.txt`, `C:\dev\ofg\ninja-version.txt`, and `C:\dev\ofg\dawn-version.txt` are useful compatibility targets for manually installed tools and future CI images. They do not download anything by themselves.
  Date/Author: 2026-06-21 / Codex

- Decision: Do not require Cloudflare API tokens in this plan.
  Rationale: The user confirmed local Wrangler login works and CI is not in scope yet. Token-based auth belongs to a later CI/container plan.
  Date/Author: 2026-06-21 / User and Codex

- Decision: Use the local Wrangler package dependency, not `npx wrangler`.
  Rationale: `npx` can download packages when missing. The repo pins Wrangler through `package-lock.json`, so npm scripts launch `node_modules\wrangler\bin\wrangler.js` with `process.execPath`.
  Date/Author: 2026-06-21 / Codex

- Decision: Continue this plan toward Cloudflare Pages rather than the existing Workers deployment.
  Rationale: The user originally requested an `npm deploy` command for Cloudflare Pages, and the current packaging contract already creates `.deploy` plus Pages `_headers` for WebGPU isolation. The Workers URL is useful as the current public reference, but Workers deployment should remain out of scope unless the user explicitly changes the target.
  Date/Author: 2026-06-21 / User and Codex

- Decision: Make `OFG_DAWN_SOURCE_DIR` explicit for native render smoke, but do not fail solely on Dawn revision drift.
  Rationale: Dawn is not a simple compiler on `PATH`; the smoke wrapper needs a source checkout path. The cleaned script should never clone or fetch Dawn, and it should report revision drift clearly. A newer installed Dawn checkout is acceptable when the native smoke build and render validation pass.
  Date/Author: 2026-06-21 / Codex, revised after user clarification

- Decision: Discover installed Visual Studio LLVM, Windows SDK tools, and Visual Studio Ninja when the matching tools are not on `PATH`.
  Rationale: These are already installed system tools on the developer machine and using them avoids requiring repository-managed downloads while keeping normal npm commands usable from an ordinary shell.
  Date/Author: 2026-06-21 / Codex

## Outcomes & Retrospective

Complete. The repository setup commands and downloader scripts for Emscripten, LLVM, Ninja, and Dawn were removed. Build, test, coverage, native smoke, packaging, and deploy wrappers now use installed tools and reject repository-local toolchain directories. `C:\dev\ofg\artifacts\toolchains` was deleted and remained absent through final validation. Cloudflare Pages project `ofg` was created and deployed through `npm run deploy -- --project-name=ofg`; the latest verified preview is `https://3c752d6a.ofg.pages.dev`. Final validation passed for build, tests, browser smoke, native Dawn smoke, coverage, packaging, Cloudflare build, dry-run deploy, real deploy, and deployed-preview browser verification.

One lesson from the work: Dawn revision files are useful compatibility references, but this cleanup should not force a system Dawn checkout to a historical revision. The proof is the smoke result with the installed checkout. Another lesson: on this Windows setup, spawning `.cmd` shims from Node wrappers can fail with `EINVAL`; launching package JS entrypoints through `process.execPath` is more reliable.

## Contract and Quality Baseline

This plan preserves the active contracts in `C:\dev\ofg\docs\API_CONTRACTS.md` unless explicitly noted here.

OFG-BOOT-007 Generated Artifacts is preserved and clarified. `C:\dev\ofg\artifacts` remains generated local output, but `C:\dev\ofg\artifacts\toolchains` must not be produced or required by repository commands after this plan. Build output under `C:\dev\ofg\artifacts\build`, coverage output under `C:\dev\ofg\artifacts\coverage`, and smoke output under `C:\dev\ofg\artifacts\browser-smoke`, `C:\dev\ofg\artifacts\browser-smoke-cpp`, and `C:\dev\ofg\artifacts\render-smoke` remain valid generated outputs.

OFG-BOOT-008 Deployment is preserved and expanded. Cloudflare Pages remains the deployment target with build output directory `C:\dev\ofg\.deploy`. Packaged runtime files still include `C:\dev\ofg\assets\wasm\ofg_cpp\ofg_cpp.js` and `C:\dev\ofg\assets\wasm\ofg_cpp\ofg_cpp.wasm`. This plan adds `npm run deploy -- --project-name=ofg` as the local upload path using authenticated Wrangler. Cloudflare Pages should receive prebuilt static assets; it should not build C++ or install toolchains.

OFG-BOOT-009 Coverage is preserved. Modified implementation files should meet the documented 90% line coverage attention gate unless this plan records a specific exception. Tool wrapper scripts are implementation files for this plan and should have focused TypeScript/Node tests where practical; otherwise their behavior must be covered by command-level validation and documented rationale.

The readability rules from `C:\dev\ofg\AGENTS.md` apply. New or changed scripts should keep a maintained top-of-file purpose comment. New functions should have comments or docstrings describing their purpose. Larger functions over 50 lines should include internal comments describing phases.

## Context and Orientation

The repository root is `C:\dev\ofg`. The current runtime is C++/WASM with a TypeScript browser host. Build orchestration is primarily in npm scripts in `C:\dev\ofg\package.json`, with Node wrapper scripts under `C:\dev\ofg\tools`.

At plan start, source-controlled setup/download scripts were:

    C:\dev\ofg\tools\setup-emscripten.mjs
    C:\dev\ofg\tools\setup-llvm.mjs
    C:\dev\ofg\tools\setup-ninja.mjs
    C:\dev\ofg\tools\setup-dawn.mjs

At plan start, npm setup commands were:

    npm run setup:emscripten
    npm run setup:llvm
    npm run setup:ninja
    npm run setup:dawn

At plan start, wrappers that referred to repository-local toolchain downloads or setup behavior included:

    C:\dev\ofg\tools\build-cpp-wasm.mjs
    C:\dev\ofg\tools\test-cpp.mjs
    C:\dev\ofg\tools\cpp-coverage.mjs
    C:\dev\ofg\tools\smoke-render-cpp.mjs
    C:\dev\ofg\tools\cloudflare-build.mjs

The generated repository-local toolchains to remove were:

    C:\dev\ofg\artifacts\toolchains\dawn
    C:\dev\ofg\artifacts\toolchains\emsdk
    C:\dev\ofg\artifacts\toolchains\llvm
    C:\dev\ofg\artifacts\toolchains\ninja

Installed system tools observed before implementation:

    C:\tools\emsdk
    C:\tools\dawn
    C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin
    C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe
    C:\Program Files\CMake\bin\cmake.exe

`C:\dev\ofg\DEVELOPING.md` describes manual installation expectations. This plan should make that document the source of truth for installing tools, while repository commands only verify and use those tools.

The current public Cloudflare URL is `https://ofg.chriscummings1024.workers.dev/`. That URL belongs to a Workers deployment built from GitHub. This plan does not modify that Workers route. The planned deployment path creates or uses a Cloudflare Pages project named `ofg` and uploads the prebuilt `.deploy` directory through Wrangler Pages Direct Upload.

## Plan of Work

Milestone 1 removes the old bootstrap surface. Delete `tools/setup-emscripten.mjs`, `tools/setup-llvm.mjs`, `tools/setup-ninja.mjs`, and `tools/setup-dawn.mjs`. Remove `setup:*` entries from `package.json`. Update documentation so no current command list tells developers to run repository-managed downloaders. Keep install instructions in `DEVELOPING.md`, but rewrite any text saying the repo still contains helper scripts. Update `README.md`, `docs/SYSTEMS.md`, and `AGENTS.md` to match the new command set.

Milestone 2 refactors tool discovery. Add a small shared helper under `C:\dev\ofg\tools\lib` if it reduces duplication. The helper should resolve required commands from `PATH`, explicit environment variables, and known installed-tool roots only when that does not download or mutate installs. For Emscripten, prefer `emcmake` and `em++` from the active environment and require `EMSDK` only when needed for Emscripten cache/config paths. For native C++ tools, require Clang-family compilers and LLVM coverage tools. The implementation may discover Visual Studio LLVM and Visual Studio Ninja as installed system tools, but it must never use `C:\dev\ofg\artifacts\toolchains`.

Milestone 2 also removes local mutation hacks from wrappers. `tools/test-cpp.mjs`, `tools/cpp-coverage.mjs`, and `tools/smoke-render-cpp.mjs` should not copy `lld.exe` to `lld-link.exe` inside a toolchain directory. The installed system toolchain must provide required binaries. Coverage should not patch the LLVM resource directory by copying profiling runtimes unless there is a narrowly documented reason and the destination is not a repository-managed downloaded toolchain.

Milestone 2 changes `tools/smoke-render-cpp.mjs` so native smoke requires `OFG_DAWN_SOURCE_DIR`. The script should validate that the path exists, contains Dawn's `CMakeLists.txt`, and, when it is a Git checkout, compare `git rev-parse HEAD` to `C:\dev\ofg\dawn-version.txt`. Revision drift should be reported clearly, but a newer installed checkout is accepted if the native smoke build and render validation pass.

Milestone 3 adds local deploy. Add `npm run deploy` to `package.json`, implemented by a new script such as `C:\dev\ofg\tools\deploy-cloudflare-pages.mjs`. The script should run the existing package flow, verify `.deploy` through `tools/package-site.mjs`, accept `--project-name=ofg` or `CLOUDFLARE_PAGES_PROJECT_NAME`, and run local Wrangler Pages deploy. It must not use `npx`. The Cloudflare upload command should be:

    wrangler pages deploy .deploy --project-name=ofg

When run through npm, `wrangler` resolves from `C:\dev\ofg\node_modules\.bin`. The script should also support a local `--dry-run` mode that packages, verifies, and prints the exact Wrangler command without uploading.

Milestone 3 should also create the Cloudflare Pages project if it does not exist and the user has approved external Cloudflare state changes. The expected one-time command is:

    wrangler pages project create ofg --production-branch main

Milestone 4 removes generated old toolchains. Delete `C:\dev\ofg\artifacts\toolchains` after verifying the wrappers no longer reference it. This operation should be guarded by resolving the absolute path and confirming it is exactly under `C:\dev\ofg\artifacts\toolchains` before recursive deletion. After deletion, run a repository search for `setup:`, `setup-emscripten`, `setup-llvm`, `setup-ninja`, `setup-dawn`, `artifacts/toolchains`, `artifacts\\toolchains`, and downloader APIs such as `https.get` in tool scripts.

Milestone 5 validates, reviews, and records outcomes. Run the required build, test, smoke, package, coverage, and deploy commands. Because this plan affects deployment output and browser artifacts, run browser smoke and present the latest screenshot artifact path in chat for human review. Run the `milestone-review` skill after each milestone before marking the milestone complete, as required by `C:\dev\ofg\PLANS.md`.

## Concrete Steps

Work from the repository root:

    cd C:\dev\ofg

Before edits, capture baseline:

    git status --short
    rg -n "setup:|setup-emscripten|setup-llvm|setup-ninja|setup-dawn|artifacts/toolchains|artifacts\\toolchains|https\\.get|download" package.json tools README.md DEVELOPING.md docs AGENTS.md
    Get-Command emcmake,em++,cmake,clang,clang++,clang-cl,llvm-cov,llvm-profdata,ninja -ErrorAction SilentlyContinue

Milestone 1 edit targets:

    C:\dev\ofg\package.json
    C:\dev\ofg\README.md
    C:\dev\ofg\DEVELOPING.md
    C:\dev\ofg\docs\SYSTEMS.md
    C:\dev\ofg\AGENTS.md
    C:\dev\ofg\tools\setup-emscripten.mjs
    C:\dev\ofg\tools\setup-llvm.mjs
    C:\dev\ofg\tools\setup-ninja.mjs
    C:\dev\ofg\tools\setup-dawn.mjs

Milestone 2 edit targets:

    C:\dev\ofg\tools\build-cpp-wasm.mjs
    C:\dev\ofg\tools\test-cpp.mjs
    C:\dev\ofg\tools\cpp-coverage.mjs
    C:\dev\ofg\tools\smoke-render-cpp.mjs
    C:\dev\ofg\tools\lib\toolchain.mjs

Milestone 3 edit targets:

    C:\dev\ofg\package.json
    C:\dev\ofg\package-lock.json
    C:\dev\ofg\tools\deploy-cloudflare-pages.mjs
    C:\dev\ofg\tools\cloudflare-build.mjs
    C:\dev\ofg\README.md
    C:\dev\ofg\DEVELOPING.md
    C:\dev\ofg\docs\SYSTEMS.md

Milestone 4 cleanup target:

    C:\dev\ofg\artifacts\toolchains

Validation commands:

    npm run build
    npm test
    npm run smoke:browser
    npm run smoke:render
    npm run coverage
    npm run package:site
    npm run deploy -- --project-name=ofg --dry-run
    npm run deploy -- --project-name=ofg

If `npm run smoke:render` fails because `OFG_DAWN_SOURCE_DIR` is unset, stop and record the exact failure in Surprises & Discoveries. The intended final command is:

    $env:OFG_DAWN_SOURCE_DIR = "C:\tools\dawn"
    npm run smoke:render

The final implementation should not silently accept Dawn revision drift; it should report drift and then prove the installed checkout by building and running native smoke.

## Milestone Review

After each milestone:

1. Update changed active docs and `C:\dev\ofg\docs\API_CONTRACTS.md` if the generated artifact or deployment contract changed.
2. Run the `milestone-review` skill against the milestone diff and this ExecPlan.
3. Apply required findings before marking the milestone complete, or record a rejected finding with rationale in Decision Log.
4. Re-run relevant validation commands for the milestone.
5. Record the review summary, commands, artifacts, and remaining risks in Progress or Outcomes & Retrospective.

## Validation and Acceptance

This plan is accepted only when all of the following are true.

There are no repository commands or scripts that auto-download build tools or Dawn. A search must find no active setup/downloader command for Emscripten, LLVM, Ninja, CMake, Visual Studio Build Tools, or Dawn. Version files and human-readable install docs may remain.

There are no wrapper fallbacks to `C:\dev\ofg\artifacts\toolchains`. Repository commands may write build products, smoke artifacts, coverage reports, and deployment packages under `C:\dev\ofg\artifacts`, but must not write or read toolchains there.

The old downloaded build tools are gone. `Test-Path C:\dev\ofg\artifacts\toolchains` must be false, or the directory must exist but be empty because a non-tool generated process recreated the parent. In either case, `dawn`, `emsdk`, `llvm`, and `ninja` must not exist under that path.

The repo builds and runs using installed system tools. `npm run build`, `npm test`, `npm run smoke:browser`, and `npm run smoke:render` must pass with no `setup:*` step. `npm run smoke:render` must use `OFG_DAWN_SOURCE_DIR=C:\tools\dawn` or another explicit installed Dawn checkout, and the smoke result is the compatibility proof.

Cloudflare deploy works locally. `npm run deploy -- --project-name=ofg` must package `.deploy`, verify expected runtime files and `_headers`, and upload to the Cloudflare Pages project `ofg` through the already authenticated local Wrangler session. No Cloudflare API token is required by this plan.

Coverage remains healthy. Run `npm run coverage` and confirm changed implementation files do not appear in the default filtered coverage attention output. If a wrapper script is not covered by unit tests, record the command-level validation exception and rationale here before completion.

Deployment and visual tracking are complete. Run `npm run smoke:browser` after packaging/deploy changes and present the latest browser smoke screenshot artifact path in chat. The human reviewer should verify the app still opens to the actual render surface and that deployment packaging did not break WebGPU headers.

## Idempotence and Recovery

Deleting setup scripts is source-controlled and reversible through Git if a mistaken file is removed. Do not revert unrelated user changes in `C:\dev\ofg\package.json`, `C:\dev\ofg\package-lock.json`, or the active renderer plan; inspect and preserve them.

Removing `C:\dev\ofg\artifacts\toolchains` is safe because it is generated and ignored, but recursive deletion must be guarded. Resolve the target absolute path, confirm it is exactly `C:\dev\ofg\artifacts\toolchains`, and use native PowerShell deletion rather than string-built shell deletion.

If system tools are missing, wrappers should fail with clear install/activation instructions from `C:\dev\ofg\DEVELOPING.md`. Do not reintroduce auto-download behavior to recover from missing tools.

If Visual Studio LLVM or Ninja are installed but not on `PATH`, implementation may either require the user to add them to `PATH` or discover the installed Visual Studio paths without downloading anything. Record the chosen behavior in Decision Log and docs.

If the Cloudflare deploy upload fails because the project name, account, or authentication is wrong, keep `npm run deploy -- --project-name=ofg --dry-run`, `npm run package:site`, and Wrangler command printing intact. Record the external failure and do not weaken packaging verification.

## Artifacts and Notes

Important generated outputs after success:

    C:\dev\ofg\.deploy
    C:\dev\ofg\.deploy\_headers
    C:\dev\ofg\.deploy\index.html
    C:\dev\ofg\.deploy\dist\app\main.js
    C:\dev\ofg\.deploy\assets\wasm\ofg_cpp\ofg_cpp.js
    C:\dev\ofg\.deploy\assets\wasm\ofg_cpp\ofg_cpp.wasm
    C:\dev\ofg\artifacts\browser-smoke
    C:\dev\ofg\artifacts\render-smoke
    C:\dev\ofg\artifacts\coverage

Important paths that should not exist after success:

    C:\dev\ofg\artifacts\toolchains\dawn
    C:\dev\ofg\artifacts\toolchains\emsdk
    C:\dev\ofg\artifacts\toolchains\llvm
    C:\dev\ofg\artifacts\toolchains\ninja

Reference commands for human deployment checks:

    .\node_modules\.bin\wrangler.cmd whoami
    npm run deploy -- --project-name=ofg --dry-run
    npm run deploy -- --project-name=ofg

## Interfaces and Dependencies

At the end of this plan, `C:\dev\ofg\package.json` should expose these build and deploy interfaces:

    npm run build:wasm
    npm run build
    npm run package:site
    npm run package:site:from-build
    npm run build:cloudflare
    npm run deploy
    npm run test:cpp
    npm run test:ts
    npm test
    npm run smoke:browser
    npm run smoke:browser:cpp
    npm run smoke:render
    npm run smoke
    npm run coverage:cpp
    npm run coverage:ts
    npm run coverage
    npm run dev

At the end of this plan, `C:\dev\ofg\package.json` should not expose:

    npm run setup:emscripten
    npm run setup:llvm
    npm run setup:ninja
    npm run setup:dawn

The deployment script interface should be:

    npm run deploy -- --project-name=ofg
    npm run deploy -- --project-name=ofg --dry-run

The native render smoke interface should be:

    $env:OFG_DAWN_SOURCE_DIR = "C:\tools\dawn"
    npm run smoke:render

The script should not infer `C:\dev\ofg\artifacts\toolchains\dawn` as a fallback.
