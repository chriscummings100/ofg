# Add C++ formatting and naming conventions

This ExecPlan is a living document. The sections Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective must stay up to date as work proceeds.

Once this plan is started, proceed independently for as long as possible. Return to the user only for critical input that cannot be safely inferred, or when the plan is complete.

If PLANS.md is present in the repo, maintain this document in accordance with it and link back to it by path.

## Purpose / Big Picture

Set up a repeatable C++ formatting command and bring the current C++ code toward the naming conventions requested on 2026-06-22. After this work, contributors should be able to run one local command to format C++ code with four-space indentation and a 120-column limit, and the active code should consistently use the agreed identifier style.

The desired C++ naming shape is:

    Classes and structs: CamelCase starting with a capital letter, matching the current style.
    Functions: lowercase_with_underscores, matching the current style.
    Member variables: m_name_with_underscores.
    Local variables: name_with_underscores.
    Static variables: _name_starts_with_underscore.
    Globals: g_name_with_underscores.

This plan is valuable before renderer work resumes because formatting churn and naming churn become much harder after the renderer/resource pipeline adds many more files.

## Progress

- [x] (2026-06-22 07:32Z) Re-read `C:\dev\ofg\PLANS.md` and `C:\dev\ofg\AGENTS.md`.
- [x] (2026-06-22 07:32Z) Confirmed the worktree was clean before this plan started.
- [x] (2026-06-22 07:32Z) Confirmed no `.clang-format` or `_clang-format` file currently exists in the repo.
- [x] (2026-06-22 07:32Z) Found Visual Studio LLVM `clang-format` 22.1.3 at `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe`.
- [x] (2026-06-22 07:39Z) Milestone 1 complete: added clang-format config, repo format/check commands, and AGENTS.md C++ style guidance.
- [x] (2026-06-22 07:55Z) Milestone 2 complete: mechanically formatted the current C++ source set and verified the formatter command is stable.
- [x] (2026-06-22 08:03Z) Milestone 3 complete: renamed member/static state in shared core/game/render/runtime code and tests, then formatted and validated with the incremental native C++ gate.
- [x] (2026-06-22 08:13Z) Milestone 4 complete: renamed browser/native smoke state and static variables file-by-file, then formatted and validated the browser WASM and native smoke paths.
- [x] (2026-06-22 08:29Z) Milestone 5 complete: ran final validation, updated this plan, and recorded the only intentional naming exception.

## Surprises & Discoveries

- Observation: There is no existing clang-format configuration to preserve.
  Evidence: `rg --files -g '.clang-format' -g '_clang-format'` returned no files.

- Observation: `clang-format` is installed but not on PATH in this environment.
  Evidence: `clang-format --version` failed, while `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe --version` reported version 22.1.3.

- Observation: Formatter discovery may find an older Visual Studio LLVM before the newest installed LLVM.
  Evidence: The first `npm run format:cpp:check` used a Visual Studio 2019 `clang-format.exe` and rejected `AlignAfterOpenBracket: BlockIndent`, `SortIncludes: Never`, and `ReferenceAlignment`, so the config now uses the more compatible `DontAlign`, `SortIncludes: false`, and `PointerAlignment: Left` values.

- Observation: Most current private C++ member state uses trailing underscores.
  Evidence: Representative members include `FrameState::frame_count_`, `Game::renderer_`, `GameRuntime::status_`, `BootstrapRenderer::pipeline_`, and `BrowserGame::surface_`.

- Observation: A too-short `npm run test:cpp` timeout can leave child CMake/Ninja/clang-cl processes holding `artifacts\build\cpp-native`.
  Evidence: The first validation attempt timed out after about two minutes, the next run failed with `EBUSY` removing `artifacts\build\cpp-native`, and process inspection showed stale build children. After stopping that process tree, a background `npm run test:cpp` completed successfully.

- Observation: Mechanical formatting reduced the two C++ file-size pressure points rather than growing them.
  Evidence: `cpp/src/native/render_smoke.cpp` went from 845 to 662 lines and `cpp/src/web/browser_game.cpp` went from 615 to 488 lines after formatting. `render_smoke.cpp` remains in the 500-1000 line watch range.

## Decision Log

- Decision: Base formatting on LLVM style with local overrides for four-space indentation and a 120-column limit.
  Rationale: The existing C++ code is already close to LLVM brace, namespace, and function style, but the user explicitly requested four-space indentation and a 120-character maximum line length.
  Date/Author: 2026-06-22 / User and Codex

