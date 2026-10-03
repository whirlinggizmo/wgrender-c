"""wgrender's public structs as wasm32 lays them out, for the binding generators.

A struct returned by value crosses the wasm boundary through a pointer to its bytes, so a
binding's JS side reads each field at its offset. The offsets come from here, computed
from the fields clang reports (tools/headers.py), so no generator hand-counts them and a
layout change moves them.

Every scalar member of these structs is 4 bytes and 4-aligned (int, float, a handle, a
color), or a nested struct of them, which is what lets this be a running sum. A bool
counts 4: C pads it to the next member, which is always 4-aligned here. layout() refuses
a member type it has no size for rather than guessing.
"""

# The C types a public struct's members use, and their size on wasm32.
SIZES = {
    'bool': 4,
    'int': 4,
    'unsigned int': 4,
    'unsigned': 4,
    'float': 4,
    'wgr_handle_t': 4,
    'wgr_color_t': 4,
    'const char *': 4,
    'char *': 4,
    'void *': 4,
}


def layout(structs, name):
    """`name`'s members as (field, type, byte offset, array count), and its size in bytes.
    `structs` maps a struct's name to its fields as (type, name, count) triples."""
    offsets, at = [], 0
    for ctype, field, count in structs[name]:
        if ctype in structs:
            unit = layout(structs, ctype)[1]
        elif ctype in SIZES:
            unit = SIZES[ctype]
        else:
            raise ValueError(f'{name}.{field}: no wasm32 size for {ctype}')
        offsets.append((field, ctype, at, count))
        at += unit * max(count, 1)
    return offsets, at
