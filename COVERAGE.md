# Coverage

Coverage is a testing gate for the OFG bootstrap, not just a report. The wrappers fail when checked implementation files fall below the documented line threshold, currently 90%.

## Commands

Run from `C:\dev\ofg`.

- `npm run coverage:cpp`: runs `tools/cpp-coverage.mjs`, which uses Clang source-based coverage for native C++ doctest executables.
- `npm run coverage:ts`: runs `tools/ts-coverage.mjs`, which uses `c8` around the Mocha TypeScript tests.
- `npm run coverage`: runs the C++ gate and then the TypeScript gate.

Use `npm run coverage` before completing implementation plans. Use the language-specific commands when iterating on one side of the codebase.

The C++ coverage wrapper clears generated profile/report output on every run, but it reuses `artifacts/build/cpp-coverage` so Dawn and OFG object files stay incremental. Use `npm run coverage:cpp -- --fresh` only when you intentionally need a clean CMake configure/build.

## Output Locations

- `artifacts/coverage/cpp/cpp-summary.json`: generated C++ machine-readable summary.
- `artifacts/coverage/ts/coverage-summary.json`: generated TypeScript summary.
- `docs/coverage/`: committed latest coverage information for human review and task history.

`artifacts/coverage` is generated local output. Do not rely on it surviving clean builds. After a meaningful coverage run, refresh `docs/coverage` with the current summary and note the command/date in `docs/coverage/latest.md`.

## Interpreting Results

The wrapper scripts enforce per-file line coverage for checked source files. If a wrapper prints that coverage passed, the gate passed even if a global summary includes lower percentages from documented exceptions.

Current TypeScript exception:

- `src/app/main.ts`: browser entrypoint exercised by `npm run smoke:browser` rather than Node-based Mocha coverage.

Current C++ exception:

- `cpp/src/assets/`: glTF parsing/importing is fixture-matrix code with many malformed-format and unsupported-feature branches. It is covered by focused glTF/model/skinning/player asset tests, `npm run build:wasm`, `npm run smoke:browser`, and `npm run smoke:render`; it is not yet part of the per-file 90% native line gate.
- `cpp/src/game/game.cpp`: device-bound `Game` renderer ownership and command encoding. Native tests cover invalid setup before WebGPU calls, while full render behavior is covered by `npm run build:wasm`, `npm run smoke:browser:cpp`, and `npm run smoke:render` through the browser/native frame drivers that call `Game`.
- `cpp/src/web/`: browser-only Emscripten/Embind/WebGPU glue and frame-driver submission. It is covered by `npm run build:wasm`, TypeScript adapter tests, and `npm run smoke:browser` / `npm run smoke:browser:cpp` rather than native line coverage.
- `cpp/src/native/`: native Dawn smoke harness code. It is covered by `npm run smoke:render` because its value is the produced PNG/report and GPU readback behavior, not line-only unit coverage.
- `cpp/src/render/bloom_pass.cpp`, `cpp/src/render/scene_color_target.cpp`, `cpp/src/render/sky_pass.cpp`, and `cpp/src/render/tone_map_pass.cpp`: only narrow defensive WebGPU null-return, impossible tangent overflow, and partial-creation cleanup lines are excluded by `tools/cpp-coverage.mjs`. Normal resize, move, validation, pass creation, uniform packing, draw, render, diagnostics, bind-group reuse, and bloom pixel behavior remains in the per-file gate and is also exercised by browser/native smoke.

When adding an exception, document it in the relevant coverage script, active ExecPlan, and this file. Prefer adding targeted tests over adding exceptions.

## Refresh Workflow

1. Run `npm run coverage`.
2. Confirm both wrappers report pass/fail status clearly.
3. Copy or summarize the generated results into `docs/coverage`.
4. Update `docs/coverage/latest.md` with the date, command, pass/fail result, notable percentages, and exceptions.

The latest committed summaries should let a reviewer understand the most recent coverage state without rerunning the full suite.
