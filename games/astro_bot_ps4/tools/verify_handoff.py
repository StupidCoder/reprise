#!/usr/bin/env python3
"""Verify the pinned source and exact namespace-only core copy, without game data.
Usage: python3 games/astro_bot_ps4/tools/verify_handoff.py /path/to/Beacon
"""
import hashlib
import json
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
source = Path(sys.argv[1])
manifest = json.loads((root / "source-handoff.json").read_text())
for entry in manifest["files"]:
    relative = entry["path"]
    original = (source / relative).read_bytes()
    if hashlib.sha256(original).hexdigest() != entry["source_sha256"]:
        raise SystemExit(f"source changed: {relative}")
    if relative == "src/tools/LevelTool.cpp":
        continue  # Extracted probe, not a whole-file namespace transformation.
    copied = (root / entry.get("destination", relative)).read_bytes()
    if relative.startswith("src/") and "third_party" not in relative:
        text = copied.decode().replace("namespace beacon::", "namespace ")
        if relative.startswith("src/tools/"):
            text = text.replace("\nusing namespace beacon;\n", "")
        copied = text.encode()
    if relative in ("src/audio/MusicCatalog.hpp", "src/audio/MusicCatalog.cpp"):
        import difflib
        expected = "".join(difflib.unified_diff(original.decode().replace("namespace audio {", "namespace beacon::audio {").splitlines(True),
            (root / relative).read_text().splitlines(True), fromfile=relative, tofile=relative))
        if expected not in (root / "reprise/music-catalog.patch").read_text():
            raise SystemExit(f"unrecorded music adapter change: {relative}")
        continue
    if copied != original:
        raise SystemExit(f"copy differs beyond namespace isolation: {relative}")
print(f"Verified {len(manifest['files'])} source hashes and whole-file copies (including the recorded music catalog adapter)")
