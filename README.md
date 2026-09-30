# Online Factory Game

OFG starts as a terrain laboratory for large-scale terrain generation, hydrology and streaming. The longer-term goal is an open-world multiplayer factory game.

The application uses C++ and Slang RHI, with a native Windows D3D12 build for daily development and an Emscripten/WebGPU build for browsers. Dear ImGui provides the laboratory UI. A walking character and follow camera will provide an early way to inspect terrain at human scale.

The repository currently builds a native console application that creates a D3D12 device through the pinned Slang RHI submodule, reports its adapter and exits. A CTest startup check verifies this path. Windowing, rendering and browser configuration are still to come. Python/SlangPy may support offline experiments and baking. SGL is an implementation reference, not a runtime dependency.

- [Restart findings and terrain direction](docs/research/terrain-first-restart.md)
- [Repository guidance](AGENTS.md)
- [Native build instructions](DEVELOPING.md)
- [Terrain laboratory bootstrap plan](docs/plans/terrain-lab-bootstrap.md)
- [ExecPlan template and conventions](PLANS.md)
