---
name: milestone-review
description: Review an OFG ExecPlan milestone before marking it complete, using sub-agents when available to catch API contract drift, poor coding practice, stale docs, legacy leftovers, missing tests, validation gaps, oversized files, and unclear ownership. Use after each ExecPlan milestone, before completion, when substantial code lands, when active docs/contracts change, or when the user asks for milestone/quality/drift review.
---

# Milestone Review

Use this skill as the final gate for an OFG milestone. It is not only an API
drift review. It is a quality review that checks whether the milestone left the
codebase cleaner, coherent, tested, and aligned with the current contracts.

## Inputs To Gather

Read these first:

- `AGENTS.md`
- `PLANS.md`
- `docs/API_CONTRACTS.md`
- `docs/ARCHITECTURE.md`
- The active ExecPlan being implemented
- Any touched source files and nearest tests

Identify the milestone scope:

- Milestone name and acceptance criteria.
- Changed files and diff.
- Commands already run.
- Generated artifacts touched or expected.
- Screenshots, reports, or benchmark artifacts referenced by the plan.

Use `git -c safe.directory=C:/dev/ofg diff --stat` and
`git -c safe.directory=C:/dev/ofg diff -- <paths>` when a local diff is the
review target. Use a PR/branch/explicit file scope if the user provided one.

## Reviewers

When sub-agent tools are available and the user has asked for milestone review,
spawn read-only reviewers in parallel. Give each reviewer the same scope,
contract docs, and validation evidence. Do not let reviewers edit files.

Use these roles:

- Contract reviewer: compare the change against `docs/API_CONTRACTS.md`, public
  TypeScript/Rust/WASM surfaces, packet shapes, fixture-only APIs, and forbidden
  ownership.
- Code-quality reviewer: look for poor practice, oversized files, missing
  purpose headers, unclear ownership, unnecessary abstractions, duplicated logic,
  unexplained `any`, `as unknown as`, `#[allow(...)]`, `#[no_mangle]`, or
  `static mut`.
- Legacy reviewer: search for retired systems, stale comments, dead wrappers,
  runtime-looking test fixtures, and old TypeScript terrain/render/worker
  ownership.
- Correctness reviewer: look for concrete bugs, lifecycle mistakes, stale state,
  race/generation mistakes, invalid assumptions, edge cases, and user-visible
  regressions.
- Validation reviewer: check test coverage, required commands, generated
  artifact freshness, screenshot/report inspection, performance evidence, and
  ExecPlan living-section updates.

If sub-agent tools are unavailable, do these five passes locally and say that no
sub-agents were available.

## Reviewer Prompt Template

Use a concise prompt like this for each reviewer:

    Review this OFG milestone read-only.

    Scope:
    <milestone, changed files, diff, commands, artifacts>

    Required context:
    - AGENTS.md
    - PLANS.md
    - docs/API_CONTRACTS.md
    - docs/ARCHITECTURE.md
    - <active ExecPlan>

    Role:
    <contract/code-quality/legacy/correctness/validation reviewer>

    Rules:
    - Read only. Do not edit files.
    - Return only concrete, actionable findings.
    - Include file paths and line/function names where possible.
    - Explain impact and suggested fix.
    - Say "No findings" if there are no actionable issues.

## What To Check

API and ownership:

- Supported browser runtime stays behind `RustBrowserGame` and the TypeScript
  wrapper in `src/engine/web`.
- Playable TypeScript does not call raw `ofg_*` wasm exports.
- Frame input, commands, debug snapshots, asset-loader packets, terrain vertex
  layout, and preset codes match `docs/API_CONTRACTS.md`.
- Fixture-only `terrain_core.wasm` adapters stay out of runtime app code.
- TypeScript does not regain forbidden ownership: scene/ECS, terrain
  generation, terrain stream scheduling, terrain worker protocols, mesh upload,
  texture semantics, WebGPU rendering, or world simulation.

Code quality:

- Non-generated files over 600 lines are flagged for split pressure; files over
  1000 lines need a split plan before further growth.
- Boundary, terrain, renderer, and tool files have top-of-file purpose comments.
- Complex public functions have useful comments or clear structure.
- New abstractions remove real duplication or match existing patterns.
- New `any`, `as unknown as`, `#[allow(...)]`, `#[no_mangle]`, `static mut`, or
  raw pointer/buffer surfaces have clear local justification.
- Duplicated export lists, preset maps, terrain vertex offsets, shader layout
  data, smoke helpers, or validation constants are either generated or called out
  as known debt.

Legacy and docs:

- Active docs' "Current State" and "Supported" claims have live source evidence.
- Historical migration notes are marked as historical and do not read like
  current instructions.
- Comments and debug field names do not imply retired TypeScript workers,
  renderer uploads, scene models, or terrain managers.
- Completed plans are archived and active replacements are named.

Validation gates:

- Logic or TypeScript behavior: `npm test`.
- Rust engine changes: `cargo test -p engine_core`.
- Rust terrain changes: `cargo test -p terrain_core`.
- Rust browser/WASM game changes: `cargo test -p engine_web`.
- Rust/WASM boundary or generated wasm artifacts: `npm run check:wasm`.
- Shader source or generated shader artifacts: `npm run check:shaders`.
- Rendering, input, camera, HUD, browser integration, or WebGPU: `npm run smoke:browser`, plus report and screenshot inspection.
- Terrain seams, chunk topology, Dual Contouring, or material seam risk:
  `npm run smoke:terrain-seams`.
- Terrain presets, descriptor parsing, biome/material classification, or terrain
  visual changes: `npm run smoke:terrain-presets`.
- Performance-sensitive density, meshing, streaming, or render-upload changes:
  `npm run bench:terrain:wasm` with before/after reports and an explicit budget
  or regression decision.
- Docs-only/process changes: `git -c safe.directory=C:/dev/ofg diff --check`.

## Collation And Action

The parent agent is the review captain. Deduplicate findings, drop vague style
preferences, and verify disputed findings locally before acting.

Classify each finding:

- Required: must fix before marking the milestone complete.
- Follow-up: real issue, but outside the milestone; record it in the ExecPlan.
- Rejected: not valid or intentionally accepted; record rationale in the
  Decision Log.

Required findings must be fixed and relevant validation rerun. Do not mark a
milestone complete until required findings are resolved or explicitly rejected
with rationale.

## Final Report Shape

Use this shape in the parent response or ExecPlan note:

    Milestone review:
    - Scope: ...
    - Reviewers: contract, code quality, legacy, correctness, validation.
    - Required findings fixed: ...
    - Follow-ups recorded: ...
    - Rejected findings: ... with rationale.
    - Validation rerun: ...
    - Remaining risk: ...

If there are no findings, say that clearly and still list residual risks or
validation that was not run.
