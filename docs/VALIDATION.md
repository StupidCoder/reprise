# Foundation validation — 2026-10-28

- Built and packaged `build/Reprise.app` in the final Reprise root. Packaging
  verifies the ad-hoc signature and bundled SDL dependency paths.
- All three root CTest suites pass: shell_navigation, shell_motion and
  persistent_saves. Tests use isolated data; original saves were not changed.
- Launched the actual macOS bundle and used keyboard navigation to switch to
  Sonic, then Layton, enter game details, and return to the library.
- Rendered final headless library, Commodore, Sonic, Layton and Spy Mouse
  detail screenshots under `design/reprise/implemented/`.
- Configured and compiled the relocated standalone `picarat`, `doubleocheese`
  and `bluestreak` targets under `build/engines/`. All completed successfully.
  This verifies relocation/builds, not gameplay equivalence or new adapters.
- Verified every tracked imported source file against its Git blob: Layton
  551, Spy Mouse 133, Sonic 416 (1,100 total), all exact matches. Only separate
  REPRISE-HANDOFF.md guidance and ignored research links were added.
- Verified `images/` and all local research links are Git-ignored. Packaging
  copies only the runtime executable, SDL, and curated resources, not images,
  engine trees, source repositories, research links or user save libraries.
- Original Joyport, Picarat, DoubleOCheese and BlueStreak working trees remain
  clean and intact. Reprise changes have not been committed or pushed.

## Limits

The Joyport entries are connected; the other engines are relocated but remain
standalone. Their Reprise catalog cards are explicitly labelled previews.
Source-project imports and saves have not been migrated automatically. No
Android build, portrait layout, new replay comparison, or connected gameplay
validation for the three incoming engines was performed. Their standalone
binaries are relocation checks, not packaged replacements for their apps.

## Collection artwork and folder naming follow-up

- Original bitmap collection backgrounds now replace the primitive motifs.
- Renamed games/spy_mouse to games/spy_mouse_ios and updated handoff metadata.
  Internal game code, namespaces and saved-game identifiers are unchanged.
- Configured and compiled the standalone doubleocheese target from the new
  path under build/engines/spy_mouse_ios; build succeeded.
- Rebuilt the macOS bundle, reran all three shell/save suites, and visually
  inspected rendered screenshots of each collection.

## Spy Mouse integration — 2026-10-28

Connected engine: `games/spy_mouse_ios` (catalog id `spy_mouse_ios`, no collection). Tested
with the user's `images/com.ea.spymouse.inc-iOS3.1-(Clutch-2.0.4).ipa` (version 1.1.1) in
scratch data folders under the session's temporary directory; the user's own Reprise and
DoubleOCheese folders were only read.

What was run and seen:

- **Import** (`reprise --data D --import IPA` and a file dropped on the real window):
  "Spy Mouse imported (Spy Mouse 1.1.1 (iOS))", 3,503 files, 91 MB, about 2 s. The 1.0.5 IPA
  is refused with "This app is not a supported version of the game (the program is not Spy
  Mouse 1.1.1: its file keys were not found)". A Wizball disk image still goes to Wizball.
- **Real window** (`build/Reprise.app`, scripted with `--events`, frames written by the host):
  library → details ("Ready to play") → Start → level 1-1 drawn at the window's resolution;
  the 1-1 path drawn with real mouse press/move/release events; Reprise's pause menu over the
  dimmed game; Quick save ("Saved state"); resume; the level won and the results counted;
  1-1's best total 96 and 1-2 unlocked in the campaign progress.
- **Restart of the application**: the details page shows the save's picture and date; Continue
  restores the saved moment, which plays on to the win; Return to library from the pause menu.
- **Takeover**: a fresh data folder took over DoubleOCheese's progress (copied; the original
  unchanged) and reported that its 1.0.5 import cannot be used; Start then opened the world 1
  map at the boss chase where that progress stood.
- **Module check** `spy_mouse_checks` (CTest `spy_mouse_module`; skipped unless
  `SPY_MOUSE_CONTENT` and `SPY_MOUSE_SCRIPT` name an import and a 1-1 touch script, e.g.
  `.analysis/refruns/l11_obj.txt`): GPU frame size; the level's music in the mix before any
  touch (peak 0.24; 0.03 with the music switched off, the ambience alone); drawing twice
  gives the same picture; save at tick 400 and load in a new instance through the save files:
  the same picture, and still identical 300 ticks later; the details preview; 1-1 won and
  recorded; results → map → the 1-2 pad → Play → level 1-2; a save on the map loads and goes
  on identically; loading the pre-win save keeps 1-1's win, best total and 1-2's unlock; the
  map's quit button leaves the game. 0 failures.
