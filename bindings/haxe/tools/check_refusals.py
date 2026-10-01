#!/usr/bin/env python3
"""Every way a wgrender call can return false, read from the C with clang.

    tools/check_refusals.py                       the report
    tools/check_refusals.py --check               the gate: a refusal a header names and
                                             the binding's docs do not
    tools/check_refusals.py --json                the report, as JSON on stdout
    tools/check_refusals.py --require-clang       fail rather than skip when clang is missing

Two halves, both about refusals. The report reads the C. The check reads prose
against prose: AGENTS.md makes a header's "false for ..." a contract, so a binding
that does not repeat the sentence is dropping what it was told.

This replaces tools/setters.py, which asked a question that no longer has subjects.
Its rule was "a property is right when every way the call can fail is one wgrender
logs, and a method returning Bool is right when it can fail silently" -- which only
matters while there are properties. There is one left, and it has no C call behind
it. What made that tool 388 lines was inferring which C call a member wrapped, since
`wgr_model_set_tint -> Model.tint` cannot be read off a name; flattening put each C
call in the body of exactly one member (tools/check_coverage.py --check holds that), so the
call is read off the body, and the inference, the DELEGATED table of hand-written
verdicts and the regex reader of C control flow all went with it.

This replaces the regular expressions that tools/setters.py used to read wgrender's
control flow with. Those could not be trusted -- they produced one false clean (a
guard written with braces), two false positives (an allocation check read as a
refusal), and could not read wgr_window.c at all, which is why setters.py grew a
DELEGATED table of hand-written verdicts. An AST has none of those failure modes:
a condition is a condition whatever it is spelled like.

clang comes from emsdk, which the web build already needs -- `emcc` on PATH leads to
it. There is no regex fallback on purpose: a second implementation that disagrees
with the first is worse than no answer, and the regex was the flaky one. Without
clang this exits 0 and says it is skipping, and CI (which builds the web target, so
always has emsdk) is what makes sure the step actually runs before main moves.

A file that fails to parse is a hard error, never a skip. Under `2>/dev/null` an
earlier version of this reported a clean audit over 44 of 46 sources, because
compile_flags.txt was missing -Ideps/sokol_utils. Silently reading less of the
library than you think is the exact failure this tool exists to end.
"""
import json
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from wgrpath import WGRENDER  # noqa: E402
import headers  # noqa: E402  (wgrender's tools/headers.py)
import members  # noqa: E402
import cli  # noqa: E402

if __name__ == '__main__':
    cli.parse(__doc__, ('--check', '--json', '--require-clang'), positional=0)

ROOT = Path(__file__).resolve().parent.parent


REFUSAL = re.compile(
    r'\bfalse\b[^.;]{0,80}?\b(for|when|outside|below|above|if|unless)\b'
    r'|\brefus(e|es|ed|ing)\b|\bnot accepted\b', re.I)

SAYS_SO = re.compile(r'\brefus(e|es|ed|ing)\b|\bfalse\b|\bkeeps? what it had\b'
                     r'|\bignored\b|\bnot a \w+\b', re.I)


# Refusals the binding's types make unreachable, so documenting them would be noise
# rather than accuracy: an `enum abstract` with two values cannot produce the third
# that wgrender would reject. This is the only exception list left -- the tool this
# replaced also needed DELEGATED (verdicts it could not read from the C) and ACCEPTED
# (properties that swallowed a refusal anyway), and both went with the regex reader.
UNREACHABLE = {
    'wgr_camera3d_set_projection': 'Projection: the two values it refuses outside are all there are',
    'wgr_material_set_shading': 'MaterialShading: same',
    'wgr_sprite3d_set_facing': 'SpriteFacing: same',
    'wgr_text3d_set_facing': 'SpriteFacing: same',
    'wgr_text2d_set_align': "AlignX and AlignY: wgrender refuses one axis's value passed for "
                            'the other, and the two abstracts make that unrepresentable '
                            'rather than a runtime false',
    'wgr_text3d_set_align': 'AlignX and AlignY: same',
}


def documented_refusals(wgrender):
    """Every bool wgr_* whose header comment names a refusal, with that sentence: the
    comment clang attaches to the declaration (tools/headers.py)."""
    api = headers.read(wgrender, tool='check_refusals')
    return {name: ' '.join(f.doc.split()) for name, f in api.functions.items()
            if f.returns == 'bool' and f.doc and REFUSAL.search(f.doc)}


