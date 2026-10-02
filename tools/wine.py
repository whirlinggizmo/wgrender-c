"""Which Wine runs a Windows program here: $WINE if set, else wine64 or wine on PATH, else
the newest Proton in a Steam library (its files/bin/wine; Proton is Valve's Wine,
installed from Steam's Library > Tools).

    import wine
    wine.find()   # a path, or None

Steam lists its libraries in libraryfolders.vdf, Valve's KeyValues format: what this
reads of it is each library's "path". Standard library only.
"""
import os
import re
import shutil
from pathlib import Path


def version_key(path):
    """'Proton 11.0' after 'Proton 9.0'; Experimental and the like before numbers."""
    return [int(n) for n in re.findall(r'\d+', path.parent.parent.parent.name)]


def find():
    """The Wine to run with, or None."""
    if os.environ.get('WINE'):
        return os.environ['WINE']
    for name in ('wine64', 'wine'):
        if shutil.which(name):
            return shutil.which(name)
    home = Path.home()
    libraries = [home / '.local/share/Steam', home / '.steam/steam']
    vdf = home / '.local/share/Steam/steamapps/libraryfolders.vdf'
    if vdf.exists():
        libraries += [Path(p) for p in re.findall(r'"path"\s*"([^"]*)"', vdf.read_text(errors='replace'))]
    found = {w for lib in libraries for w in lib.glob('steamapps/common/Proton*/files/bin/wine')
             if os.access(w, os.X_OK)}
    return str(max(found, key=version_key)) if found else None


