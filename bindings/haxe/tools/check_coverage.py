#!/usr/bin/env python3
"""What of wgrender this binding reaches, and what it would take to reach more.

    tools/check_coverage.py                    the report
    tools/check_coverage.py --check            fail if OMISSIONS has rotted, or a C call has
                                         more than one name

Two layers answer separately. `wgr.impl.Raw` is generated and covers the C surface;
the API layer above it — the handle abstracts and their methods — is hand-written, and
is where the gap lives. Both are reported, because "callable" and "wrapped" are
different questions and conflating them flatters the binding.

Example reachability is the useful number: a subsystem matters in proportion to how
many of wgrender's own examples it blocks.

Borrowed from librl's tools/audit_binding_parity.py, which audits six bindings across
four languages. This one has one binding to audit, and its methods call Raw directly
rather than through a name mapping, so it is much smaller — but the idea of recording
*intentional* omissions is taken whole. Without it a decision and a to-do look alike.

`--check` also holds the rule every wgrender binding keeps (wgrender's AGENTS.md,
"Bindings"): each C call has one public name, and a public member that calls C calls
one C function. So `Version.runtime` builds its string from `major()`, `minor()` and
`patch()` rather than calling C three times itself, and the two rounded rectangles are
overloads of one name. Private plumbing -- a trampoline, a shared on/once -- is exempt.
The C name does not have to be read off the Haxe one; the rule is that there is
exactly one to find.
"""
import pathlib
import re
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from wgrpath import WGRENDER  # noqa: E402
import headers  # noqa: E402  (wgrender's tools/headers.py)
import members  # noqa: E402
import cli  # noqa: E402

if __name__ == '__main__':
    cli.parse(__doc__, ('--check',), positional=0)

ROOT = pathlib.Path(__file__).resolve().parent.parent

# Reachable on hxcpp but deliberately not on js, and why: every one takes a C function
# pointer or a void *, which the guest ABI replaces there, or returns something too big
# to copy per frame. `--check` fails if one turns up in Raw.js.hx after all, so the
# list cannot quietly go stale.
OMISSIONS = {
    'wgr_set_init': 'js: takes a C callback; the guest ABI installs ops instead',
    'wgr_set_frame': 'js: takes a C callback; the guest ABI installs ops instead',
    'wgr_set_tick': 'js: takes a C callback; the guest ABI installs ops instead',
    'wgr_set_shutdown': 'js: takes a C callback; the guest ABI installs ops instead',
    'wgr_asset_add_task': 'js: takes a C callback; wgr_guest_asset_load takes an id',
    'wgr_asset_set_fetcher': 'js: takes a C callback, and has no job there -- the browser '
                             'is the downloader. Asset.setFetcher compiles anyway and '
                             'answers false, so a caller needs no #if.',
}


# Reachable from Raw on both targets, but deliberately not given an API-layer
# method, and why. This is the other kind of decision: not a target asymmetry but a
# judgement that wrapping the call would make the binding worse. `--check` fails if
# one turns up wrapped after all, so a change of mind has to be recorded here too.
API_OMISSIONS = {
    'wgr_text_draw_n': 'length is BYTES; a Haxe string measures in UTF-16 units, so the '
                       'two disagree for anything non-ASCII. Pass a substring to draw().',
    'wgr_text_measure_n': 'same: measure() on a substring is correct and this is not.',
}


def c_functions():
    """{header stem: {wgr_* function}}: the public headers as clang reads them."""
    out = {}
    for name, fn in headers.read(WGRENDER, tool='check_coverage').functions.items():
        out.setdefault(fn.header.rsplit('.', 1)[0], set()).add(name)
    return out


def reached():
    """What the hand-written API layer calls (and the guest ABI's implementation beside
    it), and what each target's generated Raw declares: wgr.macros.Members, per target."""
    api, raw = set(), {}
    for target, records in members.by_target(ROOT).items():
        for r in records:
            if not r['impl'] or r['module'] == 'GuestAbi':
                api |= {c for c in r['calls'] if c.startswith('wgr_')}
        raw['hxcpp' if target == 'cpp' else target] = {r['name'] for r in records if r['impl'] and r['module'] == 'Raw'
                                                      and r['name'].startswith('wgr_')}
    return api, raw


def one_name(every):
    """Each C call has one public name, and each public member calls one C function."""
    found = members.members(ROOT)
    problems = []
    for c, names in sorted(members.names_by_call(found).items()):
        if c in every and len(names) > 1:
            problems.append(f'{c}: called by {", ".join(sorted(names))} -- one name, '
                            'and the others call it')
    for m in found:
        if m.public and len(m.calls & every) > 1:
            problems.append(f'{m.qualified} ({m.file}:{m.line}): calls '
                            f'{", ".join(sorted(m.calls & every))} -- give each its own '
                            'member and call those')
    return problems


