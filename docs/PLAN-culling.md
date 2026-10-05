# Plan: Frustum culling

> Carried into libwgt (17f3f18, cb5dee5), and kept here while wgrender is maintained:
> CONVENTIONS.md, "Docs".

Status: **phases 1 and 2 built** (2026-09-21). Phase 3 (a visibility mask, 2D members,
a spatial index) is open and not obviously needed yet.
Builds on the scene's bounds registry ([ARCHITECTURE.md](ARCHITECTURE.md)) and shadows
([PLAN-shadows.md](PLAN-shadows.md)).

## What's left

Phase 3, if wanted: the visibility mask from decision 1; 2D members against the screen rectangle; a spatial index so the per-member test itself stops scaling with the scene.

The design, decisions and what was built and measured are in [HISTORY.md](HISTORY.md#frustum-culling).