- **Music decoder** (2026-10-28): `media_checks` (IMDCT against its definition; SBR
  filterbank at unit gain, 72.9 dB reconstruction). All 24 tracks decoded by the engine and by
  AudioToolbox (`afconvert -d LEF32@44100`), compared with
  `.analysis/tools/media/compare_music.py`: identical lengths (gapless trimming from the
  files' iTunSMPB: priming 2112 core samples, plus 962 output samples of SBR delay), sample
  aligned; below 8 kHz 59.4–63.0 dB SNR; 9.5–19 kHz per-band level median within ±0.15 dB
  (worst band −0.53 dB; the noise and sinusoid parts are random by design, so only levels
  compare).
- **Replay comparison**: the embedded game's frames at ticks 400 and 700 of 1-1 equal the
  standalone `doubleocheese --replay` frames pixel for pixel. `doc-verify` on the 1.1.1
  reference run `v111_l11_obj` agrees tick for tick on the mouse (follower check) and the
  random draws at the start, then reports the known 1.1.1 differences still being migrated
  (renumbered object states, moved prediction fields).
- CTest: shell_navigation, shell_motion, persistent_saves, spy_mouse_module all pass. The
  standalone engine build (`games/spy_mouse_ios/build.sh`: doubleocheese, doc-verify) builds.

Limitations and open items:

- The rules were reconstructed and verified on 1.0.5; 1.1.1 data now drives them. 1.1.1's own
  rule changes are still being compared (.analysis/docs/v111-migration.md). Not ported: the
  store and its cheese wedges, dossiers, the new-world card on the results, world 7, the secret
  level 4-S2. The 1.1.1 results items for those are hidden.
- Music is decoded by the engine's own HE-AAC decoder on every platform; it has only been
  built and run on macOS so far (no Linux or Android build yet). Sound was checked as samples
  in the mix, not by listening.
- The window checks used scripted mouse events through SDL's queue; a physical trackpad,
  mouse and touch screens (SDL's synthesized mouse events) were not exercised by hand.
- Reprise's step loop keeps up to four ticks of backlog per frame; the standalone app three.
  Both run whole 17 ms ticks. The standalone app now runs `Session::prepareFrame` once per tick
  (it ran once per display frame), as Reprise does and as the original's 60 Hz render did.
- With Spy Mouse installed the library checks its 91 MB import at each start (about 1 s).
- `reprise --pause-preview` (a CPU-picture preview tool) does not draw GPU-drawn games.

## Screen filter stage for GPU-drawn games — 2026-10-29

The VHS look over Spy Mouse through the shared filter stage (ADDING_A_GAME.md section 6,
docs/MIGRATION.md). Tested on macOS (Apple silicon) with scratch copies of a data folder.

- **Renderer**: the host now prefers SDL's GPU renderer. Before the change, the library,
  details page, pause menu and Bruce Lee (a picture game, VHS "Medium") were shot under the
  default Metal renderer and under the GPU renderer: pixel-identical. Spy Mouse's frame differed
  only in the HUD's counting score (the shot's moment).
- **Shader against CPU form** (`build/screen_effect_check`, test card, levels 1–4, six frames
  each): Metal (MSL) and Vulkan through MoltenVK (`SDL_GPU_DRIVER=vulkan`, SPIR-V) both within
  one 8-bit step of `ui::vhs` on a few hundred of 2.4 million pixels (mean below 0.001). The
  filter off: identical frames before and after an effect frame.
- **`ui::vhs` refactor** (frame state into `ui::vhs_frame`): 870 pictures (three sizes, all
  levels, 58 frames) bit-identical to the previous implementation.
- **Real window** (`--events`): Spy Mouse with "VHS look = Medium" through the GPU shader
  ("Screen effect: GPU shader (MSL)"), and with `SDL_RENDER_DRIVER=metal` through the CPU form;
  the pause menu and the "Saved state" note drawn sharp over the filtered game; the save's
  picture is the raw frame. The look chosen on the pause menu's options page is written to
  `<data>/options/spy_mouse_ios.txt` and is on again after quitting, starting again and Continue.
  Bruce Lee's VHS look is unchanged (pixel-identical to the previous build at the same frame;
  one of two runs landed a frame later).
