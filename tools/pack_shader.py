#!/usr/bin/env python3
"""Compile a custom material shader into a .wgrshader file (docs/HISTORY.md, "Materials and shaders").

    tools/pack_shader.py name.glsl [-o name.wgrshader]

Your file has a fragment shader `@fs fs` and may have a vertex hook `@block vertex`;
shaders/wgr.glsl says what libwgrender gives them. This puts shaders/wgr.glsl in front of the
file, adds libwgrender's vertex shaders (static and skinned models, and sprites: instanced,
and read from a texture where there's no base instance), compiles it for every
backend libwgrender runs on (GL 4.1, WebGL2, WebGPU) with sokol-shdc, and writes one
.wgrshader file: each backend's sources, what sokol needs to know about them, and the
parameters by name. Load it with wgr_shader_create(path). Only needed to make the file,
never at runtime; sokol-shdc is downloaded the first time (tools/shdc.py).

A fragment shader that includes wgr_screen instead of wgr_surface is a screen effect
(wgr_render_add_effect): it gets one program, drawn over the finished frame.
"""
import os
import re
import subprocess
import sys
from pathlib import Path
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # an embedded Python (Windows) doesn't add it
import shdc as shader_compiler  # noqa: E402  (sokol-shdc, pinned)
import spirv  # noqa: E402  (a SPIR-V module's uniform blocks)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INTERFACE = os.path.join(ROOT, "shaders", "wgr.glsl")
SLANGS = ["glsl410", "glsl300es", "wgsl"]
FORMAT_VERSION = 8

FS_PARAMS_BINDING = 2  # binding 0 is libwgrender's vertex block, 1 its wgr_frame block
VS_PARAMS_BINDING = 3
MAX_TEXTURES = 8
MAX_PARAMS = 32   # src/internal/wgr_shader.h
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


def shdc_errors(output, temp_path, user_path, user_first_line):
    """sokol-shdc's errors and warnings, once each, with the temporary file's line numbers
    turned back into the user's (or a word that the line is libwgrender's)."""
    def remap(m):
        line = int(m.group(1))
        if line >= user_first_line:
            return f"{user_path}:{line - user_first_line + 1}"
        return f"shaders/wgr.glsl or the generated vertex shaders (line {line})"
    lines = [re.sub(re.escape(temp_path) + r":(\d+)", remap, l.strip()) for l in output.splitlines()
             if ": error" in l or ": warning" in l]
    return "\n".join(dict.fromkeys(lines)) or "shaderpack: sokol-shdc failed"


def structure(shdc, path, work, report):
    """How sokol-shdc parses `path`: {block name: input lines}, {fragment shader name: input
    lines}, the lines being where each came from in the input, with every block it
    includes expanded in. From shdc's own parse (its --dump); nothing is compiled."""
    done = subprocess.run([shdc, "-i", path, "-o", os.path.join(work, "parse"), "-t", work, "-l", "glsl410",
                           "-f", "bare_yaml", "-d"], capture_output=True, text=True)
    if done.returncode != 0:
        sys.exit(report(done.stdout + done.stderr))  # the dump is on stderr too: report picks the errors
    snippets, maps, current, section = {}, {}, None, None
    for line in done.stderr.splitlines():
        if line.startswith("  ") and not line.startswith("   ") and line.strip().endswith(":"):
            section = line.strip()[:-1]  # snippets, block_map, fs_map, ...
        elif section == "snippets":
            text = line.strip()
            if text.startswith("snippet ") and text.endswith(":"):
                current = snippets.setdefault(int(text[len("snippet "):-1]), {"lines": set()})
            elif current is not None and text.startswith("name: "):
                current["name"] = text[len("name: "):]
            elif current is not None and "(" in text and text.split("(", 1)[0].isdigit():
                current["lines"].add(int(text.split("(", 1)[1].split(")", 1)[0]))
        elif section in ("block_map", "fs_map") and " => snippet " in line:
            name, index = line.strip().split(" => snippet ")
            maps.setdefault(section, {})[name] = snippets[int(index)]["lines"]
    return maps.get("block_map", {}), maps.get("fs_map", {})


def own_lines(blocks, name):
    """A block's own lines: its lines, less those of each block it includes (any block
    whose lines it wholly contains)."""
    mine = set(blocks.get(name, ()))
    for other, lines in blocks.items():
        if other != name and lines and lines < blocks.get(name, set()):
            mine -= lines
    return mine


