# Guiding principles

This is a living document to describe guiding principles for work on OFG.

## Tests (2026-06-19)

All systems should have a thourough set of tests that cover the public interface. Code coverage should be used to ensure 90%+ coversage is attained unless there is good reason not to. 

## File sizes (2026-06-19)

Large files make for poor readability, small files are just noise:
- Files between 500-1000 lines should begin to be of small concern
- Files above 1000 lines should be broken into smaller units
- Files above 2000 lines should be considered a critical architectural problem

## Comments and readability (2026-06-19)

Code should remain readable to a human at all times. Every function written should have doc strings or comments attached defining its purpose, and larger functions (over 50 lines) should contain comments internally to explain their workings.

Files should have detailed and maintained comments at the top to document their purpose and how they function.

## Modularity (2026-06-19)

To continue being maintainable when large, the OFG code base needs to remain modular, with clear contracts between modules. Extending the public interface of a module requires clear justification and documentation.

## Facade ownership (2026-07-04)

Facade and lifecycle files such as `game.cpp` should stay thin. They may coordinate startup and shutdown, frame order, and compact status aggregation, but feature-specific behavior belongs in the owning subsystem, component, or resource type. Do not let `game.cpp` become a dumping ground for player, renderer, resource, terrain, networking, or UI implementation details. If a feature needs more than orchestration or status plumbing in `Game`, move it behind an owned API and document that boundary.


