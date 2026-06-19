# Bootstrap the Online Factory Game Foundation

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

This document follows `C:\dev\ofg\PLANS.md`. It is deliberately self-contained so a future agent can continue after context compaction by rereading this file.

## Purpose / Big Picture

This plan creates the first working OFG foundation after the restart. At the end, a user can run a local dev server, open `http://127.0.0.1:5173` or the printed fallback port, and see a WebGPU frame rendered by Rust/WASM into a canvas hosted by a small TypeScript shell. The same deterministic scene input can also be rendered without a browser through native `wgpu` and written to a PNG for automated tests.

The first visual target is fixed: a full-canvas dark blue-gray clear color with one large triangle in the center, using red, green, and blue vertex colors. The target is humble on purpose. The goal is not graphical ambition yet; it is a trustworthy architecture, test harness, browser smoke path, PNG smoke path, and Cloudflare Pages deployment package that can carry the rest of the game.

## Progress

- [x] (2026-06-19 13:27Z) Read `C:\dev\ofg\PLANS.md`, `C:\dev\ofg\docs\GUIDES.md`, and `C:\dev\ofg\docs\SYSTEMS.md`.
- [x] (2026-06-19 13:27Z) Inspected `C:\dev\ofg-old\package.json`, `C:\dev\ofg-old\tools\cloudflare-build.mjs`, `C:\dev\ofg-old\tools\package-site.mjs`, and `C:\dev\ofg-old\docs\archived\REMOTE_DEPLOYMENT_PLAN.md` for deployment precedent.
- [x] (2026-06-19 13:27Z) Drafted this proposed bootstrap ExecPlan in `C:\dev\ofg\docs\plans\initial-bootstrap-plan.md`.
- [x] (2026-06-19 14:10Z) Reviewed the plan with the `review-plan` skill using correctness, completeness, clarity, efficiency, and performance reviewers.
- [x] (2026-06-19 14:25Z) Revised the plan to add exact contracts, tighter milestones, a DOM test strategy, a reproducible WASM pipeline, stronger render validation, and explicit Cloudflare Pages vs Workers scope.
- [x] (2026-06-19 15:05Z) Implemented Milestone 1 contracts, minimal TypeScript shell substrate, dev server, and TypeScript tests.
- [x] (2026-06-19 15:08Z) Milestone 1 validation passed: `npm run build`, `npm run test:ts`, and `npm test`; Mocha reported 5 passing canvas-host tests.
- [x] (2026-06-19 15:09Z) Milestone 1 dev-server check passed: temporary `npm run dev` served `http://127.0.0.1:5173/` with HTTP 200, canvas HTML, and COOP/COEP/CORP headers.
- [x] (2026-06-19 15:26Z) Ran `milestone-review` for Milestone 1 with contract, code-quality, legacy, correctness, and validation reviewers; applied required findings.
- [x] (2026-06-19 15:29Z) Milestone 1 validation rerun passed: `npm run build`, `npm run test:ts`, and `npm test`; Mocha now reports 8 passing canvas-host tests including fractional DPR, zero-axis, and id-collision coverage. Dev-server check still returned HTTP 200 with COOP/COEP/CORP, and malformed path `/%` returned 404 instead of crashing.
- [x] (2026-06-19 14:38Z) Implemented Milestone 2: Rust core/render crates, browser WASM facade, reproducible `build:wasm`, TypeScript WASM runtime loader, and Playwright Core browser WebGPU smoke.
- [x] (2026-06-19 14:38Z) Milestone 2 pre-review validation passed: `npm run test:rust`, `cargo check -p ofg_web --target wasm32-unknown-unknown`, `npm run build:wasm`, `npm run build`, `npm run test:ts`, `npm test`, and `npm run smoke:browser`.
- [x] (2026-06-19 15:00Z) Ran `milestone-review` for Milestone 2 with contract, code-quality, legacy, correctness, and validation reviewers; applied required findings.
- [x] (2026-06-19 15:00Z) Milestone 2 post-review validation passed: `cargo fmt --all -- --check`, `npm run test:rust`, `npm run test:wasm`, `npm run test:ts`, `npm test`, and `npm run smoke:browser`.
- [x] (2026-06-19 16:07Z) Implemented Milestone 3: native offscreen renderer PNG smoke through shared `ofg_render` shader, scene, and renderer code.
- [x] (2026-06-19 16:07Z) Milestone 3 pre-review validation passed: `npm run smoke:render`, `npm run smoke`, and `cargo fmt --all -- --check`; generated `C:\dev\ofg\artifacts\render-smoke\bootstrap.png` and `C:\dev\ofg\artifacts\render-smoke\report.json`, and preserved browser-smoke artifacts after combined smoke.
- [x] (2026-06-19 15:19Z) Ran `milestone-review` for Milestone 3 with contract, code-quality, legacy, correctness, and validation reviewers; applied required findings.
- [x] (2026-06-19 15:19Z) Milestone 3 post-review validation passed: `cargo fmt --all -- --check`, `npm run test:rust`, `npm run test:wasm`, and `npm run smoke`.
- [x] (2026-06-19 16:30Z) Implemented Milestone 4: Rust coverage wrapper, TypeScript c8 coverage wrapper, and TypeScript ownership-boundary tests.
- [x] (2026-06-19 16:30Z) Milestone 4 pre-review validation passed: `npm run test:ts`, `npm run coverage:rust`, `npm run coverage:ts`, and `npm run coverage`.
- [x] (2026-06-19 16:37Z) Ran `milestone-review` for Milestone 4 with contract, code-quality, legacy, correctness, and validation reviewers; applied required findings.
- [x] (2026-06-19 16:37Z) Milestone 4 post-review validation passed: `npm run test:ts`, `npm run coverage:rust`, `npm run coverage:ts`, and `npm run coverage`.
- [x] (2026-06-19 16:47Z) Implemented Milestone 5: Cloudflare Pages packaging, Cloudflare build wrapper, deployment system docs, and README deployment settings.
- [x] (2026-06-19 16:47Z) Milestone 5 pre-review validation passed: `npm run package:site` and `npm run build:cloudflare`; `.deploy/` contains the expected runtime files and `_headers`, and the build wrapper printed generated WASM size `270131` bytes.
- [x] (2026-06-19 16:52Z) Ran `milestone-review` for Milestone 5 with contract, code-quality, legacy, correctness, and validation reviewers; applied required findings.
- [x] (2026-06-19 16:52Z) Milestone 5 post-review validation passed: `npm run package:site` and `npm run build:cloudflare`; `.deploy/` contains only runtime files and `_headers` includes both `/` and `/index.html` no-store rules.
- [x] (2026-06-19 16:57Z) Final local acceptance passed: `npm test`, `npm run coverage`, `npm run smoke`, and `npm run build:cloudflare`.

