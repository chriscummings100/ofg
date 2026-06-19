# Online Factory Game

OFG is a browser-native online factory game restarted from a small Rust/WASM and TypeScript foundation. The first objective is a reliable bootstrap: TypeScript hosts a canvas, Rust/WASM owns the first WebGPU renderer and game-facing runtime state, tests prove the public seams, and deployment packages a static Cloudflare Pages app.

## Commands

Use Node.js 20 or newer locally; Cloudflare Pages is pinned with `.node-version`. Rust is pinned by `rust-toolchain.toml` to `1.96.0` with the `wasm32-unknown-unknown` target. The local `wasm-bindgen` CLI must match `tools/wasm-bindgen-version.mjs`, currently `0.2.125`.

```powershell
npm install
npm run build:wasm
npm run build
npm run test:rust
npm run test:wasm
npm run test:ts
npm test
npm run smoke:browser
npm run smoke:render
npm run smoke
npm run coverage:rust
npm run coverage:ts
npm run coverage
npm run package:site
npm run build:cloudflare
npm run dev
```

If `npm run build:wasm` reports a generator mismatch, install the matching CLI:

```powershell
cargo install wasm-bindgen-cli --version 0.2.125 --locked --force
```

`npm run dev` builds the TypeScript app and serves it at `http://127.0.0.1:5173`, or the next available port if that port is busy. The page loads the Rust/WASM runtime and renders a WebGPU bootstrap frame into a full-window canvas.

## Current Architecture

TypeScript owns only browser hosting: DOM boot, canvas sizing, fatal-error display, dev-server ergonomics, WASM loading, and smoke-test browser control. Rust owns bootstrap frame state, render scene construction, WebGPU resources, draw submission, browser WASM facade behavior, and native offscreen rendering.

The completed bootstrap implementation plan is archived at `docs/archived/initial-bootstrap-plan.md`.

## Cloudflare Pages

Use Cloudflare Pages for this bootstrap deploy:

- Root directory: `/`
- Build command: `npm run build:cloudflare`
- Build output directory: `.deploy`
- Node version: `.node-version`

`npm run package:site` rebuilds and recreates `.deploy/` with only runtime files, then writes `_headers` with the cross-origin isolation headers required by WebGPU. The previous OFG attempt used a Workers/static-assets route with `wrangler deploy`; that is historical only here and should not be used until a future plan adds a real `wrangler.jsonc` and Workers validation.