def binding(root):
    """{c_name: "Module.member"}, and each member's doc -- from tools/members.py.

    The one-name rule (tools/check_coverage.py --check) makes this a function: each C call has
    one public member, so its doc is the one place a refusal has to be repeated. Overloads
    share a name, so their docs are read together. The index follows bodies, not first
    lines: the one-line regex this used before missed every wrapper with a default or a
    `#if`, the same silent loss the tool before it was replaced for.
    """
    out, docs = {}, {}
    for m in members.members(root):
        if not m.public:
            continue
        for c in m.calls:
            out.setdefault(c, m.qualified)
        docs[m.qualified] = docs.get(m.qualified, '') + m.doc
    return out, docs


def check(root, wgrender):
    """Fail when a refusal a header names is not repeated in the binding's docs."""
    documented = documented_refusals(wgrender)
    where, docs = binding(root)
    missing = []
    for c_name, sentence in sorted(documented.items()):
        member = where.get(c_name)
        if member is None:
            continue  # not wrapped; check_coverage.py answers for that
        if c_name in UNREACHABLE:
            continue
        if not SAYS_SO.search(docs.get(member, '')):
            missing.append((member, c_name, sentence))
    for member, c_name, sentence in missing:
        print(f"  {member}: {c_name}'s header names a refusal the doc comment does not")
        print(f'      header: {sentence[:120]}')
    stale = [n for n in UNREACHABLE if n not in where]
    for n in stale:
        print(f'  {n}: listed as unreachable, but the binding does not wrap it')
    print(f'refusals: {"stale" if missing or stale else "current"} '
          f'({len(documented)} documented, {len(missing)} not repeated; '
          f'{len(where)} calls wrapped, {len(UNREACHABLE)} unreachable by type)')
    return 1 if missing or stale else 0


def find_clang():
    """clang, or None: tools/headers.py's, which every tool that reads the C shares."""
    return headers.find_clang()


def flags(wgrender):
    """compile_flags.txt, one argument per line, as clangd reads it."""
    path = wgrender / 'compile_flags.txt'
    if not path.exists():
        sys.exit(f'no compile_flags.txt in {wgrender}')
    return [line for line in path.read_text(encoding='utf-8').splitlines() if line.strip()]


def walk(node):
    yield node
    for kid in node.get('inner') or ():
        yield from walk(kid)


def fill_files(node, current=None):
    """Carry the last-seen file forward, which is what clang's JSON means.

    A `loc` or `range` only names its file when it *changes* from the node dumped
    before it; everything after inherits. Reading each node in isolation therefore
    finds no file on almost every node, which is not "unknown" but "the same as the
    last one". Getting this wrong cost a run that reported 250 functions and zero
    refusals, all of them silently dropped for want of a filename.
    """
    for key in ('loc', 'range'):
        spot = node.get(key)
        if not spot:
            continue
        ends = [spot] if key == 'loc' else [spot.get('begin'), spot.get('end')]
        for end in ends:
            if not isinstance(end, dict):
                continue
            # A location inside a macro is nested: clang gives spellingLoc (where the
            # macro is defined) and expansionLoc (where it was written). Both inherit
            # the running file the same way the flat form does.
            for spot2 in (end, end.get('spellingLoc'), end.get('expansionLoc')):
                if not isinstance(spot2, dict):
                    continue
                if spot2.get('file'):
                    current = spot2['file']
                elif current:
                    spot2['file'] = current
    for kid in node.get('inner') or ():
        current = fill_files(kid, current)
    return current


def ast_of(clang, wgrender, source, args):
    """The wgr_* declarations in one translation unit, as JSON.

    -ast-dump-filter keeps the output to what we asked about: the whole library is
    68 MB unfiltered and a second and a half filtered, because the cost was always
    serialising the AST rather than building it.
    """
    result = subprocess.run(
        [clang, '-Xclang', '-ast-dump=json', '-Xclang', '-ast-dump-filter=wgr_',
         '-fsyntax-only', '-fparse-all-comments', *args, str(source)],
        cwd=wgrender, capture_output=True, text=True)
    if result.returncode != 0:
        first = next((line for line in result.stderr.splitlines() if 'error:' in line), '')
        sys.exit(f'refusals: clang could not parse {source}\n  {first}\n'
                 '  This is never skipped -- fix the parse, or the audit is reading '
                 'less of the library than it reports.')
    # -ast-dump-filter emits one JSON object per match, concatenated
    decoder, out, text, at = json.JSONDecoder(), [], result.stdout, 0
    while at < len(text):
        while at < len(text) and text[at].isspace():
            at += 1
        if at >= len(text):
            break
        node, at = decoder.raw_decode(text, at)
        fill_files(node, str(source))
        out.append(node)
    return out