- Decision: Add a script command instead of relying on `clang-format` being on PATH.
  Rationale: The installed formatter exists under Visual Studio LLVM on this machine. A wrapper can honor `CLANG_FORMAT` when set, use PATH when available, and fall back to installed LLVM candidates.
  Date/Author: 2026-06-22 / Codex

- Decision: Prefer `CLANG_FORMAT`, then current Visual Studio LLVM candidates, then PATH for the formatter wrapper.
  Rationale: Generic PATH lookup found an older Visual Studio 2019 formatter before the newer Visual Studio 18 formatter. The newer formatter should be the default when available, while still letting developers override with `CLANG_FORMAT`.
  Date/Author: 2026-06-22 / Codex

- Decision: Treat public data structs carefully during the naming pass.
  Rationale: The requested member-variable convention points toward `m_` fields, but some structs are lightweight data contracts or aggregate inputs. The implementation should prefer consistency while keeping JSON field names, TypeScript-facing behavior, and external command/report shapes unchanged.
  Date/Author: 2026-06-22 / Codex

## Outcomes & Retrospective

The repo now has a repeatable C++ formatter setup:

    `npm run format:cpp`
    `npm run format:cpp:check`

Both commands use the repo `.clang-format`, with four-space indentation and a 120-column limit, through the formatter wrapper at `tools/format-cpp.mjs`.

The active OFG C++ source, headers, and tests were mechanically formatted and renamed toward the requested convention. OFG-owned data members now use `m_`, internal static variables/constants now use leading-underscore names, locals remain lowercase_with_underscores, and globals were not present. The only intentional scan exception is Emscripten's external `emscripten::class_` API helper in `cpp/src/web/embind_module.cpp`; it is not an OFG identifier.

External behavior was preserved. Browser debug-status JSON field names, native render-smoke CLI flags, native render-smoke report field names, and smoke-contract thresholds stayed unchanged.

Large-file pressure improved through formatting but did not disappear. `cpp/src/native/render_smoke.cpp` remains in the 500-1000 line watch range, and this plan did not attempt an architectural split.

## Contract and Quality Baseline

This plan preserves `OFG-BOOT-001 TypeScript Host Ownership`. Formatting and C++ renames must not move gameplay, renderer, or GPU ownership into TypeScript.

This plan preserves `OFG-BOOT-002 C++ Runtime Ownership`. It may rename C++ members and locals, but it must not change which subsystem owns frame state, renderer resources, browser WebGPU setup, native Dawn setup, or queue submission.

This plan preserves `OFG-BOOT-003 WASM Facade`. Embind-facing methods exposed through TypeScript must keep the same create, resize, frame, debug status, and dispose behavior.

This plan preserves `OFG-BOOT-004` through `OFG-BOOT-006`. Formatting and identifier renames must not change the bootstrap triangle visual contract, WebGPU baseline, or durable resource lifetime behavior.

This plan preserves `OFG-BOOT-009 Coverage`. Modified implementation files should still pass the relevant test and coverage gates, or this plan must record an explicit exception. Because most changes are mechanical formatting/renaming, the expected validation is `npm test`, `npm run coverage`, and the smoke commands when browser/native renderer files are touched.

Quality constraints from `C:\dev\ofg\AGENTS.md` apply. File-size follow-ups from `C:\dev\ofg\docs\plans\shared-game-frame-architecture-plan.md` remain in force: `cpp/src/web/browser_game.cpp` and `cpp/src/native/render_smoke.cpp` should not grow meaningfully during this work.

## Context and Orientation

The repository root is `C:\dev\ofg`.

The C++ source set is under:

    C:\dev\ofg\cpp\include
    C:\dev\ofg\cpp\src
    C:\dev\ofg\cpp\tests

The current C++ formatter state is "no config". Existing code uses a mostly LLVM-like style, two-space indentation, attached braces, no namespace indentation, and manual include ordering. The requested style keeps the general shape but changes indentation to four spaces and the column limit to 120.

The current C++ naming state has capitalized classes/structs and lowercase_with_underscores functions. Private member variables mostly use a trailing underscore. Static local and namespace-scope constants mostly use names such as `next_canvas_id`, `kBytesPerPixel`, and `kRenderFormat`.

## Plan of Work