## Surprises & Discoveries

- Observation: The new repository is intentionally bare.
  Evidence: `rg --files` in `C:\dev\ofg` initially listed only `AGENTS.md`, `PLANS.md`, `docs\GUIDES.md`, and `docs\SYSTEMS.md`.

- Observation: The old project's last documented Cloudflare route was Cloudflare Workers Builds with static assets, not a pure Pages-only workflow.
  Evidence: `C:\dev\ofg-old\docs\archived\REMOTE_DEPLOYMENT_PLAN.md` records root directory `/`, build command `npm run build:cloudflare`, deploy command `npx wrangler deploy`, and static assets packaged into `.deploy/`.

- Observation: Cloudflare Pages can use a custom build output directory and a `_headers` file in that output directory.
  Evidence: Cloudflare Pages build configuration documents build commands/directories, and Cloudflare Pages headers documentation says `_headers` can go directly in the build output directory for non-framework sites.

- Observation: Cloudflare Workers static assets require an assets directory in Wrangler configuration or equivalent deploy configuration.
  Evidence: Cloudflare Workers static assets documentation describes the `assets.directory` value in `wrangler.jsonc`/`wrangler.toml` as central to deployment.

- Observation: The old deployment scripts are useful as process references but should not be copied as architecture.
  Evidence: `C:\dev\ofg-old\tools\cloudflare-build.mjs` bootstraps Rust, adds `wasm32-unknown-unknown`, installs a pinned `wasm-bindgen-cli`, runs build scripts, and calls `tools/package-site.mjs`; `C:\dev\ofg-old\tools\package-site.mjs` writes cross-origin isolation headers into `.deploy/_headers`.

- Observation: `wgpu 0.20.1` compiled for WASM but failed in current Chrome during `requestDevice`.
  Evidence: the browser reported the unrecognized WebGPU limit `maxInterStageShaderComponents`; upgrading to Rust `1.96.0`, `wgpu 29.0.3`, and `wasm-bindgen 0.2.125` made `cargo check -p ofg_web --target wasm32-unknown-unknown` and `npm run smoke:browser` pass.

- Observation: The generated WASM glue must use the same `wasm-bindgen` version family as the Rust crate.
  Evidence: `tools/build-wasm.mjs` rejected the existing local `wasm-bindgen 0.2.100`; installing `wasm-bindgen-cli 0.2.125` allowed `npm run build:wasm` to generate `assets/wasm/ofg_web/`.

- Observation: The native PNG smoke can use the same render pipeline and produce the same coverage ratios as browser smoke.
  Evidence: `npm run smoke:render` wrote `C:\dev\ofg\artifacts\render-smoke\bootstrap.png` and `report.json` with width 800, height 450, texture format `Rgba8Unorm`, triangle ratio about 23%, background ratio about 77%, and 28 non-background color buckets.

## Decision Log

- Decision: Keep TypeScript as a narrow browser shell, not a gameplay or render owner.
  Rationale: The project goal says the vast majority of code should be Rust/WASM. TypeScript owns DOM startup, canvas sizing, input collection later, WASM loading, dev ergonomics, and Playwright test helpers. Rust owns game state, frame data, and WebGPU rendering.
  Date/Author: 2026-06-19 / Codex

- Decision: Use a shared Rust render crate for browser WebGPU and native offscreen image smoke.
  Rationale: The native PNG path should test the same scene, shader source, pipeline layout, vertex data, and draw path as the browser path except for the final target: browser surface versus native offscreen texture.
  Date/Author: 2026-06-19 / Codex

- Decision: Start without a bundler.
  Rationale: Plain TypeScript compiled to ESM keeps the first build understandable. A bundler can be introduced later when asset hashing, code splitting, or module graph complexity justifies it.
  Date/Author: 2026-06-19 / Codex

- Decision: Target Cloudflare Pages as the default deployment route for this restart.
  Rationale: The current user request names Cloudflare Pages. Pages supports a custom build output directory and `_headers` in the output. The old Workers route remains useful deployment history but is documentation-only in this plan unless a later plan adds `wrangler.jsonc`, a pinned `wrangler` dependency, and Workers validation.
  Date/Author: 2026-06-19 / Codex

- Decision: Commit `Cargo.lock` once the Rust workspace is created.
  Rationale: This is an application/workspace producing deployable WASM and native test binaries. Locking dependencies makes local, CI, and Cloudflare builds reproducible.
  Date/Author: 2026-06-19 / Codex

- Decision: Treat `assets/wasm/ofg_web/` as generated output, not source.
  Rationale: The browser needs stable static paths, but the WASM JS glue, `.wasm`, and `.d.ts` files are produced by `tools/build-wasm.mjs`. They should be ignored, cleaned, rebuilt, packaged into `.deploy/`, and never edited by hand.
  Date/Author: 2026-06-19 / Codex

- Decision: Keep `test:wasm` non-GPU.
  Rationale: `wasm-bindgen-test` should validate facade and serialization behavior that can reliably pass in a headless browser. Real WebGPU availability and rendering are validated by `smoke:browser`.
  Date/Author: 2026-06-19 / Codex

