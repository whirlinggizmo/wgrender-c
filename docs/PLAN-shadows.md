# Plan: Shadows

> Carried into libwgt (17f3f18, cb5dee5): every open item here is in libwgt's ROADMAP or
> TASKS, or closed in its HISTORY; where each went is in HISTORY.md, "Carried into libwgt".
> The items stay here while wgrender is maintained.

Status: **phases 1 and 2 implemented (2026-09-21)** on desktop GL, WebGL2 and WebGPU:
one directional light, then spot lights and up to four casting at once.
Decisions 1–7 were answered as recommended, with one addition during implementation:
a shadow's strength and tint. See "Phase 1 as built", and "The WebGPU bug" for the one
that took longest. Roadmap: the largest remaining gap against three.js, which ships
shadow maps. Builds on lighting ([HISTORY.md: Lighting (light objects, per-scene lighting)](HISTORY.md#lighting-light-objects-per-scene-lighting)), materials
([HISTORY.md: Materials and shaders](HISTORY.md#materials-and-shaders)) and render targets
([HISTORY.md: Render to texture](HISTORY.md#render-to-texture)).

## What's left

Phase 3: cascades for large outdoor scenes, point lights (six faces or none), and sprite casters (alpha-tested quads in the depth pass). Also worth doing then: skip a light's pass when nothing it can see has moved.

The design, decisions and what was built and measured are in [HISTORY.md](HISTORY.md#shadows).
