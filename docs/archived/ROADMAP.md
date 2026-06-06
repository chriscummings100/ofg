# Roadmap

## North Star

Build a browser-native online factory game in a voxel terrain world. The engine
should stay deliberately lightweight, from scratch where it matters, and highly
automatable so AI agents can make changes with confidence.

## Milestone 0: Engine Seed

- TypeScript app shell served locally without a bundler.
- WebGPU renderer that draws deterministic generated terrain.
- Character movement and a debug fly camera.
- Unit tests for math, terrain generation, and camera/player behavior.
- Clear docs for architecture, workflow, and future milestones.

## Milestone 1: First Playable Terrain Walk

- Grounded player collision against terrain.
- Pointer-lock first-person controls.
- Debug fly camera with a visible player marker.
- Stable frame timing and resize handling.
- Browser smoke test that checks the canvas renders non-empty frames.

## Milestone 2: Voxel Terrain Core

- Chunk coordinate system and deterministic chunk keys.
- Signed density field abstraction.
- CPU Dual Contouring prototype with focused tests.
- Chunk mesh generation boundaries ready for Rust/WASM migration.
- Golden mesh fixtures for simple fields such as plane, sphere, and saddle.

## Milestone 3: WebGPU Terrain Pipeline

- Chunk mesh upload and reuse.
- GPU-side material IDs and simple lighting.
- Frustum-aware chunk visibility.
- Plain WGSL shader artifacts with tests for renderer contracts.

## Milestone 4: Factory Toy Loop

- Placeable machines snapped to terrain or voxel grid.
- Basic belts or item links.
- Deterministic simulation tick.
- Save/load for a small world.

## Milestone 5: Online Foundations

- Client/server state boundary.
- Deterministic simulation commands.
- Small multiplayer session with reconciliation strategy chosen from measured
  prototype behavior.

## Non-Goals For Now

- No general-purpose engine framework.
- No large renderer abstraction before there are multiple real rendering paths.
- No full physics engine before first-person terrain movement exposes actual needs.
- No Rust/WASM rewrite until TypeScript prototypes define stable data contracts.
