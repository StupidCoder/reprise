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
