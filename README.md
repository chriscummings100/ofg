# Online Factory Game

OFG starts as a terrain laboratory for large-scale terrain generation, hydrology and streaming. The longer-term goal is an open-world multiplayer factory game.

The application uses C++ and Slang RHI, with a native Windows D3D12 build for daily development and an Emscripten/WebGPU build for browsers. Dear ImGui provides the laboratory UI. A walking character and follow camera will provide an early way to inspect terrain at human scale.

The repository currently builds a native D3D12 checkerboard window through the pinned Slang RHI submodule. CTest verifies device creation and exact rendered pixels; the separate browser target renders the same shared Slang shader through WebGPU. Playwright captures and checks browser screenshots without running the full native suite. ImGui and terrain are still to come. Python/SlangPy may support offline experiments and baking. SGL is an implementation reference, not a runtime dependency.

- [Restart findings and terrain direction](docs/research/terrain-first-restart.md)
- [Repository guidance](AGENTS.md)
- [Current architecture and checkerboard example](docs/architecture.md)
- [Native and browser build instructions](DEVELOPING.md)
- [Terrain laboratory bootstrap plan](docs/plans/terrain-lab-bootstrap.md)
- [ExecPlan template and conventions](PLANS.md)