Milestone 1 adds the tooling and documentation. Add `C:\dev\ofg\.clang-format` with LLVM-derived settings, `IndentWidth: 4`, `ContinuationIndentWidth: 4`, `ColumnLimit: 120`, and conservative include behavior. Add a Node wrapper, likely `C:\dev\ofg\tools\format-cpp.mjs`, that discovers clang-format through `CLANG_FORMAT`, PATH, or installed LLVM locations. Add `format:cpp` and `format:cpp:check` package scripts. Update `C:\dev\ofg\AGENTS.md` with the requested formatting and naming conventions.

Milestone 2 runs formatting over the current C++ files and proves that `npm run format:cpp:check` is stable afterward. This milestone should not intentionally rename identifiers.

Milestone 3 renames the shared core/game/render code and nearest tests. The main files are `cpp/include/ofg/core/frame_state.hpp`, `cpp/src/core/frame_state.cpp`, `cpp/include/ofg/game/*.hpp`, `cpp/src/game/*.cpp`, `cpp/include/ofg/render/*.hpp`, `cpp/src/render/*.cpp`, and the affected tests. Run formatting after each small slice.

Milestone 4 renames browser and native smoke state. The main files are `cpp/include/ofg/web/*.hpp`, `cpp/src/web/*.cpp`, `cpp/include/ofg/native/*.hpp`, `cpp/src/native/*.cpp`, and related tests or smoke report code. Keep public JSON field names and command-line/report shapes unchanged. Run formatting after each file or small cluster.

Milestone 5 runs the final gates, updates the plan, and records any exceptions. If broad automated renaming is too risky in a given file, record the remaining exception and why before proceeding to renderer work.

## Concrete Steps

From `C:\dev\ofg`, run:

    npm run format:cpp:check
    npm run format:cpp
    npm run format:cpp:check
    npm test
    npm run coverage
    npm run smoke:browser:cpp
    npm run smoke:browser
    $env:OFG_DAWN_SOURCE_DIR='C:\tools\dawn'; npm run smoke:render
    git -c safe.directory=C:/dev/ofg diff --check

During iterative rename milestones, use narrower validation first:

    npm run format:cpp:check
    npm run test:cpp

Then run the full validation list before completing the plan.

## Milestone Review

After each milestone:

1. Update this plan's living sections.
2. Run the repo-local `milestone-review` skill against the milestone diff and validation evidence.
3. Apply required findings before marking the milestone complete, or record a rejected finding with rationale in the Decision Log.
4. Re-run relevant validation commands.
5. Record the review summary, commands, artifacts, and remaining risks in this plan.

## Validation and Acceptance

This plan is complete when:

- `.clang-format` exists and encodes four-space indentation plus a 120-column limit.
- `AGENTS.md` names the C++ formatting and naming conventions requested by the user.
- `npm run format:cpp` formats the repo's C++ source, header, and test files.
- `npm run format:cpp:check` passes on the formatted tree.
- Member/static/global/local naming either matches the requested convention in active C++ code or this plan records narrow, intentional exceptions.
- `npm test`, `npm run coverage`, `npm run smoke:browser:cpp`, `npm run smoke:browser`, native `npm run smoke:render`, and `git diff --check` pass.

## Idempotence and Recovery

Formatting is idempotent: if a rename slice fails, run `npm run format:cpp` again after fixes and re-run the relevant tests. Avoid broad semantic rewrites while formatting. If a rename creates a compile error, use compiler output and `rg` to finish the same identifier family rather than reverting unrelated files.

Generated directories such as `artifacts`, `dist`, and `assets/wasm/ofg_cpp` can be regenerated by existing npm scripts. Do not treat generated files as source of truth for naming conventions.

## Artifacts and Notes

Record concise command transcripts here as milestones complete.

Milestone 1 formatter setup validation:

    `node --check tools\format-cpp.mjs` passed.
    `node --check tools\lib\toolchain.mjs` passed.
    A direct helper probe selected `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe`.
    `npm run format:cpp:check` reached real formatting diagnostics and failed as expected before Milestone 2 formatting. The first lines were saved to `C:\dev\ofg\artifacts\format-cpp-check.log`.
    `git -c safe.directory=C:/dev/ofg diff --check` passed with a package.json LF/CRLF warning only.

