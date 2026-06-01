# OFG

OFG is an online factory game prototype for the browser. The long-term target is a
lightweight, from-scratch engine using voxel terrain, Dual Contouring, WebGPU, and a
Rust/TypeScript toolchain that is easy for AI agents to extend safely.

## First Playable Target

The first playable milestone is intentionally small:

- A browser-native WebGPU scene.
- A simple generated terrain surface.
- A visible character marker grounded on that terrain.
- First-person character movement.
- A debug fly camera that can be toggled at runtime.
- Deterministic engine code with tests.

The current seed implements chunk-streamed generated terrain with per-chunk
neighbor-aware Dual Contouring, Poly Haven terrain texture arrays, a first
Rust/WASM terrain core artifact, and the camera/player loop in TypeScript.

## Commands

```powershell
npm run build
npm run build:shaders
npm run check:shaders
npm run build:wasm
npm run check:wasm
npm run bench:terrain:wasm
npm test
npm run dev
npm run smoke:browser
```

The dev server serves the built app at `http://127.0.0.1:5173`. `npm run build`
generates shader and Rust/WASM terrain artifacts before running TypeScript. Run it
after source changes, or keep `npm run watch` open in another terminal.
`npm run bench:terrain:wasm` measures release WASM density chunk generation and
writes a JSON report under `artifacts/terrain-wasm-bench/`.

## Project Shape

- `src/engine`: deterministic engine modules that should stay easy to unit test.
- `src/app`: browser lifecycle, input, and game loop glue.
- `src/platform`: temporary local type shims for platform APIs.
- `src/generated`: deterministic generated TypeScript artifacts for shaders and
  WASM terrain metadata.
- `crates/terrain_core`: Rust terrain code built to WebAssembly for hot terrain
  generation paths.
- `assets/wasm`: checked-in generated WebAssembly artifacts used by the browser.
- `docs`: roadmap, architecture notes, and AI workflow guidance.
- `tools`: small repository scripts with no framework dependency.

See [docs/ROADMAP.md](docs/ROADMAP.md), [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md),
and [docs/AI_WORKFLOW.md](docs/AI_WORKFLOW.md) for the working direction. Browser
verification is documented in [docs/BROWSER_VERIFICATION.md](docs/BROWSER_VERIFICATION.md).
