# The adventures in Reprise

How the six games of this engine family run inside Reprise (connected
2026-10-29). Per-game scope and open items: each game's `plan.md`. This page
describes the integration, not the games' rules.

## What is connected

| Game | Catalog id | Import | Start | Reprise quick save / Continue | Original saves |
|---|---|---|---|---|---|
| Curious Village | `curious_village_ds` | A5FP NDS | title → end credits | last room checkpoint | 3 slots (`backup.sav`) |
| Pandora's Box | `pandoras_box_ds` | YLTD NDS | title → ending, post-game | last checkpoint (room, event or puzzle start) | 3 slots (`backup.sav`) |
| Lost Future | `lost_future_ds` | C3JD NDS | title → end credits | last room checkpoint | 3 slots (`slot0..2.sav`) |
| Spectre's Call | `spectres_call_ds` | BLFD NDS | title → end credits | last room checkpoint | 3 slots + table (`slot0..2.sav`, `table.sav`) |
| Miracle Mask | `miracle_mask_3ds` | AKKD CCI (unencrypted) | title → end credits | start of the current place; in the ruins the floor's retry point | 3 slots (`slot0..2.sav`) |
| Azran Legacy | `azran_legacy_3ds` | AL6D CCI (unencrypted) | **not startable** (no story yet) | — | — |