def where(spot):
    """A location's file and offset, looking through a macro expansion.

    `param == NULL` ends on a macro, so clang gives no flat offset -- only a nested
    expansionLoc (where it was written) and spellingLoc (where the macro is defined).
    The expansion is the one that points at the source a reader is looking at.
    """
    if not isinstance(spot, dict):
        return None, None, None
    for candidate in (spot, spot.get('expansionLoc'), spot.get('spellingLoc')):
        if isinstance(candidate, dict) and candidate.get('offset') is not None:
            return candidate.get('file'), candidate['offset'], candidate.get('tokLen', 0)
    return None, None, None


def source_text(cache, wgrender, node):
    """The source a node spans, from its byte offsets, or None."""
    begin_file, begin_off, _ = where(node.get('range', {}).get('begin'))
    end_file, end_off, end_len = where(node.get('range', {}).get('end'))
    if begin_off is None or end_off is None:
        return None
    begin = {'offset': begin_off}
    end = {'offset': end_off, 'tokLen': end_len}
    name = begin_file or end_file
    if name is None:
        return None
    if name not in cache:
        path = Path(name) if Path(name).is_absolute() else wgrender / name
        if not path.exists():
            return None
        cache[name] = path.read_bytes()
    blob = cache[name]
    return ' '.join(blob[begin['offset']:end['offset'] + end.get('tokLen', 0)]
                    .decode('utf8', 'replace').split())


def logs(node):
    """Whether this branch says anything before refusing.

    The rule a binding cares about is not "can this fail" but "can it fail in
    silence": a guard that log_warns is one the program finds out about whether or
    not the binding hands the bool back. The regex this replaces inferred that from
    the text between the condition and the return, which is why it needed the guard
    written in exactly one shape.
    """
    for call in (n for n in walk(node) if n.get('kind') == 'CallExpr'):
        for ref in (n for n in walk(call) if n.get('kind') == 'DeclRefExpr'):
            name = (ref.get('referencedDecl') or {}).get('name') or ''
            # log_warn and friends are macros over wgr_logger_*, so the AST -- which
            # sees the expansion, not the spelling -- never contains a "log_warn".
            if name.startswith('wgr_logger_') or name.startswith('wgri_log'):
                return True
    return False


def returns_false(node):
    """Whether this statement can return false without returning anything else."""
    for ret in (n for n in walk(node) if n.get('kind') == 'ReturnStmt'):
        literals = [n for n in walk(ret) if n.get('kind') == 'IntegerLiteral']
        if literals and literals[0].get('value') == '0':
            return True
    return False


def refusals_in(cache, wgrender, fn):
    """Each `if (cond) ... return false` in a function body, as the condition's source."""
    out = []
    for node in walk(fn):
        if node.get('kind') != 'IfStmt':
            continue
        inner = node.get('inner') or []
        if len(inner) < 2:
            continue
        cond, then = inner[0], inner[1]
        if not returns_false(then):
            continue
        text = source_text(cache, wgrender, cond)
        silent = not logs(then)
        if text is None:
            # Dropping this quietly is the false clean this tool exists to end: it
            # already happened once, when a condition ending on NULL had no flat
            # offset and wgr_material_set_texture lost a branch without a word.
            sys.exit(f'refusals: {fn.get("name")} has a branch returning false whose '
                     'condition could not be read from the source. Refusing to report '
                     'a partial answer.')
        out.append({'cond': text, 'logged': not silent})
    return out


