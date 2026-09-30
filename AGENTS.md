# OFG Terrain Laboratory

## Current direction

Build a terrain laboratory first: understand generation, large-world coordinates, hydrology, detail levels, continuity and streaming before building the factory game. A walkable character and follow camera are early terrain inspection tools. The longer-term application remains an open-world factory game.

The selected graphics foundation is C++ and Slang RHI. Native Windows D3D12 is the main development target; browser WebGPU through Emscripten/WebAssembly is a required second target. The browser API is WebGPU, not WebGL. Both editions execute locally on the user's device; web hosting distributes the browser build and terrain data.

Use Dear ImGui for laboratory UI. Take focused inspiration from SGL in SlangPy, especially its ImGui integration, without importing SGL, SlangPy or Falcor2 into the application runtime. Python/SlangPy remains an optional offline research/baking tool, not a prerequisite for building the native application.

## Simplicity and maintainability come first

Write code for a human to read, reason about and change. Prefer the smallest clear implementation that meets the current requirement. Correctness, explicit ownership and understandable control flow matter more than cleverness or saving a few lines.

- Solve the problem in front of us. Do not add extension points, configurable policies, speculative fallbacks or generic frameworks for imagined future requirements.
- Start with concrete types, ordinary functions and straightforward data structures. Introduce an interface, template, inheritance hierarchy, factory or registry only when a demonstrated requirement makes the code simpler overall. Explain that requirement when the benefit is not obvious.
- Use Slang RHI directly where it already expresses the operation clearly. A wrapper must remove repeated complexity or enforce a real lifetime/behavior contract; renaming or forwarding every RHI call is not enough.
- Keep responsibilities and ownership easy to trace. Avoid hidden global state, surprising side effects and indirection that forces the reader through several layers to understand a simple operation.
- Prefer cohesive helpers over either giant functions or a maze of trivial forwarding functions. A little local repetition is preferable to an abstraction that couples unrelated behavior.
- Validate meaningful boundaries and report useful errors. Avoid repeated defensive checks for impossible internal states, silently swallowed failures, and fallback behavior that conceals broken assumptions. Use assertions for programmer invariants where appropriate.
- Optimize measured bottlenecks. Keep necessary GPU and terrain optimizations explicit, document their tradeoffs, and preserve a way to check correctness. Do not add complexity on an unmeasured performance hunch.
- Remove obsolete code, unused options and misleading comments when replacing an approach. Keep changes focused on the task rather than bundling unrelated redesigns.

## Coding conventions

Follow Slang RHI's formatting and prevailing C++ naming conventions. The local [.clang-format](.clang-format) and [.editorconfig](.editorconfig) are copied unchanged from Slang RHI revision `16324a68af477baaede620e713644f5e9613b1a2`. Formatting configuration is authoritative; do not recreate its rules by hand or reformat vendored dependencies.

