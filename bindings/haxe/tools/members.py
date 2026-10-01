"""The API layer's members, and which C calls each one makes.

Shared by tools/check_coverage.py (the one-name rule) and tools/check_refusals.py (a refusal is
repeated in the member's doc). It reads function bodies, not first lines: an index
that only saw a member whose body was one `Raw` call lost `Camera3D.setView`,
`Input.getTouch` and every other wrapper with a default, a `#if` or a second line,
silently -- the same failure the tool before check_refusals.py was replaced for.

A body runs from its `function` to the next one in the file. That is coarse, and
enough: what lies between is a doc comment or a field, and neither calls `Raw`.
"""
import re
from dataclasses import dataclass
from pathlib import Path

FUNCTION = re.compile(
    r'(?:/\*\*((?:[^*]|\*(?!/))*)\*\*/\s*)?'              # its doc comment, if any
    r'^\t((?:@[:\w]+(?:\([^)]*\))?\s+)*)'                 # metadata
    r'((?:(?:public|private|static|inline|overload|extern|override|dynamic)\s+)*)'
    r'function\s+(\w+)', re.M)
RAW_CALL = re.compile(r'\b(?:Raw|GuestRaw)\.(wgr_[a-z0-9_]+)\s*\(')


@dataclass
class Member:
    module: str        # the Haxe type: GuestAbi.cpp.hx and GuestAbi.js.hx are both GuestAbi
    name: str
    public: bool
    doc: str
    calls: set         # the C functions it calls through Raw, directly
    file: str
    line: int

    @property
    def qualified(self):
        return f'{self.module}.{self.name}'


def members(root):
    """Every function in src/wgr, each with the C calls its body makes: its own, and
    those of the private `...Raw` members of its type it calls. An overload makes its C
    call through one (`setPositionRaw`: README, "Calling it from cppia"), and it's the
    overload a refusal has to be documented on, and the overload that is the call's
    one name."""
    out = []
    for f in sorted((Path(root) / 'src/wgr').glob('*.hx')):
        text = f.read_text(encoding='utf-8')
        found = list(FUNCTION.finditer(text))
        here, bodies = [], []
        for i, m in enumerate(found):
            end = found[i + 1].start() if i + 1 < len(found) else len(text)
            here.append(Member(
                module=f.name.split('.')[0], name=m.group(4),
                public='public' in m.group(3).split(), doc=m.group(1) or '',
                calls=set(RAW_CALL.findall(text, m.end(), end)),
                file=f.name, line=text.count('\n', 0, m.start(4)) + 1))
            bodies.append(text[m.end():end])
        helpers = {m.name: m.calls for m in here if not m.public and m.name.endswith('Raw') and m.calls}
        for m, body in zip(here, bodies):
            for name, calls in helpers.items():
                if name != m.name and re.search(r'(?<![.\w])' + name + r'\s*\(', body):
                    m.calls |= calls
        out += here
    return out


def names_by_call(found):
    """{c_name: {"Module.member", ...}} over public members: the one-name rule's subject."""
    out = {}
    for m in found:
        if m.public:
            for c in m.calls:
                out.setdefault(c, set()).add(m.qualified)
    return out