def returned_expressions(cache, wgrender, fn):
    """Returns whose value is not a literal, so the answer comes from somewhere else.

    wgr_window_set_fullscreen has no `if` at all:

        return wgri_platform_set_fullscreen(fullscreen) || unsupported(&logged, ...);

    It refuses on every platform without fullscreen, and a tool that only reads
    `if (cond) return false` calls that "never returns false" -- which is worse than
    saying nothing, because it is confidently wrong. These are reported separately
    rather than guessed at: the tool says where the answer comes from, and a reader
    follows it.
    """
    out = []
    for ret in (n for n in walk(fn) if n.get('kind') == 'ReturnStmt'):
        inner = ret.get('inner') or []
        if not inner:
            continue
        value = inner[0]
        while value.get('kind') in ('ImplicitCastExpr', 'ParenExpr') and (value.get('inner') or []):
            value = value['inner'][0]
        if value.get('kind') == 'IntegerLiteral':
            continue  # a literal 0 is an outright refusal, caught as a branch above
        text = source_text(cache, wgrender, value)
        if text and text not in out:
            out.append(text)
    return out


def comment_of(fn):
    parts = [n['text'].strip() for n in walk(fn)
             if n.get('kind') == 'TextComment' and n.get('text')]
    return ' '.join(p for p in parts if p) or None


def collect(clang, wgrender):
    """Every bool-returning wgr_* definition, with its comment and its refusals."""
    args = flags(wgrender)
    cache, api = {}, {}
    for source in sorted((wgrender / 'src').glob('wgr_*.c')):
        for node in ast_of(clang, wgrender, source.relative_to(wgrender), args):
            for fn in (n for n in walk(node)
                       if n.get('kind') == 'FunctionDecl' and n.get('name', '').startswith('wgr_')):
                name = fn['name']
                entry = api.setdefault(name, {'name': name, 'returns': None, 'comment': None,
                                              'refuses': None, 'returns_expr': [], 'file': None})
                entry['returns'] = entry['returns'] or fn.get('type', {}).get('qualType')
                entry['comment'] = entry['comment'] or comment_of(fn)
                if any(k.get('kind') == 'CompoundStmt' for k in (fn.get('inner') or ())):
                    entry['file'] = str(source.relative_to(wgrender))
                    entry['refuses'] = refusals_in(cache, wgrender, fn)
                    entry['returns_expr'] = returned_expressions(cache, wgrender, fn)
    return {n: e for n, e in api.items()
            if e['returns'] and e['returns'].startswith('bool') and e['refuses'] is not None}


def main():
    clang = find_clang()
    if clang is None and '--check' in sys.argv:
        # The check reads the headers' doc comments, through clang too
        headers.require_clang('check_refusals')
    if clang is None:
        if '--require-clang' in sys.argv:
            # What CI passes. A skip that can happen everywhere is not a gate, so the
            # one place clang is guaranteed is the one place its absence is fatal.
            sys.exit('refusals: no clang, and --require-clang was given. emsdk provides '
                     'one; `emcc` on PATH or $EMSDK leads to it.')
        print('refusals: no clang found (emsdk provides one; `emcc` on PATH leads to it)')
        print('          skipping -- CI builds the web target, so it runs there')
        return 0
    wgrender = WGRENDER
    if '--check' in sys.argv:
        return check(ROOT, wgrender)
    api = collect(clang, wgrender)

    if '--json' in sys.argv:
        json.dump(api, sys.stdout, indent=2, sort_keys=True)
        print()
        return 0

    refusing = {n: e for n, e in api.items() if e['refuses'] or e['returns_expr']}
    branch = sum(1 for e in api.values() if e['refuses'])
    indirect = sum(1 for e in api.values() if e['returns_expr'] and not e['refuses'])
    print(f'{len(api)} bool wgr_* functions, read from the C with clang '
          f'({Path(clang).name}).')
    print(f'  {branch} refuse on a branch this can read')
    print(f'  {indirect} hand the answer to another call, which a reader has to follow')
    print(f'  {len(api) - len(refusing)} return true on every path\n')
    for name, entry in sorted(refusing.items()):
        print(f'{name}  ({entry["file"]})')
        for item in entry['refuses']:
            mark = '' if item['logged'] else '   (silent)'
            print(f'    false when   {item["cond"]}{mark}')
        for expr in entry['returns_expr']:
            print(f'    answer from  {expr}')
    never = sorted(n for n, e in api.items() if not e['refuses'] and not e['returns_expr'])
    if never:
        print(f'\n{len(never)} return true on every path:')
        for name in never:
            print(f'  {name}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
