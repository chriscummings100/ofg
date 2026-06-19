# OFG (Online factory game)

This project is a game designed to be:
- fully open world
- a mega factory simulation (think Satisfactory - https://www.satisfactorygame.com/)
- with an evolving world (think Planet Crafters - https://planet-crafter.fandom.com/wiki/Planet_Crafter_Wiki)
- multiplayer (through server running same simulation app as clients)
- browser based
- written from the ground up, with no engine

The goal is to hit a high bar, and prove that it is possible to write a fully functional game with high end graphical and networking components from scratch.

## Languages + Tests

We will use 2 languages for this job:
- the vast majority of the code should be in rust / web assembly
- with a type script based web front end

Tests are critical for both languages.
- rust: https://doc.rust-lang.org/rust-by-example/testing/unit_testing.html
- type script: https://mochajs.org/

Coverage is part of the testing contract for both Rust and TypeScript. Use `COVERAGE.md` for how to run it, how to interpret exceptions, and where results are stored. Latest committed coverage summaries belong in `docs/coverage`; generated local reports belong in `artifacts/coverage`.

## Plans

- Written plans should follow the ExecPlan template as described in [PLANS.md](PLANS.md). It is critical that after any context compaction the most recent plan is re-read in full, to repopulate context.
- Working plans should be stored in docs/plans.
- When a plan is completed it should be moved to docs/archived. Archived plans do not need to be updated or maintained, but can be used as reference if necessary.
- During tasks that affect browser UI, rendering, visual output, or deployment output, take screenshots regularly and present them in chat for human review and tracking. Include the artifact path when the screenshot is stored in the repo.

## Guiding principles

Guiding principles for code development are in [GUIDES.md](GUIDES.md). These principles can and should be added to over time.

## Existing Commands

Run commands from the repository root, `C:\dev\ofg`.

- `npm install`: installs Node/TypeScript tooling from `package-lock.json`.
- `npm run clean`: removes generated TypeScript build output.
- `npm run build:wasm`: builds the Rust `ofg_web` WASM package and generated JS glue into `assets/wasm/ofg_web`.
- `npm run build`: cleans, builds WASM, and compiles the browser TypeScript app.
- `npm run package:site`: rebuilds the app and packages Cloudflare Pages output into `.deploy`.
- `npm run package:site:from-build`: packages `.deploy` from an already-built app, useful when a caller has just run `npm run build`.
- `npm run build:cloudflare`: Cloudflare Pages build command; packages the site and reports deployable WASM size.
- `npm run dev`: builds the app and starts the local static dev server, normally at `http://127.0.0.1:5173`.
- `npm run smoke:browser`: builds the app, controls a browser through Playwright core, and validates browser startup/render behavior.
- `npm run smoke:render`: renders the shared Rust bootstrap renderer without a browser and writes a PNG/report.
- `npm run smoke`: runs browser smoke followed by native render smoke.
- `npm run coverage:rust`: runs the Rust coverage gate through `cargo-llvm-cov`.
- `npm run coverage:ts`: runs the TypeScript coverage gate through `c8`.
- `npm run coverage`: runs both Rust and TypeScript coverage gates.

## Test Commands

- `npm run test:rust`: runs all native Rust unit tests in the workspace with `cargo test --workspace`.
- `npm run test:wasm`: verifies the `ofg_web` browser-facing contract, runs wasm32 tests, and type-checks the WASM target.
- `npm run test:ts`: builds the app/test output and runs Mocha tests against the TypeScript browser host and WASM runtime wrapper.
- `npm test`: runs `test:rust`, `test:wasm`, and `test:ts`; this is the default unit/integration test gate.
- `npm run smoke:browser`: validates the built browser app with Playwright core and should be used when TypeScript host, WASM loading, resize, browser WebGPU, or deployment shell behavior changes.
- `npm run smoke:render`: validates the Rust renderer can produce a browser-free PNG and JSON diagnostics.
- `npm run coverage`: validates coverage thresholds and records generated summaries under `artifacts/coverage`.

## Dev Server

For browser, renderer, or visual work, keep a local dev server running so a human can continuously review the app. Use `npm run dev`; if port 5173 is busy, the server chooses the next available port and prints the URL. When starting or restarting it, report the URL in chat.

Before finalizing visual/browser work, verify the running app with a browser smoke or screenshot. During longer tasks, refresh screenshots regularly and present them in chat so visual progress is inspectable.

## Old code base

A previous attempt at this project can be found here: C:\dev\ofg-old. Its code should **not** be used as good examples, however simple information such as how to deploy to cloud flare can be utilized.
