#!/usr/bin/env python3
"""Compile a custom material shader into a .skshader file (docs/PLAN-materials.md).

    tools/shaderpack.py name.glsl [-o name.skshader]

Your file has a fragment shader `@fs fs` and may have a vertex hook `@block vertex`;
shaders/sk.glsl says what libsk gives them. This puts shaders/sk.glsl in front of the
file, adds libsk's vertex shaders (static and skinned models, and sprites: instanced,
and read from a texture where there's no base instance), compiles it for every
backend libsk runs on (GL 4.1, WebGL2, WebGPU) with sokol-shdc, and writes one
.skshader file: each backend's sources, what sokol needs to know about them, and the
parameters by name. Load it with sk_shader_create(path). Only needed to make the file,
never at runtime; needs tools/sokol-shdc (see the Makefile's `shaders` target).

A fragment shader that includes sk_screen instead of sk_surface is a screen effect
(sk_render_add_effect): it gets one program, drawn over the finished frame.
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHDC = os.path.join(ROOT, "tools", "sokol-shdc")
INTERFACE = os.path.join(ROOT, "shaders", "sk.glsl")
SLANGS = ["glsl410", "glsl300es", "wgsl"]
FORMAT_VERSION = 5

FS_PARAMS_BINDING = 2  # binding 0 is libsk's vertex block, 1 its sk_frame block
VS_PARAMS_BINDING = 3
MAX_TEXTURES = 8
MAX_PARAMS = 32   # src/internal/sk_shader.h
NAME_MAX = 32
PARAM_TYPES = {  # std140: size, alignment
    "float": (4, 4),
    "int": (4, 4),
    "vec2": (8, 8),
    "vec3": (12, 16),
    "vec4": (16, 16),
}


def fail(message):
    sys.exit(f"shaderpack: {message}")


def parse_yaml(text):
    """The subset of YAML sokol-shdc writes: maps of `key: value`, and lists of maps
    ("-" alone on a line, the map indented below it)."""
    lines = [l for l in text.splitlines() if l.strip()]
    pos = 0

    def indent(line):
        return len(line) - len(line.lstrip(" "))

    def scalar(value):
        value = value.strip()
        if re.fullmatch(r"-?\d+", value):
            return int(value)
        if value in ("true", "false"):
            return value == "true"
        return value

    def block(level):
        nonlocal pos
        if lines[pos].strip() == "-":
            items = []
            while pos < len(lines) and indent(lines[pos]) == level and lines[pos].strip() == "-":
                pos += 1
                items.append(block(indent(lines[pos])))
            return items
        result = {}
        while pos < len(lines) and indent(lines[pos]) == level and lines[pos].strip() != "-":
            key, _, value = lines[pos].strip().partition(":")
            pos += 1
            if value.strip():
                result[key] = scalar(value)
            else:
                result[key] = block(indent(lines[pos]))
        return result

    return block(0)


def sections(source):
    """The text of each @fs/@vs/@block section, by (kind, name)."""
    found = {}
    for m in re.finditer(r"^@(fs|vs|block)\s+(\w+)\s*$(.*?)^@end", source, re.M | re.S):
        found[(m.group(1), m.group(2))] = m.group(3)
    return found


def params_block(text, binding, where):
    """The members of `layout(binding=N) uniform name { ... }` in `text`, with their
    std140 offsets; [] when there's no such block."""
    m = re.search(r"layout\s*\(\s*binding\s*=\s*%d\s*\)\s*uniform\s+(\w+)\s*\{(.*?)\}" % binding, text, re.S)
    if m is None:
        return None, []
    members, offset = [], 0
    body = re.sub(r"/\*.*?\*/|//[^\n]*", "", m.group(2), flags=re.S)
    for decl in [d.strip() for d in body.split(";") if d.strip()]:
        dm = re.fullmatch(r"(\w+)\s+(\w+)", decl)
        if dm is None or dm.group(1) not in PARAM_TYPES:
            fail(f"{where} parameter '{decl}': parameters are float, int, vec2, vec3 or vec4, one per line")
        kind, name = dm.group(1), dm.group(2)
        size, align = PARAM_TYPES[kind]
        offset = (offset + align - 1) // align * align
        members.append((name, kind, offset))
        offset += size
    return m.group(1), members


