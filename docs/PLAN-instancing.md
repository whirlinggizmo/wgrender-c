# Plan: Model instancing

> Carried into libwgt (17f3f18, cb5dee5): every open item here is in libwgt's ROADMAP or
> TASKS, or closed in its HISTORY; where each went is in HISTORY.md, "Carried into libwgt".
> The items stay here while wgrender is maintained.

Status: **built** (2026-09-21), phases 1–4. Phase 5 (per-instance light sets,
transparent runs, a persistent buffer) is open and not obviously needed yet. Decisions below are answered:
automatic grouping with no new API (an explicit instanced handle only if a measured case
ever needs one), opaque models may be reordered, and custom shaders follow in phase 4 of
the same release.
Builds on the model draw queue (`src/wgr_model.c`), the joint texture it already uses for
skinning, and the sprite batch's instancing (`src/wgr_sprite_batch.c`), which is the
closest thing libwgrender already has to this.

## What's left

Phase 5: per-instance light sets, so grouping isn't constrained by light selection; transparent runs; a persistent instance buffer for placements that don't move.

The design, decisions and what was built and measured are in [HISTORY.md](HISTORY.md#model-instancing).
