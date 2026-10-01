"""The tools' command lines: `--help` prints the tool's docstring, and an argument it
doesn't take stops it before it starts.

Most tools here read `'--check' in sys.argv` and ignore everything else, so `--help`
or a typo ran the whole thing -- a test suite or a benchmark run, minutes of work,
for a question about usage.
"""
import sys


def parse(doc, flags=(), positional=0, argv=None):
    """(the flags given, the positional arguments), or exit.

    `--help`/`-h` prints `doc` and exits 0. A flag not in `flags`, or more than
    `positional` positional arguments (None: any number), prints it and exits 2.
    """
    argv = sys.argv[1:] if argv is None else argv
    usage = (doc or '').strip()
    if '--help' in argv or '-h' in argv:
        print(usage)
        sys.exit(0)
    given = [a for a in argv if a.startswith('-')]
    rest = [a for a in argv if not a.startswith('-')]
    bad = [a for a in given if a not in flags]
    if positional is not None:
        bad += rest[positional:]
    if bad:
        print(f'{", ".join(bad)}: not an argument this takes\n\n{usage}', file=sys.stderr)
        sys.exit(2)
    return set(given), rest
