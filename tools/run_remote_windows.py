#!/usr/bin/env python3
"""Build and test this checkout on a Windows machine over SSH, as it is, committed
or not, and leave nothing behind there.

    tools/run_remote_windows.py HOST [--msvc] [--variant PRESET ...] [--path DIR ...] [--keep]

Packs the working tree (every tracked file, and new ones git doesn't ignore), copies
it to HOST with scp into a scratch folder in the remote user's profile, configures,
builds, and tests each preset there (default: windows-x64-mingw-debug-headless and
windows-x64-mingw-release, or with --msvc windows-x64-msvc-debug-headless and
windows-x64-msvc-release; ctest runs where the preset has tests), and prints how it
went. So a change can be tried on real Windows before it is pushed. The MSVC presets use
Visual Studio's generator, which finds the compiler itself, so nothing is set up first. The scratch folder and the copied
files are deleted afterwards, also when a step fails, unless --keep.

HOST is an ssh destination, as `ssh HOST` takes it (a Host from ~/.ssh/config), whose
shell is cmd.exe. It needs CMake, Ninja, and Python 3.9 or later, and for MSVC Visual
Studio with its C++ tools; MinGW builds use the pinned MinGW-w64 that the toolchain file
sets up there the first time (tools/setup_mingw.py). --path puts
directories at the front of PATH for the run, for tools the machine keeps elsewhere, as
%USERPROFILE%\\... paths. Exits 0 when every preset built and passed. Ported from
libwgt's. Standard library only.
"""
import argparse
import os
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_VARIANTS = ('windows-x64-mingw-debug-headless', 'windows-x64-mingw-release')
MSVC_VARIANTS = ('windows-x64-msvc-debug-headless', 'windows-x64-msvc-release')


def working_tree():
    """Every file to send: tracked, and new ones not ignored, as git lists them."""
    listed = subprocess.run(['git', 'ls-files', '-co', '--exclude-standard', '-z'], cwd=ROOT, check=True,
                            stdout=subprocess.PIPE).stdout.decode()
    return [name for name in listed.split('\0') if name and (ROOT / name).is_file()]


def script(folder, archive, variants, paths, keep, msvc):
    """The .bat that runs on the remote machine."""
    lines = ['@echo off',
             'rem written by wgrender tools/run_remote_windows.py; it deletes itself',
             f'set ROOT=%USERPROFILE%\\{folder}',
             f'set PATH={";".join(paths + ["%PATH%"])}',
             'set FAILED=0',
             'if exist "%ROOT%" rmdir /s /q "%ROOT%"',
             'mkdir "%ROOT%"',
             'cd /d "%ROOT%"',
             f'tar -xzf "%USERPROFILE%\\{archive}"',
             'if errorlevel 1 (echo run_remote_windows: unpacking failed & set FAILED=1 & goto done)']
    for variant in variants:
        lines += [f'echo run_remote_windows: {variant}',
                  f'cmake --preset {variant} > configure-{variant}.log 2>&1',
                  f'if errorlevel 1 (type configure-{variant}.log & set FAILED=1 & goto next_{variant})',
                  # every error, not the first: Ninja's -k 0 (MSBuild goes on by itself)
                  f'cmake --build --preset {variant}' + ('' if msvc else ' -- -k 0') + f' > build-{variant}.log 2>&1',
                  f'if errorlevel 1 (findstr /c:"error:" /c:"error C" /c:"warning C" /c:"FAILED:" build-{variant}.log & set FAILED=1'
                  f' & goto next_{variant})',
                  # only the headless presets have tests
                  *([f'ctest --preset {variant}', 'if errorlevel 1 set FAILED=1'] if 'headless' in variant else []),
                  f':next_{variant}']
    lines += [':done', 'cd /d "%USERPROFILE%"']
    if not keep:
        lines += ['rmdir /s /q "%ROOT%"', f'del "%USERPROFILE%\\{archive}"']
    lines += ['(goto) 2>nul & del "%~f0" & exit /b %FAILED%']  # delete this .bat as it ends
    return '\r\n'.join(lines) + '\r\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('host')
    parser.add_argument('--msvc', action='store_true', help='build with MSVC (default: MinGW-w64)')
    parser.add_argument('--variant', action='append', dest='variants')
    parser.add_argument('--path', action='append', dest='paths', default=[])
    parser.add_argument('--keep', action='store_true', help='leave the scratch folder there, to look into it')
    args = parser.parse_args()
    variants = args.variants or list(MSVC_VARIANTS if args.msvc else DEFAULT_VARIANTS)
    tag = f'wgrender-remote-{os.getpid()}'
    archive, bat = f'{tag}.tgz', f'{tag}.bat'

    with tempfile.TemporaryDirectory() as scratch:
        local_archive, local_bat = Path(scratch) / archive, Path(scratch) / bat
        with tarfile.open(local_archive, 'w:gz') as tar:
            for name in working_tree():
                tar.add(ROOT / name, arcname=name)
        local_bat.write_bytes(script(tag, archive, variants, args.paths, args.keep, args.msvc).encode())
        print(f'run_remote_windows: {", ".join(variants)} on {args.host}', flush=True)
        copied = subprocess.run(['scp', '-q', str(local_archive), str(local_bat), f'{args.host}:'])
        if copied.returncode != 0:
            print('run_remote_windows: copying to the host failed')
            return 1
    ran = subprocess.run(['ssh', args.host, bat])
    if args.keep:
        print(f'run_remote_windows: kept in %USERPROFILE%\\{tag} on {args.host}')
    print('run_remote_windows: ' + ('PASS' if ran.returncode == 0 else 'FAIL'))
    return 0 if ran.returncode == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
