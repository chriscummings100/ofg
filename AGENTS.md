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
- the vast majority of the code should be in C++ / web assembly
- with a type script based web front end

Tests are critical for both languages.
- C++: use Clang-only builds, doctest test executables registered with CMake/CTest, and LLVM/Clang source-based coverage
- type script: https://mochajs.org/

Coverage is part of the testing contract for both C++ and TypeScript. Use `COVERAGE.md` for how to run it, how to interpret exceptions, and where results are stored. Latest committed coverage summaries belong in `docs/coverage`; generated local reports belong in `artifacts/coverage`.

The project retired Rust from active runtime work under `docs/archived/cpp-wasm-migration-plan.md`. Do not start new Rust engine work unless a later plan explicitly reverses this decision.

## Plans

- Written plans should follow the ExecPlan template as described in [PLANS.md](PLANS.md). It is critical that after any context compaction the most recent plan is re-read in full, to repopulate context.
- Working plans should be stored in docs/plans.
- When a plan is completed it should be moved to docs/archived. Archived plans do not need to be updated or maintained, but can be used as reference if necessary.
- During tasks that affect browser UI, rendering, visual output, or deployment output, take screenshots regularly and present them in chat for human review and tracking. Include the artifact path when the screenshot is stored in the repo.

## Guiding principles

Guiding principles for code development are in [GUIDES.md](GUIDES.md). These principles can and should be added to over time.

## Comments and readability

Code should remain readable to a human at all times. Every function written should have doc strings or comments attached defining its purpose, and larger functions (over 50 lines) should contain comments internally to explain their workings.

Files should have detailed and maintained comments at the top to document their purpose and how they function.

## File sizes

Large files make for poor readability, small files are just noise:
- Files between 500-1000 lines should begin to be of small concern
- Files above 1000 lines should be broken into smaller units
- Files above 2000 lines should be considered a critical architectural problem

## Engine references

- The Bevy documentation is a useful reference for game-engine patterns, especially asset handles and `Assets<T>` style collections: https://docs.rs/bevy/latest/bevy/index.html. Use it as design reference only; OFG should remain written from the ground up and should not take a dependency on Bevy unless a future plan explicitly decides to.

## Existing Commands

Run commands from the repository root, `C:\dev\ofg`.

- `npm install`: installs Node/TypeScript tooling from `package-lock.json`.
- `npm run clean`: removes generated TypeScript build output.
- `npm run setup:emscripten`: installs the pinned Emscripten SDK under `artifacts/toolchains/emsdk`.
- `npm run setup:dawn`: installs the pinned Dawn source checkout under `artifacts/toolchains/dawn/src` for native C++ render smoke.
- `npm run setup:llvm`: installs the pinned native LLVM/Clang bundle under `artifacts/toolchains/llvm`.
- `npm run setup:ninja`: installs the pinned Ninja generator under `artifacts/toolchains/ninja`.
- `npm run build:wasm`: builds the C++/WASM package and generated JS glue into `assets/wasm/ofg_cpp`.
- `npm run build`: cleans, builds WASM, and compiles the browser TypeScript app.
- `npm run package:site`: rebuilds the app and packages Cloudflare Pages output into `.deploy`.
- `npm run package:site:from-build`: packages `.deploy` from an already-built app, useful when a caller has just run `npm run build`.
- `npm run build:cloudflare`: Cloudflare Pages build command; packages the site and reports deployable WASM size.
- `npm run dev`: builds the app and starts the local static dev server, normally at `http://127.0.0.1:5173`.
- `npm run smoke:browser`: builds the app, controls a browser through Playwright core, and validates browser startup/render behavior.
- `npm run smoke:browser:cpp`: runs the focused C++/WASM browser fixture and validates WebGPU initialization/status behavior plus bootstrap triangle pixels.
- `npm run smoke:render`: builds/runs the Clang-native C++ Dawn render smoke without a browser and writes a PNG/report.
- `npm run smoke`: runs browser smoke followed by native C++ render smoke.
- `npm run coverage:cpp`: runs the C++ coverage gate through Clang/LLVM source-based coverage.
- `npm run coverage:ts`: runs the TypeScript coverage gate through `c8`.
- `npm run coverage`: runs both C++ and TypeScript coverage gates.

## Test Commands

- `npm run test:cpp`: runs native C++ doctest tests through CMake/CTest.
- `npm run test:ts`: builds the app/test output and runs Mocha tests against the TypeScript browser host and WASM runtime wrapper.
- `npm test`: runs `test:cpp` and `test:ts`; this is the default unit/integration test gate.
- `npm run smoke:browser`: validates the built browser app with Playwright core and should be used when TypeScript host, WASM loading, resize, browser WebGPU, or deployment shell behavior changes.
- `npm run smoke:render`: validates the C++ Dawn renderer can produce a browser-free PNG and JSON diagnostics.
- `npm run coverage`: validates coverage thresholds and records generated summaries under `artifacts/coverage`.

## Dev Server

For browser, renderer, or visual work, keep a local dev server running so a human can continuously review the app. Use `npm run dev`; if port 5173 is busy, the server chooses the next available port and prints the URL. When starting or restarting it, report the URL in chat.

Before finalizing visual/browser work, verify the running app with a browser smoke or screenshot. During longer tasks, refresh screenshots regularly and present them in chat so visual progress is inspectable.

## Old code base

A previous attempt at this project can be found here: C:\dev\ofg-old. Its code should **not** be used as good examples, however simple information such as how to deploy to cloud flare can be utilized.
