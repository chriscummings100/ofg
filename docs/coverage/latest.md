# Latest Coverage

Date: 2026-06-19

Command: `npm run coverage`

Result: passed. Rust and TypeScript coverage wrappers both reported that checked files met the 90% per-file line coverage gate.

Generated local artifacts:

- `artifacts/coverage/rust/summary.json`
- `artifacts/coverage/rust/summary.pretty.json`
- `artifacts/coverage/ts/coverage-summary.json`

Committed summary copies:

- `docs/coverage/rust-summary.pretty.json`
- `docs/coverage/ts-coverage-summary.json`

## Rust Line Coverage

| File | Lines |
| --- | ---: |
| `crates/ofg_core/src/lib.rs` | 100.00% (32/32) |
| `crates/ofg_render/src/bootstrap_scene.rs` | 100.00% (23/23) |
| `crates/ofg_render/src/renderer.rs` | 96.05% (73/76) |
| `crates/ofg_test_harness/src/bin/ofg-render-frame.rs` | 87.81% (281/320), documented exception |
| `crates/ofg_web/src/lib.rs` | 100.00% (8/8) |
| `crates/ofg_web/src/status.rs` | 100.00% (19/19) |

Rust exceptions:

- `crates/ofg_test_harness/src/bin/ofg-render-frame.rs`: exercised by instrumented native smoke; remaining uncovered lines are failure handling.
- `crates/ofg_web/src/browser.rs`: omitted from native Rust coverage because it is browser-only WASM/WebGPU code; covered by `npm run test:wasm` and `npm run smoke:browser`.

## TypeScript Line Coverage

| File | Lines |
| --- | ---: |
| `total` | 82.55% (369/447) |
| `src/app/canvasHost.ts` | 97.51% (196/201) |
| `src/app/main.ts` | 0.00% (0/66), documented exception |
| `src/app/wasmRuntime.ts` | 96.11% (173/180) |

TypeScript exception:

- `src/app/main.ts`: browser entrypoint exercised by `npm run smoke:browser` rather than Node-based Mocha coverage.