def main():
    args = sys.argv[1:]
    out = None
    if "-o" in args:
        i = args.index("-o")
        out = args[i + 1]
        del args[i:i + 2]
    if len(args) != 1 or not args[0].endswith(".glsl"):
        sys.exit(__doc__)
    path = args[0]
    out = out or path[: -len(".glsl")] + ".skshader"
    if not os.path.exists(SHDC):
        fail("tools/sokol-shdc is missing (see the Makefile's `shaders` target for how to get it)")

    user = open(path, encoding="utf-8").read()
    interface = open(INTERFACE, encoding="utf-8").read()
    user_sections = sections(user)
    if ("fs", "fs") not in user_sections:
        fail(f"{path}: needs a fragment shader `@fs fs` (see shaders/sk.glsl)")
    hook = "vertex" if ("block", "vertex") in user_sections else "sk_vertex_default"
    # a fragment shader that includes sk_screen is a screen effect, not a surface
    screen = re.search(r"@include_block\s+sk_screen\s*$", user_sections[("fs", "fs")], re.M) is not None
    if screen and ("block", "vertex") in user_sections:
        fail(f"{path}: a screen effect has no vertex hook (it draws over the finished frame)")

    fs_block, fs_params = params_block(user_sections[("fs", "fs")], FS_PARAMS_BINDING, "fragment")
    vs_block, vs_params = params_block(user_sections.get(("block", "vertex"), ""), VS_PARAMS_BINDING, "vertex")
    names = [p[0] for p in fs_params + vs_params]
    if any(len(n) >= NAME_MAX for n in names):
        fail(f"parameter names are at most {NAME_MAX - 1} characters")
    if len(names) > MAX_PARAMS:
        fail(f"at most {MAX_PARAMS} parameters")
    if len(set(names)) != len(names):
        fail("parameter names must differ between the vertex and fragment blocks")

    if screen:
        generated = """
@vs sk_vs_screen
@include_block sk_vs_screen_main
@end

@program screen sk_vs_screen fs
"""
    else:
        programs = []
        for kind in ("static", "skinned"):
            programs.append(f"""
@vs sk_vs_{kind}
@include_block sk_vs_{kind}_uniforms
@include_block sk_vs_outputs
@include_block {hook}
@include_block sk_vs_{kind}_main
@end
""")
        generated = "".join(programs) + """
@vs sk_vs_sprite
@include_block sk_vs_sprite_common
@include_block sk_vs_sprite_main
@end

@vs sk_vs_sprite_pulled
@include_block sk_vs_sprite_common
@include_block sk_vs_sprite_pulled_main
@end

@program static sk_vs_static fs
@program skinned sk_vs_skinned fs
@program sprite sk_vs_sprite fs
@program sprite_pulled sk_vs_sprite_pulled fs
"""
    combined = interface + "\n" + user + "\n" + generated
    user_first_line = interface.count("\n") + 2  # the user's line 1 in the combined file

    with tempfile.TemporaryDirectory(prefix="shaderpack.") as work:
        combined_path = os.path.join(work, os.path.basename(path))
        with open(combined_path, "w", encoding="utf-8") as f:
            f.write(combined)
        result = subprocess.run([SHDC, "-i", combined_path, "-o", os.path.join(work, "out"), "-l", ":".join(SLANGS),
                                 "-f", "bare_yaml"], capture_output=True, text=True)
        if result.returncode != 0:
            def remap(m):
                line = int(m.group(1))
                if line >= user_first_line:
                    return f"{path}:{line - user_first_line + 1}"
                return f"shaders/sk.glsl or the generated vertex shaders (line {line})"
            message = re.sub(re.escape(combined_path) + r":(\d+)", remap, result.stdout + result.stderr)
            sys.exit(message.strip() or "shaderpack: sokol-shdc failed")
        reflection = parse_yaml(open(os.path.join(work, "out_reflection.yaml"), encoding="utf-8").read())

        lines = [f"skshader {FORMAT_VERSION}", f"kind {'screen' if screen else 'surface'}"]
        for name, kind, offset in fs_params:
            lines.append(f"param {name} {kind} fs {offset}")
        for name, kind, offset in vs_params:
            lines.append(f"param {name} {kind} vs {offset}")
        blobs = []
        textures = None
        for shader in reflection["shaders"]:
            for program in shader["programs"]:
                check_program(shader["slang"], program, fs_block, fs_params, vs_block, vs_params)
                program_textures = sorted(v["texture"]["name"] for v in program.get("views", [])
                                          if not v["texture"]["name"].startswith("sk_"))
                if textures is None:
                    textures = program_textures
                    for name in textures:
                        lines.append(f"texture {name}")
                lines.append(f"program {program['name']} {shader['slang']}")
                lines.extend(describe(shader["slang"], program))
                for stage, key in (("vs", "vertex_func"), ("fs", "fragment_func")):
                    source = open(program[key]["path"], "rb").read()
                    lines.append(f"source {stage} {len(source)}")
                    blobs.append((len(lines), source))
        lines.append("end")

    with open(out, "wb") as f:
        at = {index: source for index, source in blobs}
        for i, line in enumerate(lines, start=1):
            f.write(line.encode() + b"\n")
            if i in at:
                f.write(at[i] + b"\n")
    print(f"shaderpack: wrote {out} ({'screen effect' if screen else 'surface shader'}, "
          f"{len(fs_params) + len(vs_params)} parameter(s), {len(textures or [])} texture(s))")


