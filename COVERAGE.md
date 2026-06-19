# Coverage

Coverage is a testing gate for the OFG bootstrap, not just a report. The wrappers fail when checked implementation files fall below the documented line threshold, currently 90%.

## Commands

Run from `C:\dev\ofg`.

- `npm run coverage:rust`: runs `tools/rust-coverage.mjs`, which uses `cargo-llvm-cov` for Rust unit tests and the native render-smoke binary.
- `npm run coverage:ts`: runs `tools/ts-coverage.mjs`, which uses `c8` around the Mocha TypeScript tests.
- `npm run coverage`: runs the Rust gate and then the TypeScript gate.

Use `npm run coverage` before completing implementation plans. Use the language-specific commands when iterating on one side of the codebase.

## Output Locations

- `artifacts/coverage/rust/summary.json`: generated Rust machine-readable summary.
- `artifacts/coverage/rust/summary.pretty.json`: generated Rust pretty-printed summary.
- `artifacts/coverage/ts/coverage-summary.json`: generated TypeScript summary.
- `docs/coverage/`: committed latest coverage information for human review and task history.

`artifacts/coverage` is generated local output. Do not rely on it surviving clean builds. After a meaningful coverage run, refresh `docs/coverage` with the current summary and note the command/date in `docs/coverage/latest.md`.

## Interpreting Results

The wrapper scripts enforce per-file line coverage for checked source files. If a wrapper prints that coverage passed, the gate passed even if a global summary includes lower percentages from documented exceptions.

Current Rust exceptions:

- `crates/ofg_web/src/browser.rs`: browser-only WASM/WebGPU facade. It is covered by `npm run test:wasm` and `npm run smoke:browser` rather than native `cargo-llvm-cov`.
- `crates/ofg_test_harness/src/bin/ofg-render-frame.rs`: exercised by the instrumented native smoke path; remaining uncovered lines are mostly failure handling and environment error cases.

Current TypeScript exception:

- `src/app/main.ts`: browser entrypoint exercised by `npm run smoke:browser` rather than Node-based Mocha coverage.

When adding an exception, document it in the relevant coverage script, active ExecPlan, and this file. Prefer adding targeted tests over adding exceptions.

## Refresh Workflow

1. Run `npm run coverage`.
2. Confirm both wrappers report pass/fail status clearly.
3. Copy or summarize the generated results into `docs/coverage`.
4. Update `docs/coverage/latest.md` with the date, command, pass/fail result, notable percentages, and exceptions.

The latest committed summaries should let a reviewer understand the most recent coverage state without rerunning the full suite.
