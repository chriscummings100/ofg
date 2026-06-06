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

The current seed implements chunk-streamed generated terrain with a Rust/WASM
runtime terrain path, Poly Haven terrain texture arrays, and Rust-owned
camera/player state. Rust/wgpu now owns browser WebGPU rendering and draw
submission through `engine_web`. The forward architecture is Rust-first: Rust
should own world state, simulation, terrain streaming, render extraction, asset
ownership, and WebGPU rendering, while TypeScript becomes browser shell, UI, and
only genuinely browser-specific utility code.

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
npm run smoke:terrain-seams
npm run smoke:terrain-presets
```

The dev server serves the built app at `http://127.0.0.1:5173`. `npm run build`
generates shader and Rust/WASM terrain artifacts before running TypeScript. Run it
after source changes, or keep `npm run watch` open in another terminal.
`npm run bench:terrain:wasm` measures release WASM density chunk generation and
chunk mesh generation, then writes a JSON report under
`artifacts/terrain-wasm-bench/`.

## Project Shape

- `src/engine`: deterministic engine modules that should stay easy to unit test.
- `src/app`: browser lifecycle, input, and game loop glue.
- `src/platform`: temporary local type shims for platform APIs.
- `src/generated`: deterministic generated TypeScript artifacts for shaders and
  WASM terrain metadata.
- `crates/terrain_core`: Rust terrain code built to WebAssembly for hot terrain
  generation paths.
- `crates/engine_web`: browser-facing Rust/WASM game and Rust/wgpu renderer
  bridge.
- `assets/wasm`: checked-in generated WebAssembly artifacts used by the browser.
- `docs`: active architecture, API contracts, terrain plan, and terrain
  research docs. Retired plans live under `docs/archived/`.
- `tools`: small repository scripts with no framework dependency.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md),
[docs/API_CONTRACTS.md](docs/API_CONTRACTS.md), and
[docs/TERRAIN_PLAN.md](docs/TERRAIN_PLAN.md) for the working direction. Agent
workflow and browser verification expectations live in [AGENTS.md](AGENTS.md).