- Decision: Add validation with each slice instead of batching it late.
  Rationale: TypeScript tests should land with the shell, Rust tests with Rust modules, browser smoke with browser rendering, native PNG smoke with native rendering, and coverage once implementation files exist.
  Date/Author: 2026-06-19 / Codex

- Decision: Pin the bootstrap Rust toolchain and WebGPU stack to Rust `1.96.0`, `wgpu 29.0.3`, and `wasm-bindgen 0.2.125`.
  Rationale: The older `wgpu` stack emitted a browser WebGPU limit that current Chrome no longer accepts. The newer stack keeps browser WebGPU smoke aligned with the installed Chromium-family browser while preserving a fixed, reproducible bootstrap.
  Date/Author: 2026-06-19 / Codex

- Decision: Use `Rgba8Unorm` for the native offscreen smoke texture instead of the provisional `Rgba8UnormSrgb`.
  Rationale: Browser smoke currently renders to a non-sRGB `Bgra8Unorm` surface. Using `Rgba8Unorm` for native readback keeps the clear-color byte contract `[27, 37, 50, 255]` and pixel-classification thresholds identical across browser and browser-free smoke while still reporting the actual texture format.
  Date/Author: 2026-06-19 / Codex

## Outcomes & Retrospective

Milestone 1 implementation and validation are complete. The repository now has the initial TypeScript canvas host, dev server, API contracts, systems docs, package metadata, and TypeScript test lane.

Milestone 2 implementation and review are complete. Rust now owns the bootstrap frame state, scene data, WebGPU renderer, and browser WASM facade. TypeScript loads the generated WASM package, forwards resize/frame calls, and exposes `window.__ofgDebugStatus` for smoke diagnostics. Browser smoke generated `C:\dev\ofg\artifacts\browser-smoke\bootstrap.png` and `C:\dev\ofg\artifacts\browser-smoke\report.json`; the report recorded WebGPU available, cross-origin isolation true, final `frameCount: 10`, one pipeline, one vertex buffer, a resize probe from 800x450 to 640x360 and back, final `surfaceConfigureCount: 3`, and a sampled triangle ratio of about 23%.

Milestone 1 review:

- Scope: contracts, minimal TypeScript browser shell, dev server, Node package metadata, and TypeScript tests.
- Reviewers: contract, code quality, legacy, correctness, validation.
- Required findings fixed: synchronized `docs/API_CONTRACTS.md` with the active plan, removed the placeholder system entry, updated this Outcomes section, added Node `>=20` metadata/docs, made canvas id collisions explicit errors, preserved zero-size canvas axes, corrected fractional DPR sizing to floor after DPR multiplication, and made malformed dev-server paths return a controlled 404.
- Follow-ups recorded: none.
- Rejected findings: none.
- Validation rerun: `npm run build`, `npm run test:ts`, `npm test`, and a temporary `npm run dev` HTTP/header/malformed-path check.
- Remaining risk: npm audit reports dev-dependency vulnerabilities. They are not in runtime code, but should be revisited once dependency versions stabilize.

Milestone 2 review:

- Scope: Rust core/render crates, browser WASM facade, TypeScript WASM runtime wrapper, reproducible WASM build script, and Playwright Core browser WebGPU smoke.
- Reviewers: contract, code quality, legacy, correctness, validation.
- Required findings fixed: added an actual `wasm-bindgen-test` lane to `test:wasm`; added strict TypeScript validation for `RuntimeDebugStatus`; made TypeScript disposal call the generated wasm-bindgen `free()` once; made Rust `dispose()` drop runtime state and reject later use; cleared stale `last_error` after successful resize/frame calls; made DPR-only resize changes trigger Rust surface reconfiguration; fixed the canvas host so browser resize events are not consumed before Rust sees them; added a browser-smoke resize probe; recovered from `Outdated` and `Lost` surface acquisition states; closed the browser in smoke-test failure paths; parsed the exact `wasm-bindgen` CLI version; added purpose comments to boundary tool/shader files; updated generated WASM artifact docs; relabeled the Milestone 1 review block; and clarified that `docs/API_CONTRACTS.md` is the active contract source of truth.
- Follow-ups recorded: none.
- Rejected findings: none.
- Validation rerun: `cargo fmt --all -- --check`, `npm run test:rust`, `npm run test:wasm`, `npm run test:ts`, `npm test`, and `npm run smoke:browser`.
- Remaining risk at Milestone 2 completion: browser smoke depended on a local Chromium-family browser with WebGPU available; Milestone 3 resolved the missing browser-free render coverage with native PNG smoke.

Milestone 3 implementation and review are complete. The new `ofg_test_harness` crate provides `ofg-render-frame`, which renders the shared `ofg_render` bootstrap shader/scene/renderer into an 800x450 offscreen texture, unpads GPU readback rows, writes `C:\dev\ofg\artifacts\render-smoke\bootstrap.png`, and writes `C:\dev\ofg\artifacts\render-smoke\report.json`. The PNG was visually inspected and shows the expected RGB triangle on the dark clear color. The browser and native smoke paths now load `C:\dev\ofg\tools\smoke-contract.json` for dimensions, clear color, sampling, and threshold predicates. The native report recorded `Rgba8Unorm`, adapter `NVIDIA GeForce RTX 3050 Ti Laptop GPU`, backend `Vulkan`, clear color `[27,37,50,255]`, triangle ratio about 23%, background ratio about 77%, 28 non-background color buckets, `passed: true`, and `failureReason: null`.

Milestone 3 review:

- Scope: native offscreen renderer PNG smoke through shared `ofg_render` code, the `ofg_test_harness` crate, smoke script wiring, contracts, and current docs.
- Reviewers: contract, code quality, legacy, correctness, validation.
- Required findings fixed: changed active docs/comments from future-tense native smoke to present-tense; updated Milestone 3 validation evidence to include `npm run smoke` and format checks; removed unused `serde` from `ofg_render`; stopped requesting adapter-elevated limits in browser and native device creation; added a shared machine-readable smoke contract used by browser and native smoke; made native readback use finite map and channel timeouts; made native smoke always write a report with thresholds, `passed`, and `failureReason`; printed native artifact paths on success; passed render dimensions explicitly through readback/PNG/inspection helpers.
- Follow-ups recorded: none.
- Rejected findings: none.
- Validation rerun: `cargo fmt --all -- --check`, `npm run test:rust`, `npm run test:wasm`, and `npm run smoke`.
- Remaining risk: native smoke depends on a local native `wgpu` adapter. If no adapter is available, the script fails clearly rather than generating a fake image.

