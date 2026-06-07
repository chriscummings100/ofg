# Integrate Completed Feature Worktrees

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

This plan follows `C:\dev\ofg\PLANS.md`.

## Purpose / Big Picture

Merge the completed feature worktrees at `C:\dev\ofg-sky`, `C:\dev\ofg-postprocess`, `C:\dev\ofg-terrain`, `C:\dev\ofg-shadow-maps`, and `C:\dev\ofg-touch-controls` into the repository `main` branch, repair conflicts and generated artifacts, validate with tests and smoke commands, and push the final integrated `main` branch.

The user-visible outcome is that the remote `main` branch contains the completed sky, post-process, terrain, shadow map, and touch control work in one verified integration state.

## Progress

- [x] (2026-06-07 16:59Z) Read `PLANS.md`, listed worktrees, and checked the initial status of each branch.
- [x] (2026-06-07 17:01Z) Commit and push dirty feature branches: `sky` at `06f2b01`, `postprocess` at `9fd65f8`, and `terrain` at `2067a1b`.
- [x] (2026-06-07 17:02Z) Pulled latest `origin/main`; it was already up to date with `main`.
- [x] (2026-06-07 17:16Z) Merged `sky` into `main`, resolved conflicts by preserving both shadow and sky debug contracts, regenerated shaders/WASM, fixed two Rust test helpers for the 31-float render snapshot shape, and validated with `npm run check:shaders`, `npm run check:wasm`, `npm run test:rust`, `npm run test:ts`, and `npm run smoke`.
- [x] (2026-06-07 17:38Z) Merged `postprocess` into `main`, resolved conflicts by preserving sky, shadow, and post-process debug/renderer contracts together, regenerated shaders/WASM, and validated with `npm run check:shaders`, `npm run check:wasm`, `npm run test:rust`, `npm run test:ts`, and `npm run smoke`.
- [x] (2026-06-07 17:59Z) Merged `terrain` into `main`, resolved Rust smoke/test/debug conflicts, regenerated WASM artifacts, fixed browser smoke to wait for rendered multi-LOD terrain before mobile assertions, and validated with `npm run check:shaders`, `npm run check:wasm`, `npm run test:rust`, `npm run test:ts`, `npm run smoke:terrain-seams`, `npm run smoke:terrain-presets`, `cargo run -p ofg_test_harness --bin ofg-render-smoke -- --out artifacts/rust-smoke --scenario lods`, `npm run bench:terrain:rust`, and `npm run smoke:browser`; the Rust half of `npm run smoke` also passed before the browser wait fix.
- [ ] Confirm `shadow-maps` is already included in `main` or merge it if needed, then validate and review.
- [ ] Merge `touch-controls` into `main`, resolve conflicts, regenerate artifacts, validate, and run milestone review.
- [ ] Run final `npm test`, `npm run build`, `npm run smoke:rust`, `npm run smoke:browser`, and `npm run smoke`.
- [ ] Push final `main`.
- [ ] Archive this completed ExecPlan under `C:\dev\ofg\docs\archived\` with a note that `main` is the active source of truth.

## Surprises & Discoveries

- Observation: `sky`, `postprocess`, and `terrain` had uncommitted work; `shadow-maps` and `touch-controls` were clean.
  Evidence: `git status --short --branch` in each sibling worktree.
- Observation: `shadow-maps` points at the same commit as `main` and `origin/shadow-maps`.
  Evidence: `git worktree list --porcelain` and `git log --oneline --decorate --graph --max-count=30 --all`.
- Observation: The sky merge needed source-level conflict resolution because `main` already had shadow debug contracts and sky added sky debug contracts plus larger camera uniforms.
  Evidence: conflicts in `crates/engine_web/src/render_uniforms.rs`, `docs/API_CONTRACTS.md`, `src/app/game.ts`, `src/engine/web/browserGameTypes.ts`, `src/engine/web/rustBrowserGameAdapter.ts`, generated shader/WASM metadata, and `tools/browser-smoke.mjs`.
- Observation: `npm run test:rust` initially failed because two shadow test helper snapshots still had 19 floats after sky expanded `ENGINE_RENDER_SNAPSHOT_FLOATS` to 31.
  Evidence: Rust compiler errors in `crates/engine_web/src/render_math_tests.rs` and `crates/engine_web/src/render_uniform_tests.rs`; fixed by adding the same 12 sky packet floats used by `crates/engine_web/src/tests.rs`.
- Observation: Sky milestone review required archiving the completed sky ExecPlan.
  Evidence: `docs/SKY_RENDERING_PLAN.md` said completion was done but lived outside `docs/archived/`; it was moved to `docs/archived/SKY_RENDERING_PLAN.md` with an archive note.
- Observation: The post-process merge overlapped the already-merged sky and shadow renderer paths in the WGSL scene output, Rust renderer status, browser debug hooks, generated shader metadata, and browser smoke.
  Evidence: conflicts were resolved in `src/engine/render/shaders/uber.wgsl`, `crates/engine_web/src/wgpu_renderer.rs`, `src/app/game.ts`, `src/engine/web/*`, `tools/build-shaders.mjs`, and `tools/browser-smoke.mjs`; smoke captured final, shadow, linear-depth, bloom, tone-map, DoF CoC, and DoF blurred views.
- Observation: Post-process milestone review found no required fixes, but `crates/engine_web/src/post_process.rs` is above the 600-line split-pressure threshold.
  Evidence: local milestone review checked contract, code-quality, legacy, correctness, and validation passes; `crates/engine_web/src/post_process.rs` is 875 lines, below the 1000-line hard concern but worth splitting before additional post effects are added.
- Observation: The terrain merge overlapped the shadow smoke scenario flags and terrain branch LOD stream controls.
  Evidence: `crates/ofg_test_harness/src/render_smoke/scenarios.rs` now preserves `shadow_debug` for the boot shadow images while adding `ScenarioStreamMode`, per-scenario tick budgets, and LOD smoke scenarios.
- Observation: `npm run smoke` initially failed in the browser mobile-touch scenario because the debug contract was asserted before the Rust stream had rendered any terrain nodes in that mobile page.
  Evidence: the failure showed loaded LOD0/LOD1/LOD2 node keys but `terrainNodeKeys: []` and `renderedNodeCount: 0`; passing `waitForTerrainLodFrame` into `tools/browser-smoke-mobile-touch.mjs` made `npm run smoke:browser` pass.
- Observation: Terrain milestone review found no required fixes, with file-size follow-ups already visible in the active terrain plan and API risk register.
  Evidence: local review checked contract, code-quality, legacy, correctness, and validation passes; `docs/TERRAIN_PLAN.md` records stream split-pressure handling and coverage evidence, while `docs/API_CONTRACTS.md` already tracks oversized renderer/facade risk. `crates/terrain_core/src/tests.rs` is now over 1000 lines and should be split by terrain subsystem before further test growth.

## Decision Log

- Decision: Treat "merge the feature branch main" as merging each completed feature branch into `main`, because the user listed completed feature worktrees and requested a final push after smoke verification.
  Rationale: The requested order says to pull latest main, merge the feature branch, fix conflicts, smoke, and push; that describes landing branches onto `main`.
  Date/Author: 2026-06-07 / Codex.
- Decision: During the sky merge, preserve both `shadowDebugView` and sky debug fields in TypeScript debug hooks, adapter snapshots, docs, and browser smoke.
  Rationale: The fields are independent black-box Rust debug surfaces and both are required by current smoke coverage.
  Date/Author: 2026-06-07 / Codex.
- Decision: Archive `docs/SKY_RENDERING_PLAN.md` as part of the merge.
  Rationale: The sky feature branch marked that ExecPlan complete, and repository instructions require completed active plans to move under `docs/archived/` with the active source of truth named.
  Date/Author: 2026-06-07 / Codex.
- Decision: Accept `crates/engine_web/src/post_process.rs` as a merge-time follow-up rather than splitting it during this integration.
  Rationale: The module has a purpose header, validated Rust-owned boundaries, tests, and smoke coverage; splitting during conflict resolution would add avoidable merge risk. Future post-process growth should extract target allocation, settings, and pass helpers before the file approaches 1000 lines.
  Date/Author: 2026-06-07 / Codex.
- Decision: Keep the new focused `docs/TERRAIN_PLAN.md` active and the previous terrain plan archived at `docs/archived/TERRAIN_PLAN_2026-06-07.md`.
  Rationale: The terrain branch replaced the historical mixed terrain document with an active multi-resolution terrain view-distance ExecPlan, and `docs/archived/README.md` now names the current source of truth.
  Date/Author: 2026-06-07 / Codex.
- Decision: Treat the mobile browser-smoke wait fix as required merge glue, not a terrain behavior change.
  Rationale: The Rust stream was exposing loaded LOD nodes, but the mobile page had not yet reached a rendered multi-LOD frame before the common debug assertion ran. Reusing the desktop `waitForTerrainLodFrame` keeps smoke black-box and avoids weakening the LOD assertion.
  Date/Author: 2026-06-07 / Codex.

## Outcomes & Retrospective

To be completed after the final push.

## Contract and Quality Baseline

The work must preserve the runtime ownership rules in `C:\dev\ofg\docs\API_CONTRACTS.md` and `C:\dev\ofg\docs\ARCHITECTURE.md`: Rust owns browser player/camera state, terrain streaming, WebGPU draw submission, render resources, terrain density, and mesh generation; TypeScript remains the browser shell, DOM input collector, URL parser, debug hook surface, and generic browser asset decoder.

Because this is branch integration rather than a new feature design, this plan does not intentionally change API contracts. Any conflict resolution that changes contracts must update the active docs and record the decision here.

## Context and Orientation

`C:\dev\ofg` is the `main` worktree. The sibling directories are independent Git worktrees for feature branches:

- `C:\dev\ofg-sky` on branch `sky`.
- `C:\dev\ofg-postprocess` on branch `postprocess`.
- `C:\dev\ofg-terrain` on branch `terrain`.
- `C:\dev\ofg-shadow-maps` on branch `shadow-maps`.
- `C:\dev\ofg-touch-controls` on branch `touch-controls`.

The integration target is the checked-out `main` branch in `C:\dev\ofg`.

## Plan of Work

First, commit and push any dirty feature worktree so every feature branch has a durable remote checkpoint. Then fetch and pull the latest `origin/main` in `C:\dev\ofg`. Merge feature branches into `main` one at a time, resolving conflicts in favor of the integrated architecture rather than by wholesale file replacement. After each merge, regenerate shader/WASM artifacts when source contracts change, run targeted tests, and run the repo-local `milestone-review` skill before marking that branch integration complete.

The final validation pass runs the full test suite, build, Rust smoke, browser smoke, and combined smoke. After final validation, push `main` and archive this plan under `docs/archived/`.

## Concrete Steps

Run commands from `C:\dev\ofg` unless a worktree path is explicitly named:

1. Commit dirty feature branches:
   `git -c safe.directory=C:/dev/ofg status --short --branch`
   `git -c safe.directory=C:/dev/ofg add -A`
   `git -c safe.directory=C:/dev/ofg commit -m "<message>"`
   `git -c safe.directory=C:/dev/ofg push -u origin <branch>`

2. Update main:
   `git -c safe.directory=C:/dev/ofg fetch origin`
   `git -c safe.directory=C:/dev/ofg pull --ff-only origin main`

3. Merge each feature:
   `git -c safe.directory=C:/dev/ofg merge --no-ff <branch>`
   Resolve conflicts.
   Regenerate artifacts with `npm run build:shaders` and/or `npm run build:wasm` when source changes require them.
   Validate with targeted commands followed by required smoke commands.

4. Final validation:
   `npm test`
   `npm run build`
   `npm run smoke:rust`
   `npm run smoke:browser`
   `npm run smoke`

## Milestone Review

After each feature branch is merged into `main`, update this plan, run the repo-local `milestone-review` skill, apply required findings or record a rejected finding with rationale in the Decision Log, then mark the branch integration complete.

## Validation and Acceptance

Acceptance requires:

- `main` includes the five feature branches, with no unresolved conflicts and no dirty tracked implementation files.
- Generated shader and WASM artifacts are current after conflict resolution.
- `npm test` passes.
- `npm run build` passes.
- `npm run smoke:rust` passes.
- `npm run smoke:browser` passes.
- `npm run smoke` passes.
- `git push origin main` succeeds.

For this merge-only integration, the coverage gate from `PLANS.md` is not run as a separate final command unless implementation files are edited beyond conflict resolution, because the final acceptance is smoke-heavy integration verification and the source branches already contain their feature-level test work.

## Idempotence and Recovery

Every feature branch is pushed before integration, so merge recovery can return to durable branch commits. If a merge fails before commit, use `git merge --abort` from `C:\dev\ofg`, inspect the conflict again, and retry. Do not reset or checkout over user changes. If final validation fails, keep the merge state, repair the issue, rerun the failing command, then rerun the final validation set.

## Artifacts and Notes

Evidence will be recorded in Progress and Outcomes as concise command results.

## Interfaces and Dependencies

No new public interface is intended. If conflict resolution changes exported WASM, TypeScript, shader, renderer, terrain, or smoke-test contracts, the matching docs and generated artifacts must be updated in the same integration.
