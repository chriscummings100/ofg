# Codex Execution Plans (ExecPlans)

Adapted on 2026-09-30 from `C:\dev\ofg-old2\PLANS.md`. This fresh repository keeps the old plan structure and evidence requirements without inheriting its retired build commands, coverage tooling, API contracts, or review-skill dependencies.

An ExecPlan is a self-contained, living specification that someone unfamiliar with the repository can follow to deliver observable behavior.

## When to use an ExecPlan

Use an ExecPlan for substantial multi-step implementation, new systems, and architectural changes. Keep working plans in `docs/plans`. Move completed plans to `docs/archived`; do not mark planned implementation complete because its design is finished.

Read this file before authoring a plan. After context compaction, re-read the active plan in full. During authorized implementation, continue through its milestones, maintaining the living sections. A proposal or research task does not automatically authorize executing every implementation milestone it describes.

## Requirements

Make each plan self-contained: explain terminology, paths, ownership, assumptions, prerequisites, commands, and observable acceptance criteria. Distinguish verified facts, recommendations, and untested hypotheses. Record external dependencies with exact revisions when implementation begins.

Maintain Progress, Surprises & Discoveries, Decision Log, and Outcomes & Retrospective at every stopping point. Timestamp progress; explain decisions and rejected alternatives. Keep milestones small enough to demonstrate independently. Each milestone describes its goal, work, visible result, and proof.

Validation must fit the change. Test numerical invariants and behavioral contracts, inspect visual output, and measure performance on named hardware. Record actual results and artifact paths; never turn a proposed command or historical test result into current evidence. Implementation plans must establish applicable test and coverage commands before claiming completion. Coverage thresholds must be chosen explicitly for the new codebase, with justified exclusions; the old repository's approximate 90% threshold and scripts are not implicitly installed here.

For visual work, save screenshots regularly under `artifacts`, present representative images to the user, and inspect transitions in motion as well as still images. Document any unsupported platform or unverified claim.

Before completing a milestone, review correctness, API contracts, ownership, code quality, obsolete paths, documentation, and validation evidence. Use a repository review skill if one is installed and applicable; otherwise perform and record that review directly. Do not require a nonexistent skill, architecture document, or contract identifier.

## Formatting

Use prose for explanation and milestones. Use timestamped checkboxes only in Progress. Tables are useful for comparable choices and validation matrices. A plan stored alone in a Markdown file needs no enclosing fence. When embedding a whole plan in another document, use a single `md` fence and indent any commands inside it.

## ExecPlan skeleton

```md
# <Action-oriented title>

This ExecPlan follows [PLANS.md](../../PLANS.md). Its living sections must remain current. State whether this is proposed, in progress, or complete and what execution scope is authorized.

## Purpose / Big Picture

Explain what a user can do after this work and how to observe it.

## Progress

- [x] (YYYY-MM-DD HH:MMZ) Completed discovery with evidence.
- [ ] Next deliverable and remaining work.

## Surprises & Discoveries

Record observations and evidence, including failed hypotheses.

## Decision Log

Record each decision, rationale, date, and whether it is provisional.

## Outcomes & Retrospective

State achieved outcomes, remaining gaps, and lessons.

## Contract and Quality Baseline

Define ownership, data formats, numerical requirements, platform support, and applicable checks. Reference existing contracts only when they exist. Distinguish invariants from provisional performance targets.

## Context and Orientation

Describe the current repository and relevant files. Define unfamiliar terms and identify external prerequisites.

## Plan of Work

Describe independently verifiable milestones in sequence. Name proposed files and responsibilities without prematurely fixing unnecessary abstractions.

## Concrete Steps

Specify working directory, exact commands, and expected outputs. Label commands for tools or targets not yet created as proposed interfaces, not runnable instructions. Resolve them before implementation completion.

## Milestone Review

Record the contract, correctness, quality, legacy, documentation, and validation review and how findings were resolved.

## Validation and Acceptance

Specify scenarios, numerical tolerances, failure cases, tests, measured budgets, visual evidence, and coverage policy where applicable.

## Idempotence and Recovery

Describe safe retries, cache invalidation, interrupted work, rollback, and preservation of user work.

## Artifacts and Notes

Record concise evidence, artifact paths, measurements, and sources.

## Interfaces and Dependencies

Describe data ownership, stable boundaries, libraries and pinned versions, and intentional limitations.
```

## Revising a plan

When scope changes, revise all affected sections so the plan remains coherent. Add a brief revision note explaining significant changes. Keep a clear boundary between existing behavior, the current milestone, and future directions.