Milestone 4 implementation and review are complete. `tools/rust-coverage.mjs` runs `cargo-llvm-cov` across workspace tests, runs the native render-smoke binary under instrumentation, writes `C:\dev\ofg\artifacts\coverage\rust\summary.json` and `summary.pretty.json`, and enforces 90% line coverage for non-exception workspace files while failing if a Rust implementation file is absent from coverage without an explicit omitted-file exception. `tools\ts-coverage.mjs` builds the app/tests, runs Mocha through `c8`, writes `C:\dev\ofg\artifacts\coverage\ts\coverage-summary.json`, and enforces 90% line coverage for non-exception TypeScript app files. `tests\ts\ownershipBoundary.test.ts` verifies `src/app/**` only references generated WASM internals through `wasmRuntime.ts` and does not take WebGPU draw ownership. Validation passed with Rust checked files at or above 90%, `src/app/canvasHost.ts` at 97.51% line coverage, `src/app/wasmRuntime.ts` at 96.11% line coverage, and the recorded smoke-covered exceptions printed by the coverage wrappers.

Milestone 4 review:

- Scope: Rust and TypeScript coverage wrappers, TypeScript ownership-boundary tests, coverage exceptions, docs, and command wiring.
- Reviewers: contract, code quality, legacy, correctness, validation.
- Required findings fixed: changed coverage docs from changed-file language to the implemented non-exception-file policy; added explicit Rust omitted-file handling and output for `crates/ofg_web/src/browser.rs`; made Rust coverage fail when implementation files under `crates/**/src/**/*.rs` are absent from coverage without an explicit omitted-file exception; made the TypeScript ownership test recurse through `src/app/**` and compare generated-WASM import exceptions by normalized repo-relative path; expanded the TypeScript WebGPU denylist and added representative denylist snippets; fixed Windows cross-drive path handling in coverage wrappers; added unit coverage for the WASM wrapper lifecycle and removed the broad `src/app/wasmRuntime.ts` exception.
- Follow-ups recorded: none.
- Rejected findings: none.
- Validation rerun: `npm run test:ts`, `npm run coverage:rust`, `npm run coverage:ts`, and `npm run coverage`.
- Remaining risk: coverage thresholds are line-based only for now. Branch coverage is reported by c8 and cargo-llvm-cov but not enforced in this bootstrap milestone.

Milestone 5 implementation and pre-review validation are complete. `tools\package-site.mjs` recreates `.deploy`, copies an explicit allowlist of browser runtime files, writes `_headers` with COOP/COEP/CORP and conservative cache rules, and verifies required output files including `index.html`, `dist/app/main.js`, `src/app/styles.css`, `ofg_web.js`, and `ofg_web_bg.wasm` while rejecting non-runtime files such as declarations and source maps. `npm run package:site` rebuilds before packaging, while `package:site:from-build` is the internal packager used after an existing build. `tools\cloudflare-build.mjs` verifies Rust/rustup, adds `wasm32-unknown-unknown`, verifies or installs the shared `wasm-bindgen-cli` version from `tools/wasm-bindgen-version.mjs`, runs `npm run build`, runs `npm run package:site:from-build`, and prints generated WASM size. README documents Cloudflare Pages root `/`, build command `npm run build:cloudflare`, build output directory `.deploy`, Node version `.node-version`, and marks the old Workers route as historical until a future `wrangler.jsonc` plan exists.

Milestone 5 review:

- Scope: Cloudflare Pages packaging, Cloudflare build wrapper, deploy headers, generated artifact policy, README deploy instructions, and old Workers route documentation.
- Reviewers: contract, code quality, legacy, correctness, validation.
- Required findings fixed: switched packaging from whole-directory copies to an exact recursive runtime allowlist; removed declarations and source maps from `.deploy`; added a recursive unexpected-file check; added a `/` no-store rule alongside `/index.html`; made `package:site` rebuild before packaging and added internal `package:site:from-build`; gated automatic rustup installation to Linux only; moved the wasm-bindgen CLI version into `tools/wasm-bindgen-version.mjs`; added `.node-version`; documented the Pages Node setting.
- Follow-ups recorded: none.
- Rejected findings: none.
- Validation rerun: `npm run package:site` and `npm run build:cloudflare`.
- Remaining risk: Cloudflare account/project configuration is user-owned; local scripts validate the package and build command but do not deploy to a Cloudflare account.

Final validation is complete. `npm test` passed native Rust tests, real WASM tests, target `cargo check`, and 18 Mocha tests. `npm run coverage` passed Rust and TypeScript coverage gates with recorded exceptions only: `crates/ofg_test_harness/src/bin/ofg-render-frame.rs` line coverage is below 90% because the uncovered lines are failure handling while the success path is instrumented by native smoke, `crates/ofg_web/src/browser.rs` is a wasm32-only omitted-file exception covered by `test:wasm` and `smoke:browser`, and `src/app/main.ts` is covered by browser smoke. `npm run smoke` passed browser WebGPU smoke and native offscreen PNG smoke; both reports recorded 800x450 output, about 23% triangle coverage, about 77% background coverage, and 28 non-background color buckets. `npm run build:cloudflare` passed, packaged exactly the runtime deploy files, wrote `_headers`, and printed WASM size `270131` bytes.

## Contract and Quality Baseline

`C:\dev\ofg\docs\API_CONTRACTS.md` is the active source of truth for these contract IDs. The list below is retained as a synchronized milestone snapshot so the plan stays self-contained after context compaction.

