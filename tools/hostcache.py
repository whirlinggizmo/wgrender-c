#!/usr/bin/env python3
"""Where this machine keeps what the tools set up once and every build shares: the Wine
prefix, sokol-shdc, basisu. Not a build's result or its work, so neither out/ nor
build/ (whirlinggizmo/.github CONVENTIONS.md, "Build directories"), and not per
checkout: one copy per user, safe to delete, made again on first use.

    from hostcache import cache_dir
    prefix = cache_dir('wine')

$WGR_CACHE_DIR if set, else the OS's per-user cache: $XDG_CACHE_HOME or ~/.cache on
Linux, ~/Library/Caches on macOS, %LOCALAPPDATA% on Windows; in a wgrender folder there.
"""
import os
import platform
from pathlib import Path


def cache_root():
    if os.environ.get('WGR_CACHE_DIR'):
        return Path(os.environ['WGR_CACHE_DIR'])
    system = platform.system()
    if system == 'Windows':
        base = Path(os.environ.get('LOCALAPPDATA') or Path.home() / 'AppData/Local')
    elif system == 'Darwin':
        base = Path.home() / 'Library/Caches'
    else:
        base = Path(os.environ.get('XDG_CACHE_HOME') or Path.home() / '.cache')
    return base / 'wgrender'


def cache_dir(*parts):
    """A directory in the cache, made if it isn't there."""
    path = cache_root().joinpath(*parts)
    path.mkdir(parents=True, exist_ok=True)
    return path


if __name__ == '__main__':
    print(cache_root())
