#!/usr/bin/env python3
"""Compare complete native imports, permitting only the standalone import timestamp to differ.
Usage: python3 tools/compare_captain_imports.py REPRISE_IMPORT SOURCE_IMPORT [REPORT.json]
No source/asset bytes are written into the report; its manifest pins every compared payload.
"""
import hashlib
import json
from pathlib import Path
import sys


def compare(left, right):
    a = {p.relative_to(left) for p in left.rglob('*') if p.is_file()}
    b = {p.relative_to(right) for p in right.rglob('*') if p.is_file()}
    differences, manifest = [], hashlib.sha256()
    for relative in sorted(a & b):
        x, y = (left / relative).read_bytes(), (right / relative).read_bytes()
        if relative.as_posix() == 'Opening/manifest.json':
            x, y = json.loads(x), json.loads(y)
            x.pop('imported', None)
            y.pop('imported', None)
        else:
            manifest.update(relative.as_posix().encode() + b'\0' + hashlib.sha256(x).digest())
        if x != y:
            differences.append(relative.as_posix())
    return {'files': len(a), 'only_reprise': sorted(map(str, a - b)),
            'only_source': sorted(map(str, b - a)), 'differences': differences,
            'payload_manifest_sha256': manifest.hexdigest(),
            'manifest_comparison': 'JSON equality, excluding standalone import timestamp only'}


def main():
    if len(sys.argv) not in (3, 4):
        raise SystemExit(__doc__)
    left, right = map(Path, sys.argv[1:3])
    if not all((p / 'Opening/manifest.json').is_file() and (p / 'Rom/complete').is_file()
               for p in (left, right)):
        raise SystemExit('Both inputs must be complete native imports.')
    report = compare(left, right)
    text = json.dumps(report, indent=2) + '\n'
    if len(sys.argv) == 4:
        Path(sys.argv[3]).write_text(text)
    print(text, end='')
    return int(bool(report['only_reprise'] or report['only_source'] or report['differences']))


if __name__ == '__main__':
    sys.exit(main())