Milestone 1 review:

    Scope: `.clang-format`, `tools/format-cpp.mjs`, `tools/lib/toolchain.mjs` clang-format discovery, package scripts, AGENTS.md formatting/naming guidance, and this ExecPlan.
    Reviewers: local contract, code quality, legacy, correctness, and validation passes. Sub-agent tools were not used because the user described them as optional rather than required for this checkpoint.
    Required findings fixed: make `findClangFormat` prefer `CLANG_FORMAT`, then current Visual Studio LLVM candidates, then PATH so an older PATH-discovered formatter does not win by default.
    Follow-ups recorded: none for this milestone.
    Rejected findings: none.
    Validation rerun: wrapper syntax checks, formatter path probe, expected failing `npm run format:cpp:check`, and `git diff --check`.
    Remaining risk: the newly introduced formatter config intentionally makes the current C++ tree non-compliant until Milestone 2 runs the formatter.

Milestone 2 formatter application validation:

    `npm run format:cpp` passed and formatted 33 C++ files with `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe`.
    `npm run format:cpp:check` passed and checked 33 C++ files with the same formatter.
    `npm run test:cpp` passed after a clean native C++ build; the transcript was written to `C:\dev\ofg\artifacts\test-cpp-format-m2.log` and ended with `100% tests passed, 0 tests failed out of 1`.
    `git -c safe.directory=C:/dev/ofg diff --check` passed with LF/CRLF working-copy warnings only.

Milestone 2 review:

    Scope: mechanical clang-format output across `cpp/include`, `cpp/src`, and `cpp/tests`, plus validation evidence from the formatter check and native C++ doctests.
    Reviewers: local contract, code quality, legacy, correctness, and validation passes. Sub-agent tools were not used because this checkpoint is formatting-only and the milestone-review request was satisfied with local read-only passes.
    Required findings fixed: none.
    Follow-ups recorded: `cpp/src/native/render_smoke.cpp` still has existing split pressure at 662 lines, though this milestone reduced it from 845 lines.
    Rejected findings: none.
    Validation rerun: `npm run format:cpp:check`, `npm run test:cpp`, and `git -c safe.directory=C:/dev/ofg diff --check`.
    Remaining risk: the large diff is intentionally whitespace/formatting-heavy; subsequent naming milestones should remain small and validated independently.

Milestone 3 shared naming validation:

    Scope: `FrameState`, `Game`, `GameRuntime`, shared `GpuContext`, `RenderTarget`, `RuntimeDebugStatus`, `BootstrapRenderer`, `BootstrapVertex`, `ClearColor`, nearest doctests, and direct browser status consumers affected by shared struct renames.
    Naming applied: private trailing-underscore members now use `m_`; OFG-owned public data structs in this scope now use `m_`; shared namespace-scope constants `kBootstrapShaderSource` and `kBootstrapVertices` now use leading-underscore static/internal names.
    Contract check: `RuntimeDebugStatus::to_json()` still writes the browser-facing JSON keys such as `frameCount`, `canvasWidth`, `adapterName`, and `lastError`; only C++ member identifiers changed.
    `npm run format:cpp:check` passed after the shared renames.
    `cmake --build artifacts\build\cpp-native --target ofg_cpp_tests` passed incrementally.
    `ctest --test-dir artifacts\build\cpp-native -R "^ofg_cpp_tests$" --output-on-failure` passed with `100% tests passed, 0 tests failed out of 1`.

Milestone 3 review:

    Scope: shared C++ naming diff plus the browser status-read updates required by the shared `RuntimeDebugStatus` field rename.
    Reviewers: local contract, code quality, legacy, correctness, and validation passes. Sub-agent tools were not used because the milestone scope was mechanical and validated locally.
    Required findings fixed: none.
    Follow-ups recorded: browser-specific and native-smoke naming cleanup remains for Milestone 4.
    Rejected findings: none.
    Validation rerun: `npm run format:cpp:check`, incremental native C++ build, and CTest.
    Remaining risk: `npm run test:cpp` was not rerun from a clean build for this milestone to avoid repeatedly rebuilding Dawn; the final milestone must run the official full command set.

