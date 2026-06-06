# Deploy OFG to a Public Remote Test URL

This ExecPlan is a living document. The sections Progress, Surprises &
Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as
work proceeds.

Once this plan is started, proceed independently for as long as possible.
Return to the user only for critical input that cannot be safely inferred, or
when the plan is complete.

If `PLANS.md` is present in the repo, maintain this document in accordance with
it.

## Purpose / Big Picture

OFG should automatically build and publish to a public HTTPS URL whenever the
main branch is pushed, with optional preview URLs for other branches. The user
should be able to develop remotely, push changes, wait for CI, then open a
stable Cloudflare URL such as `https://ofg.<account-subdomain>.workers.dev` on
any WebGPU-capable browser.

The deployed site is static: `index.html`, compiled JavaScript under `dist/`,
WASM and texture assets under `assets/`, and the stylesheet currently served
from `src/app/styles.css`. The host must serve HTTPS and must support custom
headers because the local dev server already sends cross-origin isolation
headers used by the browser smoke test.

The chosen first target is now Cloudflare Workers Builds with static assets,
because the user connected the repository through Cloudflare's integrated build
flow. Cloudflare Workers can host static assets from a configured output
directory, create deployments through `npx wrangler deploy`, and read a
`_headers` file from the static asset directory. The repository remains the build
authority: it installs or verifies Rust/WASM tooling, builds shaders and WASM,
compiles TypeScript, packages static assets, and lets Wrangler upload `.deploy/`.

## Progress

- [x] (2026-06-06 08:56Z) Read `PLANS.md` and created this deployment ExecPlan.
- [x] (2026-06-06 08:56Z) Inspected `package.json`, `index.html`,
  `tools/dev-server.mjs`, `tools/browser-smoke.mjs`, `assets/`, and `dist/` to
  identify deploy inputs and required response headers.
- [x] (2026-06-06 09:17Z) Added `tools/package-site.mjs`,
  `npm run package:site`, `.deploy/` ignore rules, and `_headers` generation.
- [x] (2026-06-06 09:17Z) Added `tools/cloudflare-build.mjs` and
  `npm run build:cloudflare` to bootstrap Rust/WASM tooling in Cloudflare's
  build image before packaging the site.
- [x] (2026-06-06 09:17Z) Added `wrangler.jsonc` for Cloudflare Workers static
  assets with `assets.directory = "./.deploy"` and SPA fallback routing.
- [ ] Decide whether to stop ignoring and start committing `Cargo.lock` for
  reproducible Rust/WASM CI builds.
- [x] (2026-06-06 09:17Z) User created the Cloudflare Git integration through
  Workers Builds rather than GitHub Actions direct upload.
- [x] (2026-06-06 09:17Z) Verified `npm run build:cloudflare` locally; it
  builds OFG and packages `.deploy/`.
- [x] (2026-06-06 09:17Z) Verified `npm test` locally; all 62 tests passed.
- [ ] User updates the Cloudflare build command to `npm run build:cloudflare`.
- [ ] Push the repo-side Cloudflare config and confirm the first production
  deployment URL.
- [ ] Verify the remote URL loads OFG, includes required headers, serves WASM
  assets correctly, and renders a playable WebGPU frame.

## Surprises & Discoveries

- Observation: `index.html` links the stylesheet at `/src/app/styles.css`, not
  under `dist/`.
  Evidence: `index.html` contains `<link rel="stylesheet" href="/src/app/styles.css" />`.

- Observation: The local dev server sends cross-origin isolation headers, and
  the browser smoke test expects cross-origin isolation plus
  `SharedArrayBuffer`.
  Evidence: `tools/dev-server.mjs` sets
  `cross-origin-embedder-policy: require-corp` and
  `cross-origin-opener-policy: same-origin`; `tools/browser-smoke.mjs` checks
  `globalThis.crossOriginIsolated === true` and `SharedArrayBuffer`.

- Observation: The current browser smoke script is local-Windows oriented.
  Evidence: `tools/browser-smoke.mjs` searches Chrome and Edge under
  `C:/Program Files` unless `OFG_BROWSER_PATH` is set.

- Observation: `Cargo.lock` exists locally but is ignored and not tracked.
  Evidence: `.gitignore` includes `Cargo.lock`, and
  `git -c safe.directory=C:/dev/ofg ls-files Cargo.lock` prints no tracked
  path.

