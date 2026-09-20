#!/usr/bin/env bash
# Enforce the optional-subsystem boundary (src/internal/wgri_module.h): the core must
# not reference an optional subsystem's symbols, or every program links that subsystem
# (and what it pulls in: glTF parsing, decoders, ...) whether it uses it or not.
# Optional subsystems are the objects that register a module (WGRI_MODULE); the core
# reaches them only through the module list and the hooks in wgr_render.h / wgr_scene.h.
#
# Reads the headless library's symbol tables (builds it if missing). Run from
# anywhere: tools/check_modules.sh
set -u
cd "$(dirname "$0")/.." || exit 2

lib=build/headless/libwgrender.a
if [ ! -f "$lib" ]; then
    make --no-print-directory -s all HEADLESS=1 >/dev/null || exit 2
fi
nm -A "$lib" 2>/dev/null | python3 -c '
import sys, collections
defs, refs, optional = {}, collections.defaultdict(set), set()
for line in sys.stdin:  # "lib.a:obj.o:ADDRESS TYPE NAME", or "lib.a:obj.o: U NAME"
    fields = line.split()
    if len(fields) < 3:
        continue
    obj, kind, name = fields[0].split(":")[1], fields[-2], fields[-1]
    if kind == "U":
        refs[obj].add(name)
    elif kind in "TDRBW":
        defs.setdefault(name, obj)
    if name.startswith("wgri_register_") and name.endswith("_module"):
        optional.add(obj)
bad = []
for obj in sorted(set(refs) - optional):
    for name in sorted(refs[obj]):
        if defs.get(name) in optional:
            bad.append(f"  {obj} -> {name} ({defs[name]})")
if bad:
    print("FAIL: core code calls optional subsystems by name (go through wgri_module.h and the hooks):")
    print("\n".join(bad))
    sys.exit(1)
print(f"ok: core reaches the {len(optional)} optional subsystems only through modules and hooks")
'
