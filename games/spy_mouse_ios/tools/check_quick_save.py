#!/usr/bin/env python3
"""Checks that quick saves keep every field (game/state_io.hpp).

Every class with an io() field list must name each of its fields either in that list or in a
"Not saved:" comment next to it. The fields come from clang's view of the code (-ast-dump of
src/app/game_flow.cpp, which includes every class a quick save reaches), so a field added later
and forgotten in io() is reported.

Usage: tools/check_quick_save.py   (from the game's folder; needs clang++ on the path)
"""
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

NODE = re.compile(r"^([| `]*)-(\w+) ")
FILE = re.compile(r"[<,( ]((?:src|/)[^:<>, ]+\.(?:hpp|h|cpp|c)):\d+")
RECORD = re.compile(r"^[| `]*-CXXRecordDecl 0x\w+ (?:prev 0x\w+ )?<([^>]*)> \S+ (?:referenced )?(?:implicit )?"
                    r"(?:class|struct|union) (\w+) definition")
FIELD = re.compile(r"^[| `]*-FieldDecl 0x\w+ <.*?> \S+ (?:referenced )?(\w+) '")


def classes():
    """Classes defined under src/: {qualified name: (file, last line, [fields])}."""
    dump = subprocess.run(["clang++", "-std=c++20", "-Isrc", "-fsyntax-only", "-fno-color-diagnostics",
                           "-Xclang", "-ast-dump", "src/app/game_flow.cpp"],
                          cwd=ROOT, capture_output=True, text=True).stdout
    out, stack, current = {}, [], None
    for line in dump.splitlines():
        node = NODE.match(line)
        for m in FILE.finditer(line):
            current = m.group(1)
        if not node:
            continue
        depth = len(node.group(1))
        while stack and stack[-1][0] >= depth:
            stack.pop()
        m = RECORD.match(line)
        if m:
            name = (stack[-1][1] + "::" if stack else "") + m.group(2)
            stack.append((depth, name))
            ends = re.findall(r"line:(\d+)", m.group(1))
            if current and current.startswith("src/") and ends and name not in out:
                out[name] = (current, int(ends[-1]), [])
            continue
        m = FIELD.match(line)
        if m and stack and stack[-1][1] in out:
            out[stack[-1][1]][2].append(m.group(1))
    return out


def listed(text):
    """Names in the io() code and in its "Not saved:" comments."""
    code = re.sub(r"//[^\n]*", "", text)
    names = set(re.findall(r"[A-Za-z_]\w*", code))
    comments = " ".join(re.findall(r"//([^\n]*)", text))
    for note in re.findall(r"Not saved:(.*?)(?:\.\s|\.$)", comments + " "):
        note = re.sub(r"\([^)]*\)", "", note)
        names.update(n.strip() for n in re.split(r",|\band\b", note) if n.strip())
    return names


def main():
    found = classes()
    if not found:
        print("clang++ gave no classes (is it on the path?)")
        return 2
    missing, checked = [], 0
    for name, (file, end, fields) in sorted(found.items()):
        lines = (ROOT / file).read_text().split("\n")[:end]
        # The io() list is the last thing in its class: from its "Quick saves" comment on.
        start = None
        for i in range(end - 1, -1, -1):
            if "Quick saves (state_io.hpp)" in lines[i]:
                start = i
                break
            if re.match(r"\s*(class|struct) \w+", lines[i]):
                break
        if start is None:
            continue
        checked += 1
        names = listed("\n".join(lines[start:end]))
        for f in fields:
            if f not in names:
                missing.append(f"{file}: {name}::{f}")
    for m in missing:
        print("not in io() or 'Not saved':", m)
    print(f"{checked} classes checked, {len(missing)} fields missing")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
