# AGENTS

Read [docs/CONVENTIONS.md](docs/CONVENTIONS.md) before working in libwgrender, and follow
it exactly; [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) is how it works now. The
developer docs are complete without this file, and every rule lives in one of them,
once (CONVENTIONS.md, "Docs"): this file links to rules and adds what only an agent
needs. It never restates or contradicts a rule; anything here a developer also needs
moves into the developer docs.

## Rules that are easy to break

Each is CONVENTIONS.md's; the words here only find it.

- Correct over compatible; bindings change with the API, in the same commit ("Scope").
- Presets, `out/` and `build/`, `build.json` as the build's data ("Build and verify").
- No tool reads source as text ("Build and verify").
- Which doc is true; what moves to HISTORY; clamp or refuse ("Docs: which one is true").
- What a public call may take and return; getters for setters; load on create ("Public API shape").
- One name per C call, sugar on top ("Bindings: one name per C call").
- The core never names an optional module ("Core and optional subsystems").
- Flattened uniform arrays indexed at a constant ("Shaders").
- `wgr_` public, `wgri_` internal; predicates; transforms; `_ptr` ("Naming").

## Agent practice

- **Ask before changing observable behavior or public API** (`include/*.h`), and
  recommend the correct option (CONVENTIONS.md, "Scope"). Purely internal refactors
  with no behavioral impact don't need that step.
- For feature work or non-trivial fixes, **outline the plan first** and wait for the
  go-ahead, unless already told to implement. Read-only tasks (questions, reviews) need
  no approval.
- Before calling a change done, run what "Build and verify" says a change passes
  (`tools/verify_builds.py`, with `--web` or `--windows HOST` where it says so). A
  skipped check is reported as skipped, never as passed.
- After any edit to `include/*.h`, comments included, regenerate both bindings
  (`bindings/haxe/tools/gen_raw_externs.py`, `bindings/js/tools/gen_binding.py`) before
  verifying.
- Read `docs/HISTORY.md` before proposing a change: it says why things are as they are,
  and what was already tried.

## Scripted renames

When a `static` helper's old name is a prefix of a longer `wgr_*` symbol in the same
file, use **whole-identifier** (word-boundary) replacement, never blind substring
replace, or you'll corrupt the public API. Guard against matching inside comments
(possessives) and reused short names (loop counters, value structs).
