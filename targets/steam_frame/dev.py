#!/usr/bin/env python3
"""Mac-driven native ARM64 development build. Never synchronizes imports or saves."""
import argparse
import pathlib
import shlex
import subprocess
import tarfile
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(__doc__)
parser.add_argument('action', choices=['build', 'catalog', 'launch', 'stop', 'logs', 'inspect', 'import'])
parser.add_argument('--host', default='steamos@192.168.1.129')
parser.add_argument('--identity', default=str(pathlib.Path.home() / '.ssh/degauss_frame_ed25519'))
parser.add_argument('--image', action='append', default=[], help='Headset image or ZIP path; repeat for batch import')
parser.add_argument('--bridge', action='store_true', help='Build the existing Spy Mouse module bridge')
parser.add_argument('--launcher', action='store_true', help='Build/launch the full shared launcher integration')
parser.add_argument('--data', default='/home/steamos/reprise-dev/test-data', help='Headset Reprise data directory')
parser.add_argument('--content', help='Headset path to an existing Spy Mouse import (launch only)')
parser.add_argument('--seconds', type=int, default=0, help='Request orderly XR exit after this time')
parser.add_argument('--room', action='store_true', help='Show the shader-rendered room floor')
parser.add_argument('--benchmark', action='store_true', help='Diagnostic: tick/upload even when not visible')
parser.add_argument('--catalog', action='store_true', help='Show the exported real catalog ring')
parser.add_argument('--reduced-motion', action='store_true')
args = parser.parse_args()
ssh = ['ssh', '-i', args.identity, '-o', 'IdentitiesOnly=yes', '-o', 'BatchMode=yes',
       '-o', 'ConnectTimeout=10', '-o', 'ServerAliveInterval=10',
       '-o', 'ServerAliveCountMax=3', args.host]

def remote(command, **kwargs):
    return subprocess.run(ssh + [command], check=True, **kwargs)

if args.action == 'build':
    # Explicit source allowlist: no images/, imports, build products, or user data.
    paths = ['targets/steam_frame', 'shared/platform']
    if args.bridge:
        paths += ['games/spy_mouse_ios/CMakeLists.txt', 'games/spy_mouse_ios/src',
                  'games/spy_mouse_ios/tools/media_checks.cpp', 'games/spy_mouse_ios/tools/verify_traces.cpp',
                  'games/spy_mouse_ios/reprise', 'app', 'shared', 'collections']
    if args.launcher:
        tracked = subprocess.check_output(['git', '-C', str(ROOT), 'ls-files', '-z']).decode().split('\0')
        source_suffixes = {'.c', '.cc', '.cpp', '.h', '.hpp', '.inc', '.inl', '.ipp', '.def', '.cmake'}
        paths = [name for name in tracked if name and
                 (pathlib.Path(name).suffix in source_suffixes or pathlib.Path(name).name == 'CMakeLists.txt') and
                 name.split('/')[0] in {'app','shared','games','collections','tools','tests','CMakeLists.txt'}]
        paths += ['targets/steam_frame', 'tools/frame_catalog_export.cpp']
        # Artwork is unchanged by code iterations; upload it once per checkout's
        # content hash, not on every native rebuild.
        import hashlib
        digest = hashlib.sha256()
        resource_paths = ['resources', 'games/sonic/resources', 'games/layton/resources']
        for folder in resource_paths:
            for file in sorted((ROOT/folder).rglob('*')):
                if file.is_file():
                    digest.update(str(file.relative_to(ROOT)).encode())
                    digest.update(file.read_bytes())
        resource_stamp = digest.hexdigest()
        cached = remote('cat "$HOME/reprise-dev/source/.resource-stamp" 2>/dev/null || true',
                        capture_output=True, text=True).stdout.strip()
        if cached != resource_stamp:
            paths += resource_paths
    with tempfile.TemporaryFile() as archive:
        with tarfile.open(fileobj=archive, mode='w:gz') as tar:
            for relative in paths:
                tar.add(ROOT / relative, arcname=relative,
                        filter=lambda info: None if '/evidence/' in info.name else info)
        print(f'Uploading {archive.tell()} compressed source bytes', flush=True)
        archive.seek(0)
        remote('mkdir -p "$HOME/reprise-dev/source" && tar -xzf - -C "$HOME/reprise-dev/source"', stdin=archive)
    if args.launcher:
        remote('printf %s ' + shlex.quote(resource_stamp) + ' > "$HOME/reprise-dev/source/.resource-stamp"')
    remote('bash "$HOME/reprise-dev/source/targets/steam_frame/build-native.sh" ' +
           ('LAUNCHER' if args.launcher else 'ON' if args.bridge else 'OFF'))
