# Internal Headers

This directory contains **internal-only** headers used to share implementation details across `src/*.c` files.

## Why This Exists

- Keep the public API surface small and stable (`include/*.h`).
- Avoid exposing unnecessary lifecycle/store internals to bindings and external consumers
- Make it clear which interfaces are safe for external use vs subject to change.

## What Belongs Here

- Per-module internal headers (`rl_*.h`) for private lifecycle hooks and cross-`src/*.c` declarations
- Export/attribute implementation helpers (`exports.h`)

## What Does Not Belong Here

- Public API declarations intended for consumers/bindings.
- Types/functions that should be stable across versions.

Public-facing headers stay in `include/`.

## C symbol naming

- **Shared across `src/*.c`** (non-`static`): use **`rl_<subsystem>_...`** and declare them **only** in headers here (`src/internal/rl_*.h`). They are **not** public API unless also added to `include/`.
- **File-local `static` functions:** no **`rl_`** prefix; prefer shortest clear **`verb_noun`** in `snake_case`; add qualifiers only to disambiguate within the file. Use a **`_ptr` suffix** when the API takes a **raw `*` instance** vs a sibling path that uses a **`rl_handle_t`**. Boolean predicates: prefer **`is_*`**. Handle→pointer helpers: prefer **`resolve_*`** / **`lookup_*`**. **`static`** is the contract; naming signals “this translation unit only.” **Exception:** do **not** rename identifiers used in **`EM_ASYNC_JS` / `EM_JS`** (Emscripten embeds the name). Scripted renames must be **whole-identifier** (word-boundary) when a static old name can prefix a longer **`rl_*`** symbol in the same file. See **AGENTS.md** § C implementation naming (`src/`) for the full checklist and promotion rule (internal header + `rl_*` when a helper leaves one `.c`).
