#!/usr/bin/env python3
"""System build dependencies for the Linux desktop build.

sokol links against the platform's audio, GL and windowing libraries, which can't be
vendored under deps/: they come from the system package manager.

    tools/deps.py check     report missing dev packages; exit non-zero if any
    tools/deps.py install   install them with apt, dnf or pacman (uses sudo)

A Linux desktop configure runs the check. Windows and macOS need nothing beyond their
compiler (system libraries and frameworks), so both modes do nothing there; nor does a
headless build, which links none of these.
"""
import platform
import shutil
import subprocess
import sys

# the pkg-config modules sokol needs on Linux (mk/build.json's desktop linux libs)
MODULES = ['alsa', 'gl', 'x11', 'xi', 'xcursor', 'xrandr']

PACKAGES = {
    'apt': {'pkg-config': 'pkg-config', 'alsa': 'libasound2-dev', 'gl': 'libgl-dev',
            'x11': 'libx11-dev', 'xi': 'libxi-dev', 'xcursor': 'libxcursor-dev',
            'xrandr': 'libxrandr-dev'},
    'dnf': {'pkg-config': 'pkgconf-pkg-config', 'alsa': 'alsa-lib-devel', 'gl': 'mesa-libGL-devel',
            'x11': 'libX11-devel', 'xi': 'libXi-devel', 'xcursor': 'libXcursor-devel',
            'xrandr': 'libXrandr-devel'},
    'pacman': {'pkg-config': 'pkgconf', 'alsa': 'alsa-lib', 'gl': 'libglvnd', 'x11': 'libx11',
               'xi': 'libxi', 'xcursor': 'libxcursor', 'xrandr': 'libxrandr'},
}
INSTALL = {
    'apt': ['sudo', 'apt-get', 'install', '-y'],
    'dnf': ['sudo', 'dnf', 'install', '-y'],
    'pacman': ['sudo', 'pacman', '-S', '--needed', '--noconfirm'],
}
HINT = {'apt': 'sudo apt install', 'dnf': 'sudo dnf install', 'pacman': 'sudo pacman -S --needed'}


def package_manager():
    for pm, tool in (('apt', 'apt-get'), ('dnf', 'dnf'), ('pacman', 'pacman')):
        if shutil.which(tool):
            return pm
    return None


def missing():
    if not shutil.which('pkg-config'):
        return ['pkg-config', *MODULES]
    return [m for m in MODULES if subprocess.run(['pkg-config', '--exists', m]).returncode != 0]


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else 'check'
    if mode not in ('check', 'install'):
        sys.exit(__doc__)
    if platform.system() != 'Linux':
        return
    absent = missing()
    pm = package_manager()
    packages = [PACKAGES[pm][m] for m in absent] if pm else []
    if not absent:
        if mode == 'install':
            print('wgrender: all system build dependencies present')
        return
    if mode == 'check':
        print(f'wgrender: missing system build dependencies: {" ".join(absent)}', file=sys.stderr)
        if pm:
            print(f'  install: {HINT[pm]} {" ".join(packages)}', file=sys.stderr)
        else:
            print(f'  install the development packages for: {" ".join(absent)}', file=sys.stderr)
        print('  or run: python3 tools/deps.py install', file=sys.stderr)
        sys.exit(1)
    if not pm:
        sys.exit(f'wgrender: unsupported package manager; install dev packages for: {" ".join(absent)}')
    print(f'wgrender: installing {" ".join(packages)}')
    sys.exit(subprocess.run([*INSTALL[pm], *packages]).returncode)


if __name__ == '__main__':
    main()
