# AI Workflow

This repository should be easy for AI agents to change without guesswork.

## Ground Rules

- Keep modules small and named after the concept they own.
- Prefer pure functions for engine logic.
- Add tests beside risky behavior before broadening the engine surface.
- Document new boundaries in `docs/ARCHITECTURE.md` when they become stable.
- Avoid speculative abstractions until two real call sites need them.

## Expected Agent Loop

1. Read the nearest module and test before changing behavior.
2. Make the smallest coherent change.
3. Run `npm test`.
4. Run `npm run build`.
5. For visual changes, run `npm run smoke:browser`.
6. Summarize what changed and any remaining risk.

## Test Naming

Use behavior names rather than implementation names:

- Good: `grounds first-person player on sampled terrain`
- Good: `builds a heightfield mesh with shared vertices`
- Avoid: `calls updatePlayerPosition`

## Future Automation Hooks

- Extend browser smoke tests to cover pointer-lock look controls and movement.
- Add mesh golden fixtures before optimizing Dual Contouring or moving it to Rust/WASM.
- Add benchmark scripts for chunk generation after chunk boundaries exist.
