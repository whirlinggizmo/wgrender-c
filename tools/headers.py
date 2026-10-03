"""wgrender's public API as clang reads it: every function, enum, struct, typedef and
#define that include/*.h declares, with the doc comment above each. The tools that need
to know what's in the headers ask this, never the headers' text (AGENTS.md: no tool
scans source).

    import headers
    api = headers.read()           # exits saying why when there's no clang
    api.functions['wgr_model_create'].params   # [Param(name='mesh', type='wgr_handle_t', ...)]

One translation unit includes every public header, and clang parses it: its JSON AST
for the declarations (-fparse-all-comments attaches each comment to what it precedes),
and its preprocessor (-E -dD) for the #defines, which the AST doesn't keep. Types are
clang's spelling (`const char *`, `wgr_handle_t`), arrays resolved to a count.

clang is emsdk's (the one the web build uses: `emcc` on PATH, or $EMSDK), else a clang
on PATH. Standard library only.
"""
import json
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


@dataclass
class Param:
    name: str
    type: str        # clang's spelling: 'const char *', 'wgr_handle_t', 'vec3_t'
    canonical: str   # through every typedef: 'unsigned int', 'struct vec3_t'


@dataclass
class Function:
    name: str
    returns: str
    params: list     # [Param]; empty for (void)
    header: str      # 'wgr_model.h'
    doc: str         # the comment above it, as text ('' when none)
    variadic: bool = False


@dataclass
class Field:
    name: str
    type: str        # the element type for an array: 'int' of `int keys[256]`
    count: int       # 0 when it isn't an array


@dataclass
class Struct:
    name: str
    fields: list     # [Field]
    header: str
    doc: str


@dataclass
class Enum:
    name: str
    values: dict     # {'WGR_LIGHT_SPOT': 2, ...} in declaration order
    header: str
    doc: str


@dataclass
class Api:
    functions: dict = field(default_factory=dict)   # name -> Function, in header order
    structs: dict = field(default_factory=dict)     # typedef name -> Struct
    enums: dict = field(default_factory=dict)       # typedef name -> Enum
    typedefs: dict = field(default_factory=dict)    # name -> (type, header): every other typedef
    defines: dict = field(default_factory=dict)     # name -> (value text, header): object-like
    values: dict = field(default_factory=dict)      # name -> int: the defines that are integer
                                                    # constants, as clang evaluates them
    macros: dict = field(default_factory=dict)      # name -> header: function-like (wgr_log_debug)
    headers: list = field(default_factory=list)     # every public header, sorted


def find_clang():
    """clang, or None. emsdk's is the one the web build already uses.

    `clang` on Windows is `clang.exe`, and the emsdk lookups build a path by hand rather
    than going through PATH, so they try the suffix themselves; shutil.which reads
    PATHEXT."""
    def at(directory):
        for leaf in ('clang', 'clang.exe'):
            candidate = directory / leaf
            if candidate.exists():
                return str(candidate)
        return None

    emcc = shutil.which('emcc')
    if emcc:
        found = at(Path(emcc).resolve().parent.parent / 'bin')
        if found:
            return found
    for name in ('clang', 'clang-23', 'clang-22', 'clang-21', 'clang-20', 'clang-19', 'clang-18'):
        found = shutil.which(name)
        if found:
            return found
    emsdk = os.environ.get('EMSDK')
    if emsdk:
        return at(Path(emsdk) / 'upstream' / 'bin')
    return None


def require_clang(tool):
    clang = find_clang()
    if clang is None:
        sys.exit(f'{tool}: needs clang to read the headers (emsdk brings one: `emcc` on PATH, '
                 'or $EMSDK; or a clang on PATH)')
    return clang


def _unit(include):
    return ''.join(f'#include "{h.name}"\n' for h in sorted(include.glob('*.h')))


def _run(clang, args, source, cwd):
    done = subprocess.run([clang, *args, '-x', 'c', '-'], input=source, cwd=cwd,
                          capture_output=True, text=True)
    if done.returncode != 0:
        first = next((line for line in done.stderr.splitlines() if 'error' in line), done.stderr.strip())
        sys.exit(f'headers: clang could not parse include/*.h\n  {first}')
    return done.stdout


def _walk_files(node, current=None):
    """Carry the last file clang named forward through the dump, which is what its JSON
    means: a location omits the file when it's the one before it. Sets '_file' on every
    node that has a location."""
    def files_in(value):
        if isinstance(value, dict):
            if 'file' in value:
                yield value['file']
            for key in ('spellingLoc', 'expansionLoc', 'begin', 'end'):
                if key in value:
                    yield from files_in(value[key])

    for key in ('loc', 'range'):
        for f in files_in(node.get(key, {})):
            current = f
    if 'loc' in node:
        node['_file'] = current
    for kid in node.get('inner') or ():
        current = _walk_files(kid, current)
    return current


