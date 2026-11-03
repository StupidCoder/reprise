# Character and music import and viewer

This slice makes original assets viewable inside Reprise's game details. It does
not enable gameplay. Direct PKG import is now available (see `package-import.md`). The source Beacon project and the
separate title-card work are unchanged.

## Use

Choose Import on the game's details page and select `CUSA12392.v1.04.pkg`, or
drop it onto Reprise. Once installed, choose **View assets**, or Documentation →
Assets. Reprise extracts the needed character/music assets directly from that
package. No intermediate export is required.

The previous `.beacon` export format remains supported. For research workflows,
`tools/export_assets.py EXTRACTED_GAME OUTPUT.beacon` exports it using the native
`build/games/astro_bot_ps4/beacon_asset_list` dependency helper. Normal package
imports need neither tool nor the Beacon folder.

- Top page arrows / directional navigation: choose an asset. Astro and the crew bot come first, then music, then the remaining models.
- Music: click/tap the waveform or press Enter/gamepad A to play or stop. Tracks play once; switching pages stops playback.
- Arrows beside the clip name / Page Up and Page Down / gamepad shoulders: choose a clip.
- Play/Pause / Enter or gamepad A: animation transport.
- -1f / +1f: step one authored frame and pause.
- Speed: 0.25x, 0.5x, 1x or 2x. Loop toggle: repeat or stop at the last frame.
- Click/drag the timeline to seek; this pauses playback.
- Drag to orbit; Shift-drag or two fingers to pan; wheel/pinch or +/- to zoom.
  Reset restores the camera. Controls have matching landscape and portrait layouts.

Selecting a new model releases the previous session. Meshes, textures and clips
are loaded lazily; the full model catalog is not retained in memory. The hero
and crew bot appear first, followed by other animated models and props. The clip
list includes Bind pose. Unsupported clips display their decoder error instead
of pretending to play. Entering a model starts its `idle` clip when present,
unless Reduced Motion is enabled.

## Import envelope and provenance

Version 1 is uncompressed, original payloads with relative paths:

- 16 bytes ASCII `BEACON-ASSETS-1` followed by newline; u32 little-endian count.
- Each entry: u16 path length, u64 payload length, 32 raw SHA-256 bytes, ASCII
  relative path, then the exact payload bytes. No alignment or trailing data.
- First entry: `sce_sys/param.sfo`, SHA-256
  `b8198d82c595d41f092424cc14234bf1b26e82c0427970fe0c70ac5876af2d9c`, measured
  from the supplied extracted copy. This pins the same release as game.md.
- Remaining paths are below `data/`, extensions `.jxm`, `.gnf`, `.skel`, `.anim`, plus `*_bgm.sxd` music banks.
  The hero model and skeleton are required. No eboot/native code is exported.
- Limits: 2 GiB bundle, 12,000 files, 64 MiB per file, 512-byte safe relative paths.
  Reject duplicate paths (case-insensitive), traversal, invalid extension/identity,
  digest mismatch, truncation, excessive lengths/counts and trailing data.

`Library` owns staging, verification, manifests and replacement. The importer
reports byte-based progress; exceptions preserve any earlier installation.
The envelope checks integrity, not authenticity of arbitrary modified payloads.
Full fuzzing of the inherited binary decoders remains future work.

The supplied export contains 2,267 files and 661,257,260 payload bytes. Dependency
selection decodes the original material references; it is not a hand-written
asset map. Source samples, original tables, texture mip levels, animation timing
and skeleton user channels remain data. The new viewer is a native service.
bcdec is copied unchanged from the pinned Beacon source; its MIT alternative
notice is retained in `resources/licenses/bcdec.txt`.

## Visual scope

The viewer decodes base-colour textures (original mip levels, normally capped at
512 pixels), material tint, skeleton skinning and authored facial UV offsets.
The software renderer uses perspective-correct texture sampling, a depth buffer
and simple face lighting. Fixed bind-pose framing avoids animation-driven zoom;
root motion is followed to keep the model in view. This is an asset inspection
view, not a claim of original-game visual equivalence: PBR lighting, normal maps,
particles, bloom, shader-specific effects and sorted glass are not reproduced.
The meshless skeletal/effect resources are not offered as visible models.

## Validation (2026-11-02)

- Real export → Library import → fresh Library startup passed. All 2,246 files
  were checksummed and installed. A deliberately failed replacement preserved the
  installed inventory. The final copy was installed in the user's normal library.
- All 144 displayed models construct and decode their base textures successfully.
  Hero: 20,234 vertices, 14,596 faces, 279 clip-list entries including bind pose.
  Crew bot: 12,094 vertices, 9,942 faces, 327 entries including bind pose.
  Sampled idle animations change vertex positions. This is not validation of
  every animation in the catalog; inherited unsupported formats remain explicit.
- 17 CTest checks pass. New import checks cover unrelated media, zero/excessive
  fields, truncation, path traversal and bad identity/digest. Shared viewer checks
  exercise texturing, lazy loading, play/pause, frame stepping, seek, speed,
  no-loop stopping, render purity and portrait input mapping.
- Landscape and portrait details-page PNGs were rendered through Launcher and
  inspected. Character textures, controls, timeline and framing are visible.
  Model-only 800×600 software renders measured roughly 5–7 ms on this Mac;
  this is a local observation, not an Android performance claim.
- `./build.sh` builds and packages the macOS app. Both packages were checked
  for accidental game payloads; none are present. The bcdec notice is bundled.
- Android ARM64 APK builds and passes its alignment and v2/v3 signing checks
  (103.8 MiB). No physical Android or Windows/Linux run is claimed.

Reproduction after export:

```sh
cmake --build build --target beacon_assets_check shell_checks
build/games/astro_bot_ps4/beacon_assets_check characters.beacon /path/to/test-library /path/to/captures
build/shell_checks --beacon-viewer /path/to/test-library /path/to/captures
ctest --test-dir build --output-on-failure
./build-android.sh
```

Checks and small screenshots are in `asset-viewer-validation/`. Source version,
bundle identity and test-binary hashes are recorded there. No game payloads are
stored in Git or packaged in either app. The one-time bundle under `build/` and
Library contents are private imported data.

## Music validation (2026-11-02)

The expanded export adds 21 original music banks (125,792,912 bytes) and offers
109 music previews alongside 144 animated models. All 109 decoded successfully
and produced nonzero PCM; all 144 models still loaded. The user's installed
library was replaced transactionally through Reprise and verified afterwards.
Music uses the existing Beacon catalog's authored sound names, states and layer
grouping, including TV variants and unnamed cinematic stems. It is not a list
of official soundtrack album titles. Playback preserves mono/stereo channels,
resamples through Reprise's audio path, and plays once without interactive music
transitions or automatic looping. Only the selected track retains decoded PCM;
track decoding is synchronous and can briefly delay a page change.

The data-only `MusicCatalog::scan(paths)` overload is recorded exactly in
`../reprise/music-catalog.patch`, checked by the source handoff verifier. Source
Beacon remains unchanged. No dummy executable or private audio device is used.

`music-validation/` contains the all-assets decode log and inspected desktop and
portrait screenshots. The shared shell regression checks lazy decode, render
purity, release/reload, stereo separation and end-of-track silence. All 17 CTest
checks pass. `./build.sh` and `./build-android.sh` pass, including APK alignment
and signature checks. Android runtime audio has not been exercised on a device.
