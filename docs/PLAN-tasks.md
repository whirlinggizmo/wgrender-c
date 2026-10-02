# Plan: Load on create, and polled tasks instead of callbacks

Status: **phases 1 and 2 built** (load on create, the resource section; ensure, ping
and the fetcher as polled tasks, the event bus gone: their design, decisions and what
was built are in [HISTORY.md](HISTORY.md#load-on-create-and-polled-tasks-instead-of-callbacks)).
**Phase 3 next**, approved as below: `wgr_fs.h` and byte spans.

## Why

A C callback is the hardest thing in the public API for a binding: a function pointer
and a `void *` it hands back. A JS guest can't pass one at all (the Haxe binding's guest
ABI works around six), and every binding writes a trampoline for each. And most of
wgrender's callbacks exist for one reason: a resource can only be created once its file
is local and loaded, so the program ensures the file, waits for a callback, and creates
the resource in it.

libwgt (`gfx/include/wgt_texture.h`, `core/src/wgt_core_load_priv.h`) shows the way out,
built on a pipeline it says it cribbed from wgrender's own: **a resource loads on
create.** `create(path)` returns a handle at once, PENDING; the file is made local,
prepared on a worker and finished on the main thread over the frames that follow; the
handle becomes READY or FAILED, and nothing is ever called back. Making a file local
*without* loading it is a separate thing, a task (libwgt's `wgt_asset_ensure`, designed
there and not yet built). Whatever may wait is read, never called back: a status that
changes only at the start of a frame, so a frame callback that checks it sees each
change once, in order.

## Design (phase 3)

### A public `wgr_fs.h`, and byte spans

libwgt's `wgt_fs.h` over wgrender's storage layer, which stays as it is inside:
`wgr_fs_read`, `_write`, `_exists`, `_remove`, `_mkdir`, `_rmdir` each return a task, on
every platform (a desktop read too, answered the next frame, as a web read is);
`wgr_fs_task_get_status` (NONE, PENDING, DONE, NOT_FOUND, FAILED), `_get_path`,
`_get_data`, `_get_size`, `_get_text`, `_destroy`. Requests made before storage is ready
wait for it. Paths are confined under the root (normalized; an absolute one, a `:`, a
control character, or one climbing above the root is refused). `wgr_fs.c` splits into
`_native` and `_web`. AGENTS.md's hard rule takes libwgt's byte span: `const unsigned
char *data, int size`, in copied before the call returns, out owned by the task until
it's destroyed; `check_rules.py` allows one only as a parameter followed by `int size`,
or a `_get_data` beside a `_get_size`.

## What changes beside the library

- **The Haxe binding**: an `Fs` module over `wgr_fs.h`, its tasks as `AssetTask`'s are
  (status, path, destroy), and a byte span as `haxe.io.Bytes` in and out (copied, so
  nothing on the Haxe side points into wgrender).
- **Tests**: the root jail (every refused path), a read and a write answered the next
  frame on desktop as on the web, NOT_FOUND apart from FAILED, requests made before
  storage is ready; the web store in the browser.
- **check_rules.py**: the byte span, allowed only in the two shapes above.

## Decisions

1. **`wgr_fs.h` in this branch, last.** It's the same model over the same machinery, but
   a new public surface with its own tests (the root jail, the web store). Recommended:
   phase 3, once load on create has settled.

## Phasing

1. **Load on create.** Built.
2. **Ensure as a task; callbacks out.** Built.
3. **`wgr_fs.h` and byte spans.**

## Verification

`tools/verify_builds.py --web --windows HOST` (storage differs by platform),
`tools/check_asset_cache.py` with and without `--manifest` (the asset layer reads and
writes through the same store), and the binding's suite and its web examples.