elif args.action == 'catalog':
    subprocess.run(['cmake', '--build', str(ROOT/'build'), '--target', 'frame_catalog_export', '-j', '4'], check=True)
    destination = ROOT/'build/steam-frame-catalog'
    subprocess.run([str(ROOT/'build/frame_catalog_export'), str(destination)], check=True)
    with tempfile.TemporaryFile() as archive:
        with tarfile.open(fileobj=archive, mode='w:gz') as tar:
            tar.add(destination/'catalog.tsv', arcname='catalog.tsv')
            tar.add(destination/'cards.png', arcname='cards.png')
        archive.seek(0)
        remote('mkdir -p "$HOME/reprise-dev/bundle/catalog" && tar -xzf - -C "$HOME/reprise-dev/bundle/catalog"', stdin=archive)
elif args.action == 'import':
    if not args.image:
        parser.error('import requires at least one --image HEADSET_PATH')
    command = ['/home/steamos/.local/share/Steam/steamapps/common/SteamLinuxRuntime_4-arm64/run',
               '--', '/home/steamos/reprise-dev/bundle/reprise-frame-probe', '--data', args.data]
    for path in args.image:
        command += ['--import', path]
    # Dedicated service survives an SSH disconnect; output remains on the headset.
    script = 'exec ' + shlex.join(command) + ' > "$HOME/reprise-dev/import-results.log" 2>&1'
    remote(shlex.join(['systemd-run', '--user', '--wait', '--collect',
                      '--unit=reprise-frame-import', '/bin/sh', '-c', script]))
elif args.action == 'launch':
    command = ['systemd-run', '--user', '--collect', '--unit=reprise-frame-probe',
               '--setenv=DISPLAY=:0', '--setenv=SDL_GPU_DRIVER=vulkan',
               '--setenv=XR_RUNTIME_JSON=/opt/steamvr/steamxr_linuxarm64.json',
               '/home/steamos/.local/share/Steam/steamapps/common/SteamLinuxRuntime_4-arm64/run',
               '--', '/home/steamos/reprise-dev/bundle/launch-probe.sh', '--seconds', str(args.seconds)]
    if args.content:
        command += ['--content', args.content]
    if args.room:
        command += ['--room']
    if args.benchmark:
        command += ['--benchmark']
    if args.catalog:
        command += ['--catalog', '/home/steamos/reprise-dev/bundle/catalog']
    if args.launcher:
        command += ['--room', '--catalog', '/home/steamos/reprise-dev/bundle/catalog', '--data', args.data]
    if args.reduced_motion:
        command += ['--reduced-motion']
    remote(shlex.join(command))
elif args.action == 'stop':
    remote('systemctl --user stop reprise-frame-probe')
elif args.action == 'logs':
    remote('cat "$HOME/.local/state/reprise/steam-frame-probe.log"')
else:
    remote('uname -a; cat /etc/os-release; c++ --version; pkg-config --modversion sdl3 vulkan; '
           'cat "$HOME/.local/share/Steam/steamapps/common/SteamLinuxRuntime_4-arm64/VERSIONS.txt"; '
           'readlink -f "$HOME/.config/openxr/1/active_runtime.json"; vulkaninfo --summary')
