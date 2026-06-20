# Online Factory Game

OFG is a browser-native online factory game. The current bootstrap is C++/WASM plus a TypeScript browser host: TypeScript owns the canvas and module loading, while C++ owns frame state, WebGPU resources, draw submission, browser runtime behavior, and native Dawn render smoke.

## Commands

Use Node.js 20 or newer locally; Cloudflare Pages is pinned with `.node-version`. C++ builds use pinned Emscripten, LLVM/Clang, Ninja, and Dawn toolchain sources managed by npm setup scripts.

```powershell
npm install
npm run setup:emscripten
npm run setup:llvm
npm run setup:ninja
npm run setup:dawn
npm run build:wasm
npm run build
npm run test:cpp
npm run test:ts
npm test
npm run smoke:browser
npm run smoke:render
npm run smoke
npm run coverage:cpp
npm run coverage:ts
npm run coverage
npm run package:site
npm run build:cloudflare
npm run dev
```

`npm run dev` builds the TypeScript app and C++/WASM runtime, then serves it at `http://127.0.0.1:5173`, or the next available port if that port is busy. The page loads the C++/WASM runtime and renders a WebGPU bootstrap frame into a full-window canvas.

## Current Architecture

TypeScript owns only browser hosting: DOM boot, canvas sizing, fatal-error display, dev-server ergonomics, WASM loading, and smoke-test browser control. C++ owns bootstrap frame state, render scene construction, WebGPU resources, draw submission, browser WASM facade behavior, and native offscreen rendering.

The completed bootstrap implementation plan is archived at `docs/archived/initial-bootstrap-plan.md`. The completed Rust-to-C++ migration is archived at `docs/archived/cpp-wasm-migration-plan.md`. The active renderer/resource follow-up plan is `docs/plans/cpp-renderer-resources-pipeline-plan.md`.

## Cloudflare Pages

Use Cloudflare Pages for this bootstrap deploy:

- Root directory: `/`
- Build command: `npm run build:cloudflare`
- Build output directory: `.deploy`
- Node version: `.node-version`

`npm run package:site` rebuilds and recreates `.deploy/` with only runtime files, then writes `_headers` with the cross-origin isolation headers required by WebGPU. The previous OFG attempt used a Workers/static-assets route with `wrangler deploy`; that is historical only here and should not be used until a future plan adds a real `wrangler.jsonc` and Workers validation.