- Observation: The user-created Cloudflare integration is Workers Builds, not a
  Pages Git integration or GitHub Actions direct upload.
  Evidence: The Cloudflare build settings show `Deploy command` as
  `npx wrangler deploy`; Cloudflare's Workers Builds documentation describes
  this as the default Worker deploy command.

- Observation: Cloudflare's Workers Builds image does not list Rust, Cargo, or
  wasm-bindgen as preinstalled tooling.
  Evidence: Cloudflare's Workers Builds build image documentation lists Node.js,
  Python, Ruby, Go, Bun, Hugo, npm, yarn, pnpm, pip, gem, poetry, pipx, and
  bundler, but not Rust/Cargo. OFG's `npm run build` calls Rust/WASM build
  scripts.

## Decision Log

- Decision: Initially prefer Cloudflare Pages rather than GitHub Pages for the
  first public remote deployment.
  Rationale: This app needs custom response headers for cross-origin isolation;
  Cloudflare Pages supports a `_headers` file in the static output. GitHub Pages
  is attractive for simple static sites, but custom response headers are not a
  dependable fit for this project. This decision is superseded in practice by
  the later Workers Builds decision because the user created a Worker
  integration, and Workers static assets also support `_headers`.
  Date/Author: 2026-06-06 / Codex

- Decision: Initially build and test in GitHub Actions, then upload a prepared
  `.deploy/` folder with Wrangler.
  Rationale: OFG's build has Rust/WASM, shader generation, checked browser
  assets, and TypeScript output. Keeping the deploy artifact explicit makes the
  deployed contents reviewable and avoids committing generated `dist/` output.
  This decision is superseded by the later Workers Builds decision because the
  Cloudflare Git integration now owns the build and deploy run.
  Date/Author: 2026-06-06 / Codex

- Decision: Preserve the app's current absolute paths in the first deployment
  instead of introducing a bundler or path rewrite.
  Rationale: The browser app already expects `/dist/...`, `/assets/...`, and
  `/src/app/styles.css`. A packaging step can publish those paths with minimal
  risk, while bundling can be considered later as a separate build-system
  change.
  Date/Author: 2026-06-06 / Codex

- Decision: Treat Cloudflare account creation and dashboard integration as
  user-owned setup.
  Rationale: Those actions require account access and credentials that should
  not be created or exposed by an agent. With Workers Builds, Cloudflare manages
  the build token in the dashboard, so this plan no longer requires GitHub
  Actions secrets.
  Date/Author: 2026-06-06 / Codex

- Decision: Adapt the implementation to Cloudflare Workers Builds with static
  assets instead of requiring the user to recreate the project as Cloudflare
  Pages.
  Rationale: The user already created an `ofg` Cloudflare integration whose
  build settings use `npx wrangler deploy`. Workers static assets support a
  configured assets directory and `_headers`, so this path can host OFG with
  fewer account-side changes.
  Date/Author: 2026-06-06 / Codex

- Decision: Use `npm run build:cloudflare` as the Cloudflare build command.
  Rationale: The normal `npm run build` assumes Rust and wasm-bindgen are
  already available. Cloudflare's Workers Builds image does not preinstall that
  Rust/WASM toolchain, so the Cloudflare-specific wrapper installs/verifies
  Rust, adds the `wasm32-unknown-unknown` target, installs `wasm-bindgen-cli`
  `0.2.100` if needed, then runs the normal build steps and packages `.deploy/`.
  Date/Author: 2026-06-06 / Codex

## Outcomes & Retrospective

The first repo-side Cloudflare Workers implementation is in place. The expected
outcome remains an automatic remote deployment process with a stable public URL,
plus a clear local packaging command that shows exactly what will be uploaded.

Remaining gaps before completion are updating the Cloudflare dashboard build
command, pushing the repo-side config, waiting for a successful Cloudflare
deployment, and verifying the remote browser behavior. Local validation passed
for the repo-side build and packaging path.

## Context and Orientation

The repository root is `C:\dev\ofg`. The app is a browser-native WebGPU game
prototype. `npm run build` currently runs:

    npm run clean
    npm run build:shaders
    npm run build:wasm
    tsc -p tsconfig.json

The build emits compiled TypeScript into `dist/`. Rust/WASM browser assets live
under `assets/wasm/`, and terrain textures live under `assets/textures/`. The
checked-in `index.html` is served from the repository root by
`tools/dev-server.mjs`, which means the deploy output must include a root
`index.html`, a root `dist/`, a root `assets/`, and a root `src/app/styles.css`
unless the app paths are changed in a separate refactor.