- **Checks**: `persistence_checks` (a load restores the save's game options but keeps the
  current filter; picture games get no host effect); `spy_mouse_checks` (loading keeps the
  chosen look; with the look on, the same raw pictures, state and sound for 300 ticks). CTest:
  4/4 pass. `./build.sh` and `./build-android.sh` build.

Not verified: Android and Linux were built but not run (whether an Android device gets the GPU
renderer and the SPIR-V shader or falls back to OpenGL ES and the CPU form is untested);
Windows was not built (no DXIL shader is shipped: there SDL's GPU renderer can use Vulkan,
otherwise the CPU form runs). The CPU form reads the frame back every frame while the filter is
on; its cost on slow devices is unmeasured. Picture games still filter on the CPU as before.

## Initial Reprise commit checks — 2026-10-29

After the completed GPU-filter handoff, rebuilt the macOS bundle and signed
ARM64 Android APK. All four CTest tests passed (navigation, motion, persistence,
Spy Mouse). APK signature and 16 KB alignment checks passed. Final packages
include the project MIT license and explicit system-font/imagery exceptions.
The new README distinguishes connected gameplay, prototypes, asset viewers and
Sonic/Layton catalog previews. All 19 required documentation pairs are present.
Source-file review excludes local research symlinks, images, saves, build outputs
and signing material. Physical Android filter performance remains unverified.

## Fixed VHS video raster (2026-10-29)

VHS now samples every game's picture into a 360×240 virtual signal and renders
a 720×480 raster with scanline gaps. Grain, chroma smear, jitter, tracking,
dropouts and rolling bands all use that fixed grid. This is a stylised progressive
CRT/VHS look, not an exact PAL/NTSC or interlaced tape simulation. Presentation
preserves the game's display aspect ratio and pointer mapping; filter-off is unchanged.

`screen_effect_check` checks that equivalent pictures at native and doubled
resolution acquire identical effects at all four levels, then compares CPU and
GPU rendering across six frames per level. Metal comparison passed with zero
pixels outside tolerance. Full app/Android rebuilds were blocked during this
change by an unrelated in-progress Rock ’n Roll `Screen::draw` interface mismatch.
The shell tests also cover fixed raster dimensions, aspect ratio and pointer mapping.

## Shared GPU VHS presentation (2026-10-29)

Software-rendered game frames now upload their original picture and use the
same host GPU screen-effect stage as GPU-rendered games. CPU filtering remains
for offline captures and compatibility fallback only. VHS requests a 720×480
GPU render target and nearest-neighbour upscaling; other effects may leave the
processing raster unset to render directly at display resolution. Menus and
notifications are composited afterwards at display resolution.

The Metal `screen_effect_check` compared direct shading against the intermediate
at 1920×1280 and 3024×1834 window output sizes: all four strengths at frame 241
were pixel-identical. Uploaded software pictures and GPU-drawn pictures likewise
matched exactly. The existing six-frame-per-strength GPU/CPU comparison passed.
The shader already quantises its entire output to the virtual video grid; the
intermediate therefore removes repeated shading without reducing its current
detail. This equivalence is specific to this shader and nearest sampling; a
future continuous CRT mask or other display-resolution effect needs a separate
assessment. These checks measure appearance, not Android frame rate or battery use.

To repeat the larger window comparison, run
`build/screen_effect_check '' /tmp/reprise-filter-check large`. The test asserts
that the software-picture route actually selected a GPU shader.


## Sonic games (BlueStreak) connected — 2026-10-29

`sonic_gg`, `sonic2_gg`, `triple_trouble_gg` and `sonic_advance_gba` through
games/sonic/reprise. macOS (Apple silicon), scratch data folders; the user's
dumps and the standalone BlueStreak data folder were only read.

- **Module check** `sonic_checks` (CTest `sonic_module`; skipped unless
  `SONIC_ROMS` names a folder with the dumps; `SONIC_LEGACY` adds the takeover):
  each importer rejects the other three cartridges and other data as "not this
  game" and explains a damaged dump of its own; the library installs a copy that
  is then removed; previews decode (24, 27, 40 and 3 previews; 17, 20 and 33
  sound tracks; 0.5-3 s each); 2400 frames from power-on with sound; Start during
  play asks for the pause menu (GG) or stays with the game (Advance); drawing
  with the LCD look at x5 and x1 changes nothing; the expanded view widens the
  frame (399 x 224 at 16:9) and a portrait canvas gives a taller one; save →
  fresh module → load: the save's expanded view restored, the current VHS/LCD
  choices kept, 300 later frames picture-identical to the original run, repeated
  loads; a damaged save and a foreign state refused with the running game
  unchanged; Sonic Advance reports why it has no quick saves. All 92 checks pass.
