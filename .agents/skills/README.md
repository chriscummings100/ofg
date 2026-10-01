# Project skills

Repository-specific skills live here. Keep ordinary project rules in `AGENTS.md` rather than repeating them in every skill.

The [build-native skill](build-native/SKILL.md) covers the verified Windows x64 environment, CMake/Ninja application build and D3D12 startup check. Invoke it as `$build-native`, or use it for native build/startup work.

The [build-web skill](build-web/SKILL.md) covers the separate Emscripten/WebGPU build and Playwright screenshot/diagnostic workflow. Invoke it as `$build-web`, or use it for browser build and smoke-check work. The [architecture note](../../docs/architecture.md) maps both hosts to the shared checkerboard renderer.

Additional useful skills to create as their workflows become available are:

| Candidate | Purpose | When to create it |
| --- | --- | --- |
| `review-plan` | Check a substantial ExecPlan for missing contracts, unnecessary scope, platform assumptions, achievable milestones and observable acceptance. | During bootstrap planning; keep it independent of historical OFG APIs and languages. |
| `milestone-review` | Review the actual diff for correctness, ownership, resource lifetime, browser compatibility, documentation and validation evidence. | Once the first source milestone and build/test commands exist. |
| `graphics-smoke` | Run the native and browser fixture, collect device/validation diagnostics, save screenshots and inspect the outcome. | Once both launch paths and automation are verified. |

Keep each skill narrow. Reuse documented commands rather than embedding another build system. Browser smoke must distinguish a failed launch or unsupported device from a passing render. A later terrain-validation skill can add mixed-resolution seams, request reordering, coordinate precision and residency scenarios once those contracts exist.