Cloudflare Workers is a serverless platform that can also serve static assets.
Workers Builds is Cloudflare's integrated Git build system for Worker projects.
Wrangler is Cloudflare's command line tool. In this repository,
`wrangler.jsonc` configures the Worker named `ofg` to deploy static assets from
`.deploy/`. A `_headers` file in the static asset directory tells Cloudflare to
apply the listed response headers to matching paths.

The local dev server currently sends these headers for every response:

    Cache-Control: no-store
    Cross-Origin-Embedder-Policy: require-corp
    Cross-Origin-Opener-Policy: same-origin
    Cross-Origin-Resource-Policy: same-origin

The remote deployment should preserve these headers, at least for the first
iteration, so remote behavior matches local browser smoke expectations.

## Plan of Work

Milestone 1 packages the deploy output without changing runtime behavior. Add
`tools/package-site.mjs`, a Node script that removes and recreates `.deploy/`,
then copies `index.html`, `dist/`, `assets/`, and `src/app/styles.css` into the
same paths inside `.deploy/`. The script also writes `.deploy/_headers` with the
cross-origin isolation headers above. Add `.deploy/` to `.gitignore`. Add
`"package:site": "node tools/package-site.mjs"` to `package.json`. This is now
implemented.

Milestone 2 makes the Rust/WASM CI build reproducible enough for deployments.
Because `Cargo.lock` is currently ignored and untracked, choose one of two paths.
The recommended path is to remove `Cargo.lock` from `.gitignore` and commit the
workspace `Cargo.lock`, because this repository builds application WASM artifacts
and CI should not silently resolve different dependency versions. The alternate
path is to leave `Cargo.lock` ignored and accept that CI resolves dependencies
from `Cargo.toml` constraints.

Milestone 3 adds the Cloudflare Workers integration files. Add
`tools/cloudflare-build.mjs`, which checks for Rust tooling, installs rustup on
Cloudflare's Linux build image if needed, adds the
`wasm32-unknown-unknown` target, installs `wasm-bindgen-cli` version `0.2.100`
if needed, runs OFG's build scripts, and packages `.deploy/`. Add
`wrangler.jsonc`, with `name = "ofg"` and `assets.directory = "./.deploy"`.
This is now implemented.

Milestone 4 completes user-owned dashboard setup. In the Cloudflare project
settings, keep the root directory as `/`, keep the deploy command as
`npx wrangler deploy`, and change the build command from `npm run build` to
`npm run build:cloudflare`.

Milestone 5 validates the remote site. After the Cloudflare build succeeds, open
the production URL and confirm a WebGPU-capable browser renders the terrain
scene. Confirm the remote response headers include
`Cross-Origin-Embedder-Policy: require-corp` and
`Cross-Origin-Opener-Policy: same-origin`. Confirm remote WASM assets return a
successful HTTP status and an appropriate content type.

Milestone 6 is optional hardening after the first successful deployment. Extend
`tools/browser-smoke.mjs` so it can run against an external URL through an
environment variable such as `OFG_SMOKE_URL`, and make Linux browser discovery
CI-friendly. Add a separate scheduled or manual workflow job for remote smoke
once browser availability is stable in CI.

## Concrete Steps

Run these commands from `C:\dev\ofg` while implementing the repo-side changes:

    npm test

Expected result: the command exits with status 0 after rebuilding shaders, WASM,
TypeScript, and running Mocha tests.

After adding `tools/package-site.mjs` and the package script:

    npm run package:site

Expected result: the command exits with status 0 and creates `.deploy/` with at
least these paths:

    .deploy/index.html
    .deploy/_headers
    .deploy/dist/main.js
    .deploy/assets/wasm/terrain_core.wasm
    .deploy/assets/wasm/engine_web/engine_web.js
    .deploy/assets/wasm/engine_web/engine_web_bg.wasm
    .deploy/src/app/styles.css

For the current Cloudflare Workers Builds integration, use these dashboard build
settings:

    Root directory: /
    Build command: npm run build:cloudflare
    Deploy command: npx wrangler deploy

The deploy command reads `wrangler.jsonc`, finds `assets.directory = "./.deploy"`,
and uploads the packaged static assets.

