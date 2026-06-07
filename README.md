# Open Factor Game (OFG!)

Welcome to the Open Factory Game code base.

## What/why this game

For many years (a couple of decades in fact!) I've made games of various forms / sizes, and even had a stab at running my own studio. However making the exact game I wanted to make was often limited by a combination of time, money, pressure and what-people-actually-want! With the advent of some pretty powerful tools for writing code quick (if you know what code you want to write), I decided now might be the time for a renewed experiment - can I make the game I want, from scratch, for free?

I love factory games, and I love the modding community. So the open factory game is intended to be a game inspired by (and in some cases shamelessly being very inspired by) some of my favourites. Modding systems are hard though, so instead, I figured let's just make it open source, then people can add what they want.

The goal is stupidly high, because why not:

- Fully procedural voxel based world, complete with biomes / caves / vegetation etc
- Custom engine with all sorts of lovely proper effects
- Fully browser / wasm based
- Full factory / automation gameplay
- Online multiplayer, aimed at self hosted servers, not sure how MMO to go
- Probably other things I think of along the way

I'm also using it as a bit of an experiment in AI tools. Coding agents are pretty stunning these days. I don't believe they are at a 'prompt me a game' point (and if I'm honest I hope it never gets there!). But I am pretty convinced that if you know what you want to make, and have a solid idea of exactly how to make it, you can build it very quickly without actually typing a lot of code. So, I'm building a lot of it from my phone! Which in practice means a lot of careful architecutre, design docs, and tests!

## Credit

So far I've used creative commons assets from these brilliant sources:
- Polyhaven terrain textures (https://polyhaven.com/)
- Quaternius animations and characters (https://quaternius.com/)

## Contributions

The project's probably not at an architectural level at which it's easy to contribute just yet, but contributions are always welcome. I'd start by leaving a github issue with a proposal of something to add. Please just be aware that this is, and will stay, a fully open source project, so code that gets added to it can't then be taken and placed in a commercial project (see the LICENSE for details).

## First Playable Target

At current, I am laying the foundations for terrain generation and basic asset loading, with a player that can run around on a procedural world. Once that's in to a semi nice degree I'll get some tools for tuning the terrain and setting up actual interesting biomes etc.

The WIP is hosted here: https://ofg.chriscummings1024.workers.dev/

## Commands

```powershell
npm run build
npm run build:shaders
npm run check:shaders
npm run build:wasm
npm run check:wasm
npm run bench:terrain:rust
npm run coverage:rust
npm run test:rust
npm run test:ts
npm test
npm run dev
npm run smoke:rust
npm run smoke:browser
npm run smoke
npm run smoke:terrain-seams
npm run smoke:terrain-presets
```

The dev server serves the built app at `http://127.0.0.1:5173`. `npm run build`
generates shader and Rust/WASM artifacts before running the TypeScript app build.
Run it after source changes, or keep `npm run watch` open in another terminal.
`npm test` runs Rust workspace tests and the separated TypeScript test lane.
`npm run smoke:rust` renders Rust-owned terrain image smoke through native
`wgpu` and writes PNG/report artifacts under `artifacts/rust-smoke/`.
`npm run smoke:browser` is the narrow browser integration smoke for wasm loading,
WebGPU canvas setup, browser assets, HUD, reload, and keyboard input forwarding.
`npm run bench:terrain:rust` measures Rust density chunk generation, retained
density-window preparation, and chunk mesh generation, then writes a JSON report
under `artifacts/terrain-bench/`.
`npm run coverage:rust` runs Rust workspace coverage through `cargo-llvm-cov`
and writes reports under `artifacts/coverage/rust/`. By default, terminal output
and `summary.json` / `summary.pretty.json` show only implementation files below
90% line coverage, excluding tests, the smoke/benchmark harness, and Rust export
glue such as `lib.rs` and `facade.rs`. Use `npm run coverage:rust -- --full`
for the full cargo summary. If the coverage tool is not installed, it prints
setup guidance and exits before touching build output.