`OFG-BOOT-001 TypeScript Host Ownership`: TypeScript may own DOM boot, canvas lookup/creation, canvas resize policy, fatal-error display, local dev ergonomics, WASM module loading, and Playwright smoke helpers. TypeScript must not own gameplay simulation, scene graph state, GPU pipeline creation, render draw submission, or game-world data structures.

`OFG-BOOT-002 Rust Runtime Ownership`: Rust owns frame state, debug status, scene data for the bootstrap triangle, renderer setup, WebGPU resource creation, draw submission, and native offscreen rendering.

`OFG-BOOT-003 WASM Facade`: The browser facade is narrow. TypeScript can create the runtime, resize it, request a frame, read debug status, and dispose it. The facade should not expose raw renderer internals, GPU handles, or mutable scene ownership to TypeScript.

`OFG-BOOT-004 Renderer Compatibility`: Browser and native smoke use the same WGSL source at `crates/ofg_render/src/shaders/bootstrap.wgsl`, the same bootstrap scene data from `crates/ofg_render/src/bootstrap_scene.rs`, and the same renderer module from `crates/ofg_render/src/renderer.rs`. Allowed differences are only the final output target and reported adapter/surface format. The browser path renders to a canvas surface; the native path renders to an offscreen texture for readback.

`OFG-BOOT-005 WebGPU Baseline`: The renderer requests no optional GPU features, does not manually request limits above the adapter defaults, uses one render pipeline for the bootstrap scene, and records adapter/backend/format data in smoke reports. It uses a dark blue-gray clear color and a red/green/blue triangle. Surface or texture formats must be reported; native smoke uses `Rgba8Unorm` so PNG readback preserves byte-identical clear-color classification with browser smoke.

`OFG-BOOT-006 Resource Lifetime`: Pipeline, shader module, vertex buffer, and bind-group-like resources must be created during initialization or explicit resize, not every frame. Resize reconfigures the surface only when physical width, physical height, or clamped device-pixel-ratio changes. Zero-size canvas axes must be preserved by the browser host so the Rust facade can skip surface configuration and report a recoverable debug status instead of panicking.

`OFG-BOOT-007 Generated Artifacts`: `dist/`, `dist-test/`, `target/`, `.deploy/`, `artifacts/`, and `assets/wasm/ofg_web/` are generated and ignored. `Cargo.lock` and `package-lock.json` are source-controlled.

`OFG-BOOT-008 Deployment`: The default deployment target is Cloudflare Pages with build output directory `.deploy`. Workers static-assets deployment is documentation-only in this plan. If Workers becomes an implementation target, add `wrangler.jsonc` with `assets.directory = "./.deploy"` and a pinned `wrangler` dependency before claiming support.

`OFG-BOOT-009 Coverage`: Implementation files should meet 90% line coverage unless an exception is recorded in this plan. Current exceptions are `crates/ofg_web/src/browser.rs` because browser-only WASM/WebGPU code is covered by `test:wasm` and `smoke:browser` rather than native `cargo-llvm-cov`; `crates/ofg_test_harness/src/bin/ofg-render-frame.rs` because its success path is covered by instrumented native smoke and its remaining uncovered lines are failure handling; and `src/app/main.ts` because the browser entrypoint is covered by `smoke:browser`.

Quality rules for this plan: keep files below 500 lines when practical, treat 500-1000 lines as review pressure, split files above 1000 lines unless there is a recorded reason, add top-of-file comments only for modules with important ownership or runtime responsibilities, and document public functions or non-obvious logic. Avoid placeholder scripts: a script is added in the milestone where it can pass.

## Context and Orientation

The repository root is `C:\dev\ofg`. The project is an online factory game intended to be browser based, mostly Rust/WASM, with a TypeScript front end and no external game engine. The current repository starts from planning documents only. `C:\dev\ofg-old` is the previous attempt. Its code is not a design model, but its Cloudflare deployment scripts and archived deployment plan are useful deployment references.

WebGPU is the browser GPU API used by OFG. In Rust, the `wgpu` crate provides a cross-platform API that can target native GPUs for tests and WebGPU in the browser through WASM. WASM means WebAssembly, the compiled Rust artifact loaded by the browser. `wasm-bindgen` generates JavaScript glue so TypeScript can instantiate and call Rust code compiled to WASM.

The first page loads `index.html`, compiled TypeScript from `dist/`, generated WASM assets from `assets/wasm/ofg_web/`, and `src/app/styles.css`. A local static dev server serves the app at `http://127.0.0.1:5173` unless the port is busy, in which case it chooses the next available port and prints the URL. The server must send these headers on every app response:

    Cross-Origin-Embedder-Policy: require-corp
    Cross-Origin-Opener-Policy: same-origin
    Cross-Origin-Resource-Policy: same-origin

The server must serve `.wasm` files as `application/wasm`.

Cloudflare Pages should run the repository build and publish `.deploy/`. The `_headers` file in `.deploy/` must apply the cross-origin isolation headers above. Cache policy is intentionally conservative for the first deploy: `index.html` should be `no-store`; JS and WASM can be `no-cache` until a future hashed-asset plan introduces immutable caching.

## Plan of Work

Milestone 1 creates the minimal project skeleton and TypeScript shell substrate. Add `package.json`, `package-lock.json`, `tsconfig.json`, `tsconfig.app.json`, `tsconfig.test.json`, `.gitignore`, `.gitattributes`, `README.md`, `docs/API_CONTRACTS.md`, and update `docs/SYSTEMS.md`. Add `index.html`, `src/app/styles.css`, `src/app/canvasHost.ts`, `src/app/main.ts` with a temporary "runtime unavailable" path, `tests/ts/setupDom.ts`, `tests/ts/canvasHost.test.ts`, `tools/clean-dist.mjs`, and `tools/dev-server.mjs`. Use `happy-dom` for TypeScript unit tests so Mocha can exercise DOM and canvas sizing in Node. The first scripts are `clean`, `build`, `test:ts`, `test`, and `dev`; each must pass in this milestone. Do not create empty future crate or tool directories just to reserve names.

