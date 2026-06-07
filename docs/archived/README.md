# Archived Documentation

This folder contains retired plans and historical reference snapshots. Documents
here are not active instructions and should not be used as the current source of
truth for implementation.

Use active docs in `docs/` first:

- `ARCHITECTURE.md`
- `API_CONTRACTS.md`
- `TERRAIN_PLAN.md`
- `TERRAIN_GEN_RESEARCH.md`

When an active plan is completed or replaced, move it here and leave a short note
in the active replacement explaining where the source of truth moved.

## Notes

- `RUST_CONVERSION_PLAN.md` was completed on 2026-06-06. Current runtime
  ownership and API contracts moved to `docs/ARCHITECTURE.md` and
  `docs/API_CONTRACTS.md`.
- `TERRAIN_PLAN_2026-06-07.md` was replaced on 2026-06-07. Current terrain work
  moved to the focused view-distance ExecPlan at `docs/TERRAIN_PLAN.md`.
- `CASCADING_SHADOW_MAPS_PLAN.md` was completed on 2026-06-07. Current shadow
  behavior is documented in `docs/API_CONTRACTS.md`, `docs/ARCHITECTURE.md`,
  renderer/shader code, and smoke tests.
