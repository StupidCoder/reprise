#!/usr/bin/env python3
"""Export character and music assets from the user's extracted Beacon game folder.
No executable, game code or keys are included. Import the result in Reprise.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import os
import subprocess

IDENTITY = 'b8198d82c595d41f092424cc14234bf1b26e82c0427970fe0c70ac5876af2d9c'
def export(root, output, dependency_tool):
    root = root.resolve()
    sfo = root / 'sce_sys/param.sfo'
    if hashlib.sha256(sfo.read_bytes()).hexdigest() != IDENTITY:
        raise ValueError('Unsupported release: expected the supplied CUSA12392 merged package')
    files = [sfo]
    # These are asset directories from the supplied folder, not a re-created asset map.
    for directory in ('data/multi_platformer/gfx', 'data/multi_platformer/anim', 'data/common'):
        files += sorted(p for p in (root / directory).rglob('*') if p.is_file() and p.suffix in ('.jxm', '.gnf', '.skel', '.anim'))
    files += sorted((root / 'data').rglob('*_bgm.sxd'))
    dependencies = subprocess.check_output([str(dependency_tool.resolve()),str(root)],text=True).splitlines()
    files += [root / p for p in dependencies]
    files = [sfo] + sorted(set(files) - {sfo})
    if len(files) > 12000: raise ValueError('Too many assets')
    total = sum(p.stat().st_size for p in files)
    if total > (2 << 30) - (1 << 20): raise ValueError('Viewer bundle exceeds 2 GiB')
    if output.exists(): raise FileExistsError(output)
    temporary = output.with_suffix(output.suffix + '.partial')
    # Open outside cleanup: an existing partial file may belong to another export.
    stream = temporary.open('xb')
    try:
        with stream as out:
            out.write(b'BEACON-ASSETS-1\n' + struct.pack('<I', len(files)))
            for p in files:
                if p.is_symlink() or not p.resolve().is_relative_to(root): raise ValueError(f'Unsafe asset: {p}')
                if p.stat().st_size > 64 << 20: raise ValueError(f'Asset too large: {p}')
                data = p.read_bytes()
                name = p.relative_to(root).as_posix().encode('ascii')
                out.write(struct.pack('<HQ', len(name), len(data)))
                out.write(hashlib.sha256(data).digest()); out.write(name); out.write(data)
            out.flush(); os.fsync(out.fileno())
        temporary.rename(output)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise
    print(f'{len(files)} assets, {total:,} payload bytes: {output}')
if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game_folder', type=Path)
    parser.add_argument('output', type=Path, help='New .beacon file')
    parser.add_argument('--dependency-tool',type=Path,default=Path(__file__).resolve().parents[3] / 'build/games/astro_bot_ps4/beacon_asset_list')
    args = parser.parse_args()
    export(args.game_folder, args.output, args.dependency_tool)