Milestone 2 adds Rust/WASM browser rendering. Add `rust-toolchain.toml`, root `Cargo.toml`, `crates/ofg_core`, `crates/ofg_render`, `crates/ofg_web`, and `tools/build-wasm.mjs`. `crates/ofg_web` must be a Rust library with `crate-type = ["cdylib", "rlib"]`. `tools/build-wasm.mjs` runs:

    cargo build -p ofg_web --target wasm32-unknown-unknown --release
    wasm-bindgen target/wasm32-unknown-unknown/release/ofg_web.wasm --target web --out-dir assets/wasm/ofg_web --out-name ofg_web --typescript

The generated files are `assets/wasm/ofg_web/ofg_web.js`, `assets/wasm/ofg_web/ofg_web_bg.wasm`, `assets/wasm/ofg_web/ofg_web.d.ts`, and `assets/wasm/ofg_web/ofg_web_bg.wasm.d.ts`. The script checks the installed `wasm-bindgen` CLI version against the `wasm-bindgen` crate version pinned in `Cargo.lock` or a single version constant in the script, and fails with setup guidance rather than silently using a mismatched CLI. Add `src/app/wasmRuntime.ts` to load the generated module and create the Rust runtime. Add `tools/browser-smoke.mjs` after the browser path renders. The scripts `build:wasm`, `build`, `test:rust`, `test:wasm`, `smoke:browser`, `smoke`, and `test` must pass. `test:wasm` covers non-GPU facade behavior only; `smoke:browser` covers real WebGPU.

Milestone 3 adds native offscreen rendering. Add `crates/ofg_test_harness` and a binary named `ofg-render-frame`. The binary uses `ofg_render` and the same bootstrap scene/shader as the browser path, creates a native `wgpu` device, renders to an 800x450 offscreen texture, reads pixels back with correct `wgpu` copy alignment and padded `bytes_per_row` handling, unpads rows, writes `artifacts/render-smoke/bootstrap.png`, and writes `artifacts/render-smoke/report.json`. The script `smoke:render` must pass and `smoke` must run both render and browser smoke.

Milestone 4 adds coverage and ownership guardrails. Add `tools/rust-coverage.mjs`, TypeScript coverage through `c8`, and a small TypeScript import-boundary test that verifies `src/app/**` does not import Rust-generated WASM internals except through `src/app/wasmRuntime.ts` and does not contain WebGPU draw ownership. `coverage:rust` uses `cargo-llvm-cov` without installing it automatically; if missing, it exits non-zero with setup guidance. `coverage:ts` runs Mocha through `c8` with source maps. `coverage` runs both and fails when non-exception implementation files appear below 90% line coverage unless an exception is recorded in this plan.

Milestone 5 implements Cloudflare Pages packaging. Add `tools/package-site.mjs` to recreate `.deploy/`, copy only runtime files, verify exact expected output paths, and write `_headers`. Add `tools/cloudflare-build.mjs` based on the old process: verify Rust and rustup, install rustup only on Linux when missing, add `wasm32-unknown-unknown`, verify/install the pinned `wasm-bindgen-cli`, run `npm run build`, run the from-build packager, and print generated WASM size. Update `README.md` with local setup, commands, and Cloudflare Pages settings: root directory `/`, build command `npm run build:cloudflare`, build output directory `.deploy`, and Node version `.node-version`. Document the old Workers route only as historical context and say it requires a later `wrangler.jsonc` implementation before use.

## Concrete Steps

Run these commands from `C:\dev\ofg`.

Milestone 1 validation:

    npm install
    npm run build
    npm run test:ts
    npm test

Expected result: `package-lock.json` is created and committed, TypeScript app files compile to `dist/`, TypeScript tests compile to `dist-test/`, Mocha runs with `happy-dom`, and all tests pass.

Manual dev-server check after Milestone 1:

    npm run dev

Expected result: the server prints `http://127.0.0.1:5173` or a fallback port. Opening that URL shows the canvas host page and a clear "runtime unavailable" message until Milestone 2 replaces it with WASM rendering. Response headers include COOP, COEP, and CORP.

Milestone 2 validation:

    rustup target add wasm32-unknown-unknown
    npm run build:wasm
    npm run build
    npm run test:rust
    npm run test:wasm
    npm run smoke:browser
    npm test

Expected result: `assets/wasm/ofg_web/ofg_web.js`, `ofg_web_bg.wasm`, `ofg_web.d.ts`, and `ofg_web_bg.wasm.d.ts` are generated but ignored; native Rust tests pass; WASM non-GPU facade tests pass; browser smoke opens a Chromium-family browser through Playwright Core and verifies a WebGPU-rendered triangle.

Milestone 3 validation:

    npm run smoke:render
    npm run smoke

Expected result: `artifacts/render-smoke/bootstrap.png` and `artifacts/render-smoke/report.json` are written. The report records width 800, height 450, texture format, adapter/backend, clear color, non-background pixel counts, distinct color bucket counts, and pass/fail predicates. The image is not accepted merely for being nonblank.

Milestone 4 validation:

    npm run coverage:rust
    npm run coverage:ts
    npm run coverage

Expected result: `artifacts/coverage/rust/summary.json`, `artifacts/coverage/rust/summary.pretty.json`, and `artifacts/coverage/ts/coverage-summary.json` are written. Default terminal output lists non-exception implementation files below 90% line coverage and recorded exceptions. A passing run says no checked implementation files are below threshold, with the recorded `ofg_web` browser-only exception covered by `test:wasm` and `smoke:browser`.

Milestone 5 validation:

    npm run package:site
    npm run build:cloudflare

Expected result: `.deploy/` contains exactly the runtime files needed by the browser, including:

    .deploy/index.html
    .deploy/_headers
    .deploy/dist/app/main.js
    .deploy/src/app/styles.css
    .deploy/assets/wasm/ofg_web/ofg_web.js
    .deploy/assets/wasm/ofg_web/ofg_web_bg.wasm
    .deploy/dist/app/canvasHost.js
    .deploy/dist/app/wasmRuntime.js