def _doc(node):
    """The comment clang attached to a declaration, as plain text."""
    def texts(n):
        if n.get('kind') == 'TextComment' and n.get('text', '').strip():
            yield n['text'].strip()
        for kid in n.get('inner') or ():
            yield from texts(kid)
    comment = next((k for k in node.get('inner') or () if k.get('kind') == 'FullComment'), None)
    return ' '.join(texts(comment)) if comment else ''


def _split_array(spelling):
    """'int[256]' -> ('int', 256); anything else -> (spelling, 0)."""
    if spelling.endswith(']') and '[' in spelling:
        base, bound = spelling[:-1].split('[', 1)
        return base.strip(), int(bound)
    return spelling, 0


def _returns(fn_type):
    """The return type in a function type's spelling: 'const char *(wgr_handle_t)' ->
    'const char *'. clang spells the parameters as the last parenthesized group."""
    depth = 0
    for i in range(len(fn_type) - 1, -1, -1):
        c = fn_type[i]
        if c == ')':
            depth += 1
        elif c == '(':
            depth -= 1
            if depth == 0:
                return fn_type[:i].strip()
    return fn_type.strip()


_cache = {}


def read(root=ROOT, clang=None, tool='headers'):
    """The public API of the wgrender at `root`, read by clang. Cached per process."""
    root = Path(root).resolve()
    if root in _cache:
        return _cache[root]
    clang = clang or require_clang(tool)
    include = root / 'include'
    source = _unit(include)
    flags = ['-std=gnu2x', f'-I{include}']  # C23's bool is a keyword, so clang spells it bool, not _Bool
    ast = json.loads(_run(clang, ['-fsyntax-only', '-fparse-all-comments', '-Xclang', '-ast-dump=json', *flags],
                          source, root))
    _walk_files(ast)
    api = Api(headers=sorted(h.name for h in include.glob('*.h')))

    def ours(node):
        f = node.get('_file') or ''
        return Path(f).name if Path(f).resolve().parent == include.resolve() else None

    tags = {}            # id -> RecordDecl / EnumDecl, for the typedefs that name them
    pending = []
    for node in ast.get('inner') or ():
        header = ours(node)
        if header is None:
            continue
        kind = node.get('kind')
        if kind in ('RecordDecl', 'EnumDecl'):
            tags[node['id']] = node
        elif kind == 'FunctionDecl':
            params = [Param(p.get('name', ''), p['type']['qualType'],
                            p['type'].get('desugaredQualType', p['type']['qualType']))
                      for p in node.get('inner') or () if p.get('kind') == 'ParmVarDecl']
            api.functions.setdefault(node['name'], Function(
                node['name'], _returns(node['type']['qualType']), params, header, _doc(node),
                variadic=bool(node.get('variadic'))))
        elif kind == 'TypedefDecl':
            pending.append((node, header))

    for node, header in pending:
        name = node['name']
        owned = None
        for kid in node.get('inner') or ():
            decl = kid.get('ownedTagDecl') or kid.get('decl')
            if decl and decl.get('id') in tags:
                owned = tags[decl['id']]
        if owned is not None and owned['kind'] == 'RecordDecl':
            fields = []
            for f in owned.get('inner') or ():
                if f.get('kind') != 'FieldDecl':
                    continue
                base, count = _split_array(f['type']['qualType'])
                fields.append(Field(f['name'], base, count))
            api.structs[name] = Struct(name, fields, header, _doc(node) or _doc(owned))
        elif owned is not None and owned['kind'] == 'EnumDecl':
            values, last = {}, -1
            for c in owned.get('inner') or ():
                if c.get('kind') != 'EnumConstantDecl':
                    continue
                value = next((int(k['value']) for k in c.get('inner') or () if 'value' in k), last + 1)
                values[c['name']] = last = value
            api.enums[name] = Enum(name, values, header, _doc(node) or _doc(owned))
        else:
            api.typedefs[name] = (node['type']['qualType'], header)

    # The #defines: the preprocessor's own listing, with the line markers that say which
    # file each came from.
    current = None
    for line in _run(clang, ['-E', '-dD', *flags], source, root).splitlines():
        if line.startswith('# ') and '"' in line:
            # a line marker: the file, its backslashes escaped (Windows); or a pseudo-file
            # such as <built-in> or <command line>, which names nothing on disk
            named = line.split('"')[1].replace('\\\\', '\\')
            current = None if named.startswith('<') else named
        elif line.startswith('#define ') and current:
            path = Path(current)
            if path.resolve().parent == include.resolve():
                name, _, value = line[len('#define '):].partition(' ')
                if '(' in name:
                    api.macros[name.split('(')[0]] = path.name
                elif not name.endswith('_H'):
                    api.defines[name] = (value.strip(), path.name)
    api.values = _evaluate(clang, flags, source, root, [n for n, (v, _) in api.defines.items() if v])
    _cache[root] = api
    return api