After the first successful deploy, verify the remote headers from a terminal:

    curl.exe -I <remote-url>/

Expected result: status is `200` and the response includes:

    Cross-Origin-Embedder-Policy: require-corp
    Cross-Origin-Opener-Policy: same-origin

Verify a WASM asset:

    curl.exe -I <remote-url>/assets/wasm/terrain_core.wasm

Expected result: status is `200`; `Content-Type` should be `application/wasm` or
another browser-accepted WASM MIME type. If the browser refuses to instantiate
WASM, fix the content type with a Cloudflare rule or worker-backed response.

## Validation and Acceptance

The deployment process is accepted when all of these are true:

1. `npm test` passes locally after the repo-side deployment changes.
2. `npm run package:site` creates `.deploy/` and includes exactly the runtime
   paths the browser app requests.
3. `npm run build:cloudflare` passes locally and creates `.deploy/`.
4. Cloudflare Workers Builds shows a successful production deployment from
   `main`.
5. Opening the production URL over HTTPS displays the OFG canvas, HUD, and
   terrain in a WebGPU-capable browser.
6. Pressing `C` or `F1` toggles the camera mode from `FIRST` to `FLY` on the
   remote site.
7. Remote headers include `Cross-Origin-Embedder-Policy: require-corp` and
   `Cross-Origin-Opener-Policy: same-origin`.
8. The remote page reports `crossOriginIsolated === true` in DevTools.
9. No generated `dist/`, `.deploy/`, `node_modules/`, `target/`, or `artifacts/`
   output is committed.

## Idempotence and Recovery

The packaging script must be safe to rerun. It should delete only the resolved
`.deploy/` directory inside `C:\dev\ofg`, recreate it, and copy known inputs. It
must not remove arbitrary paths computed from user input.

Cloudflare Workers deploys are versioned. If a deployment is bad, use the
Cloudflare dashboard to roll back to the prior deployment, or push a fix to
`main` and let Workers Builds publish a new version.

If a Workers Build fails before deployment, no remote site changes should occur.
Fix the failing build or test and rerun the Cloudflare build.

If the build fails during deployment because Cloudflare credentials or Git
integration permissions are invalid, update the Cloudflare project integration
and rerun the build. Do not print secret values in build logs.

If remote rendering is blank while local `npm run smoke:browser` passes, first
check remote headers, WASM asset status, texture asset status, browser console
errors, and whether the test browser supports WebGPU on that machine.

## Artifacts and Notes

The deploy `_headers` content should be:

    /*
      Cache-Control: no-store
      Cross-Origin-Embedder-Policy: require-corp
      Cross-Origin-Opener-Policy: same-origin
      Cross-Origin-Resource-Policy: same-origin

Relevant external documentation used to shape this plan:

    Cloudflare Workers Builds configuration:
    https://developers.cloudflare.com/workers/ci-cd/builds/configuration/

    Cloudflare Workers Builds build image:
    https://developers.cloudflare.com/workers/ci-cd/builds/build-image/

    Cloudflare Workers static assets:
    https://developers.cloudflare.com/workers/static-assets/

    Cloudflare Workers static asset headers:
    https://developers.cloudflare.com/workers/static-assets/headers/

## Interfaces and Dependencies

The repo-side implementation should leave these stable entry points:

`tools/package-site.mjs`:
Runs under Node from the repo root or from any current working directory. It
resolves the repository root from `import.meta.url`, deletes and recreates
`C:\dev\ofg\.deploy`, copies the deploy inputs, writes `_headers`, and exits
non-zero if a required input is missing.

`package.json`:
Includes `"package:site": "node tools/package-site.mjs"` and
`"build:cloudflare": "node tools/cloudflare-build.mjs"`.

`tools/cloudflare-build.mjs`:
Runs under Node in Cloudflare Workers Builds or locally. It installs/verifies
Rust tooling, ensures the `wasm32-unknown-unknown` target is available, ensures
`wasm-bindgen-cli` `0.2.100` is available, runs the normal OFG build steps, and
then runs `tools/package-site.mjs`.

`wrangler.jsonc`:
Configures the Cloudflare Worker named `ofg`, sets `compatibility_date`, and
points static asset deployment at `.deploy/`.

Cloudflare Workers Builds:
Root directory is `/`. Build command is `npm run build:cloudflare`. Deploy
command is `npx wrangler deploy`. The Cloudflare dashboard owns the Git
integration token.