Generated declaration files and source maps are build artifacts but not runtime deploy inputs, so `package:site` must not copy them into `.deploy/`.

Final local acceptance:

    npm test
    npm run coverage
    npm run smoke
    npm run build:cloudflare

Expected result: all commands pass, and generated output remains ignored.

## Milestone Review

After each milestone:

1. Update `docs/API_CONTRACTS.md`, `docs/SYSTEMS.md`, `README.md`, and this ExecPlan if the implementation changes architecture, commands, or acceptance criteria.
2. Run the repo-local `milestone-review` skill against the milestone diff and this ExecPlan.
3. Apply required findings before marking the milestone complete, or record a rejected finding with rationale in the Decision Log.
4. Re-run relevant validation commands.
5. Record the review summary, commands, artifacts, and remaining risks in Progress or Outcomes & Retrospective.

## Validation and Acceptance

The bootstrap is accepted when all of these are true:

1. `npm run dev` serves a local URL, and opening it displays a full-viewport canvas with a visible Rust/WASM WebGPU triangle after Milestone 2.
2. TypeScript does not contain gameplay simulation, scene graph ownership, renderer ownership, GPU pipeline creation, or WebGPU draw submission beyond hosting the canvas and calling the Rust facade.
3. The app uses a concrete DPR policy: clamp `globalThis.devicePixelRatio || 1` to the range 1.0 through 2.0, compute physical canvas size as `floor(cssSize * clampedDpr)`, preserve zero-sized axes, and reconfigure only when physical size or clamped DPR changes. Tests simulate at least DPR 1.0, 1.5, 2.0, above-clamp values, fractional CSS sizes, and a zero-sized axis.
4. `RuntimeDebugStatus` includes at least `initialized`, `frameCount`, `canvasWidth`, `canvasHeight`, `devicePixelRatio`, `surfaceFormat`, `adapterName`, `backend`, `pipelineCreateCount`, `bufferCreateCount`, `surfaceConfigureCount`, and `lastError`.
5. Browser smoke uses Playwright Core to set a deterministic 800x450 viewport with device scale factor 1, starts the local dev server, verifies COOP/COEP/CORP headers, checks `crossOriginIsolated`, confirms WebGPU availability, waits for `RuntimeDebugStatus.initialized === true` and `frameCount >= 2`, crops the screenshot to canvas bounds, verifies backing canvas dimensions, and samples the canvas crop.
6. Browser and native smoke require expected color coverage, not just "nonblank": at least 5% of sampled pixels must be classified as triangle/non-background, at least 40% as clear/background, and at least three non-background color buckets must be present within a documented tolerance.
7. `npm run smoke:render` writes a valid PNG from a browser-free native `wgpu` path that uses the shared `ofg_render` shader, scene, and renderer modules.
8. `npm run test:rust`, `npm run test:wasm`, `npm run test:ts`, and `npm test` pass. `test:wasm` must pass and must not depend on WebGPU.
9. `npm run coverage` runs Rust and TypeScript coverage and reports no non-exception implementation files below 90% line coverage, except explicitly recorded exceptions.
10. `npm run package:site` creates `.deploy/` with only runtime deploy inputs and a `_headers` file containing cross-origin isolation headers.
11. `npm run build:cloudflare` succeeds locally, prints the generated WASM size, and is documented as the Cloudflare Pages build command.
12. `README.md` documents local setup, commands, Cloudflare Pages settings, and the old Workers route as historical/documentation-only unless a later plan adds real Workers config.
13. Generated outputs such as `dist/`, `.deploy/`, `target/`, `node_modules/`, `dist-test/`, `artifacts/`, and `assets/wasm/ofg_web/` are ignored and not committed. `Cargo.lock` and `package-lock.json` are committed.

## Idempotence and Recovery

Build and packaging scripts must be safe to rerun. `tools/clean-dist.mjs` may remove only known generated directories inside `C:\dev\ofg`: `dist`, `dist-test`, `.deploy`, `artifacts`, and `assets/wasm/ofg_web`. It must not delete `target` by default because Rust rebuilds are expensive. `tools/package-site.mjs` must resolve the repository root from its own file path and refuse to delete or copy outside the workspace.

If browser WebGPU is unavailable locally, `smoke:browser` should fail with a clear message and preserve diagnostic artifacts under `artifacts/browser-smoke/`. Native offscreen smoke remains valuable regression coverage, but browser WebGPU support is required for full acceptance.

If the native `wgpu` adapter is unavailable, `smoke:render` should fail clearly and preserve logs. Do not replace it with a fake image.

If `cargo-llvm-cov` is missing, `coverage:rust` should print install guidance and exit non-zero without mutating reports. If `wasm-bindgen` CLI is missing or mismatched, `build:wasm` should print the expected version and exit non-zero unless `tools/cloudflare-build.mjs` is running in its Linux bootstrap path.

If Cloudflare changes its build image, `tools/cloudflare-build.mjs` should print the missing tool and attempted install step. Cloudflare account setup remains user-owned; repository scripts must not require secrets for local packaging.

If a deployment is bad, rollback is performed in Cloudflare Pages or by pushing a fix. Generated deploy output is recreated from source rather than edited by hand.

## Artifacts and Notes

