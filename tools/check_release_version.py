#!/usr/bin/env python3
"""Reject release tags that do not match the app version or lack release notes."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
version = re.search(r'project\(Reprise VERSION ([\d.]+)', (root / 'CMakeLists.txt').read_text()).group(1)
expected = 'v' + version
if len(sys.argv) != 2 or sys.argv[1] != expected:
    raise SystemExit('Release tag must match CMakeLists.txt: ' + expected)
if not (root / 'docs/releases' / (expected + '.md')).is_file():
    raise SystemExit('Missing release notes for ' + expected)
print('Release version and notes verified: ' + expected)
