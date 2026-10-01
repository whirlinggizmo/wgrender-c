"""Where a build lives, from its preset <platform>-<variant>: what it makes in
out/<platform>/<variant>/ (the library in lib/, programs in bin/, a web build's site in
site/), its work (CMake's cache and objects, the tools' byproducts) in build/<preset>/.

The platform is what a program links against: linux-x64, macos-arm64,
windows-x64-msvc, windows-x64-mingw, wasm32. The variant is the configuration, release
or debug, then what the build adds, in order: a backend other than the platform's
default (webgpu), options (headless, threads), a sanitizer (tsan, asan, ubsan). So
linux-x64-debug-headless, wasm32-release, wasm32-release-webgpu-threads. The tools here
import this rather than spell a directory.
"""
import platform
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# every platform a preset names; a preset is one of these, '-', and its variant
PLATFORMS = ('linux-x64', 'macos-arm64', 'windows-x64-msvc', 'windows-x64-mingw', 'wasm32')

# this machine's platform, as the presets name it (MSVC's on Windows)
HOST = {'Linux': 'linux-x64', 'Darwin': 'macos-arm64', 'Windows': 'windows-x64-msvc'}.get(
    platform.system(), 'linux-x64')


def native(variant='release'):
    """This machine's preset for a variant: release, debug, debug-headless, debug-tsan..."""
    return f'{HOST}-{variant}'


def web(backend='webgl2', threads=False, debug=False):
    """The web preset for a backend, with or without threads, release or debug."""
    return ('wasm32-' + ('debug' if debug else 'release') + ('' if backend == 'webgl2' else f'-{backend}')
            + ('-threads' if threads else ''))


def split(preset):
    """(platform, variant) of a preset: wasm32-release-threads is (wasm32, release-threads)."""
    for name in PLATFORMS:
        if preset.startswith(name + '-'):
            return name, preset[len(name) + 1:]
    raise ValueError(f'{preset}: not a preset (a platform is one of {", ".join(PLATFORMS)})')


def out(preset):
    """What a preset makes: wasm32-release's is out/wasm32/release."""
    platform_name, variant = split(preset)
    return ROOT / 'out' / platform_name / variant


def lib(preset):
    """The directory a preset's library is in: out/<platform>/<variant>/lib."""
    return out(preset) / 'lib'


def programs(preset):
    """Where a preset's programs are: out/<platform>/<variant>/bin, or on the web the
    site, out/wasm32/<variant>/site."""
    return out(preset) / ('site' if preset.startswith('wasm32-') else 'bin')


def work(preset):
    """A preset's work (CMake's cache, the objects), and the tools' own byproducts for
    it (webcheck's screenshots, websize's table): build/wasm32-release."""
    split(preset)
    return ROOT / 'build' / preset


def preset_of(path):
    """The preset that makes a directory: out/<platform>/<variant>, its lib/, bin/ or
    site/, or build/<preset>. The inverse of out(), programs() and work()."""
    path = Path(path).resolve()
    if path.name in ('lib', 'bin', 'site'):
        path = path.parent
    if path.parent.name == 'build':
        return path.name
    preset = f'{path.parent.name}-{path.name}'
    split(preset)
    return preset