def _evaluate(clang, flags, source, root, names):
    """The defines among `names` that are integer constants, {name: value}, as clang
    evaluates them: each becomes an enum constant (C23's 64-bit underlying type, so a
    color's 0xF5F5F5FFu fits), and its value is read from the AST, as an enum's are.
    _Generic picks 0 for a string or a floating define and a flag says which it was, so
    nothing here reads a define's spelling as C."""
    if not names:
        return {}
    probes = []
    for i, name in enumerate(names):
        kind = (f'_Generic(({name}), char *: 0, const char *: 0, float: 0, double: 0, long double: 0, '
                f'default: 1)')
        value = f'_Generic(({name}), char *: 0, const char *: 0, float: 0, double: 0, long double: 0, default: ({name}))'
        probes.append(f'enum : long long {{ __wgr_define_is_{i} = {kind}, __wgr_define_value_{i} = {value} }};')
    out = _run(clang, ['-fsyntax-only', '-std=gnu2x', *flags, '-Xclang', '-ast-dump=json',
                       '-Xclang', '-ast-dump-filter=__wgr_define_'],
               source + '\n' + '\n'.join(probes) + '\n', root)
    # with a filter, clang prints one JSON document per declaration it matched, in a row
    decoder, documents, at = json.JSONDecoder(), [], 0
    while at < len(out):
        while at < len(out) and out[at].isspace():
            at += 1
        if at < len(out):
            document, at = decoder.raw_decode(out, at)
            documents.append(document)
    ast = documents
    found = {}

    def evaluated(node):
        # the ConstantExpr clang evaluated, under the cast to the enum's type
        for child in node.get('inner') or ():
            if 'value' in child:
                return int(child['value'])
            deeper = evaluated(child)
            if deeper is not None:
                return deeper
        return None

    def walk(node):
        if isinstance(node, dict):
            if node.get('kind') == 'EnumConstantDecl' and node.get('name', '').startswith('__wgr_define_'):
                found[node['name']] = evaluated(node)
            for child in node.get('inner') or ():
                walk(child)
        elif isinstance(node, list):
            for child in node:
                walk(child)
    walk(ast)
    return {name: found[f'__wgr_define_value_{i}'] for i, name in enumerate(names)
            if found.get(f'__wgr_define_is_{i}') == 1 and found.get(f'__wgr_define_value_{i}') is not None}


def functions_in(header, root=ROOT, include=(), clang=None, tool='headers'):
    """The functions one header outside include/ declares (a binding's own C, say), by
    name, as clang reads it alone with the public headers and `include` on the path."""
    root, header = Path(root).resolve(), Path(header).resolve()
    clang = clang or require_clang(tool)
    ast = json.loads(_run(clang, ['-fsyntax-only', '-std=gnu2x', f'-I{root / "include"}',
                                  *[f'-I{d}' for d in include], '-Xclang', '-ast-dump=json'],
                          f'#include "{header.as_posix()}"\n', root))
    _walk_files(ast)
    return {n['name'] for n in ast.get('inner') or ()
            if n.get('kind') == 'FunctionDecl' and Path(n.get('_file') or '').resolve() == header}


def calls_in(source, root=ROOT, include=(), clang=None, tool='headers'):
    """The wgr_* functions a C file's own code calls: clang's AST of it, with the public
    headers and `include` on the path, every reference from the file itself to a wgr_
    function."""
    root = Path(root).resolve()
    clang = clang or require_clang(tool)
    source = Path(source).resolve()
    done = subprocess.run([clang, '-fsyntax-only', '-std=gnu2x', f'-I{root / "include"}',
                           *[f'-I{d}' for d in include], '-Xclang', '-ast-dump=json', str(source)],
                          cwd=root, capture_output=True, text=True)
    if done.returncode != 0:
        first = next((line for line in done.stderr.splitlines() if 'error' in line), done.stderr.strip())
        sys.exit(f'headers: clang could not parse {source}\n  {first}')
    ast = json.loads(done.stdout)
    _walk_files(ast)
    found = set()

    def visit(node, here):
        here = here or Path(node.get('_file') or '').resolve() == source
        decl = node.get('referencedDecl')
        if here and decl and decl.get('kind') == 'FunctionDecl' and decl.get('name', '').startswith('wgr_'):
            found.add(decl['name'])
        for kid in node.get('inner') or ():
            visit(kid, here and not (kid.get('_file') and Path(kid['_file']).resolve() != source))
    for node in ast.get('inner') or ():
        if Path(node.get('_file') or '').resolve() == source:
            visit(node, True)
    return found


if __name__ == '__main__':
    sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
    import cli  # noqa: E402
    cli.parse(__doc__, (), positional=0)
    a = read()
    print(f'{len(a.functions)} functions, {len(a.structs)} structs, {len(a.enums)} enums, '
          f'{len(a.typedefs)} typedefs, {len(a.defines)} defines in {len(a.headers)} headers')
