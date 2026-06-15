# Terrain Legacy Reference Snapshot

This folder is a reference-only copy of the terrain implementation as it existed
when the terrain rebuild plan started on 2026-06-15.

Do not import, compile, or treat files in this folder as active source. The
active rebuild source of truth is `docs/TERRAIN_REBUILD_PLAN.md`, with current
runtime contracts in `docs/API_CONTRACTS.md` and `docs/ARCHITECTURE.md`.

The snapshot exists so future rebuild work can consult old terrain behavior,
tests, worker routing, water bathymetry, and streaming details without keeping
the legacy implementation alive as an active dependency.
