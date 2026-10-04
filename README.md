# Online Factory Game

OFG starts as a terrain laboratory for large-scale terrain generation, hydrology and streaming. The longer-term goal is an open-world multiplayer factory game.

The application uses C++ and Slang RHI, with a native Windows D3D12 build for daily development and an Emscripten/WebGPU build for browsers. Dear ImGui is planned for the laboratory UI. A walking character and follow camera will provide an early way to inspect terrain at human scale.

The default application renders a PBR material sphere grid with a debug fly camera on native D3D12 and browser WebGPU through the pinned Slang RHI submodule. Core metallic/roughness shading, punctual lights, baked image-based lighting, HDR output and layered surface swatches are implemented. The textured cube scene remains selectable with `--scene`. Textures support on-demand PNG/JPEG loading, UNORM8/sRGB8/fp16/fp32 storage, shared samplers, mip views and GPU mip generation. The original checkerboard remains selectable. CTest verifies CPU resource/scene contracts, device creation, rendering and texture numerics; the separate browser target runs the same shared renderer. Playwright captures and checks browser screenshots without running the full native suite. ImGui and terrain are still to come. Python/SlangPy may support offline experiments and baking. SGL is an implementation reference, not a runtime dependency.

- [Restart findings and terrain direction](docs/research/terrain-first-restart.md)
- [Repository guidance](AGENTS.md)
- [Current renderer architecture](docs/architecture.md)
- [Native and browser build instructions](DEVELOPING.md)
- [Terrain laboratory bootstrap plan](docs/plans/terrain-lab-bootstrap.md)
- [ExecPlan template and conventions](PLANS.md)
- [PBR sphere grid, controls and material contracts](docs/pbr.md)
