# Latest Coverage

Date: 2026-06-20

Command: `npm run coverage`

Result: passed. The C++ coverage wrapper reported that checked C++ core/runtime/render scene files met the 90% per-file line coverage gate, and the TypeScript wrapper reported that checked TypeScript files met the same threshold.

Generated local artifacts:

- `artifacts/coverage/cpp/cpp-summary.json`
- `artifacts/coverage/ts/coverage-summary.json`

Committed summary copies:

- `docs/coverage/cpp-summary.json`
- `docs/coverage/ts-coverage-summary.json`

## C++ Line Coverage

| File | Lines |
| --- | ---: |
| `cpp/src/core/frame_state.cpp` | 100.00% (10/10) |
| `cpp/src/render/bootstrap_scene.cpp` | 100.00% (3/3) |
| `cpp/src/runtime/browser_runtime.cpp` | 100.00% (136/136) |
| `cpp/src/runtime/runtime_debug_status.cpp` | 100.00% (71/71) |

C++ exceptions:

- `cpp/src/web/` and `cpp/src/render/bootstrap_renderer.cpp`: browser-only Emscripten/Embind/WebGPU glue and draw submission; covered by `npm run build:wasm`, TypeScript adapter tests, and browser smoke rather than native line coverage.
- `cpp/src/native/`: native Dawn smoke harness; covered by `npm run smoke:render` because the validation value is GPU readback plus PNG/report output.

## TypeScript Line Coverage

| File | Lines |
| --- | ---: |
| `total` | 82.76% (413/499) |
| `src/app/canvasHost.ts` | 97.51% (196/201) |
| `src/app/main.ts` | 0.00% (0/71), documented exception |
| `src/app/wasmRuntime.ts` | 95.59% (217/227) |

TypeScript exception:

- `src/app/main.ts`: browser entrypoint exercised by `npm run smoke:browser` rather than Node-based Mocha coverage.