def check_program(slang, program, fs_block, fs_params, vs_block, vs_params):
    for block in program.get("uniform_blocks", []):
        slot, stage, size = block["slot"], block["stage"], block["size"]
        if slot in (0, 1, 4):
            continue  # libsk's: per object (or sprite view), per draw, sprite batch
        expected = {FS_PARAMS_BINDING: ("fragment", fs_params), VS_PARAMS_BINDING: ("vertex", vs_params)}.get(slot)
        if expected is None or stage != expected[0]:
            fail(f"uniform block binding {slot} ({stage}): parameters go in binding {FS_PARAMS_BINDING} "
                 f"(fragment) or {VS_PARAMS_BINDING} (vertex hook)")
        members = expected[1]
        end = max((offset + PARAM_TYPES[kind][0] for _, kind, offset in members), default=0)
        if (end + 15) // 16 * 16 != size:
            fail(f"{slang}: the parameter block at binding {slot} is {size} bytes; expected {(end + 15) // 16 * 16}")
    for view in program.get("views", []):
        texture = view["texture"]
        if texture["name"].startswith("sk_"):
            continue  # libsk's (the environment), at bindings 8 and 9
        if len(texture["name"]) >= NAME_MAX:
            fail(f"texture {texture['name']}: names are at most {NAME_MAX - 1} characters")
        if texture["stage"] != "fragment" or texture["type"] != "2d" or texture["slot"] >= MAX_TEXTURES:
            fail(f"texture {texture['name']}: textures are texture2D in the fragment shader, bindings 0-{MAX_TEXTURES - 1}")


def describe(slang, program):
    """What sokol needs to know about a program, one line each."""
    lines = []
    for attr in program.get("attrs", []):
        lines.append(f"attr {attr['slot']} {attr.get('glsl_name', '-')}")
    for block in program.get("uniform_blocks", []):
        glsl = (block.get("glsl_uniforms") or [{}])[0]
        lines.append(f"ub {block['slot']} {block['stage']} {block['size']} {glsl.get('glsl_name', '-')} "
                     f"{glsl.get('array_count', 0)} {block.get('wgsl_group0_binding_n', 0)}")
    for view in program.get("views", []):
        texture = view["texture"]
        lines.append(f"view {texture['slot']} {texture['stage']} {texture['name']} {texture['type']} {texture['sample_type']} "
                     f"{texture.get('wgsl_group1_binding_n', 0)}")
    for sampler in program.get("samplers", []):
        lines.append(f"sampler {sampler['slot']} {sampler['stage']} {sampler['name']} {sampler['sampler_type']} "
                     f"{sampler.get('wgsl_group1_binding_n', 0)}")
    for pair in program.get("texture_sampler_pairs", []):
        lines.append(f"pair {pair['slot']} {pair['stage']} {pair['view_slot']} {pair['sampler_slot']} "
                     f"{pair.get('glsl_name', '-')}")
    return lines


if __name__ == "__main__":
    main()