Milestone 4 browser/native naming validation:

    Scope: `BrowserGame`, browser WebGPU callback context, native render-smoke contracts/options, native RAII/request helper structs, PNG writer constant naming, and native render-smoke internal constants.
    Naming applied: browser private state now uses `m_`; the static canvas id counter is `_next_canvas_id`; native smoke/PNG internal constants are `_bytes_per_pixel`, `_wait_timeout_ns`, and `_render_format`; native smoke structs use `m_` data members.
    Contract check: native smoke command-line flags remain `--width` and `--height`; native report JSON still writes `width`, `height`, `backend`, and the existing threshold/report field names; browser-facing debug JSON stays unchanged.
    `npm run format:cpp:check` passed.
    `ctest --test-dir artifacts\build\cpp-native -R "^ofg_cpp_tests$" --output-on-failure` passed.
    `npm run build:wasm` passed and compiled `cpp/src/web/browser_game.cpp` through Emscripten.
    `npm run smoke:render` passed, producing `C:\dev\ofg\artifacts\render-smoke\bootstrap.png` and `C:\dev\ofg\artifacts\render-smoke\report.json`; the report recorded `passed: true`, Vulkan backend, triangle ratio `0.230112`, background ratio `0.769888`, and 28 non-background color buckets.
    `npm run smoke:browser:cpp` passed after rebuilding the WASM package.
    Stale-name scan `rg -n "\b[a-z][a-z0-9_]*_\b|\bk[A-Z][A-Za-z0-9_]*\b" cpp\include cpp\src cpp\tests` now reports only Emscripten's external `class_` API helper.

Milestone 4 review:

    Scope: browser-specific and native-smoke naming diff plus validation evidence from Emscripten build, native CTest, native render smoke, and focused browser C++ smoke.
    Reviewers: local contract, code quality, legacy, correctness, and validation passes. Sub-agent tools were not used because this milestone was a mechanical rename with direct smoke coverage.
    Required findings fixed: corrected an overly broad native mechanical rename that had briefly changed local/API names and report keys; reran formatter and smoke validation after the fix.
    Follow-ups recorded: none.
    Rejected findings: none.
    Validation rerun: `npm run format:cpp:check`, CTest, `npm run build:wasm`, `npm run smoke:render`, and `npm run smoke:browser:cpp`.
    Remaining risk: final full clean validation, coverage gates, and general browser smoke remain for Milestone 5.

Milestone 5 final validation:

    `npm run format:cpp:check` passed.
    `npm test` passed; the clean C++ CTest target passed and the TypeScript test suite reported 19 passing tests.
    `npm run coverage` passed; C++ covered files reported 100.00% line coverage, TypeScript coverage passed for checked files, and the existing `src/app/main.ts` browser-entrypoint exception remained documented by the coverage tool.
    `npm run smoke:browser:cpp` passed.
    `npm run smoke:browser` passed.
    `npm run smoke:render` passed and left `C:\dev\ofg\artifacts\render-smoke\bootstrap.png` plus `C:\dev\ofg\artifacts\render-smoke\report.json` with `passed: true`.
    `git -c safe.directory=C:/dev/ofg diff --check` passed with LF/CRLF working-copy warnings only.
    Stale naming scan `rg -n "\b[a-z][a-z0-9_]*_\b|\bk[A-Z][A-Za-z0-9_]*\b" cpp\include cpp\src cpp\tests` reports only `emscripten::class_`, an external API name.

Milestone 5 review:

    Scope: complete formatter/naming diff, source docs, ExecPlan updates, and final validation evidence.
    Reviewers: local contract, code quality, legacy, correctness, and validation passes.
    Required findings fixed: none after final validation.
    Follow-ups recorded: none for this plan.
    Rejected findings: `emscripten::class_` is intentionally not renamed because it is part of the Emscripten API, not OFG code.
    Validation rerun: full formatter, test, coverage, browser smoke, focused browser C++ smoke, native render smoke, and diff-check gates.
    Remaining risk: the diff is large because it includes a repo-wide C++ format pass; review should expect formatting churn mixed with the mechanical identifier renames.

## Interfaces and Dependencies

Expected new artifacts:

    C:\dev\ofg\.clang-format
    C:\dev\ofg\tools\format-cpp.mjs

Expected changed docs/config:

    C:\dev\ofg\AGENTS.md
    C:\dev\ofg\package.json
    C:\dev\ofg\package-lock.json, only if npm script changes require lockfile metadata updates
    C:\dev\ofg\docs\plans\cpp-format-naming-conventions-plan.md

The formatter wrapper should discover clang-format without using repository-local toolchain downloads. It should prefer `CLANG_FORMAT`, then PATH, then installed LLVM candidates.