- **Takeover**: Sonic 1, Sonic 2 and Sonic Advance copied from the real
  BlueStreak folder (file count unchanged, 3972); its Triple Trouble pack
  (written by BlueStreak 0.14/0.15) explained as outdated and re-imported.
- **Packs**: Reprise's imports are byte-identical to BlueStreak's own packs
  (Sonic 1: 3915 files) except that stale Triple Trouble pack.
- **Real window** (`--events`, scratch data): Sonic collection → details →
  Start; the title screen drawn at the window's resolution; Cmd+S ("Saved
  state" note sharp over the game); the pause menu; quit; a new process shows
  the save's picture and date on the details page; Continue with the LCD look
  and "VHS look = Medium" from the options file: "Screen effect: GPU shader
  (MSL)"; Cmd+L.
- **Headless launcher pages**: the Sonic collection in landscape and portrait,
  details, documentation (imported files and previews), history, the options
  page and its Level select page.
- **CTest** 5/5 (`shell_navigation` now checks that Sonic cards import and a
  Layton card still cannot). The standalone BlueStreak app builds and its check
  passes. `./build-android.sh` builds the APK; it contains no game data.

Not verified: play through the real window (scripted events cannot hold keys),
physical gamepads, any Android device or emulator run, Windows and Linux,
touch (the games have no touch controls yet).

## Puzzle adventures (games/layton), 2026-10-29

macOS (Apple Silicon), scratch data folders; images and packs from the user's Picarat folder.

- **CTest** 6/6 including `layton_module` (`tests/layton.cpp`): DS and 3DS geometry in five frame
  shapes (shape, order, scale, the larger arrangement chosen), the switch on rotation, stylus
  mapping through letterbox and zoom, upper-screen and margin presses ignored, drags off the
  lower screen, a layout change during a press, movie clicks, touch hold-back and taps, pinch
  zoom, importer rejection (all six). With `LAYTON_PACKS`: the five playable stories start with
  picture and sound (Azran Legacy correctly not startable). With `LAYTON_CHECKPOINTS`
  (checkpoints written by the games): Curious Village, Pandora's Box and Lost Future continue,
  quick save, load in a fresh module, show the saved picture; a damaged save and another
  game's save are refused while the game runs on.
- **Import** (`reprise --import`, streamed): Curious Village NDS 8 s, Miracle Mask 1 GiB CCI 26 s
  at ~200 MB peak memory; manifests with sizes; start-up with 3.2 GB of large installs ~1 s.
- **Takeover**: all six Picarat packs copied and checked (`check sizes` manifests), slots
  copied, Reprise's existing imports kept, Picarat's folder unchanged.