The inspected upstream [formatting configuration](https://github.com/shader-slang/slang-rhi/blob/16324a68af477baaede620e713644f5e9613b1a2/.clang-format), [editor configuration](https://github.com/shader-slang/slang-rhi/blob/16324a68af477baaede620e713644f5e9613b1a2/.editorconfig) and [representative C++ source](https://github.com/shader-slang/slang-rhi/blob/16324a68af477baaede620e713644f5e9613b1a2/src/device.h) establish the baseline. These sources describe formatting and demonstrate naming; the stronger readability, documentation and simplicity requirements here are OFG's own rules requested by the user.

- Four spaces for C++ indentation, a 120-column limit, and braces on their own line for functions, types and control blocks. Namespace braces follow the checked-in formatter.
- Use `UpperCamelCase` for classes, structs and enum types; `lowerCamelCase` for functions, methods, parameters and local variables. Use `m_memberName` for class instance state and ordinary `lowerCamelCase` for plain data-struct fields. Follow the surrounding upstream convention when implementing an external interface.
- Use descriptive names that convey domain meaning and, where useful, units. Avoid cryptic abbreviations, vague utility buckets and inconsistent names for the same concept.
- Use `.h` and `.cpp` for C++, `.slang` for shaders, and descriptive hyphen-separated filenames such as `terrain-cache.cpp`. Keep headers self-contained and expose only the interfaces that callers need.
- Follow the formatter's left-aligned pointer/reference style, such as `Device* device`. Keep includes deliberately grouped; the upstream configuration disables automatic include sorting.
- Prefer explicit, readable control flow and appropriate standard-library facilities over clever expressions, macro machinery or unnecessary metaprogramming. Use RAII and existing owning handles to make cleanup reliable; distinguish ownership from observation.

Keep the formatter version and verified formatting commands in the development instructions once tooling is installed. Do not invent a passing formatter check. Treat an upstream style change as a deliberate update rather than fetching moving defaults during each build.

## Documentation, function comments and layout

Every project source/header and shader file should begin with a short description of its purpose and responsibilities. Keep this useful and brief; explain any important ownership or integration boundary rather than repeating the filename.

Document every project-defined function or method with a concise purpose comment at its declaration or definition. Put the caller-facing contract in the header when there is one; do not duplicate the same comment in both places. Describe significant units, coordinate spaces, ownership, preconditions, side effects, failure behavior or synchronization requirements where they matter. Even a short function should have an accurate purpose comment, but it does not need a boilerplate parameter list that repeats its signature.

Use comments to explain intent, constraints and non-obvious choices. Explain numerical formulas, border/LOD rules, GPU lifetime assumptions and unusual platform workarounds. Do not narrate obvious assignments line by line. Update or remove comments when behavior changes.

Break longer functions into visible logical phases with blank lines. Add short phase comments where they help someone scan the algorithm. Separate setup, validation, processing and publication/cleanup when those are distinct operations. Use early returns when they reduce nesting and make the successful path clearer.

If a function mixes responsibilities or becomes difficult to follow, extract cohesive, well-named helpers. If a file mixes unrelated concepts, split by responsibility. Do not fragment understandable code just to satisfy an arbitrary line-count target; readability is the deciding criterion.

Keep project documentation alongside implementation: update build/run instructions, changed contracts and the active plan in the same work. Record why a consequential decision was made and how to verify it. Clearly separate working behavior, proposals and known limitations.

## Repository and plans

Follow [PLANS.md](PLANS.md) for substantial work. The current bootstrap plan is [docs/plans/terrain-lab-bootstrap.md](docs/plans/terrain-lab-bootstrap.md). Keep active plans in `docs/plans` and completed plans in `docs/archived`. Re-read the active plan after context compaction; keep its living sections and validation evidence current.

`C:\dev\ofg-old2` is a historical reference. Preserve it. Reuse useful concepts and assets with provenance, not its build system, global resource registry, or obsolete contracts wholesale.

Project skills belong in `.agents/skills`. Use [build-native](.agents/skills/build-native/SKILL.md) for the Windows x64 build environment and native build/startup checks. Keep a small set of workflows grounded in real repository commands; do not copy stale skills or invent successful checks. Available skills and future candidates are described in [.agents/skills/README.md](.agents/skills/README.md).

## Architecture and dependencies

Use a pinned Git submodule for Slang RHI under `external/slang-rhi`, integrated with CMake. Use CMake presets and Ninja for normal builds. Native and browser builds must have separate binary directories and toolchains; native D3D12 must not require Emscripten or a native Dawn build. Enable only backends and optional dependencies needed by the selected preset. Prefer the compatible prebuilt Slang/compiler packages provided by the pinned upstream integration when available.

Keep the platform hosts small. They own windows/canvas, events, asynchronous asset loading and presentation. Shared C++ owns the laboratory behavior; Slang owns GPU kernels and shaders. Keep CPU terrain data and algorithms independently buildable and testable without graphics dependencies.

Use RHI interfaces and existing helpers directly where practical. Add small helpers for application lifetime, frame submission, shader loading, uploads and ImGui only when needed. Do not mirror every RHI resource type, introduce another virtual graphics API, or build a general engine before terrain requires it.

Use explicit ownership and resource retirement. Unloading a tile must eventually release its CPU/GPU residency, including allocations referenced by in-flight commands. Global coordinate addressing must preserve precision; use camera-relative floats for rendering. Keep request identity and cancellation explicit.

## Browser portability

The current bootstrap step is native Windows D3D12 only; the user explicitly deferred browser setup for now. Browser support remains a project goal. Once that path is introduced, prove native and browser rendering, compute and ImGui early. Native development is the normal iteration loop. Re-run browser checks at milestone boundaries and whenever shared shaders, resource bindings, upload/readback, device lifecycle, UI/input, platform abstractions or dependencies change. Pure CPU changes need focused tests, not an automatic browser rebuild for every edit.

Design shared shaders against a documented, queried WebGPU capability baseline. Do not rely on native-only features for required behavior. Staying within GPU features is necessary but insufficient: the web path also needs asynchronous initialization/loading, a callback-driven frame loop, portable shader layouts and deliberate memory budgets. Treat unsupported backend operations as explicit failures or tested fallbacks.

## Tests and validation

Use doctest for C++ tests and register them with CTest. Separate GPU-independent tests, native GPU integration tests and browser smoke tests so a CPU test run does not initialize a device or pull the full graphics dependency graph. Test contracts and failure modes; do not require identical pixels or floating-point erosion across different GPU backends.

Tests are part of implementing behavior, not a cleanup task for later. Add or update meaningful tests with each behavior change. When fixing a bug, add a focused regression test that would fail without the fix where practical. Documentation-only changes need documentation checks rather than artificial unit tests.

Test observable outcomes and invariants rather than private implementation structure. Include relevant boundaries and failures: negative terrain addresses, shared edges and corners, request reordering/cancellation, resource retirement and numerical tolerances. Use deterministic seeds, small fixtures and descriptive test names so failures are reproducible and understandable. Avoid timing-dependent sleeps, tests that merely mirror the implementation, and elaborate mocks when a pure function or small real component can establish the behavior.

Use integration tests for shader bindings, uploads, compute results, rendering and platform behavior. CPU tests cannot prove that a shader compiles or that a browser renders correctly. State numerical tolerances explicitly and explain their scale. Never weaken an assertion merely to make a failure disappear without understanding the cause.

Run the checks appropriate to the change and record their results. Report a skipped, unavailable or failing check honestly; a successful build alone is not proof of correct behavior. Coverage helps reveal missing cases but does not replace assertions, failure-path testing or visual inspection.

Record the actual build/test/run commands in [DEVELOPING.md](DEVELOPING.md) as they are implemented. The native console application and its GPU-dependent CTest startup check are implemented; CPU/doctest targets and rendering are still to come. Do not claim a command, preset or executable works until it has been run. Establish a practical coverage policy as application logic is introduced; the initial device-startup glue is verified by the integration check, and historical coverage commands and thresholds are not inherited automatically.

For visual work, inspect and present native/browser screenshots and relevant diagnostics, saving durable evidence under `artifacts`. For performance claims, name hardware, backend, viewport, workload and measurement method. Track live allocated bytes and resources, including pending retirement; cumulative allocation counts are not residency.

Preserve upstream license notices and record origin/revision when adapting code. Before completing a milestone, review correctness, ownership, API boundaries, unnecessary abstractions, readability, comments, stale scaffolding, documentation and validation. Simplify any code whose complexity is not justified by the behavior it delivers.