def parameters(work, binding, where):
    """The parameter block at `binding`, as the compiler laid it out: its name, and each
    member's (name, type, offset), from the SPIR-V sokol-shdc saved
    (--save-intermediate-spirv; tools/spirv.py). (None, []) when no shader has one."""
    for leaf in sorted(os.listdir(work)):
        if leaf.endswith(".spv"):
            with open(os.path.join(work, leaf), "rb") as f:
                found = spirv.uniform_blocks(f.read()).get(binding)
            if found:
                name, members = found
                for member, kind, _ in members:
                    if kind not in PARAM_TYPES:
                        fail(f"{where} parameter '{member}' is {kind}: parameters are float, int, vec2, vec3 or vec4")
                return name, members
    return None, []


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
    out = out or path[: -len(".glsl")] + ".wgrshader"
    shdc = str(shader_compiler.path())

    user = open(path, encoding="utf-8").read()
    interface = open(INTERFACE, encoding="utf-8").read()
    # What the file holds, as sokol-shdc parses it: whether it has a vertex hook, and
    # whether its fragment shader includes wgr_screen (a screen effect, not a surface).
    with tempfile.TemporaryDirectory(prefix="shaderpack-parse.") as parse_dir:
        parse_path = os.path.join(parse_dir, os.path.basename(path))
        with open(parse_path, "w", encoding="utf-8") as f:
            f.write(interface + "\n" + user)
        first = interface.count("\n") + 2  # the user's line 1 in this file
        blocks, fragments = structure(shdc, parse_path, parse_dir,
                                      lambda output: shdc_errors(output, parse_path, path, first))
    if "fs" not in fragments:
        fail(f"{path}: needs a fragment shader `@fs fs` (see shaders/wgr.glsl)")
    vertex_hook = "vertex" in blocks
    hook = "vertex" if vertex_hook else "wgr_vertex_default"
    screen = bool(own_lines(blocks, "wgr_screen") & fragments["fs"])
    if screen and vertex_hook:
        fail(f"{path}: a screen effect has no vertex hook (it draws over the finished frame)")

    if screen:
        generated = """
@vs wgr_vs_screen
@include_block wgr_vs_screen_main
@end

@program screen wgr_vs_screen fs
"""
    else:
        programs = []
        for kind in ("static", "skinned"):
            programs.append(f"""
@vs wgr_vs_{kind}
@include_block wgr_vs_{kind}_uniforms
@include_block wgr_vs_outputs
@include_block {hook}
@include_block wgr_vs_{kind}_main
@end
""")
        generated = "".join(programs) + """
@vs wgr_vs_sprite
@include_block wgr_vs_sprite_common
@include_block wgr_vs_sprite_main
@end

@vs wgr_vs_sprite_pulled
@include_block wgr_vs_sprite_common
@include_block wgr_vs_sprite_pulled_main
@end

@program static wgr_vs_static fs
@program skinned wgr_vs_skinned fs
@program sprite wgr_vs_sprite fs
@program sprite_pulled wgr_vs_sprite_pulled fs
"""
    combined = interface + "\n" + user + "\n" + generated
    user_first_line = interface.count("\n") + 2  # the user's line 1 in the combined file

    with tempfile.TemporaryDirectory(prefix="shaderpack.") as work:
        combined_path = os.path.join(work, os.path.basename(path))
        with open(combined_path, "w", encoding="utf-8") as f:
            f.write(combined)
        result = subprocess.run([shdc, "-i", combined_path, "-o", os.path.join(work, "out"), "-t", work,
                                 "-l", ":".join(SLANGS), "-f", "bare_yaml", "--save-intermediate-spirv"],
                                capture_output=True, text=True)
        if result.returncode != 0:
            sys.exit(shdc_errors(result.stdout + result.stderr, combined_path, path, user_first_line))
        reflection = parse_yaml(open(os.path.join(work, "out_reflection.yaml"), encoding="utf-8").read())

        # The parameters, as the compiler laid them out
        fs_block, fs_params = parameters(work, FS_PARAMS_BINDING, "fragment")
        vs_block, vs_params = parameters(work, VS_PARAMS_BINDING, "vertex")
        names = [p[0] for p in fs_params + vs_params]
        if any(len(n) >= NAME_MAX for n in names):
            fail(f"parameter names are at most {NAME_MAX - 1} characters")
        if len(names) > MAX_PARAMS:
            fail(f"at most {MAX_PARAMS} parameters")
        if len(set(names)) != len(names):
            fail("parameter names must differ between the vertex and fragment blocks")

        lines = [f"wgrshader {FORMAT_VERSION}", f"kind {'screen' if screen else 'surface'}"]
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
                                          if not v["texture"]["name"].startswith("wgr_"))
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
            continue  # libwgrender's: per object (or sprite view), per draw, sprite batch
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
        if texture["name"].startswith("wgr_"):
            continue  # libwgrender's (the environment), at bindings 8 and 9
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
    sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
    import cli  # noqa: E402  (tools/cli.py: --help, and no argument it doesn't take)
    cli.parse(__doc__, ('-o',), positional=None)
    main()