Proposed source layout by the end of the plan:

    C:\dev\ofg\index.html
    C:\dev\ofg\package.json
    C:\dev\ofg\package-lock.json
    C:\dev\ofg\rust-toolchain.toml
    C:\dev\ofg\tsconfig.json
    C:\dev\ofg\tsconfig.app.json
    C:\dev\ofg\tsconfig.test.json
    C:\dev\ofg\Cargo.toml
    C:\dev\ofg\Cargo.lock
    C:\dev\ofg\docs\API_CONTRACTS.md
    C:\dev\ofg\src\app\main.ts
    C:\dev\ofg\src\app\canvasHost.ts
    C:\dev\ofg\src\app\wasmRuntime.ts
    C:\dev\ofg\src\app\styles.css
    C:\dev\ofg\tests\ts\setupDom.ts
    C:\dev\ofg\tests\ts\canvasHost.test.ts
    C:\dev\ofg\tests\ts\ownershipBoundary.test.ts
    C:\dev\ofg\crates\ofg_core\
    C:\dev\ofg\crates\ofg_render\
    C:\dev\ofg\crates\ofg_web\
    C:\dev\ofg\crates\ofg_test_harness\
    C:\dev\ofg\tools\clean-dist.mjs
    C:\dev\ofg\tools\build-wasm.mjs
    C:\dev\ofg\tools\dev-server.mjs
    C:\dev\ofg\tools\browser-smoke.mjs
    C:\dev\ofg\tools\package-site.mjs
    C:\dev\ofg\tools\cloudflare-build.mjs
    C:\dev\ofg\tools\rust-coverage.mjs

Generated output paths:

    C:\dev\ofg\dist\
    C:\dev\ofg\dist-test\
    C:\dev\ofg\assets\wasm\ofg_web\
    C:\dev\ofg\artifacts\
    C:\dev\ofg\.deploy\
    C:\dev\ofg\target\

Initial `.deploy/_headers` intent:

    /*
      Cross-Origin-Embedder-Policy: require-corp
      Cross-Origin-Opener-Policy: same-origin
      Cross-Origin-Resource-Policy: same-origin

    /index.html
      Cache-Control: no-store

    /dist/*
      Cache-Control: no-cache

    /assets/wasm/*
      Cache-Control: no-cache

Old deployment process reference:

    C:\dev\ofg-old\package.json
      build:cloudflare = node tools/cloudflare-build.mjs
      package:site = node tools/package-site.mjs

    C:\dev\ofg-old\tools\cloudflare-build.mjs
      Ensures Rust/rustup, adds wasm32-unknown-unknown, installs wasm-bindgen-cli 0.2.100, builds, and packages.

    C:\dev\ofg-old\tools\package-site.mjs
      Recreates .deploy, copies runtime paths, and writes _headers with COOP/COEP/CORP.

    C:\dev\ofg-old\docs\archived\REMOTE_DEPLOYMENT_PLAN.md
      Records the old Workers Builds settings and the rationale for explicit .deploy packaging.

External deployment references used while revising this plan:

    Cloudflare Pages build configuration:
    https://developers.cloudflare.com/pages/configuration/build-configuration/

    Cloudflare Pages headers:
    https://developers.cloudflare.com/pages/configuration/headers/

    Cloudflare Workers static assets:
    https://developers.cloudflare.com/workers/static-assets/

    Cloudflare Workers Builds configuration:
    https://developers.cloudflare.com/workers/ci-cd/builds/configuration/

## Interfaces and Dependencies

TypeScript dependencies:

`typescript`, `mocha`, `c8`, `happy-dom`, `playwright-core`, and `pngjs` should be enough for the first shell, DOM-like unit tests, coverage, browser smoke, and PNG inspection. Avoid frontend frameworks for the initial canvas host.

Rust dependencies:

`wgpu`, `wasm-bindgen`, `wasm-bindgen-futures`, `web-sys`, `js-sys`, `console_error_panic_hook`, `png`, `serde`, `serde_json`, `bytemuck`, `pollster`, and `wasm-bindgen-test` are the expected starting set. Keep versions compatible with the selected `wasm-bindgen-cli`, and pin them through `Cargo.lock`.

Provisional TypeScript interfaces:

`src/app/canvasHost.ts` exports a small canvas host API that can be tested without WebGPU:

    export interface CanvasSize {
      readonly cssWidth: number;
      readonly cssHeight: number;
      readonly physicalWidth: number;
      readonly physicalHeight: number;
      readonly devicePixelRatio: number;
      readonly changed: boolean;
    }

    export interface CanvasHost {
      readonly canvas: HTMLCanvasElement;
      readonly size: CanvasSize;
      resize(): CanvasSize;
      dispose(): void;
    }

`src/app/wasmRuntime.ts` exposes runtime loading without leaking game ownership into TypeScript:

    export interface RuntimeDebugStatus {
      readonly initialized: boolean;
      readonly frameCount: number;
      readonly canvasWidth: number;
      readonly canvasHeight: number;
      readonly devicePixelRatio: number;
      readonly surfaceFormat: string;
      readonly adapterName: string;
      readonly backend: string;
      readonly pipelineCreateCount: number;
      readonly bufferCreateCount: number;
      readonly surfaceConfigureCount: number;
      readonly lastError: string | null;
    }

    export interface BrowserGameRuntime {
      resize(width: number, height: number, devicePixelRatio: number): void;
      frame(timeMs: number): void;
      debugStatus(): RuntimeDebugStatus;
      dispose(): void;
    }

Stable Rust/WASM facade:

`crates/ofg_web` exposes a `BrowserGame` class or factory through `wasm-bindgen`. The exact `wasm-bindgen` shape may adjust to the selected crate version, but it must provide these capabilities to TypeScript: async create from `HtmlCanvasElement`, `resize(width, height, device_pixel_ratio)`, `frame(time_ms)`, `debug_status_json()`, and `dispose()`.

Stable native smoke binary:

`crates/ofg_test_harness` provides `ofg-render-frame` with arguments:

    cargo run -p ofg_test_harness --bin ofg-render-frame -- --out artifacts/render-smoke

The binary writes `bootstrap.png` and `report.json`, exits non-zero when color coverage predicates fail, and prints the artifact paths.

Stable Cloudflare Pages interface:

    Root directory: /
    Build command: npm run build:cloudflare
    Build output directory: .deploy
    Node version: .node-version

Workers-compatible deployment is not implemented by this plan. A future Workers plan must add:

    wrangler.jsonc with assets.directory = "./.deploy"
    a pinned wrangler devDependency
    validation for npx wrangler deploy
