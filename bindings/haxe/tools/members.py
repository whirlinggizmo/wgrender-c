"""The API layer's members, and which C calls each one makes, as the Haxe parser
reads them.

Shared by tools/check_coverage.py (the one-name rule) and tools/check_refusals.py (a
refusal is repeated in the member's doc). wgr.macros.Members, a build macro on every
type in src/wgr, walks each function's body as written, so a call is a call: a wrapper
with a default, a `#if` or a second line, a call inside a closure. Run for the native
target and the JS one, since some types are a file per target (GuestAbi.cpp.hx and
.js.hx), and merged. Needs Haxe ($HAXE, else haxe on PATH).
"""
import json
import os
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

HAXE = os.environ.get('HAXE', 'haxe')
TARGETS = {'cpp': ['--cpp', '{out}', '-D', 'HXCPP_M64'], 'js': ['-js', '{out}/x.js']}


@dataclass
class Member:
    module: str        # the Haxe type's module: GuestAbi.cpp.hx and GuestAbi.js.hx are both GuestAbi
    name: str
    public: bool
    doc: str
    calls: set         # the C functions it calls through Raw, directly
    file: str
    line: int

    @property
    def qualified(self):
        return f'{self.module}.{self.name}'


def by_target(root):
    """wgr.macros.Members' records for each target: {'cpp': [...], 'js': [...]}."""
    found = {}
    with tempfile.TemporaryDirectory() as scratch:
        for target, args in TARGETS.items():
            out = Path(scratch) / f'members-{target}.json'
            # Members.dump first: it puts its build macro on the package before include
            # types it
            done = subprocess.run([HAXE, '-cp', 'src', '--macro', f"wgr.macros.Members.dump('{out.as_posix()}')",
                                   '--macro', "include('wgr', true, ['wgr.macros'])",
                                   *[a.format(out=scratch) for a in args], '--no-output'],
                                  cwd=root, capture_output=True, text=True)
            if done.returncode != 0 or not out.exists():
                sys.exit(f'members: the Haxe compiler could not type src/wgr ({target})\n{done.stderr.strip()}')
            found[target] = json.loads(out.read_text(encoding='utf-8'))
    return found


def typed(root):
    """Every target's records, merged by where each function is."""
    found = {}
    for records in by_target(root).values():
        for r in records:
            found.setdefault((r['file'], r['name'], r['line']), r)
    return list(found.values())


def members(root):
    """Every function in src/wgr, each with the C calls its body makes: its own, and
    those of the private `...Raw` members of its type it calls. An overload makes its C
    call through one (`setPositionRaw`: README, "Calling it from cppia"), and it's the
    overload a refusal has to be documented on, and the overload that is the call's
    one name."""
    records = [r for r in typed(root) if not r['impl']]  # the API layer: src/wgr, not wgr.impl
    # wgrender's calls: GuestRaw's own (attach, host) are the binding's C, not wgrender's
    out = [Member(r['module'], r['name'], r['public'], r['doc'], {c for c in r['calls'] if c.startswith('wgr_')},
                  r['file'], r['line'])
           for r in records]
    helpers = {}
    for m in out:
        if not m.public and m.name.endswith('Raw') and m.calls:
            helpers.setdefault((m.module, m.name), set()).update(m.calls)
    for m, r in zip(out, records):
        for name in r['local']:
            if name != m.name:
                m.calls |= helpers.get((m.module, name), set())
    return out


def names_by_call(found):
    """{c_name: {"Module.member", ...}} over public members: the one-name rule's subject."""
    out = {}
    for m in found:
        if m.public:
            for c in m.calls:
                out.setdefault(c, set()).add(m.qualified)
    return out