Start always opens the game's own title (the boot logos are skipped, as in
Picarat). The excluded Picarat shell features (music player, movie theatre,
puzzle browser and puzzle mode, minigame free play, the hub's "Continue
story") are not offered anywhere in Reprise; movies, music, puzzles,
minigames and the games' own menus play as part of normal play.

## Code layout

- `src/embed/layton_embed.{hpp,cpp}`: the embedding interface. Reprise sees
  no engine header; Picarat's own `platform/` and `shell/` headers never meet
  Reprise's (the include-name overlap is resolved by the library boundary).
- `src/platform/audio_pull.*` and `host_embed.cpp`: sound pulled by the host.
- `src/shell/game_session.*`: the same sessions as the standalone app, plus
  `step()` (below). `src/shell/utf8.cpp` holds two helpers moved out of
  `style.cpp` so the embedded build needs no shell drawing.
- `reprise/entry.*`: the six `GameEntry`s and the `GameModule`.
- `reprise/dual_screen.*`: layout, zoom and stylus routing (no SDL; tested).
- `reprise/saved_checkpoint.hpp`: the quick save record.
- CMake: Reprise includes this folder with `PICARAT_EMBEDDED=ON`, which builds
  only `picarat_core` and `picarat_embed`. The standalone build (the Picarat
  app, research tools, tests, its own Android script) is unchanged.

## Timing, threads and sound

The engines are straight-line game loops on a worker thread (each frame ends
in the "present frame" callback). Reprise drives them in lockstep:
`GameSession::step()` lets the worker run exactly one frame and waits until it
is out; between steps the worker waits. Reprise's fixed 60 Hz `tick()` is the
only clock, so the pause menu, a minimised window or Android backgrounding
simply stop ticking; ending the module stops and joins the worker. While a
game loads its first frame the module skips ticks instead of blocking the
window; later loads happen inside a frame, as in the original.

Sound: each game thread's `AudioStream` joins the session's `AudioPull`
instead of opening a device. After every frame the session renders exactly
one frame's worth (800 stereo frames at 48 kHz, resampled linearly from the
games' 32 728 Hz), which the module queues for Reprise's audio. The sound's
progress (music settled, voice still playing, stream position) therefore
follows the frames, not the wall clock - deterministic, and movies keep
their sync with the picture because both advance per frame. The queue keeps at
most a quarter second.

## Two screens

The frame is the window's drawing area. `DualScreen` places the two screens
with Picarat's rule (`shell/screen_layout.hpp`, via `embed::layout`): stacked
or side by side, whichever gives the larger scale, each keeping its shape and
order, centred (DS 256x192 + 256x192; 3DS 400x240 over 320x240). It follows a
resize or a rotation in the next frame without restarting. A pinch (two
fingers, `GameInput::fingers`) zooms up to 4x and pans as in Picarat.

Stylus: mouse or first finger, mapped through the layout the pointer was
over when it was drawn, the letterbox and the zoom. Presses on the upper
screen or beside the screens are ignored; a press that began on the lower
screen continues off it. Touches are held back up to 150 ms for a possible
second finger (Picarat's rule); a pinch, or a layout change during a press,
slides the stylus off the screen and lifts it there, and ignores the rest of
that press. Press/move/release reach the game in order, one per frame (the
session's queue), which keeps handwriting strokes intact.

Other controls: arrows = pad; Shift+arrows = tilt (3DS motion sensor); on
phones the accelerometer (`GameModule::wants_motion`, the host opens SDL's
sensor while a 3DS game runs; the way the phone is held at the start counts
as flat);
button 2 (X / gamepad east) = shake; fire (Space/Z/gamepad A) in a puzzle, or
the on-screen "Hold to blow" button (Pandora's Box puzzle 203) = microphone;
Start/Return = skip what plays (movies, timed waits, voices - Picarat's skip
convenience). Typed names are not connected (the games' touch keyboards work).

## Screen filter

The module draws unfiltered screens at the window's resolution
(`draw_gpu`). It reports the two screen rectangles as
`GameModule::filter_regions`; the framework turns them into
`ScreenEffect::regions`, and the host filters each screen as a picture of
its own (its own fixed 720x480 raster, its own tape noise), then composes
them at their places. One screen never bleeds into the other, unequal 3DS
screens are not stretched, and the black margins stay black. The pause menu
and notes are drawn afterwards, sharp. Checked on macOS with the Metal GPU
shader in both arrangements and with the 3DS geometry; the CPU fallback path
runs the same per-region code but was not exercised on a non-GPU renderer.

## Saves

- **Original slots** live in `<data>/progress/<id>/` (never inside the
  import). Quick saves never change them.
- **Quick save (all but Azran Legacy)**: these games write an automatic
  checkpoint whenever a room is ready (Pandora's Box also at event and puzzle
  starts; Miracle Mask since 2026-10-29 and its ruins floors at their retry
  points, Spectre's Call since 2026-10-30, see their plans) - the game's own
  save record, continued through its own load path. A quick save stores that record (`SavedCheckpoint`) in
  Reprise's persistent save; Continue/quick load starts a fresh session from
  it. It is **not** an arbitrary-moment save; the pause menu says "Saved:
  Continue returns to the last room you entered" and the details page says the
  same. Before the first room a save is refused with that explanation. The
  checkpoint file inside the progress folder is the session's own: a new
  Start removes it.
- Loading validates the record (magic, version, game id, then the game's own
  checkpoint reader in a scratch folder) before anything replaces the running
  game; a damaged or foreign save is refused and the game goes on.
- **Save picture**: `GameModule::save_picture()` gives the frame the
  checkpoint was written with (`Session::checkpointGeneration`), so the
  details page shows where Continue returns; after a fade (Pandora's Box) the
  frames that follow while it brightens, until steady.
- Options: the VHS look and the touch-area help are presentation options
  (kept on load); Curious Village's language is a game option (saved with a
  quick save, applied when the game starts).

## Import and takeover

`GameEntry::import_into` (new): the image is read through `platform::FileSource`
(random access, also Android documents) and the engine's importer writes the
pack straight into the library's staging folder. Only the recognised release
of each game is accepted (`kKnownRoms` SHA-256), the pack schema is checked by
`embed::checkPack`, and Curious Village's languages are recorded in the
release name. The library hashes the written files for the manifest and,
for these large imports, checks sizes at start-up ("check sizes" manifests).
Imports run without the cartridge afterwards. Measured on macOS: Curious
Village 8 s; Miracle Mask (1 GiB CCI) 26 s at about 200 MB of memory.

Takeover of the standalone Picarat app (`GameEntry::legacy`, once): its packs
are copied (`LegacyCopy::folder`) when they pass `checkPack` and Reprise has
no import; its cartridge saves/slots are copied when Reprise has no progress
folder for the game. Picarat's automatic checkpoints, puzzle-mode and
free-play records are not taken over. Nothing in Picarat's folder is changed.
On Android another app's storage is out of reach (no takeover).

## Previews

Curious Village and Pandora's Box show up to 24 of the pack's backgrounds,
decoded by the engine. The later games have no preview decoder exposed yet.

## Checks

- `ctest -R layton_module` (`tests/layton.cpp`): layouts, stylus, pinch,
  importer rejection; with `LAYTON_PACKS` the five stories start with picture
  and sound; with `LAYTON_CHECKPOINTS` (files `a5fp-checkpoint.sav`,
  `checkpoint.l2cp`, `c3jd-checkpoint.sav`) continue, quick save, fresh load,
  saved picture, damaged save and foreign save. `LAYTON_SHOTS=DIR` writes
  pictures. Checkpoints for the test were written by the games themselves:
  Picarat's `a5fp-checkpoint.sav`, `picarat_story_explorer PACK OUT --frames
  12000` and `picarat_a3_story_run` with `PICARAT_SAVES`,
  `PICARAT_CHECKPOINTS=1`, `PICARAT_AUTO=0`.
- `reprise --events FILE` scripts (new line kind `resize W H`) drove the real
  window: launch, landscape/portrait/wide resizes, taps, pause menu, VHS on,
  Cmd+S, relaunch and Continue.
