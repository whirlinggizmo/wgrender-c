"""Where a build lives: build/<platform>/<variant>/, from the preset <platform>-<variant>.

The layout is the wg* family's (CONVENTIONS.md, "Build directories"): the platform is
where the output runs (linux, macos, windows, web), the variant everything else. The
tools here import this rather than spell a directory.
"""
import platform
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# this machine's platform, as the presets name it
HOST = {'Linux': 'linux', 'Darwin': 'macos', 'Windows': 'windows'}.get(platform.system(), 'linux')


def native(variant='release'):
    """This machine's preset for a variant: release, debug, headless, or a sanitizer.
    On Windows that's MSVC's (windows-msvc, windows-msvc-debug, ...)."""
    if HOST != 'windows':
        return f'{HOST}-{variant}'
    return 'windows-msvc' + ('' if variant == 'release' else f'-{variant}')


def web(backend='webgl2', threads=True, debug=False):
    """The web preset for a backend, with or without threads, release or debug."""
    return f'web-{backend}' + ('' if threads else '-nothreads') + ('-debug' if debug else '')


def directory(preset):
    """A preset's build directory: web-webgl2-nothreads is build/web/webgl2-nothreads."""
    platform_name, variant = preset.split('-', 1)
    return ROOT / 'build' / platform_name / variant


def preset_of(path):
    """The preset that builds a directory: the inverse of directory()."""
    path = Path(path).resolve()
    return f'{path.parent.name}-{path.name}'