def macros():
    """The headers' function-like macros (wgr_logger_debug, ...): C conveniences, not calls."""
    return set(headers.read(WGRENDER, tool='check_coverage').macros)


def main():
    by_header = c_functions()
    every = {fn for fns in by_header.values() for fn in fns}
    api, raw = reached()
    # The guest ABI's own calls (wgr_guest_*) live in host/wgr_guest.h, not in
    # wgrender's public headers, so they are not part of the surface being measured.
    # Counting them made the headline read 467/466.
    api &= every

    if '--check' in sys.argv:
        rotted = [f'{fn}: listed as omitted from js, but Raw.js.hx has it' for fn in OMISSIONS
                  if fn in raw['js']]
        unknown = [f'{fn}: listed as omitted, but no such function'
                   for fn in list(OMISSIONS) + list(API_OMISSIONS) if fn not in every]
        wrapped = [f'{fn}: listed as deliberately unwrapped, but the API layer calls it'
                   for fn in API_OMISSIONS if fn in api]
        for line in rotted + unknown + wrapped:
            print(f'  {line}')
        bad = rotted or unknown or wrapped
        print(f'OMISSIONS: {"stale" if bad else "current"} '
              f'({len(OMISSIONS)} js, {len(API_OMISSIONS)} api)')
        names = one_name(every)
        for line in names:
            print(f'  {line}')
        print(f'one name per C call: {"broken" if names else "holds"}')
        return 1 if bad or names else 0

    print(f'wgrender: {len(every)} public functions across {len(by_header)} headers\n')
    print(f'  {"C surface (generated)":<28} hxcpp {len(raw["hxcpp"]):>3}/{len(every)}   '
          f'js {len(raw["js"]):>3}/{len(every)}')
    print(f'  {"API layer (hand-written)":<28} {len(api):>9}/{len(every)}\n')

    print(f'{"header":<22} {"calls":>5} {"wrapped":>8}')
    for header, fns in sorted(by_header.items(), key=lambda kv: (len(kv[1] & api) / len(kv[1]), -len(kv[1]))):
        done = len(fns & api)
        print(f'{header:<22} {len(fns):>5} {done:>8}  {"#" * round(done / len(fns) * 10):<10} '
              f'{done / len(fns) * 100:>3.0f}%')

    skip = macros() | OMISSIONS.keys() | API_OMISSIONS.keys()
    rows = []
    for c in sorted((WGRENDER / 'examples').glob('*.c')):
        need = headers.calls_in(c, WGRENDER, include=[WGRENDER / 'examples', WGRENDER / 'deps/clay'],
                                tool='check_coverage') - skip - api
        rows.append((len(need), c.stem, sorted(need)))
    rows.sort()
    print(f'\nwgrender has {len(rows)} examples; what each still needs from the API layer:')
    for band, lo, hi in (('nothing', 0, 0), ('1-5', 1, 5), ('6-10', 6, 10), ('11+', 11, 10 ** 6)):
        names = [n for k, n, _ in rows if lo <= k <= hi]
        print(f'  {band:<8} {len(names):>2}  {", ".join(names)}')

    blockers = {}
    for _, _, need in rows:
        for fn in need:
            blockers.setdefault(re.match(r'wgr_([a-z0-9]+)', fn).group(1), set()).add(fn)
    ranked = sorted(blockers.items(), key=lambda kv: -sum(1 for _, _, n in rows if kv[0] in {
        re.match(r'wgr_([a-z0-9]+)', f).group(1) for f in n}))
    print('\nwrapping these unblocks the most examples:')
    for sub, fns in ranked[:6]:
        blocks = sum(1 for _, _, need in rows if any(f.startswith(f'wgr_{sub}') for f in need))
        print(f'  {sub:<12} blocks {blocks:>2} examples, {len(fns):>2} calls unwrapped')

    missing_js = sorted(raw['hxcpp'] - raw['js'])
    unexplained = [fn for fn in missing_js if fn not in OMISSIONS]
    print(f'\non hxcpp but not js ({len(missing_js)}), all accounted for'
          f'{"" if not unexplained else f" except {len(unexplained)}: " + ", ".join(unexplained)}:')
    for fn, why in sorted(OMISSIONS.items()):
        print(f'  {fn:<32} {why}')

    print(f'\nreachable but deliberately not wrapped ({len(API_OMISSIONS)}):')
    for fn, why in sorted(API_OMISSIONS.items()):
        print(f'  {fn:<32} {why}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