- **Real window** (`--events`, new `resize` lines): Curious Village Start → side by side at
  960x640, stacked at 500x900, wide at 1400x560 without restart; a tap mapped through the
  stacked layout to New Game; pause menu over the game; VHS Medium per screen ("GPU shader (MSL)
  per region") in both arrangements, menu and notes sharp; Miracle Mask's unequal screens stacked
  and narrow, filtered per screen; title → Continue → slot → room → Cmd+S ("Saved: Continue
  returns to the last room you entered"; before the first room a refusal saying why) →
  relaunch → Continue into the same room; return to the library; clean quit while in game.
- **Standalone Picarat** still builds; `shell_layout_tests` passes.
- **Android**: `./build-android.sh` builds the APK with the engine. Emulator (Medium Phone API
  36.1, 2 GB RAM): Curious Village (pushed into app storage) starts from the launcher, title
  shown; rotation switches stacked ↔ side by side live; a touch tap selects New Game; a typed
  letter survives rotations. First frame took ~30 s because the AVD was swapping.

Not verified: any physical Android device; audio by ear (sample peaks only); the CPU fallback
of the per-region filter; Android import through the document picker (the emulator was seeded
via `run-as`); gamepad-only play; quick saves of Pandora's Box and Lost Future in the real
window (checked through the module and wrapper only); Windows/Linux.

## Responsive shelf and Android navigation (2026-10-29)

The shelf now retains fractional wheel motion and translates pointer dragging
into continuous card-strip offsets in both orientations. Keyboard/controller
selection retains animated row reveal. Landscape canvas width follows display
aspect; the grid fits and centres columns within the available content area.

Shell navigation and motion checks passed, including a 30-pixel drag, fractional
wheel input, four columns at 2400×1080 and two at 1280×1024, matching button hit
bounds, selection across rotation and existing documentation navigation. Rendered
wide/narrow shelf previews were visually inspected.

Android requests SDL immersive fullscreen and maps AC Back to Escape's shell
and pause actions. The Activity also forwards system Back callbacks into SDL.
The APK rebuilt and passed signature verification. Status-bar visibility and
system Back gestures still require confirmation on a physical device.

## Landscape details and documentation (2026-10-29)

Landscape details now use the available aspect ratio, expanding the description,
save preview and documentation panels horizontally on wide displays and using
a taller canvas on narrower landscape displays. Text and cover art retain their
proportions. Asset paging, preview interaction, fades and confirmation-dialog
input use the resized bounds. Portrait layouts remain unchanged.

Wide (2400×1080) and narrow (1280×1024) details and development-note renders
were inspected. Navigation checks cover matching canvas aspect and identical
cover geometry across pages; interaction checks exercise right-anchored asset
arrows, the expanded waveform panel and centred confirmation controls. The
Android APK rebuilt and passed signing verification.

The navigation checks passed. The first new asset-interaction check exposed a
test setup error (opening assets before opening Details); the fixture was
corrected. Its final rerun was blocked at link time by concurrent engine changes:
`interceptor::game::touch_bodies` and `picarat::adventure5::readCheckpoint` were
unresolved. Those engines were left untouched. Do not count that interaction
check as passing until the shared build is consistent and it is rerun.

### Responsive imports (2026-10-29)

Interactive imports now run on a worker while the SDL thread presents an
aspect-adaptive progress screen and pumps events. The worker exclusively owns
library access until completion; the launcher applies the outcome afterwards,
using completion time for its message and arrival animation. Android picker
results no longer synchronously copy documents on the UI thread. Layton forwards
its measured extraction progress; streamed installation reports a separate
byte-weighted verification phase. Other importers show animated activity.

Validated: `shell_navigation`, `shell_motion`, `import_progress`, and
`layton_module` all pass. The new check covers a background streamed fixture,
forwarded extraction progress, completed verification, failure preservation,
and the animated unknown-duration state. Portrait and landscape renders were
inspected. A live UI drop of the 1 GiB Miracle Mask CCI into an isolated temporary
library displayed the loading screen and produced the verified installation.
The Android APK built and passed signature verification. Android document-picker
return, rotation and suspend/resume still require testing on physical hardware.

### Widescreen VHS sampling correction (2026-10-29)

Supersedes the fixed-width raster described above. The video clock remains
240 signal lines / 480 output lines, but horizontal density follows displayed
aspect, including non-square source pixels: 720 samples at 4:3, 900 at 5:3,
960 at 16:9 and 1260 at 21:9. Each handheld screen supplies its own dimensions.
The old 360-sample signal plus nearest scaling could periodically duplicate or
omit source columns. Horizontal footprint integration now resamples the source;
linear final reconstruction avoids repeating whole intermediate columns.
Noise, tracking and chroma still operate in video space, independent of native
resolution. Final reconstruction intentionally differs from direct point-sampled
shading; GPU/CPU agreement is the reference, not equality to that old path.

`screen_effect_check` passed with the default fractional-sampling test card and
16:9 / 21:9 stripe-and-gradient fixtures, including 3024x1834 Retina output.
All strengths and six frame numbers matched the CPU reference (reported mean
error zero), and filter-off output stayed identical before/after filtering.
Pixel-doubled input retained identical output. Adaptive raster checks cover
4:3, 5:3, 16:9, 21:9 and non-square pixels. Wide stripe output was inspected.
Android APK compiled and passed signing checks; physical Android rendering
has not been validated in this change.

### Android startup and development articles (2026-10-30)

- The long development articles and reader were still local changes; clean GitHub
  builds contained only the short catalog fallback notes. No article reversion
  was involved. Publication must include `resources/articles`, the catalog/reader,
  and their writing/source documentation together.
- Interactive startup now presents a pure SDL loading screen before catalog and
  imported-file checks. A worker owns library verification and legacy migration;
  the UI keeps polling and drawing, and joins the worker before using the library.
  Command-line imports and preview rendering remain headless.
- Startup stage timings use SDL logging (including Android logcat). Desktop smoke
  check showed the first loading frame at 0.205 seconds and completed migration
  into an isolated temporary library before showing the shelf. These are desktop
  measurements, not Android performance claims.
- Native build and shell navigation, motion, import progress and all 24 development
  article checks passed. Article checks cover scrolling in both orientations.
  ARM64 APK built and signature verification passed; all 26 packaged article
  files (24 development stories and two technical articles) match source bytes.
- Device follow-up: cold-start on the Thor and Galaxy with imported games; confirm
  loading visibility and collect `Reprise startup` log lines if a long pause remains.

### Imported-file validation cache (2026-10-30)

Checksummed imports now keep disposable per-file validation receipts under the
app data directory's `validation/`. The manifest is read on every launch. Matching
manifest content, file size and modification timestamp let a file skip content
reads and SHA-256; changing one file rechecks that file, while a changed manifest
invalidates every receipt. Missing files remain unavailable. Cache read/write
failures fall back to validation, never hide a valid game. Staging validation is
uncached, and large imports retain their existing size-only policy.

`library_validation_cache` covers initial validation, reuse across Library
instances, per-file reuse, changed timestamps/sizes/manifests, missing assets,
malformed receipts and an unwritable cache. It also demonstrates the deliberate
limit: edits that preserve both size and timestamp cannot be detected by this
optimization. Receipts can be removed to force a full checksum pass.

### Interceptor technical lessons (2026-10-31)

Three illustrated articles with isolated experiments: Hornet polygon-list order,
startup-built road points and the player flight model. The ordering interpreter
is shared with the renderer; the road builder and flight step are reused without
altering gameplay or saves. Evidence map: `docs/articles/interceptor-technical.md`.

Real imported-data checks passed at 960x640, 640x1138 and 1422x640: experiment
selection, held-fire edge behaviour, fractional wheel, touch drag, page keys,
end clamping and resize position. The eight Hornet viewpoints exercise four
actual list orders. The imported roads contain 40 records and 120 generated
points; all 120 stored coordinate pairs are zero. All three launcher entries,
open-lesson resizing and touch return navigation passed. Rendered article
figures and endings were inspected; polygon-list overflow, road magnification
and time-axis label placement were corrected during review.

Android ARM64 APK built and passed signature verification; the three packaged
article files match their sources. Physical Android rendering remains a device
follow-up. The original-runtime comparisons cited by the articles belong to
the game agent's recorded research, not new runs claimed by these UI tests.

## Large-import startup checks (2026-10-31)

Found that folder imports marked `check sizes` bypassed the timestamp/checksum
receipt cache and issued a file-size lookup for every asset on every launch.
The local six Layton imports contain 183,637 files. Interactive startup now uses
`Library::StartupValidation::defer_large_imports`: parse their recorded inventory
for the shelf, then `Library::verify` checks the selected import before either
Start or Continue. The host displays a responsive loading screen while that
check runs. Successful validation is reused within the current session, as
startup validation previously was. Missing/resized files clear availability and
ask for re-import before constructing a game module. The shelf's availability
is provisional until that first check; this is deliberately not an integrity
claim based on directory timestamps. Same-size edits remain outside the existing
large-import size-only policy; no checksum guarantee was removed.

Small/hash-based imports keep their per-file timestamp/size receipts. Imports,
migrations, staging verification and noninteractive constructors retain strict
validation. Large manifests are still read at startup, but their assets are not
statted. No imported files or saves are changed by the optimization.

Validation in a clean snapshot (committed sources plus this change): built
reprise, shell_checks and library_cache_checks. Four CTest cases passed:
library_validation_cache, shell_navigation, shell_motion, import_progress.
Added checks for deferred inventory, missing/resized assets rejected before play,
once-per-session verification, malformed manifests and unchanged strict defaults.
`library_cache_checks <data-root>` optionally benchmarks existing size-only imports
read-only, without loading games or writing cache receipts.

One desktop measurement on the user's actual 183,637-file library: full scan
1.8994 seconds; shelf inventory 0.150704 seconds (about 12.6x faster for this
phase). This is a local measurement, not an Android startup-time claim. The
Android build and signing completed: build-android/Reprise.apk, 86.3 MiB. A
physical-device timing remains to be measured.
